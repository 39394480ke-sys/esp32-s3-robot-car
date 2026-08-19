# D2 单 ESP32 局域网实时图传实验报告

测试阶段状态：`paused / partial`

维护状态：已归档，不再作为计划中的机器人功能继续维护。

## 目标与结论

D2 的目标是在普通 2.4 GHz 局域网下实现 QVGA、平均至少 25 FPS、连续
稳定 10 分钟的视频传输。固件、HTTP 接口和 PC Receiver 已完成实验实现，
但优化版本没有完成实机性能 Gate，因此不能标记为稳定功能。

旧 Phase E 正式 LAN 基线约为 0.200 FPS，未达到性能要求。之后完成的发送
路径优化因当前整机的 PSRAM 初始化失败而无法进行 60 秒和 10 分钟正式
验证。本 Stage 没有证明目标可达。

## 已实现内容

- 独立 `camera_streaming` ESP-IDF 工程。
- `GET /` 浏览器界面、`GET /stream` MJPEG 流和 `GET /status` 状态接口。
- 单客户端限制和额外客户端 HTTP 503 响应。
- 2 秒网络发送超时及连接释放。
- 可复用 PSRAM staging buffer 和 32768 B TCP send buffer。
- QVGA JPEG 结构检查及固定 JPEG header fingerprint 过滤。
- Camera、发送、丢帧、heap 和吞吐量指标。
- PC Receiver 的 FPS、stall 和相对延迟漂移统计。

## 当前硬件阻塞

当前整机将编码器连接到 GPIO35、GPIO36、GPIO37 和 GPIO43。其中
GPIO35、GPIO36 和 GPIO37 属于当前 ESP32-S3 的 Octal PSRAM 总线。

在当前接线状态下，Camera Benchmark 和 Camera Streaming 固件都在 PSRAM
初始化阶段失败并重启。资源冲突和启动失败均已观察到，但尚未进行断开
GPIO35、GPIO36、GPIO37 外部连接的 A/B 测试，因此编码器连接是否为本次
启动失败的直接原因仍未最终确认。

这项结论不指向 Camera 或 FPC 故障，也不能替代后续的引脚级 A/B 验证。

## 未完成的 Gate

- 断开 GPIO35、GPIO36、GPIO37 外部连接后的 PSRAM A/B 验证。
- 优化后固件的 60 秒性能 Gate。
- PC Receiver 连续 10 分钟 soak。
- 浏览器连续 10 分钟 soak。
- 偶发负片、红紫闪帧的根因确认。
- 编码器 GPIO 或整车控制架构重新分配。

## 已完成的软件验证

- 16 个 PC Receiver tests 通过。
- D0、D1、D2 contracts 与 host tests 通过。
- Camera Benchmark 和 Camera Streaming 在 ESP-IDF v5.5.3 下 clean build
  通过。

这些软件验证不能替代未完成的实机性能和稳定性 Gate。

## 后续边界

若未来需要低频定时拍照，应作为独立 Stage 和独立实现，不直接建立在本
持续 MJPEG 流逻辑上。本归档不承诺继续维护 D2 实时图传功能。
