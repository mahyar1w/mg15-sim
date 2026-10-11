#pragma once

#include "amp.h"

#include <atomic>
#include <string>
#include <vector>

#include <portaudio.h>

namespace amp {

struct Device {
    int index = -1;
    std::string name;
};

class Audio {
public:
    Audio();
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool start(int input, int output, double rate, unsigned long frames);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept { return stream_ != nullptr; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] std::vector<Device> inputs() const;
    [[nodiscard]] std::vector<Device> outputs() const;
    [[nodiscard]] float input_peak() const noexcept { return in_peak_.load(std::memory_order_relaxed); }
    [[nodiscard]] float output_peak() const noexcept { return out_peak_.load(std::memory_order_relaxed); }

    // UI thread: builds changed EQ kernels and publishes through an SPSC queue.
    void set_params(const Params& p);

private:
    static int callback(const void* in, void* out, unsigned long frames,
                        const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* user);
    int process(const float* in, float* out, unsigned long frames) noexcept;

    PaStream* stream_ = nullptr;
    bool initialized_ = false;
    std::string error_;
    Amp amp_;
    Params params() const noexcept;
    std::atomic<float> p_gain{6.5f}, p_bass{5}, p_middle{5}, p_treble{5};
    std::atomic<float> p_clean{5}, p_volume{5}, p_output{-6};
    std::atomic<int> p_cabinet{0};
    std::atomic<bool> p_drive{false}, p_voicing{true}, p_speaker_ir{true};
    std::array<OutputKernel, 3> kernels_{};
    std::atomic<unsigned> write_{0}, read_{0};
    float queued_bass_ = 5, queued_middle_ = 5, queued_treble_ = 5;
    bool queued_speaker_ir_ = true;
    int queued_cabinet_ = 0;
    std::atomic<float> in_peak_{0.0f}, out_peak_{0.0f};
};

} // namespace amp
