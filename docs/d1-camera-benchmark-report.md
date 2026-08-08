# D1 OV3660 纯摄像头性能测试报告

测试日期：2026-08-08

## 证据分类

### 已在连接的实物上验证

- 硬件平台：ESP32-S3 revision v0.2、16 MB DIO Flash、8 MB Octal
  PSRAM；PSRAM 使用已经验证稳定的 40 MHz 配置。
- 软件环境：ESP-IDF v5.5.3，`espressif/esp32-camera` v2.1.4。
- 摄像头：OV3660，PID 为 `0x3660`，SCCB 地址为 `0x3c`。
- 测试期间未初始化 Wi-Fi、HTTP、电机、IMU、TOF、舵机、OLED、
  麦克风、TTS 或 Agent 相关功能。
- 安全启动验证：成功获取并归还 1 帧有效 QVGA JPEG；随后连续获取
  60/60 帧有效图像，失败 0 帧。每项检查完成后均反初始化摄像头。
- 正式基线参数：QVGA 320x240、JPEG、quality 20、XCLK 20 MHz、
  2 个 PSRAM framebuffer、LATEST grab mode、PSRAM DMA 关闭。

### 用户明确观察到的现象

用户分别在单帧验证、连续 60 帧验证、60 秒正式基线、JPEG quality
对比、framebuffer 数量对比后检查了摄像头和 FPC。PSRAM DMA 异常测试
中止并恢复 DMA-OFF 安全固件后，用户也再次进行了检查。所有检查点均
未观察到发热。QQVGA 160x120 的 15 秒短测结束后，用户再次确认摄像头
和 FPC 不热。PCLK divider 9 时序测试结束并恢复默认安全固件后，用户也明确
确认摄像头和 FPC 不热。

### AI 推测

改变 JPEG quality 会明显改变编码后的文件大小，却没有改变约 36 ms
的帧周期。因此，当前约 27.7 FPS 的上限更像是由摄像头当前时序决定，
而不是由 JPEG 数据量或 framebuffer 吞吐量决定。

### 尚未验证

- 没有进行 10 分钟 soak test，因为 60 秒正式基线没有通过 30 FPS
  性能门槛。
- 没有进行 QQVGA 60 秒正式测试，因为 15 秒短测平均帧率低于 30 FPS，
  原始短测期间还持续出现 `FB-OVF`。修正缓冲区后的短测仍低于 30 FPS。

## 60 秒正式基线结果

正式统计前丢弃了 30 帧成功采集的图像作为预热。只有在 60 秒截止时间
前由 `esp_camera_fb_get()` 返回的有效、新 framebuffer 才计入帧数。
取得的每个 framebuffer 均立即归还。

| 指标 | 结果 |
|---|---:|
| 测试时长 | 60.000 s |
| 有效总帧数 | 1661 |
| 失败帧数 | 0 |
| 平均 FPS | 27.683 |
| 最低 1 秒窗口 | 26 帧 |
| 最高 1 秒窗口 | 28 帧 |
| 平均 JPEG 大小 | 5232.0 B |
| 最小 JPEG 大小 | 4644 B |
| 最大 JPEG 大小 | 6459 B |
| 平均帧间隔 | 36.103 ms |
| P50 帧间隔 | 36.016 ms |
| P95 帧间隔 | 36.039 ms |
| 最大帧间隔 | 72.114 ms |
| Internal free heap 变化 | -220 B |
| PSRAM free heap 变化 | 0 B |

测试期间没有 reset、crash、framebuffer deadlock 或正式统计范围内的采集
失败。Internal free heap 在第一次进度日志后减少 220 B，之后保持稳定；
PSRAM free heap 始终没有变化。

驱动在摄像头初始化或其他瞬态阶段偶尔丢弃不完整 JPEG，并输出
`NO-SOI` 或 `NO-EOI`。驱动随后能够正常返回有效 JPEG framebuffer。
这些信息在 60 秒正式基线期间没有持续出现，正式失败计数保持为 0。

## 单变量诊断

每组短测都先丢弃 30 帧有效预热帧，然后固定测试 15 秒。除表中所列
变量以外，其余参数保持正式基线不变。

### JPEG quality 对比

| Quality | 平均 FPS | 最低 1 秒帧数 | P95 帧间隔 | 平均 JPEG | 失败帧数 |
|---:|---:|---:|---:|---:|---:|
| 12 | 27.733 | 27 | 36.043 ms | 6982.5 B | 0 |
| 20 | 27.667 | 27 | 36.140 ms | 5289.0 B | 0 |
| 30 | 27.733 | 27 | 36.035 ms | 4361.8 B | 0 |
| 40 | 27.400 | 26 | 36.026 ms | 3919.0 B | 0 |

所有受测 JPEG quality 均未达到 30 FPS。quality 主要改变 JPEG 文件
大小，没有带来可确认的帧率提升。

### Framebuffer 数量对比

| Framebuffer 数量 | 平均 FPS | 最低 1 秒帧数 | P95 帧间隔 | 失败帧数 |
|---:|---:|---:|---:|---:|
| 1 | 13.733 | 13 | 72.131 ms | 0 |
| 2 | 27.400 | 26 | 36.036 ms | 0 |

当前连续采集需要 2 个 framebuffer 才能获得较高帧率。使用 1 个
framebuffer 时，吞吐量大约减半。

### PSRAM DMA 对比

| PSRAM DMA | 结果 |
|---|---|
| OFF | 27.667 FPS，最低 27 帧/秒，P95 36.047 ms，失败 0 帧 |
| ON | 不可用：持续出现 `NO-EOI`，随后反复发生 `fb_get` timeout |

DMA ON 状态同时经过驱动日志和 `esp_camera_get_psram_mode()` 确认。
启用后累计出现超过 600 次额外的不完整 JPEG，并连续发生 6 次约 4 秒的
framebuffer timeout，因此按照 Stage 安全规则中止测试。随后刷回 DMA-OFF
安全固件，再次成功获取 60/60 帧有效图像，并反初始化摄像头。

第一次 DMA 测试工具实现曾在摄像头初始化前调用运行时 DMA API，得到
`ESP_ERR_INVALID_STATE`。该次没有进行摄像头采集，不计入 A/B 对比结果。
随后根据驱动源码修正调用顺序并重新测试，得到上述有效结果。

## 分辨率单变量诊断

本项只把 frame size 从 QVGA 320x240 改为 QQVGA 160x120。以下参数保持
不变：OV3660、JPEG、quality 20、XCLK 20 MHz、2 个 framebuffer、LATEST
grab mode、framebuffer 位于 PSRAM、40 MHz Octal PSRAM、PSRAM DMA OFF。
测试期间没有启用 Wi-Fi、电机或其他机器人功能。

Camera 初始化后，首个有效 framebuffer 实测为 width 160、height 120、
format JPEG。随后成功丢弃 30 帧 warm-up，再进行 15 秒正式短测。

### QQVGA 15 秒短测结果

| 指标 | 结果 |
|---|---:|
| 分辨率 | 160x120 |
| 测试时长 | 15.000 s |
| 有效总帧数 | 410 |
| 失败帧数 | 0 |
| 平均 FPS | 27.333 |
| 最低 1 秒窗口 | 26 帧 |
| 最高 1 秒窗口 | 28 帧 |
| 平均 JPEG 大小 | 2059.4 B |
| 最小 JPEG 大小 | 2039 B |
| 最大 JPEG 大小 | 2288 B |
| 平均帧间隔 | 36.544 ms |
| P50 帧间隔 | 36.016 ms |
| P95 帧间隔 | 36.050 ms |
| 最大帧间隔 | 72.033 ms |
| Internal heap（测试前） | free 349955 B；minimum 349955 B；largest 270336 B |
| Internal heap（测试后） | free 349735 B；minimum 349735 B；largest 270336 B |
| PSRAM（测试前） | free 8360360 B；minimum 8124044 B；largest 8257536 B |
| PSRAM（测试后） | free 8360360 B；minimum 8124044 B；largest 8257536 B |

测试期间没有 reset、crash 或 framebuffer deadlock，结束后摄像头正常反
初始化。驱动持续输出大量 `FB-OVF`，并出现一次 `NO-EOI`。虽然应用仍然
取得 410 个通过尺寸、格式和 JPEG SOI 校验的 framebuffer，失败计数为 0，
但持续 `FB-OVF` 必须单独记录为 Camera error，不能把本次短测描述为完全
无错误运行。

由于短测平均 FPS 小于 30，并且存在持续 Camera error，按照测试门槛没有
继续运行相同配置的 60 秒正式测试。

### QVGA 与 QQVGA 对比

| 分辨率 | 时长 | 平均 FPS | 最低/最高 1 秒窗口 | P50 / P95 间隔 | 平均 JPEG | 返回失败 | 持续 Camera error |
|---|---:|---:|---:|---:|---:|---:|---|
| QVGA 320x240 | 60 s | 27.683 | 26 / 28 | 36.016 / 36.039 ms | 5232.0 B | 0 | 无 |
| QQVGA 160x120 | 15 s | 27.333 | 26 / 28 | 36.016 / 36.050 ms | 2059.4 B | 0 | 有，持续 `FB-OVF` |

### 本项判断

**已验证事实：** 将分辨率降低到 160x120 后，JPEG 平均大小由约 5232 B
下降到约 2059 B，但平均帧率没有提高，仍约为 27 FPS。

**AI 推测：** 当前瓶颈更可能与 OV3660 的传感器时序、帧率寄存器配置或
其他固定周期有关，而不是图像尺寸本身。现有证据不足以断言具体原因。

QQVGA 结果不会改变原 QVGA baseline 的 `PARTIAL` 判断。

## QQVGA JPEG 缓冲区修正

驱动原先使用自动公式 `width * height / 5`，QQVGA 因而只分配 3840 B。
`esp32-camera` 的 Kconfig 明确提示该公式在 QQVGA 等很低分辨率下容易导致
缓冲区不足。本轮只把 JPEG mode framebuffer 配置改为自定义 8192 B，其余
采集参数保持不变。

实机初始化日志确认每个 framebuffer 实际分配 8192 B，PSRAM DMA 仍为 OFF。
30 帧有效 warm-up 后进行了 15 秒短测：

| 指标 | 原始自动 3840 B | 自定义 8192 B |
|---|---:|---:|
| 有效总帧数 | 410 | 415 |
| 失败帧数 | 0 | 0 |
| 平均 FPS | 27.333 | 27.667 |
| 最低/最高 1 秒窗口 | 26 / 28 | 27 / 28 |
| 平均 JPEG | 2059.4 B | 2168.9 B |
| 最小/最大 JPEG | 2039 / 2288 B | 2148 / 2184 B |
| 平均帧间隔 | 36.544 ms | 36.104 ms |
| P50 / P95 帧间隔 | 36.016 / 36.050 ms | 36.017 / 36.077 ms |
| 最大帧间隔 | 72.033 ms | 72.035 ms |
| `FB-OVF` | 持续大量出现 | 0 次 |

修正后测试前 internal heap 为 349955 B，测试后为 349671 B，变化 -284 B；
PSRAM 测试前后均为 8351656 B。测试期间出现一次可恢复的 `NO-EOI`，但没有
持续 Camera error、reset、crash、timeout 或 framebuffer deadlock；结束后摄像头
正常反初始化。

**已验证事实：** 8192 B 自定义缓冲区消除了原 QQVGA 测试中的持续
`FB-OVF`，证明该错误来自原自动缓冲区容量不足。干净测试仍只有 27.667 FPS，
因此持续 `FB-OVF` 和串口错误日志不是约 27.7 FPS 上限的主要原因。

## OV3660 PCLK 单变量诊断

缓冲区修正后的 QQVGA 测试仍未达到 30 FPS，因此进行了一个独立时序测试。
只把 OV3660 JPEG PLL 配置中的 PCLK divider 从 10 改为 9；SYSCLK、XCLK、
分辨率、JPEG quality、framebuffer 数量、PSRAM 配置及其他参数不变。

驱动计算日志确认 PCLK 从 10000000 Hz 提高到 11111111 Hz，寄存器 `0x3824`
低 5 位读回为 9。framebuffer 校验同时检查了 160x120、JPEG 格式、SOI 和 EOI。

| 指标 | 默认 divider 10 | divider 9 |
|---|---:|---:|
| PCLK | 10.000 MHz | 11.111 MHz |
| 有效总帧数 | 415 | 416 |
| 失败帧数 | 0 | 0 |
| 平均 FPS | 27.667 | 27.733 |
| 最低/最高 1 秒窗口 | 27 / 28 | 27 / 28 |
| 平均 JPEG | 2168.9 B | 2157.6 B |
| 最小/最大 JPEG | 2148 / 2184 B | 2133 / 2174 B |
| 平均帧间隔 | 36.104 ms | 36.017 ms |
| P50 / P95 帧间隔 | 36.017 / 36.077 ms | 36.017 / 36.078 ms |
| 最大帧间隔 | 72.035 ms | 36.085 ms |
| `FB-OVF` / `NO-EOI` | 0 / 1 | 0 / 0 |

divider 9 测试前 internal heap 为 349955 B，测试后为 349735 B，变化 -220 B；
PSRAM 测试前后均为 8351656 B。没有 reset、crash、timeout、framebuffer
deadlock 或持续 Camera error，结束后摄像头正常反初始化。

**已验证事实：** PCLK 提高 11.1% 后，平均 FPS 只从 27.667 变为 27.733，
没有形成可确认的吞吐提升，也没有达到 30 FPS。

**AI 推测：** 当前约 36 ms 的周期不是由 DVP PCLK 吞吐速度决定，更可能由
OV3660 的帧总时序、曝光/blanking 周期或驱动未调整的其他固定时序控制。现有
证据仍不足以断言其中某一个具体寄存器是根因。

按照本轮约定，不继续组合修改 PLL、XCLK、帧总长或曝光寄存器。PCLK
divider 恢复为驱动默认值 10，保留已经验证能消除 QQVGA `FB-OVF` 的 8192 B
JPEG 缓冲区配置。

恢复后的最终安全固件已重新刷入实机。复核启动确认 PCLK 10 MHz、PSRAM
DMA OFF、8192 B framebuffer；30 帧 warm-up 后取得 414 个有效帧、失败 0、
平均 27.600 FPS，`FB-OVF` 为 0。期间一次可恢复 `NO-EOI`，测试结束后摄像头
正常反初始化并进入空闲状态。

## 结论

**D1 状态：PARTIAL**

ESP32-S3 + OV3660 在 QVGA JPEG、2 个 PSRAM framebuffer、PSRAM DMA
关闭的条件下可以稳定完成摄像头采集，但测得的 Camera-only 稳定上限约为
**27.7 FPS**，低于要求的平均 30 FPS 门槛。最低 1 秒窗口也低于建议的
29 FPS 门槛；P95 帧间隔和失败帧数两项则满足要求。

因此，当前实物尚未证明具备稳定 30 FPS Wi-Fi 图传所需的采集余量。
本 Stage 没有开始 D2 网络图传工作。

本轮软件诊断证明了缓冲区错误可以修正，但提高 PCLK 不能突破帧率上限。
在不继续扩大变量范围的前提下，接受当前稳定约 27.7 FPS 的 Camera-only
工作点。
