# 环境信息数据采集综合系统 — Zigbee 透传固件

## 快速开始

### 编译
1. IAR EW8051 6.0+ 打开 `Projects/zstack/Utilities/SerialApp/CC2530DB/SerialApp.eww`
2. 选择配置: `CoordinatorEB-Pro`(协调器) / `EndDeviceEB-Pro`(终端)
3. F7 编译

### 烧录
使用 SmartRF Flash Programmer 命令行工具，**每次烧录前需先刷 EB 固件**：
```
SmartRFProgConsole.exe AU EPV F="fw0400.hex"    # 刷 EB
SmartRFProgConsole.exe S EPV F="SerialApp.hex"  # 烧 CC2530
```

### 测试
1. 板1(协调器) + 板2(终端) 各接 USB-TTL (P0_2/P0_3/GND)
2. 串口助手: 9600-8N1
3. 先给协调器上电 → 收到 `HELLO`
4. 再给终端上电 → 收到 `HELLO` + `parent:0x0000 self:0xXXXX`
5. 握手成功 → `< connect success>`
6. 任意串口发数据 → 对端接收

## 硬件
- 芯片: CC2530F256
- 调试器: SmartRF04EB
- 串口: UART0 (P0_2=RX, P0_3=TX), 9600bps

## 目录
```
ProJect/
├── Components/           # Z-Stack 系统组件
├── Projects/zstack/
│   ├── Utilities/SerialApp/  # 透明透传应用
│   │   └── CC2530DB/         # IAR 工程 (.eww/.ewp)
│   ├── Tools/CC2530DB/       # 编译配置 (.cfg)
│   ├── Libraries/            # 预编译库 (.lib)
│   └── ZMain/                # 启动代码
├── CLAUDE.md             # AI 助手指南
└── README.md             # 本文件
```
