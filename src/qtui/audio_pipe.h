#pragma once

#include <cstdint>
#include <optional>

// Forward declarations
struct SDL_AudioStream;
namespace media { struct AudioFrame; struct AudioInfo; }

// AudioPipe: encapsulates SDL audio stream lifecycle and feeding.
// Manages audio buffer, samples tracking, and gain/mute control.
//
// Thread safety: This class is not thread-safe internally. All methods
// (feed, samples_consumed, set_gain, clock) must be called from the same
// thread that owns the worker loop.
class AudioPipe {
public:
    explicit AudioPipe();
    ~AudioPipe();

    AudioPipe(const AudioPipe&) = delete;
    AudioPipe& operator=(const AudioPipe&) = delete;

    // Open audio device for the given stream parameters. Returns true on success.
    // Must be called before any feed/clock/gain operations.
    [[nodiscard]] bool open(int channels, int sample_rate);

    // Clear the audio stream (used on seek). Retained for compatibility with SDL app.
    void clear();

    // Feed one decoded audio frame to the stream. Called repeatedly from worker
    // to maintain ~200ms buffer. No-op if stream not open.
    void feed(const media::AudioFrame& frame);

    // Return the number of audio samples consumed by the device since seek_base.
    // Derived from total fed minus currently queued bytes, adjusted for interleaving.
    [[nodiscard]] uint64_t samples_consumed() noexcept;

    // Set playback gain (0.0 = silent, 1.0 = full). Clamped to [0, 1].
    // Calls SDL_SetAudioStreamGain internally.
    void set_gain(float gain) noexcept;

    // Pause audio output (called on pause/stop).
    void pause();

    // Resume audio output (called on play).
    void resume();

    // Check if stream is open and valid.
    [[nodiscard]] bool is_open() const noexcept { return stream_ != nullptr; }

    // Check if the dummy driver is active (for fallback detection).
    // Returns true if stream is open and the device name indicates dummy driver.
    [[nodiscard]] bool is_dummy_driver() const noexcept { return dummy_driver_; }

private:
    SDL_AudioStream* stream_          = nullptr;
    uint64_t         samples_fed_     = 0;
    int              channels_        = 0;
    int              sample_rate_     = 0;
    bool             dummy_driver_    = false;  // Set on open if device is dummy
    bool             subsystem_owned_ = false;  // Did we init SDL audio subsystem
};
