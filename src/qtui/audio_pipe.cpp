#include "audio_pipe.h"

#include <cstring>
#include <print>

#include "media/audio_frame.h"
#include <SDL3/SDL.h>

AudioPipe::AudioPipe()
{
}

AudioPipe::~AudioPipe()
{
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
    }
    if (subsystem_owned_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

bool AudioPipe::open(int channels, int sample_rate)
{
    if (stream_) {
        // Already open
        return false;
    }

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::println(stderr, "[AudioPipe] audio subsystem init failed: {}", SDL_GetError());
        return false;
    }
    subsystem_owned_ = true;

    SDL_AudioSpec audio_spec{};
    audio_spec.format   = SDL_AUDIO_F32;
    audio_spec.channels = channels;
    audio_spec.freq     = sample_rate;

    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                        &audio_spec, nullptr, nullptr);
    if (!stream_) {
        std::println(stderr, "[AudioPipe] audio open failed: {}", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        subsystem_owned_ = false;
        return false;
    }

    channels_     = channels;
    sample_rate_  = sample_rate;
    samples_fed_.store(0, std::memory_order_relaxed);

    // Detect dummy driver (for fallback gating in headless environments)
    const char* driver_name = SDL_GetCurrentAudioDriver();
    if (driver_name && std::strcmp(driver_name, "dummy") == 0) {
        dummy_driver_ = true;
    }

    return true;
}

void AudioPipe::clear()
{
    if (stream_) {
        SDL_ClearAudioStream(stream_);
        samples_fed_.store(0, std::memory_order_relaxed);
    }
    audio_eof_ = false;  // Reset EOF flag on seek
}

void AudioPipe::feed(const media::AudioFrame& frame)
{
    if (!stream_) return;

    // Queue audio data as interleaved F32 samples
    if (!frame.samples.empty()) {
        SDL_PutAudioStreamData(stream_, frame.samples.data(),
                               (int)(frame.samples.size() * sizeof(float)));
        uint64_t new_fed = samples_fed_.load(std::memory_order_relaxed) +
                          (frame.samples.size() / (channels_ > 0 ? channels_ : 1));
        samples_fed_.store(new_fed, std::memory_order_relaxed);
    }
}

uint64_t AudioPipe::samples_consumed() noexcept
{
    if (!stream_ || sample_rate_ <= 0) {
        return 0;
    }

    // Queued audio in bytes
    int queued_bytes = SDL_GetAudioStreamQueued(stream_);
    // Convert bytes to sample count (F32 = 4 bytes per sample)
    uint64_t queued_samples = (uint64_t)queued_bytes / (sizeof(float) * (channels_ > 0 ? channels_ : 1));

    // Consumed = fed - queued, clamped to 0 (mirrors SDL app clock(), lines 388-389)
    uint64_t fed = samples_fed_.load(std::memory_order_relaxed);
    if (fed > queued_samples) {
        return fed - queued_samples;
    }
    return 0;
}

void AudioPipe::pump_audio(std::function<std::optional<media::AudioFrame>()> get_frame_callback,
                           int sample_rate, int channels)
{
    if (!stream_ || audio_eof_) {
        return;  // Already at EOF, stop pulling
    }

    // Target ~200ms of buffered audio (mirrors SDL app pump_audio, lines 371)
    const int target = sample_rate * channels * (int)sizeof(float) / 5;

    // Feed audio frames while queued < target (mirrors lines 372-378)
    while (SDL_GetAudioStreamQueued(stream_) < target) {
        auto a = get_frame_callback();
        if (!a) {
            audio_eof_ = true;  // Mark EOF, stop calling get_frame_callback
            break;
        }
        feed(*a);
    }
}

void AudioPipe::set_gain(float gain) noexcept
{
    if (!stream_) return;
    // Clamp to [0, 1]
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    SDL_SetAudioStreamGain(stream_, gain);
}

void AudioPipe::pause()
{
    if (stream_) {
        SDL_PauseAudioStreamDevice(stream_);
    }
}

void AudioPipe::resume()
{
    if (stream_) {
        SDL_ResumeAudioStreamDevice(stream_);
    }
}
