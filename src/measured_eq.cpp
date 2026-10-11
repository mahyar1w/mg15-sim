#include "measured_eq.h"
#include "measured_eq_data.h"
#include "cab_ir_data.h"
#include "guitar_eq_data.h"
#include "user_ir_data.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace amp {
namespace {
template<class T, std::size_t N>
void fft(std::array<std::complex<T>, N>& a, bool inverse) noexcept {
    for (std::size_t i = 1, j = 0; i < N; ++i) {
        std::size_t bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t length = 2; length <= N; length <<= 1) {
        const T angle = (inverse ? 2 : -2) * std::numbers::pi_v<T> / length;
        const std::complex<T> step(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < N; i += length) {
            std::complex<T> w(1, 0);
            for (std::size_t j = 0; j < length / 2; ++j) {
                const auto u = a[i + j], v = a[i + j + length / 2] * w;
                a[i + j] = u + v;
                a[i + j + length / 2] = u - v;
                w *= step;
            }
        }
    }
    if (inverse) for (auto& x : a) x /= T(N);
}
float cabinet_tap(int cabinet, int i) {
    if (cabinet <= 0 || cabinet > kUserIrCount) return kCabIr[i];
    return i < kUserIrTaps ? kUserIrs[cabinet - 1][i] : 0.0f;
}
float control(float x) { return std::isfinite(x) ? std::clamp(x, 0.0f, 10.0f) : 5.0f; }
}

std::array<float, kTaps> make_eq_impulse(float bass, float middle, float treble, bool speaker_ir, bool include_ir) {
    const std::array<float, 3> position{control(bass) / 5, control(middle) / 5, control(treble) / 5};
    std::array<int, 3> cell{};
    std::array<float, 3> fraction{};
    for (int i = 0; i < 3; ++i) {
        cell[i] = std::min(int(position[i]), 1);
        fraction[i] = position[i] - cell[i];
    }
    auto curve = measured::baseline;
    if (speaker_ir) curve.fill(0.0f);
    for (int b = 0; b < 2; ++b) for (int m = 0; m < 2; ++m) for (int t = 0; t < 2; ++t) {
        const float weight = (speaker_ir ? 0.0f : 1.0f) * (b ? fraction[0] : 1 - fraction[0]) *
                             (m ? fraction[1] : 1 - fraction[1]) *
                             (t ? fraction[2] : 1 - fraction[2]);
        const auto& point = measured::grid[(cell[0] + b) * 9 + (cell[1] + m) * 3 + cell[2] + t];
        for (std::size_t i = 0; i < curve.size(); ++i) curve[i] += weight * point[i];
    }
    constexpr int design_size = 2 * kTaps;
    std::array<std::complex<double>, design_size> spectrum{};
    for (int bin = 0; bin <= design_size / 2; ++bin) {
        const double frequency = std::max(1.0, bin * 48000.0 / design_size);
        double db;
        if (frequency < measured::frequencies.front()) {
            // Unmeasured low end: second-order acoustic rolloff.
            db = curve.front() + (speaker_ir ? 0 : 12) * std::log2(frequency / measured::frequencies.front());
        } else if (frequency > measured::frequencies.back()) {
            // Unmeasured high end: hold continuity and roll off 24 dB/octave.
            db = curve.back() - (speaker_ir ? 0 : 24) * std::log2(frequency / measured::frequencies.back());
        } else {
            const auto it = std::lower_bound(measured::frequencies.begin(), measured::frequencies.end(), frequency);
            const int hi = std::max(1, int(it - measured::frequencies.begin()));
            const int lo = hi - 1;
            const double blend = std::log(frequency / measured::frequencies[lo]) /
                                 std::log(measured::frequencies[hi] / measured::frequencies[lo]);
            db = curve[lo] + blend * (curve[hi] - curve[lo]);
        }
        if (speaker_ir) {
            // Broad controls fitted to robust paired MG15CF measurements.
            // Do not reproduce capture comb notches in the playing model.
            const std::array<float,3> knobs{control(bass),control(middle),control(treble)};
            for (int j=0;j<3;++j) {
                const auto& c = guitar::controls[j];
                // Saturating knob taper: mid-travel already gives most of the effect.
                const double u=std::tanh(c.taper*(knobs[j]-5.0)/5.0)/std::tanh(c.taper);
                const double gain=u<0 ? -u*c.cut : u*c.boost;
                const double r=frequency/c.center;
                const double octave=std::log2(r)/c.shape;
                const double shape=j==0 ? 1/(1+std::pow(r,c.shape)) :
                    j==2 ? std::pow(r,c.shape)/(1+std::pow(r,c.shape)) : std::exp(-0.5*octave*octave);
                db += gain*shape;
            }
            // Fixed clarity EQ before the cabinet: remove sub-bass and
            // gently reduce the low-mid accumulation without thinning leads.
            const double low=frequency/65.0;
            db += 10*std::log10(std::pow(low,4)/(1+std::pow(low,4)));
            const double mud=std::log2(frequency/280.0)/0.85;
            db -= 2.5*std::exp(-0.5*mud*mud);
            // The legacy IR is peak-normalized, leaving its guitar midrange
            // about 5–8 dB below bypass. Restore level and add a gentle 3 dB
            // presence shelf while retaining the cabinet's high-end rolloff.
            // Include this in minimum-phase design; no unfiltered dry blend.
            const double ratio = frequency / 1500.0;
            db += 5.0 + 3.0 * ratio * ratio / (1.0 + ratio * ratio);
        }
        spectrum[bin] = std::clamp(db, -120.0, 60.0) * std::log(10.0) / 20;
        if (bin > 0 && bin < design_size / 2) spectrum[design_size - bin] = spectrum[bin];
    }
    fft(spectrum, true);
    for (int i = 1; i < design_size / 2; ++i) spectrum[i] *= 2;
    for (int i = design_size / 2 + 1; i < design_size; ++i) spectrum[i] = 0;
    fft(spectrum, false);
    for (auto& x : spectrum) x = std::exp(x);
    if (speaker_ir && include_ir) {
        static_assert(kCabIrTaps == kTaps && kCabIrRate == 48000);
        std::array<std::complex<double>, design_size> speaker{};
        for (int i = 0; i < kTaps; ++i) speaker[i] = kCabIr[i];
        fft(speaker, false);
        // Multiply complex responses so the original IR phase is preserved.
        for (int i = 0; i < design_size; ++i) spectrum[i] *= speaker[i];
    }
    fft(spectrum, true);
    std::array<float, kTaps> impulse{};
    for (int i = 0; i < kTaps; ++i) {
        const double fade = i < kTaps - 1024 ? 1 : 0.5 * (1 + std::cos(std::numbers::pi * (i - (kTaps - 1024)) / 1023));
        impulse[i] = float(spectrum[i].real() * fade);
    }
    return impulse;
}

EqKernel make_eq(float bass, float middle, float treble, bool speaker_ir, bool include_ir) {
    const auto impulse = make_eq_impulse(bass, middle, treble, speaker_ir, include_ir);
    EqKernel kernel;
    for (int p = 0; p < kPartitions; ++p) {
        for (int i = 0; i < kBlock; ++i) kernel.partitions[p][i] = impulse[p * kBlock + i];
        fft(kernel.partitions[p], false);
    }
    return kernel;
}

OutputKernel make_output(float bass, float middle, float treble, bool speaker_ir, int cabinet) {
    OutputKernel result;
    result.eq=make_eq(bass,middle,treble,speaker_ir,false);
    // Separate cabinet stage; measured headphones already contain their
    // output voicing, so their final IR is unity with the same block latency.
    for (int p=0;p<kPartitions;++p) {
        for (int i=0;i<kBlock;++i)
            result.ir.partitions[p][i]=speaker_ir ? cabinet_tap(cabinet,p*kBlock+i) :
                (p==0 && i==0 ? 1.0f : 0.0f);
        fft(result.ir.partitions[p],false);
    }
    return result;
}

void MeasuredEq::prepare(const EqKernel& kernel) noexcept {
    current_ = target_ = kernel;
    reset();
}
void MeasuredEq::reset() noexcept {
    history_ = {};
    input_ = output_ = overlap_ = {};
    current_ = target_;
    cursor_ = head_ = morph_blocks_ = 0;
}
void MeasuredEq::set_kernel(const EqKernel& kernel) noexcept {
    target_ = kernel;
    morph_blocks_ = 12; // 32 ms at 48 kHz; no coefficient jumps at knob changes.
}
float MeasuredEq::process(float input) noexcept {
    const float out = output_[cursor_];
    input_[cursor_] = input;
    if (++cursor_ == kBlock) { render(); cursor_ = 0; }
    return out;
}
void MeasuredEq::render() noexcept {
    if (morph_blocks_ > 0) {
        const float blend = 1.0f / morph_blocks_--;
        for (int p = 0; p < kPartitions; ++p) for (int i = 0; i < kFFT; ++i)
            current_.partitions[p][i] += blend * (target_.partitions[p][i] - current_.partitions[p][i]);
    }
    auto& newest = history_[head_];
    newest = {};
    for (int i = 0; i < kBlock; ++i) newest[i] = input_[i];
    fft(newest, false);
    Spectrum result{};
    for (int p = 0; p < kPartitions; ++p) {
        const auto& past = history_[(head_ - p + kPartitions) % kPartitions];
        for (int i = 0; i < kFFT; ++i) result[i] += past[i] * current_.partitions[p][i];
    }
    fft(result, true);
    for (int i = 0; i < kBlock; ++i) {
        output_[i] = result[i].real() + overlap_[i];
        overlap_[i] = result[kBlock + i].real();
    }
    head_ = (head_ + 1) % kPartitions;
}
} // namespace amp
