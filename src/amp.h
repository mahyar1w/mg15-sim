#pragma once
#include "measured_eq.h"

namespace amp {
struct Params {
    float gain = 6.5f;
    float bass = 5.0f, middle = 5.0f, treble = 5.0f;
    float clean_volume = 5.0f, drive_volume = 5.0f;
    float output_db = -6.0f; // Interface headroom, separate from physical controls.
    bool drive = false;
    bool voicing = true; // Enable output response, including EQ.
    int cabinet = 0; // 0 = built-in speaker; 1..4 = captured IRs.
    bool speaker_ir = true; // Broad fitted EQ + speaker IR.
};

class Amp {
public:
    // The measurements and baked convolution model are native 48 kHz.
    void prepare(double sample_rate, const Params& p = {});
    void reset() noexcept;
    // Kernel design is not realtime safe. Call off the audio thread, then
    // transfer with set_kernel at an audio block boundary (as Audio does).
    void set_kernel(const OutputKernel& kernel) noexcept { eq_.set_kernel(kernel.eq); ir_.set_kernel(kernel.ir); }
    float process(float input, const Params& p) noexcept;
private:
    struct Pole {
        float z = 0;
        float lp(float x, float a) noexcept { z += a * (x - z); return z; }
        float hp(float x, float a) noexcept { return x - lp(x, a); }
    };
    struct Lowpass {
        float b0=0, b1=0, b2=0, a1=0, a2=0, z1=0, z2=0;
        void prepare(double rate, double frequency, double q);
        float process(float x) noexcept;
    };
    MeasuredEq eq_, ir_;
    Params initial_{};
    Pole drive_bass_, drive_hf_, input_low_, input_high_, drive_dc_;
    Lowpass antialias1_, antialias2_, drive_tone_;
    std::array<float, 2*kBlock> dry_delay_{};
    int dry_index_ = 0;
    float previous_ = 0, smooth_ = 0;
    float gain_ = 0, clean_ = 0, volume_ = 0, output_ = 0, channel_ = 0, wet_ = 0;
    float bass_a_ = 0, input_low_a_ = 0, input_high_a_ = 0, dc_a_ = 0;
};
} // namespace amp
