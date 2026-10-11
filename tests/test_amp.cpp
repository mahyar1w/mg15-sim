#undef NDEBUG
#include "../src/amp.h"
#include "../src/cab_ir_data.h"
#include <cassert>
#include <cstdio>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <vector>

using namespace amp;
int main() {
    // Partitioned processing must reproduce the actual kernel, with exactly
    // one block of latency, including across nonzero partition boundaries.
    const auto kernel = make_eq(0,10,5);
    const auto impulse = make_eq_impulse(0,10,5);
    auto eq = std::make_unique<MeasuredEq>();
    eq->prepare(kernel);
    for (int i=0;i<kTaps+2*kBlock;++i) {
        const float y=eq->process(i==0 ? 1.0f : 0.0f);
        const float expected=i>=kBlock && i<kBlock+kTaps ? impulse[i-kBlock] : 0;
        assert(std::abs(y-expected)<2e-5f);
    }
    // Independent direct convolution against arbitrary input catches history
    // indexing and overlap errors that an impulse alone could miss.
    eq->reset();
    std::vector<float> input(2048);
    for (int i=0;i<int(input.size());++i) {
        input[i]=0.01f*std::sin(i*0.17f)+0.007f*std::cos(i*0.019f);
        double expected=0;
        for(int j=0;j<=i-kBlock;++j) expected+=input[i-kBlock-j]*impulse[j];
        assert(std::abs(eq->process(input[i])-expected)<2e-5);
    }
    Params p;
    p.speaker_ir=false;
    p.clean_volume=10; p.output_db=0;
    auto amp=std::make_unique<Amp>();
    amp->prepare(48000,p);
    // Default clean path is exactly measured voicing, not a second MG10 EQ/IR.
    const auto neutral=make_eq_impulse(5,5,5);
    for(int i=0;i<kTaps+2*kBlock;++i) {
        const float y=amp->process(i==0 ? 0.01f : 0,p);
        const float expected=i>=2*kBlock && i<kTaps+2*kBlock ? 0.01f*neutral[i-2*kBlock] : 0;
        assert(std::abs(y-expected)<2e-6f);
    }
    // Reset is deterministic; coefficients remain intact.
    amp->reset();
    std::vector<float> first(2048);
    for(int i=0;i<2048;++i) first[i]=amp->process(input[i],p);
    amp->reset();
    for(int i=0;i<2048;++i) assert(amp->process(input[i],p)==first[i]);
    // Separate channel volumes: inactive channel's volume and gain do not
    // affect the clean output; both channels can mute independently.
    p.voicing=false; p.drive_volume=0;
    amp->prepare(48000,p);
    for(int i=0;i<4096;++i) {
        p.gain=float(i%11);
        const float y=amp->process(0.05f,p);
        if(i>2*kBlock) assert(std::abs(y-0.05f)<1e-6f);
    }
    p.clean_volume=0; amp->prepare(48000,p);
    for(int i=0;i<2048;++i) assert(amp->process(0.1f,p)==0);
    p.drive=true; p.clean_volume=10; amp->prepare(48000,p);
    for(int i=0;i<2048;++i) assert(amp->process(0.1f,p)==0);
    // Large signals, switches, EQ changes, and invalid input stay finite.
    p.drive_volume=10; amp->prepare(48000,p);
    const auto extreme=make_output(10,0,10,false);
    for(int i=0;i<48000;++i) {
        if(i==10000) amp->set_kernel(extreme);
        p.drive=(i/4000)%2; p.voicing=(i/6000)%2;
        const float y=amp->process(4.0f*std::sin(i*0.1f),p);
        assert(std::isfinite(y) && std::abs(y)<=1);
    }
    assert(std::isfinite(amp->process(std::numeric_limits<float>::quiet_NaN(),p)));
    bool rejected=false;
    try { amp->prepare(44100); } catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected);

    // Speaker response includes the intended clarity EQ before the raw IR.
    const auto speaker=make_eq_impulse(5,5,5,true);
    for (double frequency : {150.0, 500.0, 1500.0, 4000.0, 8000.0}) {
        std::complex<double> raw{}, voiced{};
        for (int i=0;i<kTaps;++i) {
            const auto phase=std::polar(1.0,-2*std::numbers::pi*frequency*i/48000);
            raw+=double(kCabIr[i])*phase;
            voiced+=double(speaker[i])*phase;
        }
        const double lift=20*std::log10(std::abs(voiced)/std::abs(raw));
        const double low=frequency/65.0, presence=frequency/1500.0;
        const double mud=std::log2(frequency/280.0)/0.85;
        const double expected_lift=5+3*presence*presence/(1+presence*presence)
            +10*std::log10(std::pow(low,4)/(1+std::pow(low,4)))
            -2.5*std::exp(-0.5*mud*mud);
        assert(std::abs(lift-expected_lift)<0.03);
        if (frequency==500) assert(20*std::log10(std::abs(voiced))>-4.0);
        if (frequency==4000) assert(lift>7.5);
    }
    // Verify the voiced speaker kernel through the complete clean path.
    p = Params{};
    p.clean_volume=10; p.output_db=0;
    assert(p.speaker_ir);
    amp->prepare(48000,p);
    for(int i=0;i<kTaps+2*kBlock;++i) {
        const float y=amp->process(i==0 ? 0.01f : 0,p);
        const float expected=i>=2*kBlock && i<2*kBlock+kTaps ? 0.01f*speaker[i-2*kBlock] : 0;
        assert(std::abs(y-expected)<2e-6f);
    }

    // Musical EQ must move the intended band monotonically, without the
    // comb notches in the raw captures. Check actual reconstructed kernels.
    auto db_at=[](const auto& h,double f) {
        std::complex<double> z{};
        for(int i=0;i<kTaps;++i) z+=double(h[i])*std::polar(1.0,-2*std::numbers::pi*f*i/48000);
        return 20*std::log10(std::abs(z));
    };
    for(int axis=0;axis<3;++axis) {
        const double frequency=axis==0 ? 100 : axis==1 ? 757 : 6000;
        double last=-1000;
        for(float knob : {0.0f,2.5f,5.0f,7.5f,10.0f}) {
            float knobs[3]={5,5,5}; knobs[axis]=knob;
            const auto h=make_eq_impulse(knobs[0],knobs[1],knobs[2],true);
            const double db=db_at(h,frequency);
            assert(std::isfinite(db) && db>last+0.5);
            last=db;
            // Broad model stays within its intentional cut/boost envelope.
            for(double f : {80.,150.,300.,750.,1500.,3000.,6000.,10000.}) {
                const double delta=db_at(h,f)-db_at(speaker,f);
                assert(delta>-19 && delta<13);
            }
        }
    }

    // At a representative input level, default drive should provide crunch
    // rather than near-square saturation; maximum gain still adds distortion.
    auto thd = [&](float gain) {
        p=Params{}; p.drive=true; p.voicing=false;
        p.drive_volume=10; p.output_db=0; p.gain=gain;
        amp->prepare(48000,p);
        double energy=0;
        std::complex<double> fundamental{};
        for(int i=0;i<48000;++i) {
            const double phase=2*std::numbers::pi*1000*i/48000;
            const float y=amp->process(float(0.1*std::sin(phase)),p);
            if(i>=24000) {
                energy+=double(y)*y;
                fundamental+=double(y)*std::polar(1.0,-phase);
            }
        }
        const double fundamental_power=2*std::norm(fundamental/24000.0);
        return std::sqrt(std::max(0.0,energy/24000-fundamental_power)/fundamental_power);
    };
    const double crunch=thd(6.5f), saturated=thd(10);
    std::fprintf(stderr,"Drive THD: default %.5f, maximum %.5f\n",crunch,saturated);
    assert(crunch>0.01 && crunch<0.5);
    assert(saturated>=crunch);
    // Measured MG15CF output level is flat within ~1 dB from knob 2 to 10 (1 kHz, 0.05 amplitude).
    auto level=[&](float gain) {
        p=Params{}; p.drive=true; p.voicing=false;
        p.drive_volume=10; p.output_db=0; p.gain=gain;
        amp->prepare(48000,p);
        double energy=0;
        for(int i=0;i<48000;++i) {
            const float y=amp->process(float(0.05*std::sin(2*std::numbers::pi*1000*i/48000)),p);
            if(i>=24000) energy+=double(y)*y;
        }
        return 10*std::log10(energy/24000);
    };
    const double l0=level(0), l2=level(2), l3=level(3), l10=level(10);
    std::fprintf(stderr,"Drive level dB: knob0 %.1f, 2 %.1f, 3 %.1f, 10 %.1f\n",l0,l2,l3,l10);
    assert(l10-l0>8); // Knob 0 stays comparatively clean.
    // The new tone-filter history must also reset deterministically.
    amp->reset();
    for(int i=0;i<2048;++i) first[i]=amp->process(input[i],p);
    amp->reset();
    for(int i=0;i<2048;++i) assert(amp->process(input[i],p)==first[i]);
}
