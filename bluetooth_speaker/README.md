# 蓝牙音响 — ESP32 A2DP 双芯架构

基于 ESP32 + ESP32-S3 的蓝牙音响系统，采用**双芯片架构**，通过 UART 串行通信实现功能分离。

## 架构概览

```
┌─────────────────────────┐      UART (115200)      ┌─────────────────────────┐
│   音频处理端 (ESP32)     │ ◄──────────────────────► │   屏幕显示端 (ESP32-S3)  │
│                         │     V:音量指令            │                         │
│  Bluetooth A2DP Sink    │     T:歌名元数据          │  ST7735 1.8" 彩屏      │
│  → I2S → PCM5102 DAC    │     A:歌手元数据          │  旋钮编码器 (音量调节)   │
│  MAX_SAFE_VOLUME = 35  │                          │  中文字库 (U8g2+wqy)    │
└─────────────────────────┘                          └─────────────────────────┘
```

### 音频处理端 (`audio_processor/`)

- **主控**: ESP32
- **功能**: 蓝牙 A2DP 接收 → I2S 输出 → PCM5102 DAC 解码
- **安全机制**: 硬件音量上限锁定 (`MAX_SAFE_VOLUME = 35`)，防止削波失真
- **元数据回调**: 将歌名/歌手通过 UART 发送至显示端
- **蓝牙名称**: `DeepSpace_HiFi_Link`

### 屏幕显示端 (`display_unit/`)

- **主控**: ESP32-S3
- **功能**: ST7735 1.8" 彩屏显示 + 旋钮编码器控制
- **显示内容**: 歌名 (GB2312 中文)、歌手、音量条
- **UART 通信**: 将旋钮音量指令发往音频处理端

### 增强版 (`display_unit_enhanced/`)

在基础版上增加了伪音频频谱动画，UI 分区更丰富：
- 顶部状态栏
- 歌名/播放状态区
- 歌词/AI 对话预留区
- 赛博朋克风格音频频谱 (80ms 刷新)
- 极细音量进度条

## 硬件引脚

### 音频处理端

| 功能 | 引脚 |
|------|------|
| I2S BCK | GPIO26 |
| I2S WS  | GPIO25 |
| I2S DATA | GPIO22 |
| UART TX | GPIO32 |
| UART RX | GPIO33 |

### 屏幕显示端

| 功能 | 引脚 |
|------|------|
| TFT CS  | GPIO10 |
| TFT RST | GPIO8  |
| TFT DC  | GPIO9  |
| TFT MOSI | GPIO11 |
| TFT SCLK | GPIO12 |
| 背光    | GPIO13 |
| 编码器 CLK | GPIO15 |
| 编码器 DT  | GPIO16 |
| 编码器 SW  | GPIO17 |
| UART TX | GPIO4  |
| UART RX | GPIO5  |

## 依赖库

- [ArduinoESP32](https://github.com/espressif/arduino-esp32)
- [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP)
- [Adafruit_GFX](https://github.com/adafruit/Adafruit-GFX-Library)
- [Adafruit_ST7735](https://github.com/adafruit/Adafruit-ST7735-Library)
- [U8g2_for_Adafruit_GFX](https://github.com/olikraus/U8g2_for_Adafruit_GFX)

## 安全说明

音频处理端内置音量安全锁：无论蓝牙源端（如 Windows）将音量设为多少，硬件输出上限始终被钳位在 `MAX_SAFE_VOLUME = 35`，防止 PCM5102 输入过载导致削波失真。
