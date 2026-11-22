#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace a11y {
inline int64_t steady_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
// A song-style count-in for a synchronized tap: three low beats, then a high beat
// on the key's perfect moment (BCSynchronicTrackKey.StartTime). The prompt appears
// 1.1 s before it and the mod's first clock reading arrives a frame later (logged
// 1.038-1.055 s before it), so 0.33 s beats put the first, 0.99 s before the key,
// just after that reading with room for the audio queue.
inline constexpr int64_t kCountInBeatNs = 330'000'000;
inline constexpr int kCountInBeats = 4;
inline constexpr float kCountInLow = 525.f, kCountInHigh = 1050.f;
inline constexpr float kCountInLength = .05f, kCountInEdge = .005f;
struct CountInCue {
    // perfect: when the high beat starts. from: when the cue was scheduled; beats
    // that would have started before it are skipped rather than played cut off.
    int64_t perfect{}, from{};
    uint64_t generation{};
    bool active{};
};
class CountInSynth {
  public:
    // When each beat of a count-in was rendered, for the log: the steady-clock
    // time its first sample is due to play, or 0 if it has not been rendered.
    struct Played {
        uint64_t generation{};
        std::array<int64_t, kCountInBeats> at{};
    };
    Played played() const {
        Played out;
        out.generation = played_generation_.load();
        for (int beat = 0; beat < kCountInBeats; ++beat)
            out.at[size_t(beat)] = played_[size_t(beat)].load();
        if (played_generation_.load() != out.generation)
            out.at = {};
        return out;
    }
    // start: the steady-clock time at which the first rendered frame is heard.
    void render(const CountInCue &cue, float gain, float *out, unsigned frames, int64_t start) {
        auto level = std::isfinite(gain) ? std::clamp(gain, 0.f, .4f) : 0.f;
        if (cue.active && played_generation_.load() != cue.generation) {
            for (auto &beat : played_)
                beat = 0;
            played_generation_ = cue.generation;
        }
        for (unsigned i = 0; i < frames; ++i) {
            float sample = 0;
            if (cue.active)
                for (int beat = 0; beat < kCountInBeats; ++beat) {
                    auto begin = cue.perfect - (kCountInBeats - 1 - beat) * kCountInBeatNs;
                    if (begin < cue.from)
                        continue;
                    auto t = float(double(start - begin) / 1e9 + double(i) / 48000.);
                    if (t < 0 || t >= kCountInLength)
                        continue;
                    if (!played_[size_t(beat)].load())
                        played_[size_t(beat)] = start + int64_t(double(i) * 1e9 / 48000.);
                    auto envelope =
                        std::clamp(std::min(t, kCountInLength - t) / kCountInEdge, 0.f, 1.f);
                    auto frequency = beat == kCountInBeats - 1 ? kCountInHigh : kCountInLow;
                    sample += envelope * std::sin(6.2831853f * frequency * t);
                }
            out[i * 2] = out[i * 2 + 1] = level * sample * .70710678f;
        }
    }

    // Streamed blocks follow one continuous sample clock. Re-reading the wall
    // clock for every block would shift the tone's phase at each block boundary
    // (heard as crackle) and jitter the beats; resync only after real drift.
    void render_stream(const CountInCue &cue, float gain, float *out, unsigned frames,
                       int64_t estimate) {
        if (!next_ || std::llabs(estimate - next_) > 15'000'000)
            next_ = estimate;
        render(cue, gain, out, frames, next_);
        next_ += int64_t(frames) * 1'000'000'000 / 48000;
    }

  private:
    int64_t next_{};
    std::atomic<uint64_t> played_generation_{};
    std::array<std::atomic<int64_t>, kCountInBeats> played_{};
};
}
