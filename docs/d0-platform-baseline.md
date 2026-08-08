# D0 ESP32-S3 硬件平台基线

测试日期：2026-08-08

本记录覆盖进入纯摄像头 D1 Benchmark 前所需确认的 ESP32-S3 实物平台。
D0 探测固件不初始化摄像头、Wi-Fi、电机或其他机器人外设。

## 证据分类

### 已在连接的实物上验证

| 项目 | 结果 | 证据 |
|---|---|---|
| 芯片 | ESP32-S3 QFN56，revision v0.2，双核 | esptool 与 D0 启动日志 |
| 晶振 | 40 MHz | esptool |
| USB 模式 | USB-Serial/JTAG | esptool |
| ESP-IDF | v5.5.3 | 构建输出与 D0 应用日志 |
| Flash | 16 MB、DIO、80 MHz、3.3 V | esptool、镜像头与 D0 启动日志 |
| PSRAM 器件 | AP generation 3、64 Mbit（8 MB）、3 V Octal PSRAM | D0 启动日志 |
| PSRAM 可用配置 | Octal、40 MHz | ESP-IDF 启动内存测试与应用测试 |
| Internal heap | free 386459 B；minimum 386459 B；largest block 286720 B | D0 应用日志 |
| 应用测试前 PSRAM heap | free 8386192 B；minimum 8386192 B；largest block 8257536 B | D0 应用日志 |
| PSRAM 应用测试 | 262144 B 分配、写入、读取及比较全部通过 | D0 应用日志 |
| OV3660 身份 | PID `0x3660`，SCCB 地址 `0x3c` | 原固件启动日志，后由 D1 固件再次确认 |
| 摄像头 GPIO 基线 | 使用文档记录的 GPIO map 成功初始化摄像头 | 原固件日志、供应商资料与 D1 实机验证 |

D0 固件输出 `D0_RESULT=PASS`。随后观察 16 秒，收到 3 条符合预期的
5 秒心跳日志，没有 reset 标记，也没有 error 日志。

### 用户提供或供应商资料记录

| 项目 | 结果 |
|---|---|
| 产品 | GOOOUUU ESP32-S3-CAM |
| 购买的摄像头选项 | OV3660 |
| 供应商配置标识 | N16R8 |

实测的 16 MB Flash、8 MB PSRAM 和 OV3660 PID 与供应商产品信息相符。

### 尚未直接验证

- PCB revision：`UNKNOWN`
- ESP32-S3 模组的完整订货型号或屏蔽罩丝印：`UNKNOWN`
- 摄像头 `PWDN` 和 `RESET` 的实际电气走线：供应商板卡资料将两者配置为
  `-1`，但尚未在 PCB 上独立测量。

## PSRAM 频率结论

第一次探测采用供应商示例中的 80 MHz Octal PSRAM 配置。固件能够识别
8 MB PSRAM，但 MSPI timing tuning 失败；ESP-IDF 启动内存测试每次都会
报告约 47000/262144 次写入错误并重启。

只把 PSRAM 时钟从 80 MHz 改为 40 MHz 后，得到以下结果：

```text
esp_psram: Found 8MB PSRAM device
esp_psram: Speed: 40MHz
esp_psram: SPI SRAM memory test OK
hardware_info: psram: initialized=YES size=8388608 bytes (8 MB)
hardware_info: PSRAM allocation/read/write test: PASS (262144 bytes)
d0_probe: D0_RESULT=PASS
```

因此，40 MHz 是已经验证可用的 D0 PSRAM 基线。80 MHz 失败是当前平台
的明确工程约束，不能视为可用配置。

## D0 结论

当前实物具备 ESP32-S3、16 MB Flash 和可正常使用的 8 MB PSRAM，满足
进入纯摄像头 D1 Benchmark 的硬件基础。后续测试应继续使用已经验证的
40 MHz PSRAM 配置，除非新的严格单变量测试证明其他配置能够稳定运行。
