# Bluetooth Speaker Implementation Plan

> **For agentic workers:** Use `superpowers:executing-plans` to execute this plan in the current session after review. Complete each task's verification before advancing.

当前状态：保存实验原型及后续实施计划；下列待办项仍需逐项验证。

**Goal:** 小智通过板载麦克风收音、Xiaomi Sound-1802 播放；断线自动回退并后台重连。

**Architecture:** 在 AudioService 的 PCM 输出边界增加可选蓝牙输出。独立工作任务管理 A2DP 状态、重采样、AAC 编码和发送；AudioService 保有播放代次、队列和完成状态的最终所有权。Application 通过通用音频能力接口控制半双工收音，不引用具体板型。

**Tech Stack:** ESP-IDF v6.1、Bluedroid Classic A2DP source、esp_audio_codec 2.5.x、esp_audio_effects 1.3.x、FreeRTOS、C++。

**Spec:** [设计](2026-09-21-bluetooth-speaker-design.md)

## Global Constraints

- ESP-IDF v6.1；本功能仅启用于 ESP32-S31，普通构建默认关闭。Kconfig 使用 `CONFIG_IDF_TARGET_ESP32S31`、`CONFIG_BT_ENABLED`、`CONFIG_BT_CLASSIC_ENABLED`、`CONFIG_BT_A2DP_ENABLE` 与 external-codec 选项的实际 v6.1 名称，先在 IDF Kconfig 中核对后再写入 depends/select。
- 保留现有五个文件的用户改动；不得覆盖 ES8389、按键引脚及 Application 的既有修复。
- 不手改 `build/`、`managed_components/`、`components/`、`sdkconfig*` 等生成或第三方文件。
- 蓝牙回调仅传递事件；应用状态变化走 `Application::Schedule()` 或事件位。
- 音频缓冲固定容量，申请失败回退本地输出；禁止无限排队、无限重试或在主循环等待连接。
- 目标地址通过 Kconfig 在本地配置，仓库默认留空。
- `CONFIG_USE_SERVER_AEC` 保持关闭；蓝牙半双工能力强制 `GetDefaultListeningMode()` 使用 auto-stop，避免向服务端声明 AEC 或进入实时收音。
- 普通构建保留现有 AEC、唤醒及播放行为。
- 集成完成前移除 `application.cc` 中 `AudioProbe RX` 临时日志；安全审查记录与结构图在发布前检查个人信息。
- README 独立维护中英文文件与顶部语言切换链接。

## Review Focus

- 取消与发送并发：取消前的 PCM 不得在下一次播放或重连后出现。
- 播放中断线：丢弃蓝牙积压，只将后续数据送到本地，不重播整句话。
- 音箱已连接电脑、长时间关机：有上限的重试频率，本地收音和网络仍正常。
- 延迟报告缺失或异常：完成通知有界，收音既不提前也不永久等待。
- 资源不足或控制请求失败：释放本次分配，记录原因，维持可用的本地输出。

## Task 1: 可测试的输出策略与有界缓冲

**Files:** 新增 `main/audio/output/bluetooth_output_policy.h`、`scripts/tests/test_bluetooth_output_policy.py` 及相邻 C++ 测试源。

**Interfaces:** 策略类不依赖 ESP-IDF。提供 `BeginPlayback(ready)`、`Disconnected()`、`Cancel()`、`Route()`、`Generation()`；状态覆盖本地/蓝牙、播放代次、重试时间及远端尾音期限。

- [ ] 先写行为测试并运行，确认因缺少实现失败。沿用仓库已有主机 C++ 测试编译方式，从 Python unittest 调用编译器与测试二进制。

测试必须覆盖以下序列和断言，而非源码字符串检查：

```cpp
BluetoothOutputPolicy policy;
policy.BeginPlayback(false);
assert(policy.Route() == AudioOutputRoute::kLocal);
policy.BeginPlayback(true);
assert(policy.Route() == AudioOutputRoute::kBluetooth);
auto generation = policy.Generation();
policy.Disconnected();
assert(policy.Route() == AudioOutputRoute::kLocal);
assert(policy.Generation() != generation);
generation = policy.Generation();
policy.Cancel();
assert(policy.Generation() != generation);
```

- [ ] 实现固定容量、带代次的 PCM 缓冲。将环形缓冲单独测试：空读、恰好填满、溢出拒绝、绕回、取消清空、旧代次提交被拒绝。
- [ ] 重试间隔依次为 2、4、8、16、30 秒，上限 30 秒；成功后复位。使用传入的单调时钟，测试无需真实等待。
- [ ] 延迟报告单位按 API 的 0.1 ms 转换；尾音期限采用有效报告加 100 ms 余量，缺失时 500 ms，总等待限定为 2 秒。验证 0、正常值、过大值、取消与断线。
- [ ] 运行 `python3 -m unittest discover -s scripts/tests -v`，预期新旧测试全部通过。

## Task 2: A2DP 输出工作任务

**Files:** 新增 `main/audio/output/bluetooth_audio_output.h/.cc`；修改 `main/Kconfig.projbuild`、`main/CMakeLists.txt`、S31 `config.json`。

**Interfaces:**

```cpp
class BluetoothAudioOutput {
public:
    bool Start(int input_sample_rate, std::function<void()> on_progress);
    bool IsReady() const;
    bool IsDrained() const;
    bool TryWrite(const int16_t* pcm, size_t samples, uint32_t generation, int volume);
    void Cancel(uint32_t generation);
    void Stop();
};
```

`TryWrite` 接收单声道 PCM，无等待地提交至固定缓冲。`Cancel` 使旧代次失效并给工作任务发送暂停事件。`on_progress` 通知 AudioService 重新检查容量或排空；回调在输出内部锁释放后调用。

- [ ] 阅读当前版本 AAC 编码和重采样 README、接口头文件，优先采用直接 AAC 编码器接口；若必须注册，验证 `esp_aac_enc_register()` 幂等且不会重新注册全局 Opus。
- [ ] 蓝牙事件先转成定长、自持有的数据结构，再投递给工作任务；不得将 SDK 临时指针跨回调保存。队列溢出设置恢复标记，由工作任务断开并回退。
- [ ] 明确初始化顺序：注册 A2DP 回调 → 异步初始化 → 收到 `ESP_A2D_INIT_SUCCESS` → 注册 AAC 端点 → 收到端点注册成功 → 发起连接。每个 API 都检查返回值。
- [ ] 端点限制为已验证的 MPEG-4 AAC-LC、44.1 kHz、双声道，广告码率上限 160 kbps，编码器跟随最终协商值；验证实际协商配置后才标记 ready。仅注册 AAC，若协商成 SBC、MTU 不够或编码失败均回退并断开。
- [ ] 固定容量输入缓冲最多容纳 240 ms PCM；工作区在启动时分配一次。重采样成 44.1 kHz 单声道，再进行音量缩放和左右声道复制，按 AAC 帧编码。
- [ ] 使用已验证的 `esp_a2d_source_audio_data_send` 所有权规则：成功后缓冲交给协议栈，失败时由调用方释放。单帧重试次数有界，持续拥塞触发回退。RTP 时间戳按采样点递增。
- [ ] 工作任务通过定时等待保持发送节奏；处理暂停、断线与取消时优先清空旧代次。编码器残留数据需要在新代次前重置，避免前一段声音泄漏。
- [ ] 不在连接建立时抢走正在进行的本地播放；下一个播放边界才启用蓝牙。空闲时暂停流；下一段提前准备流，未准备好则整段走本地。
- [ ] 功能开关 `XIAOZHI_BLUETOOTH_AUDIO_OUTPUT` 依赖 S31、Classic Bluetooth、Bluedroid、A2DP 和 external codec；与 BluFi 共用控制器的场景先通过配置约束排除。
- [ ] 新增构建变体 `esp32-s31-korvo-1-bluetooth`，同一板型、同一工厂、同一引脚。变体显式启用所需 SDK 选项、AAC 和目标地址。
- [ ] 用可替换的传输调用测试状态机：初始化未完成不注册端点、端点失败不连接、断线清空、拥塞回退、30 秒重试上限。
- [ ] 构建蓝牙变体，预期成功且应用大小小于 OTA 分区；记录内部堆与 PSRAM 分配预算。

## Task 3: 接入 AudioService 与半双工会话

**Files:** 修改 `main/audio/audio_service.h/.cc`、`main/application.cc`；保持所有新增行为受功能开关或通用能力查询保护。

**Interfaces:** AudioService 新增 `RequiresHalfDuplexPlayback()` 通用能力查询；普通构建返回 false。保留现有 `ResetDecoder()`、`IsPlaybackIdle()`、`on_playback_drained` 的调用形式，扩展其内部语义以覆盖远端输出。

- [ ] 为播放代次增加任务标记，输出提交时再次检查；覆盖“输出任务已取走 PCM 后取消”的竞争情况。
- [ ] 输出任务按策略选择本地或蓝牙。`TryWrite` 保持无等待；缓冲满时 `AudioOutputTask` 在现有 `audio_queue_cv_` 上等待容量事件，蓝牙工作任务在释放内部锁后通过 `on_progress` 通知同一个 CV。等待带明确超时及取消条件；超时回退而不阻塞主循环。`Stop()`/`ResetDecoder()` 通知同一个 CV 解除等待。
- [ ] `ResetDecoder` 与 Stop 同时使蓝牙代次失效并请求暂停；锁顺序固定，禁止在持有音频队列锁时调用可回调 AudioService 的输出方法。
- [ ] `IsPlaybackDrainedLocked` 同时检查本地队列、解码/输出 in-flight、蓝牙队列与远端尾音期限。期限到达后工作任务发通知，触发原有 drained 事件。
- [ ] 半双工能力启用时默认会话采用 auto-stop；蓝牙播放和通知期间关闭唤醒/语音输入，播放完成后恢复原状态需要的识别。按键打断主动 ResetDecoder 并取消远端流。
- [ ] 保留普通构建原有 AEC 选择；蓝牙不向服务器宣称拥有有效本地回声消除。相关能力变化经应用主循环处理。
- [ ] `RequiresHalfDuplexPlayback()` 接入 `GetDefaultListeningMode()`：蓝牙变体无论 `aec_mode_` 值如何都强制 auto-stop；确认 `CONFIG_USE_SERVER_AEC` 关闭后 websocket 与 MQTT/UDP 均不发送 `aec=true`。蓝牙路由不调用 `codec_->EnableOutput()`，ES8389 功放计时器只关闭本地输出，DACR 参考不作为蓝牙 AEC 输入。
- [ ] 编写并运行回归：空队列但远端仍有尾音时不可 drained；取消完成只通知一次；Stop 唤醒阻塞输出；断线通知能解除 pending listening；新一段音频不能被上一段 drained 事件提前结束。
- [ ] 运行全部主机测试和 touched-file clang-format 检查；构建普通 S31 和一个现有 S3 音频变体验证关闭功能时兼容性。

## Task 4: 板上验证、恢复路径与使用说明

**Files:** 更新根目录中英文 README、音频架构说明；为新增板级蓝牙说明维护中英文 README。日志和镜像保存在仓库外的 `Documents/esp32-backups/`。

- [ ] 确认 UART 独占、保存新鲜 NVS/PHY 与 OTA 选择；先校验原镜像对应当前设备。集成镜像可能超过旧镜像长度，必须备份将被擦除的完整区间，不能沿用临时小程序的前缀恢复假设。
- [ ] 使用 `python3 scripts/build.py esp32-s31-korvo-1 --name esp32-s31-korvo-1-bluetooth` 构建，核对 partition、板型、应用大小与 ELF SHA。仅刷入经过核对的应用范围。
- [ ] 验证启动、显示、Wi-Fi、麦克风、唤醒和蓝牙连接。请用户触发一次真实小智对话并确认回复音质；日志同时证明网络音频输入、PCM 路由和 AAC 发送。
- [ ] 验证提示音、音量调节、静音、按键打断、尾音后收音。分别测试播放中断开音箱、音箱重新开启、音箱被其他设备占用，确认本地回退和重连。
- [ ] 进行持续对话与空闲/重连循环，检查堆余量、队列峰值、看门狗、播放积压和识别误触发；每项记录实际结果。
- [ ] 发现启动、网络或采音回归时恢复完整备份并校验；硬件验证通过后保留集成固件，同时明确仍未验证的项目。
- [ ] 文档说明构建命令、目标地址配置、半双工限制、断线回退和恢复方法；不增加无关 README 目录树。
- [ ] 完成一次独立最终代码审查，重点检查 Review Focus 中五类情况；修正重要问题并跑对应回归测试。提交或推送前遵守用户既有 Git 要求。

## 执行方式与交付

推荐当前会话直接执行，各任务按顺序完成，最后单独审查整体改动。实施前保留当前用户补丁快照；使用隔离构建状态，避免不同变体互相覆盖。最终交付包括代码、构建和使用说明、硬件验证记录以及可恢复镜像。

状态：实现计划待用户审阅；本文件中的任务尚未执行。
