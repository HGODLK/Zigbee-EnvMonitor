#!/usr/bin/env python3
"""
环境信息数据采集系统 — WiFi 数据接收端
========================================
连接 ESP8266 TCP Server (端口 25576)，接收传感器 JSON 数据，实时显示并存入 CSV。

用法:
    python wifi_receiver.py <ESP8266_IP> [--port 25576] [--csv data.csv]

示例:
    python wifi_receiver.py 192.168.1.100
    python wifi_receiver.py 192.168.1.100 --csv 20260609_sensor.csv

如何获取 ESP8266 的 IP:
    1. 协调器串口 (9600) 上电后会打印 ESP8266 AT 调试信息
    2. 在 CWJAP 成功后 ESP8266 会回复 WIFI GOT IP 及分配的地址
    3. 也可以登录路由器 mljtb 管理页查看 DHCP 客户端列表
"""

import socket
import json
import sys
import time
import csv
import os
import argparse
from datetime import datetime


def format_line(data: dict) -> str:
    """格式化一行传感器显示"""
    now = datetime.now().strftime("%H:%M:%S")
    parts = [now]

    if "temp" in data:
        parts.append(f"T={data['temp']:.1f}°C")
    if "humi" in data:
        parts.append(f"H={data['humi']:.1f}%")
    if "light" in data:
        parts.append(f"Light={data['light']}%")

    return " | ".join(parts)


def main():
    parser = argparse.ArgumentParser(description="WiFi 传感器数据接收端")
    parser.add_argument("host", nargs="?", default=None,
                        help="ESP8266 的 IP 地址 (不填则自动扫描)")
    parser.add_argument("--port", type=int, default=25576,
                        help="TCP 端口 (默认 25576)")
    parser.add_argument("--csv", type=str, default=None,
                        help="CSV 输出文件路径 (默认自动生成)")
    parser.add_argument("--scan", action="store_true",
                        help="扫描局域网内可能的 ESP8266 设备")
    args = parser.parse_args()

    # ==================== 自动扫描模式 ====================
    if args.scan or args.host is None:
        print("=== 扫描局域网内 ESP8266 TCP Server (port 25576) ===")
        host = scan_esp8266(args.port)
        if host is None:
            print("未找到 ESP8266，请手动指定 IP: python wifi_receiver.py <IP>")
            sys.exit(1)
    else:
        host = args.host

    # ==================== CSV 文件 ====================
    csv_path = args.csv
    if csv_path is None:
        csv_path = f"sensor_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"

    file_exists = os.path.exists(csv_path)
    csv_file = open(csv_path, "a", newline="", encoding="utf-8-sig")
    csv_writer = csv.writer(csv_file)
    if not file_exists:
        csv_writer.writerow(["timestamp", "temp", "humi", "light"])
        csv_file.flush()

    print(f"CSV 输出: {csv_path}")
    print(f"连接目标: {host}:{args.port}")
    print("等待数据... (Ctrl+C 退出)\n")

    # ==================== 主循环 (自动重连) ====================
    reconnect_delay = 1

    while True:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(30)
            print(f"[{datetime.now():%H:%M:%S}] 正在连接 {host}:{args.port}...")
            sock.connect((host, args.port))
            print(f"[{datetime.now():%H:%M:%S}] ✓ 已连接")
            reconnect_delay = 1  # reset backoff

            buf = b""
            while True:
                try:
                    chunk = sock.recv(4096)
                    if not chunk:
                        print(f"[{datetime.now():%H:%M:%S}] ✗ 连接断开，重连中...")
                        break

                    buf += chunk

                    # 尝试解析完整的 JSON 行
                    while True:
                        # JSON 以 \r\n 或 \n 分隔
                        for sep in [b"\r\n", b"\n"]:
                            idx = buf.find(sep)
                            if idx >= 0:
                                line = buf[:idx].strip()
                                buf = buf[idx + len(sep):]
                                if line:
                                    process_line(line, csv_writer, csv_file)
                                break
                        else:
                            break  # 没有找到分隔符，继续收数据

                except socket.timeout:
                    continue  # 正常超时，继续等待
                except (ConnectionResetError, ConnectionAbortedError,
                        BrokenPipeError, OSError) as e:
                    print(f"[{datetime.now():%H:%M:%S}] ✗ 连接异常: {e}")
                    break

        except (ConnectionRefusedError, socket.timeout, OSError) as e:
            print(f"[{datetime.now():%H:%M:%S}] ✗ 连接失败: {e}")
            print(f"     {reconnect_delay}s 后重试...")
            time.sleep(reconnect_delay)
            reconnect_delay = min(reconnect_delay * 2, 30)

        finally:
            try:
                sock.close()
            except Exception:
                pass


def process_line(line: bytes, csv_writer, csv_file):
    """处理一行接收到的数据"""
    try:
        text = line.decode("utf-8", errors="replace")
    except Exception:
        return

    # 尝试解析 JSON
    data = None
    try:
        data = json.loads(text)
    except json.JSONDecodeError:
        pass

    if data is None:
        # 可能是 ESP8266 的 AT 回显或其他非 JSON 数据
        # 尝试从文本中提取 JSON 对象
        import re
        m = re.search(r'\{[^}]+\}', text)
        if m:
            try:
                data = json.loads(m.group())
            except json.JSONDecodeError:
                pass

    if data is None:
        # 非 JSON 数据，直接打印
        if text.strip():
            print(f"[RAW] {text.strip()}")
        return

    # 格式化显示
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(format_line(data))

    # 写入 CSV
    temp = data.get("temp", "")
    humi = data.get("humi", "")
    light = data.get("light", "")
    csv_writer.writerow([ts, temp, humi, light])
    csv_file.flush()


def scan_esp8266(port: int, timeout: float = 0.3) -> str | None:
    """快速扫描局域网中监听指定端口的设备"""
    import subprocess
    import platform

    # 获取本机 IP 和子网
    local_ip = get_local_ip()
    if local_ip is None:
        print("无法获取本机 IP，跳过扫描")
        return None

    # 假设 /24 子网
    parts = local_ip.split(".")
    base = f"{parts[0]}.{parts[1]}.{parts[2]}."

    print(f"本机 IP: {local_ip}，扫描 {base}0/24...")

    found = []
    for i in range(1, 255):
        ip = f"{base}{i}"
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(timeout)
            result = sock.connect_ex((ip, port))
            sock.close()
            if result == 0:
                found.append(ip)
                print(f"  发现: {ip}:{port}")
        except Exception:
            pass

    if len(found) == 0:
        return None
    if len(found) == 1:
        return found[0]
    print(f"\n发现多个设备，请选择:")
    for idx, ip in enumerate(found):
        print(f"  [{idx}] {ip}")
    choice = input("选择序号: ").strip()
    try:
        return found[int(choice)]
    except (ValueError, IndexError):
        return found[0]


def get_local_ip() -> str | None:
    """获取本机局域网 IP"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.connect(("8.8.8.8", 80))
        ip = sock.getsockname()[0]
        sock.close()
        return ip
    except Exception:
        return None


if __name__ == "__main__":
    main()
