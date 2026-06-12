# 环境信息数据采集综合系统

基于 **TI Z-Stack 2.3.0-1.4.0 + CC2530F256** 的 Zigbee 传感器网络。  
协调器通过 OLED 显示终端节点状态和传感器数据，通过 ESP8266 WiFi 模块将数据上云，支持多终端 Zigbee 无线采集。

## 系统架构

```
┌──────────────┐      Zigbee       ┌──────────────────┐      TCP/JSON     ┌──────────────┐
│  终端 TEM1   │ ◄──────────────►  │   协调器 (板1)    │ ◄──────────────► │  Web 仪表盘  │
│  DHT11 温湿度 │    信道 11        │  OLED + ESP8266   │   10.25.2.31:25576│  SSE 实时推送 │
├──────────────┤    PAN 0x1223     ├──────────────────┤                   └──────────────┤
│  终端 LIGHT2 │                   │  S1 按键三屏切换   │                   ┌──────────────┐
│  光敏传感器   │                   │  心跳追踪 15s 超时  │                   │  Qt 上位机   │
└──────────────┘                   └──────────────────┘                   │  实时曲线    │
                                                                         └──────────────┘
```

## 功能特性

| 功能 | 状态 |
|------|------|
| Zigbee 透传链路（协调器 ↔ 终端） | ✅ |
| UART DMA 驱动 (9600-8N1) | ✅ |
| OLED SSD1306 128×64 显示 | ✅ |
| S1 按键三屏切换 | ✅ |
| 协调器网络建网 + 子节点追踪 | ✅ |
| CONNECTREQ/CONNECTRSP 握手协议 | ✅ |
| TEM1 — DHT11 温湿度传感器 | ✅ |
| LIGHT2 — 光敏传感器 ADC | ✅ |
| 终端每 1 秒上报传感器数据 | ✅ |
| 心跳超时 15 秒，断联自动清除 | ✅ |
| ESP8266 WiFi 驱动 — AT 状态机 | ✅ |
| WiFi 数据上传 — 每 1 秒 JSON | ✅ |
| OLED 实时显示 WiFi 状态 | ✅ |
| Web 仪表盘 — SSE 实时推送 + CSV | ✅ |
| Qt 上位机 — TCP 客户端 + 实时曲线 | ✅ |

## 快速开始

### 编译

1. IAR EW8051 6.0+ 打开 `Projects/zstack/Utilities/SerialApp/CC2530DB/SerialApp.eww`
2. 选择目标: `CoordinatorEB-Pro` / `EndDeviceEB-Pro`
3. **终端类型**（仅在终端固件中修改）:
   - `DEVICE_TYPE 1` → TEM1（DHT11 温湿度）
   - `DEVICE_TYPE 2` → LIGHT2（光敏传感器）
4. F7 编译

### 烧录

使用 SmartRF Flash Programmer 命令行工具烧录：

```powershell
$flashCli = "C:\Program Files (x86)\Texas Instruments\SmartRF Tools\Flash Programmer\bin\SmartRFProgConsole.exe"
& $flashCli S EPV F="Projects/zstack/Utilities/SerialApp/CC2530DB/CoordinatorEB-Pro/Exe/Coordinator.hex"
```

### 三块板部署

| 板号 | 固件 | 角色 | 传感器/WiFi | 引脚 |
|------|------|------|--------|------|
| 板1 | CoordinatorEB-Pro | 协调器 | OLED + S1 + ESP8266 | 见下方引脚表 |
| 板2 | EndDeviceEB-Pro, TYPE=1 | TEM1 | DHT11 温湿度 | P0_7 |
| 板3 | EndDeviceEB-Pro, TYPE=2 | LIGHT2 | 光敏传感器 | AO=P0_6, DO=P0_5 |

## 硬件引脚

> P0_5、P0_6 在不同板上复用不同功能，编译时通过 `ZDO_COORDINATOR` 宏区分。

| 功能 | 引脚 | 适用板 |
|------|------|--------|
| UART0 RX/TX (Debug) | P0_2 / P0_3 | 全部 |
| UART1 RX/TX (ESP8266) | P0_4 / P0_5 | 板1 |
| ESP8266 RST | P0_6 (低电平复位) | 板1 |
| OLED SCL/SDA/RST/DC | P1_2 / P1_3 / P1_7 / P0_0 | 板1 |
| S1 按键 | P0_1 (HAL_KEY_SW_6) | 板1 |
| LED D1/D2/D3 | P1_0 / P1_1 / P0_4 | 板1 |
| TEM1 DHT11 | P0_7 | 板2 |
| LIGHT2 AO/DO | P0_6 / P0_5 | 板3 |

## 网络参数

| 参数 | 值 |
|------|-----|
| PAN ID | `0x1223` |
| 信道 | 11 (2.405 GHz) |
| Profile ID | `0x0F05` |
| Endpoint | 11 |
| 安全 | 关闭 |

### WiFi 凭据

| 参数 | 值 |
|------|-----|
| SSID | `mljtb` |
| 密码 | `12345678` |
| TCP 端口 | `25576` |
| ESP8266 IP | `10.25.2.31`（DHCP，可能变化） |

> ⚠️ ESP8266 CIPSERVER 只支持 **1 个 TCP 客户端**。Web 和 Qt 不能同时连接。

## 显示模式 (S1 切换)

| 模式 | 行1 | 行2 | 行3 | 行4 |
|------|-----|-----|-----|-----|
| **0** — 节点+WiFi | Coord Nodes: | TEM1: OK/---- | LIGHT2: OK/---- | WiFi: 状态 |
| **1** — TEM1 | TEM1 DHT11 | T: 25.6 C | H: 56.0 % | |
| **2** — LIGHT2 | LIGHT2 Light | Light: 75 % | | |

## 协议

### 传感器数据帧 (Cluster 5)

| 类型 | 格式 |
|------|------|
| TEM1 (0x01) | `[01][temp_int][temp_dec][humi_int][humi_dec]` (5B) |
| LIGHT2 (0x02) | `[02][light_val][00]` (3B) |

### TCP JSON 格式 (ESP8266 → 服务器)

```json
{"temp":25.6,"humi":56.0,"light":75}
```

## 上位机

### Web 仪表盘

```powershell
# 启动 (自动扫描或指定 IP)
python sjk/web/server.py 10.25.2.31

# 浏览器打开
http://localhost:8080
```

- Python 标准库 + SSE 推送
- `{ }` 括号深度匹配解析 JSON（数据不带 `\n`）

### Qt 上位机

```powershell
# 直接运行（自带 DLL）
sjk/upper_computer/deploy/EnvMonitor.exe
```

- Qt 6.11.1 + MinGW，CMake + Ninja 构建
- 实时曲线：QChart 120 秒滑动窗口
- 自动 CSV 存盘 (`data/` 目录)

## 目录结构

```
ProJect/
├── Components/              # Z-Stack 系统组件
│   └── hal/target/CC2530EB/ # 板级驱动 (OLED/UART/DMA)
├── Projects/zstack/
│   ├── Tools/CC2530DB/      # 编译配置 (.cfg)
│   ├── Utilities/SerialApp/
│   │   ├── Source/          # ✦ 核心源码
│   │   │   ├── SerialApp.c  #   主应用 (Zigbee+传感器+OLED+WiFi)
│   │   │   ├── esp8266.c    #   ESP8266 AT 状态机驱动
│   │   │   ├── DHT11.c      #   DHT11 温湿度驱动
│   │   │   └── OSAL_SerialApp.c
│   │   └── CC2530DB/        # IAR 工程文件 + 预编译 .hex
│   ├── Libraries/           # 预编译库
│   └── ZMain/               # 启动代码
├── sjk/
│   ├── web/                 # Web 仪表盘 (server.py + index.html)
│   ├── upper_computer/      # Qt 上位机 (源码 + 构建 + 发布包)
│   └── 技术文档.md
├── wifi_receiver.py         # 独立 WiFi 数据接收脚本
├── CLAUDE.md                # AI 助手指南
└── README.md                # 本文件
```
