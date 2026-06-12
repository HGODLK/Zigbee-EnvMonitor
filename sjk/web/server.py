#!/usr/bin/env python3
"""
环境传感器 Web 服务。

Qt 上位机独占连接 ESP8266，并在 TCP 25577 端口广播设备状态和传感器数据。
本服务只订阅 Qt 数据，再通过 HTTP API 和 SSE 提供给浏览器。
"""

import json
import os
import queue
import socket
import sqlite3
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from http.server import HTTPServer, SimpleHTTPRequestHandler
from socketserver import ThreadingMixIn
from urllib.parse import parse_qs, urlparse


class ThreadingHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True


sensor_data = {"temp": None, "humi": None, "light": None}
data_lock = threading.Lock()

state = {
    "connected": False,
    "connecting": False,
    "scanning": False,
    "host": "",
    "mode": "qt",
    "status": "尚未连接 Qt 数据服务",
    "error": "",
    "hub_connected": False,
    "hub_host": "",
}
state_lock = threading.Lock()

qt_port = 25577
connection_lock = threading.Lock()
connection_stop = None
connection_thread = None
scan_lock = threading.Lock()
scan_stop = None

sse_clients = []
sse_lock = threading.Lock()
database = None


class SensorDatabase:
    def __init__(self, path):
        self.path = path
        self.lock = threading.Lock()
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self.connection = sqlite3.connect(
            path,
            timeout=10,
            check_same_thread=False,
        )
        self.connection.row_factory = sqlite3.Row
        with self.lock:
            self.connection.execute("PRAGMA journal_mode=WAL")
            self.connection.execute("PRAGMA synchronous=NORMAL")
            self.connection.execute(
                """
                CREATE TABLE IF NOT EXISTS sensor_readings (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    recorded_at INTEGER NOT NULL UNIQUE,
                    temp REAL,
                    humi REAL,
                    light INTEGER,
                    source_host TEXT NOT NULL DEFAULT ''
                )
                """
            )
            self.connection.execute(
                """
                CREATE INDEX IF NOT EXISTS idx_sensor_recorded_at
                ON sensor_readings(recorded_at)
                """
            )
            self.connection.commit()

    def insert(self, reading):
        with self.lock:
            cursor = self.connection.execute(
                """
                INSERT OR IGNORE INTO sensor_readings
                    (recorded_at, temp, humi, light, source_host)
                VALUES (?, ?, ?, ?, ?)
                """,
                (
                    reading["timestamp"],
                    reading["temp"],
                    reading["humi"],
                    reading["light"],
                    reading.get("source_host", ""),
                ),
            )
            self.connection.commit()
            return cursor.rowcount > 0

    def query(self, start, end, limit):
        with self.lock:
            summary = self.connection.execute(
                """
                SELECT
                    COUNT(*) AS count,
                    MIN(recorded_at) AS earliest,
                    MAX(recorded_at) AS latest
                FROM sensor_readings
                WHERE recorded_at BETWEEN ? AND ?
                """,
                (start, end),
            ).fetchone()
            total = summary["count"]

            downsampled = total > limit
            if downsampled:
                data_start = summary["earliest"]
                data_end = summary["latest"]
                bucket_ms = max(1, (data_end - data_start) // limit)
                rows = self.connection.execute(
                    """
                    SELECT
                        MIN(recorded_at) AS recorded_at,
                        AVG(temp) AS temp,
                        AVG(humi) AS humi,
                        CAST(ROUND(AVG(light)) AS INTEGER) AS light
                    FROM sensor_readings
                    WHERE recorded_at BETWEEN ? AND ?
                    GROUP BY CAST((recorded_at - ?) / ? AS INTEGER)
                    ORDER BY recorded_at ASC
                    LIMIT ?
                    """,
                    (start, end, data_start, bucket_ms, limit),
                ).fetchall()
            else:
                rows = self.connection.execute(
                    """
                    SELECT recorded_at, temp, humi, light
                    FROM sensor_readings
                    WHERE recorded_at BETWEEN ? AND ?
                    ORDER BY recorded_at ASC
                    LIMIT ?
                    """,
                    (start, end, limit),
                ).fetchall()

        return {
            "records": [
                {
                    "timestamp": row["recorded_at"],
                    "temp": row["temp"],
                    "humi": row["humi"],
                    "light": row["light"],
                }
                for row in rows
            ],
            "total": total,
            "returned": len(rows),
            "downsampled": downsampled,
        }

    def stats(self):
        with self.lock:
            row = self.connection.execute(
                """
                SELECT
                    COUNT(*) AS count,
                    MIN(recorded_at) AS earliest,
                    MAX(recorded_at) AS latest
                FROM sensor_readings
                """
            ).fetchone()
        return {
            "count": row["count"],
            "earliest": row["earliest"],
            "latest": row["latest"],
            "path": self.path,
        }

    def close(self):
        with self.lock:
            self.connection.close()


def state_snapshot():
    with state_lock:
        return dict(state)


def update_state(**changes):
    with state_lock:
        state.update(changes)
        snapshot = dict(state)
    broadcast_sse("status", json.dumps(snapshot, ensure_ascii=False))
    return snapshot


def sensor_snapshot():
    with data_lock:
        return dict(sensor_data)


def update_sensor(**values):
    changed = False
    with data_lock:
        for key in ("temp", "humi", "light"):
            if key not in values or values[key] is None:
                continue
            try:
                value = float(values[key])
                if key == "light":
                    value = round(value)
                sensor_data[key] = value
                changed = True
            except (TypeError, ValueError):
                continue
        snapshot = dict(sensor_data)

    if changed:
        try:
            timestamp = int(values.get("timestamp", int(time.time() * 1000)))
        except (TypeError, ValueError):
            timestamp = int(time.time() * 1000)
        if timestamp < 1_000_000_000_000:
            timestamp *= 1000
        snapshot["timestamp"] = timestamp
        if database and all(snapshot[key] is not None for key in ("temp", "humi", "light")):
            reading = dict(snapshot)
            reading["source_host"] = state_snapshot().get("host", "")
            try:
                database.insert(reading)
            except sqlite3.Error as exc:
                print(f"[DB] 写入失败: {exc}")
        broadcast_sse("data", json.dumps(snapshot, ensure_ascii=False))
    return changed


def broadcast_sse(event, data):
    message = f"event: {event}\ndata: {data}\n\n"
    with sse_lock:
        clients = list(sse_clients)
    for client in clients:
        try:
            client.put_nowait(message)
        except queue.Full:
            try:
                client.get_nowait()
                client.put_nowait(message)
            except (queue.Empty, queue.Full):
                pass


def sse_write(wfile, text):
    wfile.write(text.encode("utf-8"))
    wfile.flush()


def sse_generator(wfile):
    client = queue.Queue(maxsize=100)
    with sse_lock:
        sse_clients.append(client)
    try:
        sse_write(
            wfile,
            f"event: status\ndata: {json.dumps(state_snapshot(), ensure_ascii=False)}\n\n",
        )
        initial = sensor_snapshot()
        initial["timestamp"] = int(time.time() * 1000)
        sse_write(
            wfile,
            f"event: data\ndata: {json.dumps(initial, ensure_ascii=False)}\n\n",
        )
        while True:
            try:
                sse_write(wfile, client.get(timeout=15))
            except queue.Empty:
                sse_write(wfile, "event: ping\ndata: {}\n\n")
    except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
        pass
    finally:
        with sse_lock:
            if client in sse_clients:
                sse_clients.remove(client)


def get_local_ips():
    ips = []
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.settimeout(1)
        sock.connect(("8.8.8.8", 80))
        ips.append(sock.getsockname()[0])
    except OSError:
        pass
    finally:
        sock.close()

    try:
        for ip in socket.gethostbyname_ex(socket.gethostname())[2]:
            if ip not in ips:
                ips.append(ip)
    except OSError:
        pass
    return [ip for ip in ips if not ip.startswith("127.")]


def port_is_open(ip, port, timeout, stop_event):
    if stop_event and stop_event.is_set():
        return None
    try:
        with socket.create_connection((ip, port), timeout=timeout):
            return ip
    except OSError:
        return None


def scan_qt_services(port=25577, timeout=0.25, stop_event=None):
    if port_is_open("127.0.0.1", port, timeout, stop_event):
        return ["127.0.0.1"]

    subnets = []
    for ip in get_local_ips():
        parts = ip.split(".")
        if len(parts) == 4:
            subnet = ".".join(parts[:3]) + "."
            if subnet not in subnets:
                subnets.append(subnet)
    if not subnets:
        subnets = ["192.168.1.", "192.168.0.", "10.0.0."]

    targets = [f"{subnet}{host}" for subnet in subnets for host in range(1, 255)]
    found = []
    with ThreadPoolExecutor(max_workers=64) as executor:
        futures = [
            executor.submit(port_is_open, ip, port, timeout, stop_event)
            for ip in targets
        ]
        for future in as_completed(futures):
            if stop_event and stop_event.is_set():
                for pending in futures:
                    pending.cancel()
                break
            ip = future.result()
            if ip:
                found.append(ip)
    return sorted(found, key=lambda ip: tuple(int(part) for part in ip.split(".")))


def handle_qt_message(message):
    message_type = message.get("type")
    if message_type == "sensor":
        update_sensor(**message)
        return
    if message_type != "status":
        return

    update_state(
        connected=bool(message.get("connected")),
        connecting=False,
        scanning=False,
        host=str(message.get("host") or ""),
        mode="qt",
        status=str(message.get("status") or "设备未连接"),
        error=str(message.get("error") or ""),
        hub_connected=True,
    )


def qt_reader(host, port, stop_event):
    while not stop_event.is_set():
        sock = None
        try:
            update_state(
                connected=False,
                connecting=True,
                scanning=False,
                host="",
                mode="qt",
                status=f"正在连接 Qt 数据服务 {host}:{port}",
                error="",
                hub_connected=False,
                hub_host=host,
            )
            sock = socket.create_connection((host, port), timeout=5)
            if stop_event.is_set():
                return
            sock.settimeout(1)
            update_state(
                connecting=False,
                status=f"已连接 Qt 数据服务 {host}:{port}，等待设备状态",
                hub_connected=True,
                hub_host=host,
            )

            buffer = b""
            while not stop_event.is_set():
                try:
                    chunk = sock.recv(4096)
                except socket.timeout:
                    continue
                if not chunk:
                    raise ConnectionError("Qt 数据服务已断开")
                buffer += chunk
                while b"\n" in buffer:
                    raw_line, buffer = buffer.split(b"\n", 1)
                    if not raw_line.strip():
                        continue
                    try:
                        message = json.loads(raw_line)
                    except (json.JSONDecodeError, UnicodeDecodeError):
                        continue
                    if isinstance(message, dict):
                        handle_qt_message(message)
        except (ConnectionError, OSError) as exc:
            if not stop_event.is_set():
                update_state(
                    connected=False,
                    connecting=False,
                    status=f"无法连接 Qt 数据服务 {host}:{port}",
                    error=str(exc),
                    hub_connected=False,
                    hub_host=host,
                )
        finally:
            if sock:
                try:
                    sock.close()
                except OSError:
                    pass

        if not stop_event.wait(3):
            update_state(
                connecting=True,
                status=f"正在重连 Qt 数据服务 {host}:{port}",
                hub_connected=False,
            )


def start_qt_connection(host):
    global connection_stop, connection_thread
    if scan_stop:
        scan_stop.set()
    with connection_lock:
        if connection_stop:
            connection_stop.set()
        stop_event = threading.Event()
        connection_stop = stop_event
        connection_thread = threading.Thread(
            target=qt_reader,
            args=(host, qt_port, stop_event),
            daemon=True,
            name=f"qt-data-{host}",
        )
        connection_thread.start()


def disconnect_qt():
    global connection_stop, connection_thread
    if scan_stop:
        scan_stop.set()
    with connection_lock:
        if connection_stop:
            connection_stop.set()
        connection_stop = None
        connection_thread = None
    update_state(
        connected=False,
        connecting=False,
        scanning=False,
        host="",
        status="已断开 Qt 数据服务",
        error="",
        hub_connected=False,
        hub_host="",
    )


def start_auto_scan():
    global scan_stop
    if not scan_lock.acquire(blocking=False):
        return False

    with connection_lock:
        if connection_stop:
            connection_stop.set()
    stop_event = threading.Event()
    scan_stop = stop_event

    def worker():
        try:
            update_state(
                connected=False,
                connecting=False,
                scanning=True,
                host="",
                status="正在搜索 Qt 数据服务",
                error="",
                hub_connected=False,
            )
            found = scan_qt_services(qt_port, stop_event=stop_event)
            if stop_event.is_set():
                return
            if found:
                update_state(
                    scanning=False,
                    status=f"发现 Qt 数据服务 {found[0]}，准备连接",
                    hub_host=found[0],
                )
                start_qt_connection(found[0])
            else:
                update_state(
                    scanning=False,
                    status="未找到 Qt 数据服务",
                    error=f"没有发现开放 {qt_port} 端口的 Qt 上位机",
                )
        finally:
            scan_lock.release()

    threading.Thread(target=worker, daemon=True, name="qt-service-scan").start()
    return True


class SensorHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)

        if parsed.path == "/data":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            sse_generator(self.wfile)
            return

        if parsed.path == "/api/status":
            snapshot = state_snapshot()
            snapshot["sensor"] = sensor_snapshot()
            self.send_json(snapshot)
            return

        if parsed.path == "/api/history":
            query = parse_qs(parsed.query)
            now = int(time.time() * 1000)
            try:
                end = int(query.get("end", [now])[0])
                start = int(query.get("start", [end - 3600000])[0])
                limit = int(query.get("limit", [1200])[0])
            except (TypeError, ValueError):
                self.send_json({"ok": False, "error": "历史查询参数格式不正确"}, 400)
                return
            limit = max(10, min(limit, 5000))
            if start > end:
                self.send_json({"ok": False, "error": "开始时间不能晚于结束时间"}, 400)
                return
            result = database.query(start, end, limit)
            result.update({"ok": True, "start": start, "end": end})
            self.send_json(result)
            return

        if parsed.path == "/api/history/stats":
            result = database.stats()
            result["ok"] = True
            self.send_json(result)
            return

        if parsed.path == "/api/connect":
            host = parse_qs(parsed.query).get("host", [""])[0].strip()
            try:
                socket.inet_aton(host)
            except OSError:
                self.send_json({"ok": False, "error": "IP 地址格式不正确"}, 400)
                return
            start_qt_connection(host)
            self.send_json({"ok": True, "connecting": host, "mode": "qt"})
            return

        if parsed.path == "/api/auto_connect":
            started = start_auto_scan()
            message = "" if started else "搜索已在进行"
            self.send_json({"ok": True, "scanning": True, "message": message})
            return

        if parsed.path == "/api/disconnect":
            disconnect_qt()
            self.send_json({"ok": True, "status": "已断开 Qt 数据服务"})
            return

        if parsed.path in ("/", ""):
            self.path = "/index.html"

        if self.path.endswith(".html"):
            filepath = os.path.join(os.path.dirname(__file__), self.path.lstrip("/"))
            try:
                with open(filepath, "rb") as file:
                    body = file.read()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except FileNotFoundError:
                self.send_error(404)
            return

        super().do_GET()

    def send_json(self, data, status=200):
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        pass


def main():
    global qt_port, database
    web_port = 8080
    qt_host = "127.0.0.1"

    args = sys.argv[1:]
    index = 0
    while index < len(args):
        arg = args[index]
        if arg == "--port" and index + 1 < len(args):
            web_port = int(args[index + 1])
            index += 2
        elif arg == "--qt-port" and index + 1 < len(args):
            qt_port = int(args[index + 1])
            index += 2
        elif not arg.startswith("--"):
            qt_host = arg
            index += 1
        else:
            index += 1

    os.chdir(os.path.dirname(__file__))
    database = SensorDatabase(os.path.join(os.path.dirname(__file__), "data", "sensor.db"))
    server = ThreadingHTTPServer(("0.0.0.0", web_port), SensorHandler)
    start_qt_connection(qt_host)
    print(f"Web 服务器: http://localhost:{web_port}")
    print(f"Qt 数据源: {qt_host}:{qt_port}")
    print(f"SQLite 数据库: {database.path}")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n服务已停止")
    finally:
        if connection_stop:
            connection_stop.set()
        server.server_close()
        database.close()


if __name__ == "__main__":
    main()
