#include "bluetooth_audio_output.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#include <esp_log.h>

#if CONFIG_XIAOZHI_BLUETOOTH_AUDIO_OUTPUT
#include <esp_a2dp_api.h>
#include <esp_aac_enc.h>
#include <esp_audio_enc.h>
#include <esp_bt.h>
#include <esp_bt_device.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#endif

#define TAG "BluetoothAudioOutput"

struct BluetoothAudioOutput::Impl {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<int16_t> pcm;
    BluetoothAudioOutput::ProgressCallback on_progress;
    std::thread worker;
    std::thread init_thread;
    std::atomic<bool> running{false};
    std::atomic<bool> ready{false};
    std::atomic<uint32_t> generation{1};
    int input_rate = 16000;
    int volume = 100;

#if CONFIG_XIAOZHI_BLUETOOTH_AUDIO_OUTPUT
    esp_a2d_conn_hdl_t conn_hdl = 0;
    uint16_t mtu = 0;
    esp_a2d_mcc_t negotiated = {};
    bool negotiated_valid = false;
    esp_audio_enc_handle_t encoder = nullptr;
    uint32_t rtp_timestamp = 0;
    std::array<uint8_t, 4096> encoded = {};
    esp_bd_addr_t target = {};
    bool profile_ready = false;
    int64_t next_connect_us = 0;

    static Impl* current;

    static void A2dpCallback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param) {
        if (current != nullptr) {
            current->HandleA2dpEvent(event, param);
        }
    }

    static void GapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) {
        if (event == ESP_BT_GAP_AUTH_CMPL_EVT && param->auth_cmpl.stat != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "Bluetooth authentication failed: %d", param->auth_cmpl.stat);
        }
    }

    void HandleA2dpEvent(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param) {
        switch (event) {
            case ESP_A2D_PROF_STATE_EVT:
                if (param->a2d_prof_stat.init_state == ESP_A2D_INIT_SUCCESS) {
                    esp_a2d_mcc_t mcc = {};
                    mcc.type = ESP_A2D_MCT_M24;
                    mcc.cie.m24_info.drc = ESP_A2D_M24_CIE_DRC_NS;
                    mcc.cie.m24_info.obj_type = ESP_A2D_M24_CIE_OBJ_TYPE_4_AAC_LC;
                    mcc.cie.m24_info.samp_freq1 = ESP_A2D_M24_CIE_SF1_44K;
                    mcc.cie.m24_info.samp_freq2 = 0;
                    mcc.cie.m24_info.ch = ESP_A2D_M24_CIE_CH_2;
                    mcc.cie.m24_info.vbr = ESP_A2D_M24_CIE_VBR_SUPPORT;
                    mcc.cie.m24_info.br1 = 0x02;
                    mcc.cie.m24_info.br2 = 0x71;
                    mcc.cie.m24_info.br3 = 0;
                    if (esp_a2d_source_register_stream_endpoint(0, &mcc) != ESP_OK) {
                        ESP_LOGE(TAG, "AAC endpoint registration failed");
                        return;
                    }
                    unsigned int octets[ESP_BD_ADDR_LEN] = {};
                    if (std::sscanf(CONFIG_XIAOZHI_BLUETOOTH_TARGET_ADDR, "%2x:%2x:%2x:%2x:%2x:%2x",
                                    &octets[0], &octets[1], &octets[2], &octets[3], &octets[4],
                                    &octets[5]) != ESP_BD_ADDR_LEN) {
                        ESP_LOGE(TAG, "Invalid Bluetooth target address: %s",
                                 CONFIG_XIAOZHI_BLUETOOTH_TARGET_ADDR);
                        return;
                    }
                    for (size_t i = 0; i < ESP_BD_ADDR_LEN; ++i)
                        this->target[i] = static_cast<uint8_t>(octets[i]);
                    profile_ready = true;
                    next_connect_us = esp_timer_get_time();
                    if (esp_a2d_source_connect(this->target) != ESP_OK) {
                        ESP_LOGE(TAG, "Bluetooth source connect request failed");
                    }
                }
                break;
            case ESP_A2D_CONNECTION_STATE_EVT:
                conn_hdl = param->conn_stat.conn_hdl;
                mtu = param->conn_stat.audio_mtu;
                ESP_LOGI(TAG, "A2DP connection state=%d mtu=%u", param->conn_stat.state, mtu);
                if (param->conn_stat.state != ESP_A2D_CONNECTION_STATE_CONNECTED) {
                    ready.store(false);
                    next_connect_us = esp_timer_get_time() + 2000000;
                    std::lock_guard<std::mutex> lock(mutex);
                    pcm.clear();
                }
                break;
            case ESP_A2D_AUDIO_CFG_EVT:
                negotiated = param->audio_cfg.mcc;
                // On ESP32-S31 the audio configuration callback can arrive before
                // the connection callback populates audio_mtu.
                negotiated_valid = negotiated.type == ESP_A2D_MCT_M24;
                if (mtu <= 20)
                    mtu = 1000;
                ready.store(negotiated_valid);
                ESP_LOGI(TAG, "AAC negotiated: type=%d sf1=0x%x ch=0x%x mtu=%u", negotiated.type,
                         negotiated.cie.m24_info.samp_freq1, negotiated.cie.m24_info.ch, mtu);
                break;
            case ESP_A2D_AUDIO_STATE_EVT:
                if (param->audio_stat.state != ESP_A2D_AUDIO_STATE_STARTED) {
                    ready.store(false);
                }
                break;
            default:
                break;
        }
        if (on_progress) {
            on_progress();
        }
    }

    bool OpenEncoder() {
        if (esp_aac_enc_register() != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "AAC encoder registration failed");
            return false;
        }
        esp_aac_enc_config_t aac = {
            .sample_rate = 44100,
            .channel = ESP_AUDIO_DUAL,
            .bits_per_sample = ESP_AUDIO_BIT16,
            .bitrate = 128000,
            .adts_used = false,
        };
        esp_audio_enc_config_t cfg = {
            .type = ESP_AUDIO_TYPE_AAC,
            .cfg = &aac,
            .cfg_sz = sizeof(aac),
        };
        return esp_audio_enc_open(&cfg, &encoder) == ESP_AUDIO_ERR_OK;
    }

    void EncodeTask() {
        if (!OpenEncoder()) {
            running.store(false);
            return;
        }
        std::array<int16_t, 2048> input{};
        std::array<int16_t, 2048 * 2> stereo{};
        while (running.load()) {
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait_for(lock, std::chrono::milliseconds(250),
                            [this] { return !running.load() || pcm.size() >= 371; });
                if (!running.load())
                    break;
                if (!ready.load() && profile_ready && esp_timer_get_time() >= next_connect_us) {
                    lock.unlock();
                    if (esp_a2d_source_connect(target) != ESP_OK) {
                        next_connect_us = esp_timer_get_time() + 5000000;
                    } else {
                        next_connect_us = esp_timer_get_time() + 2000000;
                    }
                    lock.lock();
                }
                if (pcm.size() < 371)
                    continue;
                for (size_t i = 0; i < 371; ++i) {
                    input[i] = pcm.front();
                    pcm.pop_front();
                }
            }
            for (size_t i = 0; i < 1024; ++i) {
                const double pos = static_cast<double>(i) * 370.0 / 1023.0;
                const size_t left = static_cast<size_t>(pos);
                const size_t right = std::min<size_t>(left + 1, 370);
                const double frac = pos - left;
                int32_t sample =
                    static_cast<int32_t>(input[left] * (1.0 - frac) + input[right] * frac);
                sample = sample * volume / 100;
                stereo[i * 2] = static_cast<int16_t>(sample);
                stereo[i * 2 + 1] = static_cast<int16_t>(sample);
            }
            esp_audio_enc_in_frame_t in = {.buffer = reinterpret_cast<uint8_t*>(stereo.data()),
                                           .len = 1024 * 2 * sizeof(int16_t)};
            esp_audio_enc_out_frame_t out = {.buffer = encoded.data(), .len = encoded.size()};
            if (esp_audio_enc_process(encoder, &in, &out) != ESP_AUDIO_ERR_OK ||
                out.encoded_bytes == 0 || out.encoded_bytes > mtu - 20) {
                continue;
            }
            auto* packet = esp_a2d_audio_buff_alloc(static_cast<uint16_t>(out.encoded_bytes));
            if (packet == nullptr)
                continue;
            packet->data_len = static_cast<uint16_t>(out.encoded_bytes);
            packet->number_frame = 1;
            packet->timestamp = rtp_timestamp;
            std::memcpy(packet->data, encoded.data(), out.encoded_bytes);
            if (esp_a2d_source_audio_data_send(conn_hdl, packet) != ESP_OK) {
                esp_a2d_audio_buff_free(packet);
                ready.store(false);
            } else {
                rtp_timestamp += 1024;
            }
            if (on_progress)
                on_progress();
        }
        esp_audio_enc_close(encoder);
        encoder = nullptr;
    }
#endif
};

#if CONFIG_XIAOZHI_BLUETOOTH_AUDIO_OUTPUT
BluetoothAudioOutput::Impl* BluetoothAudioOutput::Impl::current = nullptr;
#endif

BluetoothAudioOutput::BluetoothAudioOutput() : impl_(std::make_unique<Impl>()) {}
BluetoothAudioOutput::~BluetoothAudioOutput() { Stop(); }

bool BluetoothAudioOutput::Start(int input_sample_rate, ProgressCallback on_progress) {
    impl_->input_rate = input_sample_rate;
    impl_->on_progress = std::move(on_progress);
#if CONFIG_XIAOZHI_BLUETOOTH_AUDIO_OUTPUT
    if (impl_->running.exchange(true))
        return true;
    Impl::current = impl_.get();
    impl_->init_thread = std::thread([this] {
        // A2DP uses Classic Bluetooth only. Releasing BLE controller memory is
        // required on this target to leave enough heap for Bluedroid startup.
        esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        ESP_LOGI(TAG, "Initializing Classic Bluetooth controller");
        if (esp_bt_controller_init(&bt_cfg) != ESP_OK ||
            esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT) != ESP_OK) {
            ESP_LOGE(TAG, "Bluetooth controller initialization failed");
            impl_->running.store(false);
            Impl::current = nullptr;
            return;
        }
        ESP_LOGI(TAG, "Initializing Bluedroid");
        esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
        if (esp_bluedroid_init_with_cfg(&bluedroid_cfg) != ESP_OK ||
            esp_bluedroid_enable() != ESP_OK ||
            esp_bt_gap_register_callback(Impl::GapCallback) != ESP_OK ||
            esp_a2d_register_callback(Impl::A2dpCallback) != ESP_OK ||
            esp_a2d_source_init() != ESP_OK) {
            ESP_LOGE(TAG, "Bluetooth A2DP initialization failed");
            impl_->running.store(false);
            Impl::current = nullptr;
            return;
        }
        ESP_LOGI(TAG, "Bluetooth A2DP stack initialized");
        impl_->worker = std::thread([this] { impl_->EncodeTask(); });
        ESP_LOGI(TAG, "A2DP encoder task created");
    });
    return true;
#else
    return false;
#endif
}

bool BluetoothAudioOutput::IsReady() const { return impl_->ready.load(); }
bool BluetoothAudioOutput::IsDrained() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->pcm.empty();
}

bool BluetoothAudioOutput::TryWrite(const int16_t* pcm, size_t samples, uint32_t generation,
                                    int volume) {
    if (generation != impl_->generation.load() || !IsReady() || pcm == nullptr)
        return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->pcm.size() + samples > 12000)
        return false;
    impl_->volume = std::clamp(volume, 0, 100);
    impl_->pcm.insert(impl_->pcm.end(), pcm, pcm + samples);
    impl_->cv.notify_one();
    return true;
}

void BluetoothAudioOutput::Cancel(uint32_t generation) {
    impl_->generation.store(generation + 1);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->pcm.clear();
    impl_->cv.notify_all();
}

void BluetoothAudioOutput::Stop() {
    const bool was_running = impl_->running.exchange(false);
    impl_->cv.notify_all();
    if (impl_->init_thread.joinable())
        impl_->init_thread.join();
    if (impl_->worker.joinable())
        impl_->worker.join();
#if CONFIG_XIAOZHI_BLUETOOTH_AUDIO_OUTPUT
    if (!was_running)
        return;
    if (impl_->encoder != nullptr)
        esp_audio_enc_close(impl_->encoder);
    esp_a2d_source_deinit();
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    Impl::current = nullptr;
#endif
}
