# SmartRF04EB + CC2530 烧录排错指南

## 适用场景

- 调试器：SmartRF04EB（Cebal 驱动）
- 目标芯片：CC2530
- 开发环境：IAR Embedded Workbench
- 现象：IAR 报 `#E1` 错误，无法烧录/调试

## 标准烧录流程（每次必做）

**EB 固件在每次 `S EPV` 烧录 CC2530 后都会损坏，因此每次烧录前必须先重刷 EB 固件。**

```powershell
$flashCli = "C:\Program Files (x86)\Texas Instruments\SmartRF Tools\Flash Programmer\bin\SmartRFProgConsole.exe"
$fwPath  = "C:\Program Files (x86)\Texas Instruments\SmartRF Tools\Firmware\SmartRF04EB\fw0400.hex"

# 第一步：刷写 EB 固件（必须）
& $flashCli AU EPV F=$fwPath

# 第二步：烧录 CC2530
& $flashCli S EPV F="固件.hex"
```

如果第一步失败（`Could not locate any EBs`），拔插 EB USB 线后重试。

> **注意**：不要在 IAR 中使用 Ctrl+D 下载调试——IAR 的 Chipcon 驱动也会损坏 EB 固件。建议用 IAR 编译（F7），用命令行烧录。

## 典型错误信息

IAR 报错：
```
Fatal error: Unknown exception in driver (#E1) Session aborted!
Failed to load debugee: ...\SampleLight.d51
```

命令行报错：
```
USBDeviceHandle::controlTransferIn returned -536870908
Error code 0xE0000004: HC Error: stall PID.
Not able to reset SmartRF04EB
```
→ 说明跳过了标准流程的第一步，EB 固件已损坏。立即执行标准流程即可恢复。

## 排错流程

### 第一步：基础检查

- [ ] USB 线是否插紧
- [ ] 10-pin 排线方向是否正确（Pin 1 对齐，红色标记线为 Pin 1）
- [ ] 目标板是否已上电（3.3V）
- [ ] 关闭 IAR、SmartRF Studio、Packet Sniffer 等可能占用 EB 的程序
- [ ] 拔插 SmartRF04EB USB 线，等待 5 秒后重新插入

### 第二步：用命令行检测设备

```powershell
& "C:\Program Files (x86)\Texas Instruments\SmartRF Tools\Flash Programmer\bin\SmartRFProgConsole.exe" X
```

期望输出：
```
Device:SmartRF04EB    ID:0068  (fwId:0400, fwRev:0045)  Chip:CC2530
```

- 如果 `Chip:-` → 目标板未连接或未上电
- 如果无设备 → EB USB 未插或驱动问题

### 第三步：执行标准烧录流程

回到上方「标准烧录流程」，先 AU 刷 EB 再 S 烧 CC2530。

### 第四步：解除芯片 Flash 锁定

如果 EB 固件修复后，命令行工具报 `Flash erase failed`，说明 CC2530 的 flash 处于锁定状态。

**方法 A：用 IAR 烧录（推荐）**

IAR 的 Chipcon 调试驱动内置强制全片擦除（Forced Mass Erase），能自动解除锁定。

1. 执行标准烧录流程第一步（刷 EB 固件）
2. 打开 IAR 工程
3. 点击 Download and Debug（Ctrl+D）
4. IAR 会自动强制擦除并烧录
5. 之后 EB 固件会再次损坏，如需命令行烧录，重新执行标准流程

**方法 B：按住 RESET 烧录**

1. 执行标准烧录流程第一步（刷 EB 固件）
2. 按住目标板 RESET 按键不放
3. 执行擦除/烧录命令
4. 命令开始后松开 RESET

```powershell
# 擦除
& "C:\Program Files (x86)\Texas Instruments\SmartRF Tools\Flash Programmer\bin\SmartRFProgConsole.exe" S CE

# 擦除+烧录+验证
& "C:\Program Files (x86)\Texas Instruments\SmartRF Tools\Flash Programmer\bin\SmartRFProgConsole.exe" S EPV F="路径\文件.hex"
```

### 第五步：命令行常用操作

```powershell
# 仅全片擦除
& "C:\Program Files (x86)\Texas Instruments\SmartRF Tools\Flash Programmer\bin\SmartRFProgConsole.exe" S CE

# 擦除 + 烧录（不验证）
& "C:\Program Files (x86)\Texas Instruments\SmartRF Tools\Flash Programmer\bin\SmartRFProgConsole.exe" S EP F="D:\test.hex"

# 降速模式（调试接口不稳定时使用）
& "C:\Program Files (x86)\Texas Instruments\SmartRF Tools\Flash Programmer\bin\SmartRFProgConsole.exe" S EPV IS F="D:\test.hex"
```

## 常见问题速查

| 现象 | 原因 | 解决 |
|------|------|------|
| IAR `#E1` / USB stall | EB 固件损坏 | 执行标准烧录流程 |
| `Flash erase failed` | CC2530 被锁定 | IAR 强制擦除解锁 |
| `No System-on-Chip was detected` | 目标板未连/未上电 | 检查 10-pin 排线和供电 |
| `Could not access the hardware` | 被其他程序占用 | 关闭 IAR 等工具 |
| `Could not locate any EBs` | EB 未识别 | 拔插 EB USB 线 |
| 扫描能检测到但操作全失败 | EB 固件损坏 | 执行标准烧录流程第一步 |

## 工具路径

| 工具 | 路径 |
|------|------|
| SmartRFProgConsole (CLI) | `C:\Program Files (x86)\Texas Instruments\SmartRF Tools\Flash Programmer\bin\SmartRFProgConsole.exe` |
| SmartRFProg (GUI) | `C:\Program Files (x86)\Texas Instruments\SmartRF Tools\Flash Programmer\bin\SmartRFProg.exe` |
| EB 固件文件 | `C:\Program Files (x86)\Texas Instruments\SmartRF Tools\Firmware\SmartRF04EB\fw0400.hex` |

## 故障根因链

```
SmartRF04EB 固件特性：
  S EPV 烧录 CC2530 → EB 固件损坏 → USB STALL
    ↓
  每次烧录前必须 AU EPV 重刷 EB 固件
    ↓
  如果跳过 AU → S EPV 报 HC Error: stall PID
    ↓
  重新 AU EPV → S EPV 成功 → 烧录完成 ✅
  
偶发情况：
  CC2530 flash 锁定位被设置 → 擦除失败
    → IAR 强制 Mass Erase 解锁 → 正常
```

---

> **最后更新：** 2026-06-08
> **相关工具版本：** SmartRF Flash Programmer v1.13.10, SmartRF04EB fwId:0400
