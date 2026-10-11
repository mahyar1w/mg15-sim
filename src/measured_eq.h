#pragma once

#include <array>
#include <complex>
#include <cstddef>

namespace amp {
inline constexpr int kBlock = 128;
inline constexpr int kFFT = 2 * kBlock;
inline constexpr int kTaps = 8192;
inline constexpr int kPartitions = kTaps / kBlock;
using Spectrum = std::array<std::complex<float>, kFFT>;
struct EqKernel { std::array<Spectrum, kPartitions> partitions{}; };

// Non-realtime minimum-phase design. Controls use a 0..10 scale.
// speaker_ir=true: broad fitted guitar EQ + speaker cabinet.
// speaker_ir=false: original trilinear measured response.
EqKernel make_eq(float bass, float middle, float treble, bool speaker_ir = false, bool include_ir = true);
std::array<float, kTaps> make_eq_impulse(float bass, float middle, float treble, bool speaker_ir = false, bool include_ir = true);

struct OutputKernel { EqKernel eq, ir; };
// cabinet: 0 = built-in speaker, 1..kUserIrCount = captured IRs
// (see user_ir_data.h). Ignored in measured-headphones mode.
OutputKernel make_output(float bass, float middle, float treble, bool speaker_ir, int cabinet = 0);

class MeasuredEq {
public:
    void prepare(const EqKernel& kernel) noexcept;
    void reset() noexcept;
    void set_kernel(const EqKernel& kernel) noexcept;
    float process(float input) noexcept;
private:
    void render() noexcept;
    EqKernel current_{}, target_{};
    std::array<Spectrum, kPartitions> history_{};
    std::array<float, kBlock> input_{}, output_{}, overlap_{};
    int cursor_ = 0, head_ = 0, morph_blocks_ = 0;
};
} // namespace amp
