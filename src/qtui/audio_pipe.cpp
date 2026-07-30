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
    samples_fed_  = 0;

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
        samples_fed_ = 0;
    }
}

void AudioPipe::feed(const media::AudioFrame& frame)
{
    if (!stream_) return;

    // Queue audio data as interleaved F32 samples
    if (!frame.samples.empty()) {
        SDL_PutAudioStreamData(stream_, frame.samples.data(),
                               (int)(frame.samples.size() * sizeof(float)));
        samples_fed_ += frame.samples.size() / (channels_ > 0 ? channels_ : 1);
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

    // Consumed = fed - queued, clamped to 0
    if (samples_fed_ > queued_samples) {
        return samples_fed_ - queued_samples;
    }
    return 0;
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
