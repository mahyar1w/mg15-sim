#include "amp.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace amp {
namespace {
constexpr double tau = 2 * std::numbers::pi;
float safe(float x, float lo, float hi, float fallback) noexcept {
    return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback;
}
}
void Amp::Lowpass::prepare(double rate, double frequency, double q) {
    const double w = tau * frequency / rate, c = std::cos(w), alpha = std::sin(w) / (2*q);
    b0 = float((1-c)/2/(1+alpha)); b1=2*b0; b2=b0;
    a1=float(-2*c/(1+alpha)); a2=float((1-alpha)/(1+alpha));
    z1=z2=0;
}
float Amp::Lowpass::process(float x) noexcept {
    const float y = b0*x+z1;
    z1=b1*x-a1*y+z2; z2=b2*x-a2*y;
    return y;
}
void Amp::prepare(double sample_rate, const Params& p) {
    if (sample_rate != 48000.0) throw std::invalid_argument("Amp model requires 48000 Hz");
    initial_ = p;
    const auto response=make_output(p.bass,p.middle,p.treble,p.speaker_ir,p.cabinet);
    eq_.prepare(response.eq);
    ir_.prepare(response.ir);
    smooth_ = float(1 - std::exp(-1.0/(0.012*48000)));
    bass_a_ = float(1 - std::exp(-tau * 28.42/192000));
    input_low_a_ = float(1-std::exp(-tau*1591.55/192000));
    input_high_a_ = float(1-std::exp(-tau*1293.94/192000));
    dc_a_ = float(1-std::exp(-tau*10/192000));
    antialias1_.prepare(192000, 18000, 0.5411961);
    antialias2_.prepare(192000, 18000, 1.3065630);
    drive_tone_.prepare(192000, 6500, 0.7071068);
    reset();
}
void Amp::reset() noexcept {
    eq_.reset(); ir_.reset();
    dry_delay_ = {}; dry_index_=0;
    drive_bass_=drive_hf_=input_low_=input_high_=drive_dc_={}; previous_=0;
    antialias1_.z1=antialias1_.z2=antialias2_.z1=antialias2_.z2=0;
    drive_tone_.z1=drive_tone_.z2=0;
    gain_=safe(initial_.gain,0,10,6.5f)/10;
    clean_=safe(initial_.clean_volume,0,10,5)/10;
    volume_=safe(initial_.drive_volume,0,10,5)/10;
    output_=std::pow(10.0f,safe(initial_.output_db,-36,6,-6)/20);
    channel_=initial_.drive ? 1 : 0; wet_=initial_.voicing ? 1 : 0;
}
float Amp::process(float input, const Params& p) noexcept {
    input = safe(input,-8,8,0);
    auto smooth = [this](float& z,float target) { z += smooth_*(target-z); };
    smooth(gain_,safe(p.gain,0,10,6.5f)/10);
    smooth(clean_,safe(p.clean_volume,0,10,5)/10);
    smooth(volume_,safe(p.drive_volume,0,10,5)/10);
    smooth(output_,std::pow(10.0f,safe(p.output_db,-36,6,-6)/20));
    smooth(channel_,p.drive ? 1 : 0); smooth(wet_,p.voicing ? 1 : 0);

    // MG10-informed drive only: the measured clean path already includes
    // its preamp response. Parallel feedback branches are shelves, not two
    // cascaded highpasses. Normalize the input-stage gain near 1 kHz.
    const float taper=(std::pow(100.0f,gain_)-1)/99;
    const float pot=560+999440*taper, rf=1e6f*pot/(1e6f+pot);
    const float fc=1.0f/(float(tau)*220e-12f*rf);
    const float a=float(1-std::exp(-tau*fc/192000));
    // Digital input has no measured volts-per-full-scale calibration.
    // Scale the resistor gain to retain useful clean-to-crunch travel.
    const float drive=1.5625f*1.7f*1.7f*(1+rf/2825.0f);
    float driven=0;
    for (int i=1;i<=4;++i) {
        const float x=previous_+(input-previous_)*(i*0.25f);
        const float pre=(x+6.8f*input_low_.hp(x,input_low_a_)
                          +8.292683f*input_high_.hp(x,input_high_a_))/9.2f;
        const float filtered=drive_hf_.lp(drive_bass_.hp(pre,bass_a_),a);
        // Red/green LEDs motivate mild asymmetry; thresholds are assumed.
        const float threshold=filtered>=0 ? 0.25f : 0.28f;
        const float clipped=threshold*std::tanh(0.25f*drive*filtered/threshold);
        const float centered=drive_dc_.hp(clipped,dc_a_);
        driven=antialias2_.process(antialias1_.process(drive_tone_.process(centered)));
    }
    previous_=input;
    const float selected=input*clean_*(1-channel_)+driven*volume_*channel_;
    const float dry=dry_delay_[dry_index_];
    dry_delay_[dry_index_]=selected;
    dry_index_=(dry_index_+1)%(2*kBlock);
    // Explicit signal chain: amp/channel level -> EQ -> cabinet IR.
    const float equalized=eq_.process(selected);
    const float voiced=ir_.process(equalized);
    return std::clamp(output_*(dry+wet_*(voiced-dry)),-1.0f,1.0f);
}
} // namespace amp
