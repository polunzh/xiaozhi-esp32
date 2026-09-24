#ifndef BLUETOOTH_AUDIO_OUTPUT_H
#define BLUETOOTH_AUDIO_OUTPUT_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

class BluetoothAudioOutput {
public:
    using ProgressCallback = std::function<void()>;

    BluetoothAudioOutput();
    ~BluetoothAudioOutput();

    bool Start(int input_sample_rate, ProgressCallback on_progress);
    bool IsReady() const;
    bool IsDrained() const;
    bool TryWrite(const int16_t* pcm, size_t samples, uint32_t generation, int volume);
    void Cancel(uint32_t generation);
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
