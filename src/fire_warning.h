#pragma once
// Incoming-fire warning for shooting sections without the game's lock-on
// (Episode 8's chase). Enemy fire there (BCProj_Sar03Fire) descends and then
// glides along the ground toward where the player was, and steering the cursor
// also moves the player sideways, so the player needs to know that fire is
// coming and from which side. A band-limited sawtooth buzz, panned to the side
// of the closest approaching projectile, gets louder and higher as it nears.
#include "melee_aim.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>
namespace a11y {
struct FireCue {
    bool active{};
    float pan{};     // -1 left of the player, +1 right
    float urgency{}; // 0 far, 1 about to arrive
    uint64_t tick{};
    // Demonstration: hold for hold_ms without updates, panning from pan to pan_to.
    uint32_t hold_ms{};
    float pan_to{};
};
struct FireSample {
    uint32_t id{};
    float x{}, y{};
};
// Tracks each projectile's position relative to the player between updates
// (the player moves along the route too) and predicts where its path passes:
// the warning follows the one reaching its closest approach soonest, panned to
// the side its path passes on, so fire heading straight in sounds centred.
class FireTracker {
  public:
    struct Point {
        float x{}, y{};
    };
    void reset() {
        last_.clear();
        last_tick_ = 0;
    }
    FireCue update(const std::vector<FireSample> &projectiles, float px, float py, float right_x,
                   float right_y, uint64_t now) {
        FireCue cue;
        cue.tick = now;
        const float dt = last_tick_ && now > last_tick_ ? float(now - last_tick_) / 1000.f : 0.f;
        std::map<uint32_t, Point> positions;
        float best = 0;
        for (const auto &p : projectiles) {
            Point rel{p.x - px, p.y - py};
            if (!std::isfinite(rel.x) || !std::isfinite(rel.y))
                continue;
            positions[p.id] = rel;
            auto it = last_.find(p.id);
            if (dt <= 0 || it == last_.end() || std::hypot(rel.x, rel.y) > kRange)
                continue;
            Point v{(rel.x - it->second.x) / dt, (rel.y - it->second.y) / dt};
            float speed2 = v.x * v.x + v.y * v.y;
            if (speed2 < kMinClosing * kMinClosing)
                continue;
            // Time until the path is closest to the player; negative means moving away.
            float t = -(rel.x * v.x + rel.y * v.y) / speed2;
            if (t <= 0 || t > kWarnSeconds)
                continue;
            Point pass{rel.x + v.x * t, rel.y + v.y * t};
            if (std::hypot(pass.x, pass.y) > kPassRadius)
                continue;
            float urgency = std::clamp(1.f - t / kWarnSeconds, 0.f, 1.f);
            if (urgency <= best)
                continue;
            best = std::max(urgency, 1e-3f);
            cue.active = true;
            cue.urgency = urgency;
            cue.pan = std::clamp((pass.x * right_x + pass.y * right_y) / kPanWidth, -1.f, 1.f);
        }
        last_ = std::move(positions);
        last_tick_ = now;
        return cue;
    }
    // Range and lead time (Unreal units, seconds); paths passing farther than
    // kPassRadius are not a threat; kPanWidth is the side offset for full pan.
    static constexpr float kRange = 12000.f, kWarnSeconds = 4.f, kMinClosing = 150.f,
                           kPassRadius = 1200.f, kPanWidth = 600.f;

  private:
    std::map<uint32_t, Point> last_;
    uint64_t last_tick_{};
};
// The map's projectile cache (BCMapInfo.ChacheList_BCProjectile), minus the
// player's own shots and exploded or removed projectiles, as horizontal points.
inline std::vector<FireSample> fire_samples(const Memory &m, uint32_t pc) {
    std::vector<FireSample> out;
    auto pawn = m.u32(pc + 0x5c4), world = m.u32(pc + 0xd4);
    auto map = world ? m.u32(world + 0x4ac) : 0;
    if (!pawn || !map)
        return out;
    auto array = m.u32(map + 0xc0), n = m.u32(map + 0xc4), cap = m.u32(map + 0xc8);
    if (n > 256 || n > cap || cap > 4096 || (!array && n))
        return out;
    for (unsigned i = 0; i < n; ++i) {
        auto projectile = m.u32(array + i * 4);
        if (!projectile || m.u32(projectile + 0x98) == pawn || m.u32(projectile + 0xd0) == pawn ||
            (m.u32(projectile + 0x298) & 0x1400000) || !m.u32(projectile + 0x20) ||
            (m.u32(projectile + 0xac) & 0x50000000) || (m.u32(projectile + 0xb0) & 0x400))
            continue;
        LockVector at;
        if (lock_vector(m, projectile + 0x54, at))
            out.push_back({projectile, at.x, at.y});
    }
    return out;
}
// Band-limited sawtooth (12 harmonics), panned, rising from 140 to 260 Hz and
// louder with urgency.
class FireSynth {
  public:
    void render(const FireCue &cue, float gain, float *out, unsigned frames, uint64_t now) {
        bool audible = cue.active && now >= cue.tick &&
                       now - cue.tick <= (cue.hold_ms ? cue.hold_ms : 200u) &&
                       std::isfinite(cue.pan) && std::isfinite(cue.urgency);
        auto target = cue.pan;
        if (cue.hold_ms && std::isfinite(cue.pan_to))
            target += (cue.pan_to - cue.pan) *
                      std::clamp(float(now - cue.tick) / float(cue.hold_ms), 0.f, 1.f);
        auto pan = audible ? std::clamp(target, -1.f, 1.f) : pan_;
        auto urgency = audible ? std::clamp(cue.urgency, 0.f, 1.f) : 0.f;
        auto frequency = 140.f + 120.f * urgency;
        for (unsigned i = 0; i < frames; ++i) {
            auto desired = audible && std::isfinite(gain)
                               ? std::clamp(gain, 0.f, .4f) * (.45f + .55f * urgency)
                               : 0.f;
            volume_ += std::clamp(desired - volume_, -.0005f, .0005f);
            pan_ += std::clamp(pan - pan_, -.0005f, .0005f);
            float saw = 0;
            for (int k = 1; k <= 12; ++k)
                saw += ((k & 1) ? 1.f : -1.f) * std::sin(float(k) * phase_) / float(k);
            auto sample = volume_ * .63661977f * saw * .6f;
            out[i * 2] = sample * std::sqrt((1 - pan_) * .5f);
            out[i * 2 + 1] = sample * std::sqrt((1 + pan_) * .5f);
            phase_ = std::fmod(phase_ + 6.2831853f * frequency / 48000.f, 6.2831853f);
        }
    }

  private:
    float phase_{}, volume_{}, pan_{};
};
} // namespace a11y
