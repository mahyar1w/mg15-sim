#undef NDEBUG
#include "../src/audio.h"

#include <cassert>
#include <array>
#include <cmath>
#include <memory>

// Link-time PortAudio fake: exercise recovery without requiring audio hardware.
namespace {
PaError init_result = paNoError, open_result = paNoError, start_result = paNoError;
int initializations = 0, terminations = 0, opens = 0, closes = 0, stops = 0;
bool have_devices = true;
int stream_token;
PaStreamCallback* callback = nullptr;
void* callback_user = nullptr;
PaDeviceInfo device{1, "Test device", 0, 1, 2, 0.01, 0.01, 0.1, 0.1, 48000};
}

extern "C" {
PaError Pa_Initialize() { ++initializations; return init_result; }
PaError Pa_Terminate() { ++terminations; return paNoError; }
const char* Pa_GetErrorText(PaError) { return "Simulated audio error"; }
PaDeviceIndex Pa_GetDeviceCount() { return have_devices ? 1 : 0; }
PaDeviceIndex Pa_GetDefaultInputDevice() { return have_devices ? 0 : paNoDevice; }
PaDeviceIndex Pa_GetDefaultOutputDevice() { return have_devices ? 0 : paNoDevice; }
const PaDeviceInfo* Pa_GetDeviceInfo(PaDeviceIndex index) {
    return have_devices && index == 0 ? &device : nullptr;
}
PaError Pa_OpenStream(PaStream** stream, const PaStreamParameters*,
                      const PaStreamParameters*, double, unsigned long,
                      PaStreamFlags, PaStreamCallback* cb, void* user) {
    ++opens;
    callback = cb;
    callback_user = user;
    *stream = open_result == paNoError ? &stream_token : nullptr;
    return open_result;
}
PaError Pa_StartStream(PaStream* stream) {
    assert(stream == &stream_token);
    return start_result;
}
PaError Pa_StopStream(PaStream* stream) {
    assert(stream == &stream_token);
    ++stops;
    return paNoError;
}
PaError Pa_CloseStream(PaStream* stream) {
    assert(stream == &stream_token);
    ++closes;
    return paNoError;
}
}

int main() {
    {
        amp::Audio audio;
        have_devices = false;
        assert(!audio.start(-1, -1, 48000, 256));
        have_devices = true;
        assert(!audio.start(99, 0, 48000, 256));
        device.maxOutputChannels = 1;
        assert(!audio.start(0, 0, 48000, 256));
        device.maxOutputChannels = 2;
        assert(opens == 0);

        open_result = paDeviceUnavailable;
        assert(!audio.start(0, 0, 48000, 256));
        assert(!audio.running() && !audio.error().empty());
        assert(closes == 0);

        open_result = paNoError;
        start_result = paUnanticipatedHostError;
        assert(!audio.start(0, 0, 48000, 256));
        assert(!audio.running() && !audio.error().empty());
        assert(closes == 1 && stops == 0);

        start_result = paNoError;
        assert(audio.start(-1, -1, 48000, 256));
        assert(audio.running() && audio.error().empty());
        audio.stop();
        audio.stop();
        assert(closes == 2 && stops == 1);
        assert(audio.start(0, 0, 48000, 256));
    }
    assert(closes == 3 && stops == 2);
    assert(initializations == 1 && terminations == 1);

    // Failed initialization is retried and only successful initialization is terminated.
    init_result = paUnanticipatedHostError;
    {
        amp::Audio audio;
        assert(audio.inputs().empty() && audio.outputs().empty());
        assert(!audio.start(0, 0, 48000, 256));
        init_result = paNoError;
        assert(audio.start(0, 0, 48000, 256));
        assert(audio.error().empty());
    }
    assert(initializations == 4 && terminations == 2);
    init_result = paUnanticipatedHostError;
    { amp::Audio audio; }
    assert(terminations == 2);

    // Exercise the real callback and kernel queue, including a full queue
    // followed by retry, then compare settled output with a freshly prepared amp.
    init_result = paNoError;
    auto audio = std::make_unique<amp::Audio>();
    amp::Params p;
    p.clean_volume = 10;
    p.output_db = 0;
    audio->set_params(p);
    assert(!audio->start(0, 0, 44100, 256));
    assert(audio->start(0, 0, 48000, 256));
    std::array<float, 256> input{};
    std::array<float, 512> output{};
    p.bass = 0; audio->set_params(p);
    p.bass = 10; audio->set_params(p);
    p.bass = 7.5f; p.speaker_ir = false; audio->set_params(p); // Full queue; retry mode too.
    callback(input.data(), output.data(), 256, nullptr, 0, callback_user);
    audio->set_params(p);
    auto reference = std::make_unique<amp::Amp>();
    reference->prepare(48000, p);
    for (int block = 0; block < 100; ++block) {
        std::array<float, 256> expected{};
        for (int i = 0; i < 256; ++i) {
            input[i] = 0.001f * std::sin((block * 256 + i) * 0.025f);
            expected[i] = reference->process(input[i], p);
        }
        callback(input.data(), output.data(), 256, nullptr, 0, callback_user);
        for (int i = 0; i < 256; ++i) {
            assert(std::isfinite(output[2*i]));
            assert(output[2*i] == output[2*i+1]);
            if (block > 80) assert(std::abs(output[2*i] - expected[i]) < 2e-6f);
        }
    }
}
