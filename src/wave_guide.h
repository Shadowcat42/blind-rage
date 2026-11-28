#pragma once
// Sword-wave guidance for melee fights with straight-line ground waves (Augus,
// BCProj_Ogs, Episode 11's second section). Waves fly straight at a fixed speed.
// Blades come as a fan of seven (0, +/-0.2, +/-0.4, +/-0.6 rad); sounding each
// would blur into noise, so the guide is about the player instead:
// - silent while every incoming blade will pass by;
// - when one will hit, the fire-warning buzz plays panned toward the side with
//   the wider gap to the next blade, louder as it nears;
// - a wide horizontal wave (extra side hit areas, AddColNum > 0) brings a short
//   rising sweep that ends when to jump.
// Both start during the enemy's wind-up, before the wave exists: the attack
// animation's own shot notify (BCAnimNotify_ShotArcheType) says when it fires
// and which projectile (Augus: 0.73 s into the fan swing, 0.75 s into the wide
// one). See docs/MELEE_AIM.md.
#include "fire_warning.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <cmath>
#include <map>
#include <vector>
namespace a11y {
struct WaveSample {
    uint32_t id{};
    float x{}, y{};
    bool wide{}; // the horizontal wave
    // Where the hit cylinder's centre is relative to the projectile, in world x/y:
    // its Translation turned by the actor's full rotation. Augus's blades are
    // rolled 90 degrees (bTrans, as the cylinder is taller than wide), so their
    // (-150, -350, 0) offset points up and back, not sideways. Radius is the
    // cylinder's; for a wide wave, half its whole width.
    float offset_x{}, offset_y{}, radius = 50.f;
    float yaw = NAN; // the actor's facing in degrees, for the log
};
// A shot an enemy's playing animation has yet to fire.
struct Shot {
    float time{}; // seconds into the animation
    bool wide{};
    float speed = 3000.f, radius = 50.f;
};
// The next shot after `current` in an animation playing at `rate`: its index,
// or -1. `seconds` is the real time until it fires.
inline int next_shot(const std::vector<Shot> &shots, float current, float rate, float &seconds) {
    int best = -1;
    if (!(rate > 0) || !std::isfinite(current))
        return best;
    for (size_t i = 0; i < shots.size(); ++i)
        if (shots[i].time > current && (best < 0 || shots[i].time < shots[size_t(best)].time))
            best = int(i);
    if (best >= 0)
        seconds = (shots[size_t(best)].time - current) / rate;
    return best;
}
// A wave about to be fired: where from, in how long, and what kind.
struct WindUp {
    uint32_t id{}; // stable for one wind-up, new for the next
    float x{}, y{};
    float seconds{}; // until the shot
    Shot shot;
};
struct JumpCue {
    bool active{};
    uint64_t at{};         // tick when to jump: the sweep ends here
    uint64_t generation{}; // the wave it is for
};
struct WaveCues {
    FireCue step;
    JumpCue jump;
};
class WaveGuide {
  public:
    // A wave hits when its hit cylinder's path passes within the cylinder's radius
    // plus the player's. Warnings start kWarnSeconds ahead. The jump cue ends
    // kJumpLead before the wave arrives (0.25 s was too late in play; JumpSpeedZ
    // 1000, the first 0.13 s of a jump can't be hit).
    static constexpr float kWarnSeconds = 2.5f, kJumpLead = .6f, kMinSpeed = 500.f;
    static constexpr uint64_t kMinSweepMs = 150;
    // The player's own radius, added to each hit cylinder's.
    float player_radius = 60.f;
    void reset() {
        last_.clear();
        last_tick_ = 0;
        jump_ = {};
        jump_arrival_ = 0;
        last_out_ = {};
        cued_.clear();
        logged_.clear();
    }
    // Diagnostic lines (each wave's first predicted path), taken by the caller.
    std::vector<std::string> log;
    WaveCues update(const std::vector<WaveSample> &waves, float px, float py, float right_x,
                    float right_y, uint64_t now, const std::vector<WindUp> &windups = {}) {
        // The millisecond tick only advances about every 16 ms, so some frames see no
        // time pass: those repeat the last result rather than dropping every wave
        // (which made the buzz flicker on and off).
        if (last_tick_ && now <= last_tick_) {
            auto same = last_out_;
            same.step.tick = now;
            return same;
        }
        WaveCues out;
        out.step.tick = now;
        const float dt = last_tick_ ? float(now - last_tick_) / 1000.f : 0.f;
        struct Pass {
            float side, time;
            float nx, ny, reach;
        };
        std::vector<Pass> blades;
        float bestJump = 1e9f;
        uint32_t jumpWave = 0;
        std::map<uint32_t, WaveSample> seen;
        for (const auto &w : waves) {
            seen[w.id] = w;
            auto it = last_.find(w.id);
            if (dt <= 0 || it == last_.end())
                continue;
            // World velocity: waves move straight; the player's own movement must
            // not bend the prediction.
            float vx = (w.x - it->second.x) / dt, vy = (w.y - it->second.y) / dt;
            float speed = std::hypot(vx, vy);
            if (!std::isfinite(speed) || speed < kMinSpeed)
                continue;
            float cx = w.x + w.offset_x, cy = w.y + w.offset_y;
            float rx = cx - px, ry = cy - py;
            float t = -(rx * vx + ry * vy) / (speed * speed);
            if (t <= 0 || t > kWarnSeconds)
                continue;
            // Signed distance of the path from the player, along the path's normal.
            float nx = -vy / speed, ny = vx / speed;
            float side = -(rx * nx + ry * ny);
            const float reach = w.radius + player_radius;
            if (std::find(logged_.begin(), logged_.end(), w.id) == logged_.end()) {
                logged_.push_back(w.id);
                if (logged_.size() > 64)
                    logged_.erase(logged_.begin());
                char line[256];
                std::snprintf(
                    line, sizeof line,
                    "WAVE_PATH wide=%d side=%d t=%.2f heading=%d yaw=%d cylinder=(%d, %d) "
                    "player=(%d, %d) reach=%d",
                    int(w.wide), int(side), t, int(std::atan2(vy, vx) * 57.2957795f),
                    std::isfinite(w.yaw) ? int(w.yaw) : 999, int(cx), int(cy), int(px), int(py),
                    int(reach));
                log.push_back(line);
            }
            if (w.wide) {
                // A wave that already had its sweep (or shared one) is not cued again.
                bool done = std::find(cued_.begin(), cued_.end(), w.id) != cued_.end() &&
                            !(jump_.active && jump_.generation == w.id);
                if (!done && std::abs(side) < reach && t < bestJump) {
                    bestJump = t;
                    jumpWave = w.id;
                }
                continue;
            }
            blades.push_back({side, t, nx, ny, reach});
        }
        // Wind-ups: the wave will leave the shooter in `seconds` and come straight
        // at the player (the swing turns toward them, HomingTime 1 s).
        const WindUp *coming = nullptr;
        float comingTime = 1e9f;
        if (dt > 0)
            for (const auto &w : windups) {
                float distance = std::hypot(px - w.x, py - w.y);
                if (!std::isfinite(distance) || !(w.shot.speed > 0))
                    continue;
                float t = w.seconds + distance / w.shot.speed;
                if (t <= 0 || t > kWarnSeconds)
                    continue;
                if (std::find(logged_.begin(), logged_.end(), w.id) == logged_.end()) {
                    logged_.push_back(w.id);
                    if (logged_.size() > 64)
                        logged_.erase(logged_.begin());
                    char line[128];
                    std::snprintf(line, sizeof line,
                                  "WAVE_WINDUP wide=%d shot_in=%.2f arrival=%.2f distance=%d",
                                  int(w.shot.wide), w.seconds, t, int(distance));
                    log.push_back(line);
                }
                if (w.shot.wide) {
                    bool done = std::find(cued_.begin(), cued_.end(), w.id) != cued_.end() &&
                                !(jump_.active && jump_.generation == w.id);
                    if (!done && t < bestJump) {
                        bestJump = t;
                        jumpWave = w.id;
                    }
                } else if (t < comingTime) {
                    comingTime = t;
                    coming = &w;
                }
            }
        last_.clear();
        for (const auto &[id, w] : seen)
            last_[id] = {w.x, w.y};
        last_tick_ = now;
        // One jump per burst: waves arriving within 0.5 s of the one being cued
        // share its cue. A new cue ends kJumpLead before arrival, but always has
        // room for the whole sweep; a wave too close for that gets none.
        if (jump_.active && now > jump_.at + 50)
            jump_ = {};
        if (jumpWave) {
            auto arrival = now + uint64_t(bestJump * 1000.f);
            auto cue = [&](uint32_t id) {
                cued_.push_back(id);
                if (cued_.size() > 64)
                    cued_.erase(cued_.begin());
            };
            if (jump_.active && jump_.generation == jumpWave) {
                // The wave being cued: follow its arrival until the sweep starts.
                jump_arrival_ = arrival;
                if (now + kMinSweepMs < jump_.at)
                    jump_.at = std::max(now + kMinSweepMs, arrival - uint64_t(kJumpLead * 1000.f));
            } else if (jump_arrival_ && (arrival > jump_arrival_ ? arrival - jump_arrival_
                                                                 : jump_arrival_ - arrival) < 500) {
                cue(jumpWave); // arrives with the last cued wave: one jump covers both
            } else if (bestJump * 1000.f >= kMinSweepMs) {
                cue(jumpWave);
                jump_.active = true;
                jump_.generation = jumpWave;
                jump_arrival_ = arrival;
                jump_.at = std::max(now + kMinSweepMs, arrival > uint64_t(kJumpLead * 1000.f)
                                                           ? arrival - uint64_t(kJumpLead * 1000.f)
                                                           : 0);
            }
        }
        out.jump = jump_;
        last_out_ = out;
        // The blade that will hit soonest, if any.
        const Pass *hit = nullptr;
        for (const auto &b : blades)
            if (std::abs(b.side) < b.reach && (!hit || b.time < hit->time))
                hit = &b;
        if (!hit && coming) {
            // Blades not out yet: the middle one will be aimed at the player and the
            // gaps either side are equal, so either way across its path works. The
            // buzz says right; once the blades exist, the gaps decide.
            out.step.active = true;
            out.step.urgency = std::clamp(1.f - comingTime / kWarnSeconds, 0.f, 1.f);
            out.step.pan = 1.f;
            last_out_ = out;
            return out;
        }
        if (!hit) {
            last_out_ = out;
            return out;
        }
        // Gaps to the nearest other blade on each side, measured along the hitting
        // blade's normal n. `side` is the player's offset from a blade's path along
        // n, so a blade with a larger side lies toward -n: `toward_minus` and
        // `toward_plus` are the gaps on each side of the player.
        float toward_minus = 1e9f, toward_plus = 1e9f;
        for (const auto &b : blades) {
            if (&b == hit || std::abs(b.time - hit->time) > .6f)
                continue;
            float offset = b.side - hit->side;
            if (offset > 0)
                toward_minus = std::min(toward_minus, offset);
            else
                toward_plus = std::min(toward_plus, -offset);
        }
        // Step toward the wider gap; with equal gaps, away from the blade's path.
        bool minus = toward_minus > toward_plus || (toward_minus == toward_plus && hit->side <= 0);
        float stepX = minus ? -hit->nx : hit->nx, stepY = minus ? -hit->ny : hit->ny;
        out.step.active = true;
        out.step.urgency = std::clamp(1.f - hit->time / kWarnSeconds, 0.f, 1.f);
        float pan = stepX * right_x + stepY * right_y;
        out.step.pan = pan >= 0 ? 1.f : -1.f;
        last_out_ = out;
        return out;
    }

  private:
    struct Point {
        float x{}, y{};
    };
    std::map<uint32_t, Point> last_;
    uint64_t last_tick_{};
    JumpCue jump_;
    uint64_t jump_arrival_{};
    WaveCues last_out_;
    std::vector<uint32_t> cued_; // waves that had (or shared) a sweep
    std::vector<uint32_t> logged_;
};
// A quick rising sweep, 500 to 1000 Hz over 150 ms, ending at the jump moment.
class JumpSynth {
  public:
    static constexpr float kLength = .15f, kLow = 500.f, kHigh = 1000.f;
    void render(const JumpCue &cue, float gain, float *out, unsigned frames, uint64_t now) {
        for (unsigned i = 0; i < frames; ++i) {
            float sample = 0;
            if (cue.active && std::isfinite(gain)) {
                double at = double(now) / 1000.0 + double(i) / 48000.0;
                double end = double(cue.at) / 1000.0, t = at - (end - kLength);
                if (t >= 0 && t < kLength) {
                    float f = kLow + (kHigh - kLow) * float(t / kLength);
                    phase_ = std::fmod(phase_ + 6.2831853f * f / 48000.f, 6.2831853f);
                    float edge = std::clamp(float(std::min(t, kLength - t)) / .01f, 0.f, 1.f);
                    sample = std::clamp(gain, 0.f, .4f) * edge * std::sin(phase_);
                }
            }
            out[i * 2] = out[i * 2 + 1] = sample;
        }
    }

  private:
    float phase_{};
};
} // namespace a11y
