# 环境信息数据采集综合系统 — 固件工程

## 项目概述

基于 TI Z-Stack 2.3.0-1.4.0 + CC2530F256 的 Zigbee 传感器网络。
协调器通过 OLED 显示终端节点状态和传感器数据，通过 ESP8266 WiFi 模块将传感器数据上云，支持多终端 Zigbee 无线采集。

## 当前进度

### ✅ 已完成

| 功能 | 状态 |
|------|------|
| Zigbee 透传链路（协调器 ↔ 终端） | ✅ |
| UART DMA 驱动 (9600-8N1, P0_2/P0_3) | ✅ |
| OLED SSD1306 128×64 显示 (SCL=P1_2, SDA=P1_3, RST=P1_7, DC=P0_0) | ✅ |
| S1 按键三屏切换 (P0_1, HAL_KEY_SW_6) | ✅ |
| 协调器网络建网 + 子节点追踪 | ✅ |
| CONNECTREQ/CONNECTRSP 握手协议 | ✅ |
| TEM1 — DHT11 温湿度传感器 (P0_7) | ✅ |
| LIGHT2 — 光敏传感器 ADC (AO=P0_6, DO=P0_5) | ✅ |
| DHT11 + 光敏传感器校准 | ✅ |
| 终端每 1 秒上报传感器数据 | ✅ |
| 协调器 OLED 显示实时温湿度/光照 | ✅ |
| 心跳超时 15 秒，断联自动清除 | ✅ |
| 心跳追踪 — 源地址匹配 (原硬编码索引) | ✅ |
| DEVICE_TYPE 宏切换 TEM1/LIGHT2 | ✅ |
| ESP8266 WiFi 驱动 — UART1 通信 (P0_4/P0_5, 115200) | ✅ |
| ESP8266 AT 状态机 — 配网/建服务器/数据发送 | ✅ |
| WiFi 数据上传 — 每 1 秒 JSON (原 10s) | ✅ |
| ESP8266 无 CONNECT 检测也能发数据 | ✅ |
| OLED 实时显示 WiFi 状态 (g_wifiState 变化自动刷新) | ✅ |
| 协调器串口输出 RCV-TEM1 / RCV-LIGHT2 | ✅ |
| SmartRF04EB USB 固件损坏修复 (AU EPV fw0400.hex) | ✅ |
| Web 仪表盘 — SSE 实时推送 + CSV 存盘 | ✅ |
| Qt 上位机 — TCP 客户端 + 实时曲线 + CSV | ✅ |

### 🔧 关键排错记录

**ESP8266 UART1 TX 不工作 — U1UCR 停止位电平**

- 现象：CC2530 RX 能收到 ESP8266 启动信息，但 AT 命令发出后 ESP8266 不回复
- 根因：`U1UCR` 复位值 `0x00`，其中 STOP bit 为 0（停止位低电平），ESP8266 收到帧错误
- 解决：`U1UCR = 0x02;` → 停止位高电平，标准 UART 8N1
- 辅助修复：`ADCCFG &= ~0x30;` 关闭 P0_4/P0_5 ADC，`U1BAUD = 216;` 覆盖 Z-Stack HAL 残留值
- Z-Stack HAL 会预配置部分 UART1 寄存器，裸机 BasicRF 示例代码不适用于 Z-Stack 环境

**ESP8266 TCP 无数据 — CONNECT 检测失败 + JSON 无换行**

- 现象：PC 能连上 10.25.2.31:25576 但收不到数据，OLED 显示 `WiFi:OK NoCli`
- 根因①：ESP8266 AT 固件不发送 `CONNECT` 通知（依赖固件版本），CC2530 的 `u1_has("CONNECT")` 永远为 false，`conn` 标志不置位
- 根因②：`ticks` 是 `uint8`，`ticks > 300` 的 CWJAP 超时永不为真（255 溢出），但实际不影响（ESP8266 回 OK 后正常流转）
- 解决①：`esp8266.c` S_READY 去掉 `&& conn` 前置条件，直接尝试 AT+CIPSEND；同时检测 `LINK` 事件
- 解决②：S_SENDLEN/S_SENDDAT 增加 `ERROR`/`SEND FAIL` 快速失败检测，避免 2s 超时
- 根因③：ESP8266 通过 TCP 发送的 JSON **不带 `\n` 换行**（`AT+CIPSEND` 只传 N 字节 body），PC 端 parser 按 `\n` 分割永远等不到完整行
- 解决③：服务端改为 `{ }` 括号深度匹配提取 JSON 对象

**SmartRF04EB 固件损坏 → USB STALL (#E1)**

- 现象：IAR `Fatal error: Unknown exception in driver (#E1)`，命令行 `HC Error: stall PID`
- 根因：SmartRF04EB 内部固件损坏，每次 S EPV 操作都会触发
- 解决：每次烧录前必须 `AU EPV fw0400.hex` 重刷 EB 固件
- 详情：[SmartRF04EB排错指南.md](..\SmartRF04EB排错指南.md)

### 三块板部署

| 板号 | 固件配置 | 角色 | 传感器/WiFi | 引脚 |
|------|---------|------|--------|------|
| 板1 | CoordinatorEB-Pro | 协调器 | OLED + S1 + ESP8266 | OLED: P1_2/P1_3/P1_7/P0_0, S1: P0_1, ESP: P0_4/P0_5/P0_6 |
| 板2 | EndDeviceEB-Pro, DEVICE_TYPE=1 | TEM1 | DHT11 温湿度 | P0_7 |
| 板3 | EndDeviceEB-Pro, DEVICE_TYPE=2 | LIGHT2 | 光敏传感器 | AO=P0_6, DO=P0_5 |

## 目录结构

```
ProJect/
├── Components/              # Z-Stack 系统组件
│   └── hal/target/CC2530EB/
│       ├── hal_board_cfg.h  # 板级配置 (LED/UART/DMA/时钟)
│       ├── hal_lcd.c        # SSD1306 OLED 驱动
│       └── hal_uart.c       # UART DMA 驱动
├── Projects/zstack/
│   ├── Tools/CC2530DB/      # 编译配置
│   │   ├── f8wCoord.cfg     # 协调器: ZDO_COORDINATOR + RTR_NWK
│   │   ├── f8wEndev.cfg     # 终端
│   │   └── f8wConfig.cfg    # 公共: 信道11, PAN=0x1223, 安全关
│   ├── Utilities/SerialApp/
│   │   ├── Source/
│   │   │   ├── SerialApp.c  # 主应用 (Zigbee+传感器+OLED+按键+WiFi)
│   │   │   ├── SerialApp.h  # 簇定义/端点/Profile
│   │   │   ├── esp8266.c    # ESP8266 WiFi 驱动 (AT 状态机)
│   │   │   ├── esp8266.h    # WiFi 状态定义 + API
│   │   │   ├── DHT11.c/h    # DHT11 温湿度驱动
│   │   │   └── OSAL_SerialApp.c
│   │   └── CC2530DB/
│   │       ├── SerialApp.eww  # IAR 工作空间
│   │       ├── SerialApp.ewp  # IAR 工程
│   │       ├── CoordinatorEB-Pro/  # 协调器输出 hex
│   │       └── EndDeviceEB-Pro/    # 终端输出 hex
│   ├── Libraries/           # 预编译库
│   └── ZMain/               # 启动代码
├── web/                     # Web 仪表盘
│   ├── server.py            # Python HTTP + SSE 服务器
│   └── index.html           # 前端仪表盘页面
├── upper_computer/          # Qt 上位机
│   ├── CMakeLists.txt
│   ├── src/                 # 源码 (mainwindow/tcpclient/dashboard/chartview...)
│   ├── build/               # 编译产物
│   └── deploy/              # windeployqt 打包发布 (独立运行)
├── wifi_receiver.py         # 独立 WiFi 数据接收脚本 (CSV 存盘)
├── ESP8266调试记录.md
├── README.md
└── CLAUDE.md
```

## 硬件引脚

> **注意**：P0_5、P0_6 在不同板上复用不同功能，编译时通过 `ZDO_COORDINATOR` 宏区分。

| 功能 | 引脚 | 适用板 |
|------|------|--------|
| UART0 RX/TX (Debug) | P0_2 / P0_3 | 全部 |
| UART1 RX/TX (ESP8266) | P0_4 / P0_5 | 板1 |
| ESP8266 RST/IGT | P0_6 (低电平复位，1s 脉冲) | 板1 |
| OLED SCL/SDA/RST/DC | P1_2 / P1_3 / P1_7 / P0_0 | 板1 |
| S1 按键 | P0_1 (HAL_KEY_SW_6) | 板1 |
| LED D1/D2/D3 | P1_0 / P1_1 / P0_4 | 板1 |
| TEM1 DHT11 | P0_7 | 板2 |
| LIGHT2 光敏 AO/DO | P0_6 / P0_5 | 板3 |

## ESP8266 WiFi 模块

### 连接

| ESP8266 | CC2530 板1 (协调器) |
|---------|-------------------|
| RX | P0_5 (TX1, UART1 Alt 1) |
| TX | P0_4 (RX1, UART1 Alt 1) |
| RST | 板子 RST |
| EN | 板子 EN (上拉) |
| IO0 | NC (悬空) |
| IO2 | PI (板子标注) |

### UART1 初始化

```c
PERCFG &= ~0x02;    // UART1 Alt 1: P0_4/P0_5
P0SEL |= 0x30;      // P0_4,P0_5 as peripheral
ADCCFG &= ~0x30;    // 关闭 P0_4/P0_5 ADC（Z-Stack HAL 可能开启了）
U1CSR = 0x80;       // UART mode
U1GCR = 11;         // BAUD_E = 11
U1UCR = 0x02;       // 停止位高电平（关键！默认0x00会导致帧错误）
U1BAUD = 216;       // BAUD_M = 216 → 115200 @ 32MHz（赋值不是|=，避免 HAL 残留）
URX1IE = 1;         // 使能 RX 中断
URX1IF = 0; UTX1IF = 0;
U1CSR |= 0x40;      // Enable RX
```

### AT 状态机流程

```
S_BOOT (4s) → AT+CWMODE=1
  ↓ OK
S_CWMODE (2s超时) → AT+CWJAP="SSID","PWD"
  ↓ OK
S_CWJAP (15s超时) → AT+CIPMUX=1
  ↓ OK
S_CIPMUX → AT+CIPSERVER=1,25576
  ↓ OK
S_READY → 每 1s 尝试 AT+CIPSEND（不依赖 conn 检测）
  ↓ 收到 > → 发送 JSON → S_SENDDAT → SEND OK → S_READY
  ↓ 收到 ERROR/link is not → 直接回 S_READY（无客户端）
  ↓ 同时检测 CONNECT/LINK/CLOSED 事件

任何步骤超时 → S_FAIL (2s) → 硬件复位 ESP8266 → 回到 S_BOOT
```

### WiFi 状态显示

| g_wifiState | OLED 显示 | 含义 |
|-------------|-----------|------|
| 0 | WiFi:Init | ESP8266 上电启动中 |
| 1 | WiFi:Connecting | 正在配网/连接热点 |
| 2 | WiFi:OK Client | 热点已连接 + TCP 客户端在线 |
| 2 | WiFi:OK NoCli | 热点已连接，等待客户端 |
| 3 | WiFi:Fail | 超时失败，自动复位重试 |

OLED 随 `g_wifiState` 变化自动刷新 — 在 `SERIALAPP_WIFI_EVT` 中用 `static lastWifiState` 追踪状态变化，变化时调用 `UpdateOled()`（仅 `dispMode==0`）。

### WiFi 凭据

- SSID: `mljtb`
- 密码: `12345678`
- TCP 端口: `25576`
- ESP8266 IP: `10.25.2.31`（DHCP，可能变化）

## Zigbee 网络参数

| 参数 | 值 |
|------|-----|
| PAN ID | 0x1223 |
| 信道 | 11 (2.405 GHz) |
| Profile ID | 0x0F05 |
| Endpoint | 11 |
| 安全 | 关闭 |

## 协议定义

### 簇 (Cluster)

| 簇 ID | 名称 | 方向 | 说明 |
|-------|------|------|------|
| 1 | CLUSTERID1 | 双向 | 串口透传数据 |
| 2 | CLUSTERID2 | 双向 | 流控响应 (ACK/NAK) |
| 3 | CONNECTREQ | 终端→协调器 | 终端入网握手请求 (2字节: 短地址) |
| 4 | CONNECTRSP | 协调器→终端 | 握手响应 (2字节: 短地址) |
| 5 | SENSOR_CLUSTER | 终端→协调器 | 传感器数据上报 |

### 传感器数据帧格式 (Cluster 5)

**TEM1 (0x01)**: `[01][temp_int][temp_dec][humi_int][humi_dec]` (5字节)

**LIGHT2 (0x02)**: `[02][light_val][00]` (3字节)

### TCP JSON 数据格式 (ESP8266 → 服务器)

```json
{"temp":25.6,"humi":56.0,"light":75}
```

## 编译

1. IAR EW8051 6.0+ 打开 `Projects/zstack/Utilities/SerialApp/CC2530DB/SerialApp.eww`
2. 选择目标: `CoordinatorEB-Pro` / `EndDeviceEB-Pro`
3. **终端类型**: 修改 `SerialApp.c` 中 `DEVICE_TYPE` 宏:
   - `#define DEVICE_TYPE 1` → TEM1 (DHT11)
   - `#define DEVICE_TYPE 2` → LIGHT2 (光敏)
4. F7 编译

## 烧录

**每次烧录前必须先刷 EB 固件**（EB 固件在 S EPV 后必定损坏）:

```powershell
$flashCli = "C:\Program Files (x86)\Texas Instruments\SmartRF Tools\Flash Programmer\bin\SmartRFProgConsole.exe"
$fwPath = "C:\Program Files (x86)\Texas Instruments\SmartRF Tools\Firmware\SmartRF04EB\fw0400.hex"

# 步骤1: 刷 EB 固件（必须）
& $flashCli AU EPV F=$fwPath
# 步骤2: 烧录 CC2530
& $flashCli S EPV F="hex路径"
```

> **不要用 IAR Ctrl+D 下载**——IAR 的 Chipcon 驱动也会损坏 EB 固件。用 IAR 编译（F7），用命令行烧录。

## OLED 显示模式 (S1 切换)

| 模式 | 行1 | 行2 | 行3 | 行4 |
|------|-----|-----|-----|-----|
| 0 — 节点+WiFi | Coord  Nodes: | TEM1: OK/---- | LIGHT2: OK/---- | WiFi:Init/Connecting/OK/Fail |
| 1 — TEM1 | TEM1  DHT11 | T: 25.6 C | H: 56.0 % | |
| 2 — LIGHT2 | LIGHT2 Light | Light: 75 % | | |

## 心跳与断联

- 协调器每 5 秒检查子节点心跳
- 收到传感器数据或 CONNECTREQ 时重置心跳
- 超过 15 秒无数据 → 子节点从列表移除 → OLED 更新

## 上位机

### Web 仪表盘 (`web/`)

基于 Python 标准库 + SSE 推送，浏览器实时显示传感器数据。

```powershell
# 启动 (自动扫描或指定 IP)
python web/server.py 10.25.2.31

# 浏览器打开
http://localhost:8080
```

- 使用 `ThreadingHTTPServer`（SSE 长连接不能阻塞其他请求）
- JSON 解析使用 `{ }` 括号深度匹配（数据不带 `\n`）
- 支持串口模式 (`python server.py COM8`) 作为备用

### Qt 上位机 (`upper_computer/`)

Qt 6.11.1 + MinGW，CMake + Ninja 构建。

```powershell
# 运行 (自带 DLL)
upper_computer/deploy/EnvMonitor.exe
```

- 左侧面板：IP 输入 + 自动扫描 + 连接
- 主仪表盘：温度/湿度/光照 大字卡片
- 实时曲线：QChart 三条折线，120 秒滑动窗口
- 自动 CSV 存盘 (`data/` 目录)

编译：
```powershell
$env:PATH = "C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;$env:PATH"
cd upper_computer\build
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release
ninja
windeployqt deploy\EnvMonitor.exe --release
```

> **注意**：ESP8266 CIPSERVER 只支持 **1 个 TCP 客户端**。网站和 Qt 不能同时连接。

## 下一步

- [ ] 服务器端数据解析入库 (SQLite/MySQL)
- [ ] Web 历史数据回放
- [ ] Qt 增加串口直连模式（需安装 Qt SerialPort 模块）
