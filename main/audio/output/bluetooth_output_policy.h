#ifndef BLUETOOTH_OUTPUT_POLICY_H
#define BLUETOOTH_OUTPUT_POLICY_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

enum class AudioOutputRoute {
    kLocal,
    kBluetooth,
};

class BluetoothOutputPolicy {
public:
    static constexpr size_t kBufferSamples = 12000;

    void BeginPlayback(bool bluetooth_ready) {
        route_ = bluetooth_ready ? AudioOutputRoute::kBluetooth : AudioOutputRoute::kLocal;
        ++generation_;
        ClearBuffer();
    }

    void Disconnected() {
        route_ = AudioOutputRoute::kLocal;
        ++retry_attempt_;
        ++generation_;
        ClearBuffer();
    }

    void ConnectionSucceeded() { retry_attempt_ = 0; }

    void Cancel() {
        ++generation_;
        ClearBuffer();
    }

    AudioOutputRoute Route() const { return route_; }
    uint32_t Generation() const { return generation_; }
    size_t BufferedSamples() const { return size_; }

    bool Write(const int16_t* samples, size_t count, uint32_t generation) {
        if (samples == nullptr || generation != generation_ || count > kBufferSamples - size_) {
            return false;
        }
        for (size_t i = 0; i < count; ++i) {
            buffer_[(read_index_ + size_ + i) % kBufferSamples] = samples[i];
        }
        size_ += count;
        return true;
    }

    size_t Read(int16_t* samples, size_t count, uint32_t generation) {
        if (samples == nullptr || generation != generation_) {
            return 0;
        }
        const size_t actual = std::min(count, size_);
        for (size_t i = 0; i < actual; ++i) {
            samples[i] = buffer_[(read_index_ + i) % kBufferSamples];
        }
        read_index_ = (read_index_ + actual) % kBufferSamples;
        size_ -= actual;
        return actual;
    }

    uint32_t RetryDelayMs() const {
        static constexpr uint32_t kDelays[] = {2000, 4000, 8000, 16000, 30000};
        const size_t index = retry_attempt_ == 0 ? 0 : retry_attempt_ - 1;
        return kDelays[std::min<size_t>(index, std::size(kDelays) - 1)];
    }

    void SetRemoteDelayTenthsMs(uint32_t delay_tenths_ms, uint32_t now_ms) {
        const uint32_t delay_ms = delay_tenths_ms == 0 ? 500 : delay_tenths_ms / 10 + 100;
        tail_deadline_ms_ = now_ms + std::min<uint32_t>(delay_ms, 2000);
    }

    uint32_t TailDeadlineMs() const {
        return tail_deadline_ms_ == 0 ? 500 : tail_deadline_ms_ - tail_start_ms_;
    }

    bool IsDrained(uint32_t now_ms) const { return size_ == 0 && now_ms >= tail_deadline_ms_; }

private:
    void ClearBuffer() {
        read_index_ = 0;
        size_ = 0;
    }

    AudioOutputRoute route_ = AudioOutputRoute::kLocal;
    uint32_t generation_ = 1;
    size_t retry_attempt_ = 0;
    std::array<int16_t, kBufferSamples> buffer_{};
    size_t read_index_ = 0;
    size_t size_ = 0;
    uint32_t tail_start_ms_ = 1000;
    uint32_t tail_deadline_ms_ = 1500;
};

#endif
