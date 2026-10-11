#include "audio.h"

#include <algorithm>
#include <cmath>

namespace amp {

Audio::Audio() {
    const PaError e = Pa_Initialize();
    initialized_ = e == paNoError;
    if (e != paNoError) error_ = Pa_GetErrorText(e);
}

Audio::~Audio() {
    stop();
    if (initialized_) Pa_Terminate();
}

static std::vector<Device> list(bool want_input) {
    std::vector<Device> result;
    for (int i = 0; i < Pa_GetDeviceCount(); ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info) continue;
        const bool ok = want_input ? info->maxInputChannels > 0 : info->maxOutputChannels > 0;
        if (ok) result.push_back({i, info->name});
    }
    return result;
}

std::vector<Device> Audio::inputs() const { return initialized_ ? list(true) : std::vector<Device>{}; }
std::vector<Device> Audio::outputs() const { return initialized_ ? list(false) : std::vector<Device>{}; }

bool Audio::start(int input, int output, double rate, unsigned long frames) {
    stop();
    error_.clear();
    if (!initialized_) {
        const PaError e = Pa_Initialize();
        if (e != paNoError) { error_ = Pa_GetErrorText(e); return false; }
        initialized_ = true;
    }
    if (input < 0) input = Pa_GetDefaultInputDevice();
    if (output < 0) output = Pa_GetDefaultOutputDevice();
    if (input < 0 || output < 0) { error_ = "No audio device"; return false; }
    const PaDeviceInfo* input_info = Pa_GetDeviceInfo(input);
    const PaDeviceInfo* output_info = Pa_GetDeviceInfo(output);
    if (!input_info || !output_info) { error_ = "Invalid audio device"; return false; }
    if (input_info->maxInputChannels < 1 || output_info->maxOutputChannels < 2) {
        error_ = "Audio requires a mono input and a stereo output";
        return false;
    }

    PaStreamParameters in{};
    in.device = input;
    in.channelCount = 1;
    in.sampleFormat = paFloat32;
    in.suggestedLatency = input_info->defaultLowInputLatency;

    PaStreamParameters out{};
    out.device = output;
    out.channelCount = 2;
    out.sampleFormat = paFloat32;
    out.suggestedLatency = output_info->defaultLowOutputLatency;

    if (rate != 48000.0) { error_ = "Amp model requires 48000 Hz"; return false; }
    const Params initial = params();
    amp_.prepare(rate, initial);
    read_.store(write_.load(std::memory_order_acquire), std::memory_order_release);
    queued_bass_ = initial.bass; queued_middle_ = initial.middle; queued_treble_ = initial.treble;
    queued_speaker_ir_ = initial.speaker_ir; queued_cabinet_ = initial.cabinet;
    PaStream* opened = nullptr;
    PaError e = Pa_OpenStream(&opened, &in, &out, rate, frames, paNoFlag, callback, this);
    if (e != paNoError) { error_ = Pa_GetErrorText(e); return false; }
    e = Pa_StartStream(opened);
    if (e != paNoError) {
        Pa_CloseStream(opened);
        error_ = Pa_GetErrorText(e);
        return false;
    }
    stream_ = opened;
    return true;
}

void Audio::stop() noexcept {
    if (!stream_) return;
    Pa_StopStream(stream_);
    Pa_CloseStream(stream_);
    stream_ = nullptr;
}

int Audio::callback(const void* in, void* out, unsigned long frames,
                    const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* user) {
    return static_cast<Audio*>(user)->process(static_cast<const float*>(in), static_cast<float*>(out), frames);
}

Params Audio::params() const noexcept {
    Params p;
    p.gain=p_gain.load(std::memory_order_relaxed);
    p.bass=p_bass.load(std::memory_order_relaxed);
    p.middle=p_middle.load(std::memory_order_relaxed);
    p.treble=p_treble.load(std::memory_order_relaxed);
    p.clean_volume=p_clean.load(std::memory_order_relaxed);
    p.drive_volume=p_volume.load(std::memory_order_relaxed);
    p.output_db=p_output.load(std::memory_order_relaxed);
    p.drive=p_drive.load(std::memory_order_relaxed);
    p.voicing=p_voicing.load(std::memory_order_relaxed);
    p.speaker_ir=p_speaker_ir.load(std::memory_order_relaxed);
    p.cabinet=p_cabinet.load(std::memory_order_relaxed);
    return p;
}

void Audio::set_params(const Params& p) {
    p_gain.store(p.gain,std::memory_order_relaxed);
    p_bass.store(p.bass,std::memory_order_relaxed);
    p_middle.store(p.middle,std::memory_order_relaxed);
    p_treble.store(p.treble,std::memory_order_relaxed);
    p_clean.store(p.clean_volume,std::memory_order_relaxed);
    p_volume.store(p.drive_volume,std::memory_order_relaxed);
    p_output.store(p.output_db,std::memory_order_relaxed);
    p_drive.store(p.drive,std::memory_order_relaxed);
    p_voicing.store(p.voicing,std::memory_order_relaxed);
    p_speaker_ir.store(p.speaker_ir,std::memory_order_relaxed);
    p_cabinet.store(p.cabinet,std::memory_order_relaxed);
    if (p.bass==queued_bass_ && p.middle==queued_middle_ && p.treble==queued_treble_
        && p.speaker_ir==queued_speaker_ir_ && p.cabinet==queued_cabinet_) return;
    const unsigned slot=write_.load(std::memory_order_relaxed), next=(slot+1)%3;
    if (next==read_.load(std::memory_order_acquire)) return; // Retry on next UI frame.
    kernels_[slot]=make_output(p.bass,p.middle,p.treble,p.speaker_ir,p.cabinet);
    write_.store(next,std::memory_order_release);
    queued_bass_=p.bass; queued_middle_=p.middle; queued_treble_=p.treble;
    queued_speaker_ir_=p.speaker_ir; queued_cabinet_=p.cabinet;
}

int Audio::process(const float* in, float* out, unsigned long frames) noexcept {
    const Params p = params();
    const unsigned written = write_.load(std::memory_order_acquire);
    if (read_.load(std::memory_order_relaxed) != written) {
        // Latest complete kernel only. The producer cannot reuse these slots
        // until the copy finishes and read_ is released.
        amp_.set_kernel(kernels_[(written + 2) % 3]);
        read_.store(written, std::memory_order_release);
    }

    float ip = 0.0f, op = 0.0f;
    for (unsigned long i = 0; i < frames; ++i) {
        const float x = in ? in[i] : 0.0f;
        const float y = amp_.process(x, p);
        out[i * 2] = out[i * 2 + 1] = y;
        ip = std::max(ip, std::abs(x));
        op = std::max(op, std::abs(y));
    }
    in_peak_.store(ip, std::memory_order_relaxed);
    out_peak_.store(op, std::memory_order_relaxed);
    return paContinue;
}

} // namespace amp
