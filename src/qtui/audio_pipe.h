#pragma once

#include <cstdint>
#include <optional>
#include <functional>

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

    // Clear the audio stream (used on seek). Resets fed/queued accounting and audioEof_ flag.
    void clear();

    // Feed one decoded audio frame to the stream. Called by pump_audio or directly.
    void feed(const media::AudioFrame& frame);

    // Pump audio frames until ~200ms queued (mirrors SDL app pump_audio, lines 366-379).
    // Feeds frames until SDL_GetAudioStreamQueued >= target bytes, or decoder EOF reached.
    // Called once per worker iteration to maintain buffer.
    // get_frame_callback: returns std::optional<AudioFrame> (nullptr at EOF).
    // After first call returns nullopt, pump_audio stops calling get_frame_callback
    // until the next seek (clear() resets the flag).
    void pump_audio(std::function<std::optional<media::AudioFrame>()> get_frame_callback,
                    int sample_rate, int channels);

    // Return the number of audio samples consumed by the device since seek_base.
    // Mirrors SDL app clock() derivation (lines 388-389): fed - queued_bytes_converted.
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
    bool             audio_eof_       = false;  // Decoder has reached EOF; stop calling get_frame
};
