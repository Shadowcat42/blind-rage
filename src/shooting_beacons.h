#pragma once
#include "lock_trace.h"
#include <optional>
#include <set>
#include <tuple>

namespace a11y {
// off: no beacon or cone change. beacons: audio guidance. boosted: beacons plus a
// native acquisition cone scaled by configurable multipliers. In every mode,
// holding L1 steers the cursor onto unlocked targets (tracking).
enum class ShootingMode { off, beacons, boosted };
inline const char *shooting_mode_name(ShootingMode mode) {
    switch (mode) {
        case ShootingMode::beacons:
            return "beacons";
        case ShootingMode::boosted:
            return "boosted";
        default:
            return "off";
    }
}
// Whether the audio beacon plays. L1 tracking is available in every mode.
inline bool shooting_assisted(ShootingMode mode) {
    return mode != ShootingMode::off;
}
// The order D-pad Up and the mode shortcut cycle through.
inline ShootingMode next_shooting_mode(ShootingMode mode) {
    switch (mode) {
        case ShootingMode::off:
            return ShootingMode::beacons;
        case ShootingMode::beacons:
            return ShootingMode::boosted;
        default:
            return ShootingMode::off;
    }
}
inline std::optional<ShootingMode> parse_shooting_mode(std::string_view value) {
    if (value == "off" || value == "native")
        return ShootingMode::off;
    if (value == "beacons")
        return ShootingMode::beacons;
    // "autoaim" is the earlier name of the boosted cone.
    if (value == "boosted" || value == "autoaim")
        return ShootingMode::boosted;
    // Tracking was a separate mode with beacons; it is now L1 in every mode.
    if (value == "tracking")
        return ShootingMode::beacons;
    return {};
}
inline bool shooting_mode_recognized(std::string_view value) {
    return parse_shooting_mode(value).has_value();
}
inline ShootingMode shooting_mode(std::string_view value, int legacy = -1) {
    if (auto mode = parse_shooting_mode(value))
        return *mode;
    // Unrecognized text is not a choice: fall back as if the key were absent.
    return legacy < 0 ? ShootingMode::beacons : legacy ? ShootingMode::boosted : ShootingMode::off;
}
// Backoff for (re)starting the cue's audio device after a failure, kept free of
// XAudio2 so the policy is testable. Attempts are allowed immediately at first,
// then 1 s, 2 s, 4 s ... up to 30 s apart until one succeeds.
class AudioRetry {
  public:
    bool due(uint64_t now) const {
        return !failed_ || now >= next_;
    }
    void failure(uint64_t now) {
        delay_ = failed_ ? std::min<uint64_t>(delay_ * 2, 30000) : 1000;
        failed_ = true;
        next_ = now + delay_;
    }
    void success() {
        failed_ = false;
        delay_ = 0;
    }

  private:
    bool failed_{};
    uint64_t next_{}, delay_{};
};
struct AimIdentity {
    uint32_t pointer{}, index{};
    bool operator==(const AimIdentity &) const = default;
    bool operator<(const AimIdentity &other) const {
        return std::tie(pointer, index) < std::tie(other.pointer, other.index);
    }
};
inline LockVector aim_sub(LockVector a, LockVector b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline LockVector aim_cross(LockVector a, LockVector b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float aim_dot(LockVector a, LockVector b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline std::optional<LockVector> aim_unit(LockVector v) {
    auto n = lock_length(v);
    if (!std::isfinite(n) || n < .00001f)
        return {};
    return LockVector{v.x / n, v.y / n, v.z / n};
}
struct AimTarget {
    AimIdentity id;
    LockVector direction;
    bool locked{};
    float distance{}; // Camera-to-component range in native world units.
    // Inside the cursor's safe frame, so the player can actually aim at it.
    bool reachable = true;
    // Melee rapid fire: an enemy projectile rather than an enemy pawn.
    bool projectile{};
};
// The cursor is clamped to the screen's title-safe frame (GetSafeFramePoint in
// BCPlayerCombat.AimShotAimingCtrl), and in shooting mode the camera does not turn.
// Targets are projected with the game's own BCGameInfo.CoordTransformParam (the
// matrix its native WorldLoc2ScreenLoc uses): row vector (x, y, z, 1) * M gives
// clip X, Y and W; screen = Clip / 2 * (1 + X/W, 1 - Y/W).
struct AimFrame {
    float clip_x{}, clip_y{}, min_x{}, min_y{}, max_x{}, max_y{};
    std::array<float, 16> matrix{};
    bool contains(LockVector p) const {
        auto at = [&](int column) {
            return p.x * matrix[size_t(column)] + p.y * matrix[size_t(4 + column)] +
                   p.z * matrix[size_t(8 + column)] + matrix[size_t(12 + column)];
        };
        auto w = at(3);
        if (!(w > 0))
            return false;
        auto x = clip_x / 2 * (1 + at(0) / w), y = clip_y / 2 * (1 - at(1) / w);
        return x >= min_x && x <= max_x && y >= min_y && y <= max_y;
    }
};
inline std::optional<AimFrame> aim_frame(const Memory &m, uint32_t pc) {
    auto f32 = [&](uint32_t at) { return std::bit_cast<float>(m.u32(at)); };
    auto world = m.u32(pc + 0xd4), game = world ? m.u32(world + 0x3b8) : 0;
    auto player = m.u32(pc + 0x36c), viewport = player ? m.u32(player + 0x68) : 0;
    auto hud = m.u32(pc + 0x3c8);
    if (!game || !viewport || !hud || !(m.u32(game + 0x3d0) & 0x80000000))
        return {};
    AimFrame frame;
    frame.clip_x = f32(game + 0x3d4);
    frame.clip_y = f32(game + 0x3d8);
    for (unsigned i = 0; i < 16; ++i)
        frame.matrix[i] = f32(game + 0x3e0 + i * 4);
    auto percentX = f32(viewport + 0xb4), percentY = f32(viewport + 0xb8);
    if (!(frame.clip_x > 0 && frame.clip_x < 100000) ||
        !(frame.clip_y > 0 && frame.clip_y < 100000) || !(percentX > 0 && percentX <= 1) ||
        !(percentY > 0 && percentY <= 1) ||
        !std::all_of(frame.matrix.begin(), frame.matrix.end(),
                     [](float v) { return std::isfinite(v); }))
        return {};
    frame.min_x = frame.clip_x * (1 - percentX) / 2;
    frame.max_x = frame.clip_x - frame.min_x;
    frame.min_y = frame.clip_y * (1 - percentY) / 2;
    // Letterboxed HUD (BCHUD.bDrawBlind) shifts the frame as GetSafeFramePoint does.
    if (m.u32(hud + 0x44c) & 0x4000) {
        auto offset = f32(hud + 0x528) - f32(hud + 0x530);
        if (!std::isfinite(offset))
            return {};
        frame.min_y += offset / 2;
        frame.max_y = f32(hud + 0x530) - frame.min_y + offset;
    } else
        frame.max_y = frame.clip_y - frame.min_y;
    if (!std::isfinite(frame.max_y) || frame.max_y <= frame.min_y)
        return {};
    return frame;
}
struct AimSnapshot {
    static constexpr unsigned capacity = 512;
    AimIdentity controller, pawn;
    uint64_t tick{}, fired{};
    bool active{}, valid{};
    LockVector aim, camera_up;
    unsigned count{}, locks{}, maximum{};
    // When positive, the angle within which the game itself takes the shot (melee
    // rapid-fire homing); the beacon becomes continuous at that edge.
    float window_degrees{};
    std::array<AimTarget, capacity> targets{};
};
// The game producer replaces one copied snapshot, never queues large payloads
// behind narration or retains a live target/camera pointer for the audio thread.
class AimMailbox {
  public:
    void put(const AimSnapshot &value) {
        std::lock_guard lock(mutex_);
        latest_ = value;
    }
    std::optional<AimSnapshot> take() {
        std::lock_guard lock(mutex_);
        auto result = latest_;
        latest_.reset();
        return result;
    }
    void clear() {
        std::lock_guard lock(mutex_);
        latest_.reset();
    }

  private:
    std::mutex mutex_;
    std::optional<AimSnapshot> latest_;
};
inline bool aim_context_active(const Memory &m, AimIdentity controller, AimIdentity pawn) {
    auto read = [&](uint32_t object, uint32_t offset, uint32_t &value) {
        std::array<uint8_t, 4> bytes{};
        if (!object || uint64_t(object) + offset + 4 > 0x100000000ull ||
            !m.copy(object + offset, bytes.data(), 4))
            return false;
        value = cue_word(bytes.data());
        return true;
    };
    if (!controller.pointer || !controller.index || !pawn.pointer || !pawn.index)
        return false;
    uint32_t value{}, world{}, hud{}, manager{};
    return read(controller.pointer, 0x20, value) && value == controller.index &&
           read(controller.pointer, 0x5c4, value) && value == pawn.pointer &&
           read(pawn.pointer, 0x20, value) && value == pawn.index &&
           read(controller.pointer, 0x378, value) && !(value & 0x8000) &&
           read(controller.pointer, 0xd4, world) && read(world, 0x368, value) && !value &&
           read(controller.pointer, 0x3c8, hud) && read(hud, 0x44c, value) &&
           !(value & 0x10000000) && read(hud, 0x478, manager) &&
           (!manager || (read(manager, 0x98, value) && !value));
}
inline AimSnapshot capture_aim(const Memory &m, uint32_t pc, uint32_t locals, uint64_t fired,
                               uint64_t tick) {
    AimSnapshot out;
    out.tick = tick;
    out.fired = fired;
    out.controller = {pc, m.u32(pc + 0x20)};
    auto pawn = m.u32(pc + 0x5c4);
    out.pawn = {pawn, m.u32(pawn + 0x20)};
    out.active = aim_context_active(m, out.controller, out.pawn);
    if (!out.active || !locals)
        return out;
    auto camera = lock_camera(m, pc);
    LockVector origin;
    if (!camera.valid || !lock_vector(m, locals + 0x1c, origin) ||
        lock_length(aim_sub(origin, camera.location)) > .001f ||
        !lock_vector(m, locals + 0x28, out.aim) || !aim_unit(out.aim))
        return out;
    auto cam = m.u32(pc + 0x370);
    auto pitch = float(m.u32(cam + 0x318) & 65535) * .00009587379924f;
    auto yaw = float(m.u32(cam + 0x31c) & 65535) * .00009587379924f;
    auto roll = float(m.u32(cam + 0x320) & 65535) * .00009587379924f;
    LockVector right{
        std::sin(roll) * std::sin(pitch) * std::cos(yaw) - std::cos(roll) * std::sin(yaw),
        std::sin(roll) * std::sin(pitch) * std::sin(yaw) + std::cos(roll) * std::cos(yaw),
        -std::sin(roll) * std::cos(pitch)};
    out.camera_up = aim_cross(camera.forward, right);
    out.maximum = m.u32(pc + 0x7bc);
    if (!out.maximum || out.maximum > 4096)
        return out;
    auto frame = aim_frame(m, pc);
    // CheckLockOnShotTarget rejects targets beyond LockOnShotLockDistance (+0x7c8)
    // when it is positive, before testing the angle.
    const auto lockDistance = std::bit_cast<float>(m.u32(pc + 0x7c8));
    std::set<uint32_t> locked;
    for (auto offset : {0x7e4u, 0x7f0u}) {
        auto array = m.u32(pc + offset), n = m.u32(pc + offset + 4), cap = m.u32(pc + offset + 8);
        if (n > AimSnapshot::capacity || n > cap || cap > 4096 || (!array && n))
            return out;
        for (unsigned i = 0; i < n; ++i) {
            auto target = m.u32(array + i * 12), count = m.u32(array + i * 12 + 4);
            if (count > 4096)
                return out;
            if (count) {
                locked.insert(target);
                if (offset == 0x7e4)
                    out.locks += count;
            }
        }
    }
    auto array = m.u32(locals + 0x50), n = m.u32(locals + 0x54), cap = m.u32(locals + 0x58);
    if (n > AimSnapshot::capacity || n > cap || cap > 4096 || (!array && n))
        return out;
    for (unsigned i = 0; i < n; ++i) {
        auto target = m.u32(array + i * 4);
        LockVector position;
        auto index = m.u32(target + 0x20);
        if (!target || !index || !(m.u32(target + 0x1e4) & 0x40000000) ||
            !lock_target_position(m, target, position))
            continue;
        auto delta = aim_sub(position, camera.location);
        auto distance = lock_length(delta);
        auto direction = aim_unit(delta);
        auto margin = lock_view_margin(camera, position);
        if (!direction || !margin || *margin < .5f)
            continue;
        out.targets[out.count++] = {{target, index},
                                    *direction,
                                    locked.contains(target),
                                    distance,
                                    (!frame || frame->contains(position)) &&
                                        !(lockDistance > 0 && distance > lockDistance)};
    }
    out.valid = true;
    return out;
}
// Tracking mode: a proportional virtual stick deflection that steers the native
// cursor (whose aim line is the lock cone's axis) toward one unlocked target.
// The game still moves the cursor at its own speed inside its own frame and camera
// limits; the player's stick is added on top and can always override.
struct StickNudge {
    float right{}, up{};
    AimIdentity target;
    float yaw{}, pitch{}; // Error from the cursor to the target, in degrees.
    bool focus{};         // Chosen from the beacon's cluster rather than the fallback.
};
// Errors are measured in the cursor's frame: right = up x aim, up' = aim x right.
// `focus` is the beacon's selected cluster: tracking steers to the nearest
// unlocked, reachable target in it, so the cursor goes where the beacon points.
// Only when that cluster has nothing left does it fall back to the nearest target.
inline std::optional<StickNudge> tracking_nudge(const AimSnapshot &snapshot, AimIdentity keep,
                                                float strength,
                                                const std::vector<AimIdentity> &focus = {},
                                                float full = 1.5f, float settle = .3f) {
    if (!snapshot.valid || !snapshot.active || !std::isfinite(strength) || strength <= 0 ||
        !snapshot.maximum || snapshot.locks >= snapshot.maximum)
        return {};
    auto aim = aim_unit(snapshot.aim);
    auto right = aim ? aim_unit(aim_cross(snapshot.camera_up, *aim)) : std::nullopt;
    if (!aim || !right)
        return {};
    auto up = aim_cross(*aim, *right);
    struct Error {
        float yaw, pitch;
    };
    auto error = [&](const AimTarget &target) -> std::optional<Error> {
        auto forward = aim_dot(target.direction, *aim);
        if (!(forward > 0))
            return {};
        constexpr float degrees = 57.2957795f;
        return Error{std::atan2(aim_dot(target.direction, *right), forward) * degrees,
                     std::atan2(aim_dot(target.direction, up), forward) * degrees};
    };
    const AimTarget *chosen = nullptr;
    std::optional<Error> chosenError;
    bool chosenFromFocus = false;
    auto inFocus = [&](const AimTarget &target) {
        return std::find(focus.begin(), focus.end(), target.id) != focus.end();
    };
    for (int pass = focus.empty() ? 1 : 0; pass < 2 && !chosen; ++pass) {
        chosenFromFocus = pass == 0;
        float best = std::numeric_limits<float>::infinity();
        for (unsigned i = 0; i < snapshot.count; ++i) {
            const auto &target = snapshot.targets[i];
            if (target.locked || !target.reachable || (pass == 0 && !inFocus(target)))
                continue;
            auto e = error(target);
            if (!e)
                continue;
            // Stay on the current target while it is still eligible.
            if (keep.pointer && target.id == keep) {
                chosen = &target;
                chosenError = e;
                break;
            }
            auto distance = std::hypot(e->yaw, e->pitch);
            if (distance < best) {
                best = distance;
                chosen = &target;
                chosenError = e;
            }
        }
    }
    if (!chosen)
        return {};
    // Steer in a straight line at full speed: a target locks as soon as the cursor is
    // inside its cone (Episode 1's LockOnShotLockTime is 0), so slowing down on the
    // approach only wastes time. Taper only in the last `full` degrees.
    float stickRight = 0, stickUp = 0;
    const auto distance = std::hypot(chosenError->yaw, chosenError->pitch);
    if (distance > settle) {
        const auto speed = std::min(distance / full, 1.f) * std::min(strength, 1.f);
        stickRight = chosenError->yaw / distance * speed;
        stickUp = chosenError->pitch / distance * speed;
    }
    return StickNudge{stickRight,         stickUp,        chosen->id, chosenError->yaw,
                      chosenError->pitch, chosenFromFocus};
}
struct BeaconCue {
    bool active{};
    float pan{}, elevation{}, error{};
    uint64_t generation{}, tick{};
};
class ShootingBeacons {
  public:
    void reset() {
        focus_.clear();
        groups_.clear();
        controller_ = {};
        pawn_ = {};
        guiding_ = {};
        waiting_ = false;
        manual_focus_ = false;
        ++generation_;
    }
    BeaconCue update(const AimSnapshot &snapshot, int cycle = 0) {
        BeaconCue silent;
        silent.tick = snapshot.tick;
        if (!snapshot.active || !snapshot.valid || snapshot.count > AimSnapshot::capacity ||
            !snapshot.maximum) {
            reset();
            return silent;
        }
        if (controller_ != snapshot.controller || pawn_ != snapshot.pawn)
            reset();
        controller_ = snapshot.controller;
        pawn_ = snapshot.pawn;
        if (snapshot.locks >= snapshot.maximum) {
            waiting_ = true;
            fired_ = snapshot.fired;
            return silent;
        }
        if (waiting_ && fired_ == snapshot.fired && snapshot.locks)
            return silent;
        waiting_ = false;
        auto aim = aim_unit(snapshot.aim);
        auto right = aim ? aim_unit(aim_cross(snapshot.camera_up, *aim)) : std::nullopt;
        if (!aim || !right) {
            reset();
            return silent;
        }
        auto up = aim_cross(*aim, *right);
        std::vector<AimTarget> targets;
        std::set<AimIdentity> seen;
        for (unsigned i = 0; i < snapshot.count; ++i) {
            auto target = snapshot.targets[i];
            auto unit = aim_unit(target.direction);
            if (!target.id.pointer || !target.id.index || !unit || target.locked ||
                !target.reachable || !std::isfinite(target.distance) || target.distance <= 0 ||
                !seen.insert(target.id).second || aim_dot(*unit, *aim) <= 0)
                continue;
            target.direction = *unit;
            targets.push_back(target);
        }
        std::sort(targets.begin(), targets.end(), [](const auto &a, const auto &b) {
            return a.distance != b.distance ? a.distance < b.distance : a.id < b.id;
        });
        groups_.clear();
        // Fixed angular seed radius prevents a chain of neighbors from joining
        // the whole sky into one cluster. Identity, not list order, holds focus.
        for (const auto &target : targets) {
            auto group = std::find_if(groups_.begin(), groups_.end(), [&](const Group &g) {
                auto angle = lock_angle(g.seed, target.direction);
                return angle && *angle <= cluster_angle_degrees &&
                       target.distance / g.near_distance <= cluster_depth_ratio;
            });
            if (group == groups_.end()) {
                groups_.push_back({target.direction, target.distance, {}});
                group = std::prev(groups_.end());
            }
            group->members.push_back(target);
        }
        if (groups_.empty()) {
            focus_.clear();
            return silent;
        }
        auto center = [](const Group &g) {
            LockVector v{};
            for (auto &t : g.members) {
                v.x += t.direction.x;
                v.y += t.direction.y;
                v.z += t.direction.z;
            }
            return *aim_unit(v);
        };
        std::sort(groups_.begin(), groups_.end(), [&](const Group &a, const Group &b) {
            auto x = aim_dot(center(a), *right), y = aim_dot(center(b), *right);
            return x != y ? x < y : a.near_distance < b.near_distance;
        });
        // Moving targets can separate into different depth layers. Keep focus
        // on the current member's group instead of retaining the old merged set.
        for (const auto &group : groups_)
            if (std::any_of(group.members.begin(), group.members.end(),
                            [&](const auto &t) { return t.id == guiding_; })) {
                std::erase_if(focus_, [&](const auto &id) {
                    return std::none_of(group.members.begin(), group.members.end(),
                                        [&](const auto &t) { return t.id == id; });
                });
                break;
            }
        std::vector<AimTarget> remaining;
        for (const auto &t : targets)
            if (focus_.contains(t.id))
                remaining.push_back(t);
        if (cycle || remaining.empty()) {
            int selected = -1;
            if (cycle && !focus_.empty()) {
                for (unsigned i = 0; i < groups_.size(); ++i)
                    if (std::any_of(groups_[i].members.begin(), groups_[i].members.end(),
                                    [&](const auto &t) { return focus_.contains(t.id); })) {
                        selected = int(i);
                        break;
                    }
                if (selected >= 0)
                    selected = (selected + (cycle > 0 ? 1 : int(groups_.size()) - 1)) %
                               int(groups_.size());
            }
            if (selected < 0) {
                auto preferred = preferred_target(targets, *aim);
                for (unsigned i = 0; i < groups_.size(); ++i)
                    if (std::any_of(groups_[i].members.begin(), groups_[i].members.end(),
                                    [&](const auto &t) { return t.id == preferred->id; })) {
                        selected = int(i);
                        break;
                    }
            }
            manual_focus_ = cycle != 0;
            focus_.clear();
            remaining = groups_[selected].members;
            for (const auto &t : remaining)
                focus_.insert(t.id);
            ++generation_;
        }
        // Point at a real remaining member, not an empty midpoint between
        // targets wider apart than the native acquisition cone. Hold that member
        // until it is acquired or disappears; then finish this same cluster.
        auto chosen = std::find_if(remaining.begin(), remaining.end(),
                                   [&](const auto &t) { return t.id == guiding_; });
        AimTarget guide = chosen == remaining.end() ? *preferred_target(remaining, *aim) : *chosen;
        // Automatic focus yields to a materially nearer, almost-overlapping
        // candidate. A 10% margin avoids depth jitter; explicit cycling keeps
        // the player's chosen layer until that cluster is completed or lost.
        if (!manual_focus_) {
            auto foreground = preferred_target(targets, guide.direction);
            if (foreground->distance < guide.distance * foreground_range_ratio) {
                for (const auto &group : groups_)
                    if (std::any_of(group.members.begin(), group.members.end(),
                                    [&](const auto &t) { return t.id == foreground->id; })) {
                        focus_.clear();
                        for (const auto &t : group.members)
                            focus_.insert(t.id);
                        break;
                    }
                guide = *foreground;
            }
        }
        if (guiding_ != guide.id) {
            guiding_ = guide.id;
            ++generation_;
        }
        auto unit = aim_unit(guide.direction);
        if (!unit)
            return silent;
        auto forward = aim_dot(*unit, *aim);
        auto yaw = std::atan2(aim_dot(*unit, *right), forward) * 57.2957795f;
        auto pitch = std::atan2(aim_dot(*unit, up), forward) * 57.2957795f;
        auto error = std::sqrt(yaw * yaw + pitch * pitch);
        // The synthesizer turns continuous at 4 degrees; rescale so that happens
        // at the game's own acceptance angle when the snapshot supplies one.
        if (snapshot.window_degrees > 0 && std::isfinite(snapshot.window_degrees))
            error *= kBeaconContinuousDegrees / snapshot.window_degrees;
        return {true,
                std::clamp(yaw / 30.f, -1.f, 1.f),
                std::clamp(pitch / 30.f, -1.f, 1.f),
                error,
                generation_,
                snapshot.tick};
    }
    // The member the beacon currently points at.
    AimIdentity guiding() const {
        return guiding_;
    }
    static constexpr float kBeaconContinuousDegrees = 4.f;
    size_t clusters() const {
        return groups_.size();
    }

  private:
    static constexpr float cluster_angle_degrees = 8.f, cluster_depth_ratio = 1.25f,
                           overlap_angle_degrees = 1.5f, foreground_range_ratio = .9f;
    static const AimTarget *preferred_target(const std::vector<AimTarget> &targets,
                                             LockVector aim) {
        auto best =
            &*std::max_element(targets.begin(), targets.end(), [&](const auto &a, const auto &b) {
                return aim_dot(a.direction, aim) < aim_dot(b.direction, aim);
            });
        auto reference = best->direction;
        for (const auto &target : targets) {
            auto angle = lock_angle(reference, target.direction);
            if (angle && *angle <= overlap_angle_degrees && target.distance < best->distance)
                best = &target;
        }
        return best;
    }
    struct Group {
        LockVector seed;
        float near_distance{};
        std::vector<AimTarget> members;
    };
    AimIdentity controller_, pawn_, guiding_;
    std::set<AimIdentity> focus_;

  public:
    // The selected cluster, shared with tracking so both aim at the same targets.
    std::vector<AimIdentity> focus() const {
        return {focus_.begin(), focus_.end()};
    }

  private:
    std::vector<Group> groups_;
    bool waiting_{}, manual_focus_{};
    uint64_t fired_{}, generation_{};
};
// Pure stereo synthesis, shared by the audio backend and offline tests.
class BeaconSynth {
  public:
    void render(const BeaconCue &cue, float gain, float *out, unsigned frames, uint64_t now) {
        bool audible = cue.active && now >= cue.tick && now - cue.tick <= 200 &&
                       std::isfinite(cue.pan) && std::isfinite(cue.elevation) &&
                       std::isfinite(cue.error);
        if (generation_ != cue.generation) {
            pulse_ = 0;
            generation_ = cue.generation;
        }
        auto pan = std::isfinite(cue.pan) ? std::clamp(cue.pan, -1.f, 1.f) : 0.f;
        // Middle pitch matches the synchronic count-in's low beat (525 Hz).
        auto frequency = 525.f * std::pow(2.f, std::isfinite(cue.elevation)
                                                   ? std::clamp(cue.elevation, -1.f, 1.f) * .65f
                                                   : 0.f);
        auto error = std::isfinite(cue.error) ? std::clamp(cue.error, 0.f, 45.f) : 45.f;
        auto period = .055f + .295f * error / 45.f;
        // Fill the gaps progressively inside eight degrees; within four degrees
        // the guide is continuous. This indicates alignment, not a native lock.
        auto continuous = std::clamp((8.f - error) / 4.f, 0.f, 1.f);
        for (unsigned i = 0; i < frames; ++i) {
            auto desired = audible && std::isfinite(gain) ? std::clamp(gain, 0.f, .4f) : 0.f;
            volume_ += std::clamp(desired - volume_, -.001f, .001f);
            auto onTime = pulse_ * period;
            auto pulseEnvelope = std::clamp(std::min(onTime, .055f - onTime) / .005f, 0.f, 1.f);
            auto desiredEnvelope = continuous + (1.f - continuous) * pulseEnvelope;
            envelope_ += std::clamp(desiredEnvelope - envelope_, -.004f, .004f);
            // Band-limited triangle: alternating odd harmonics decay as 1/n^2.
            // Even at the highest guide pitch, all five harmonics stay below
            // Nyquist. Keep the existing peak gain limit and stereo positioning.
            auto triangle =
                .810569469f *
                (std::sin(phase_) - std::sin(3.f * phase_) / 9.f + std::sin(5.f * phase_) / 25.f -
                 std::sin(7.f * phase_) / 49.f + std::sin(9.f * phase_) / 81.f);
            auto sample = volume_ * envelope_ * triangle;
            out[i * 2] = sample * std::sqrt((1 - pan) * .5f);
            out[i * 2 + 1] = sample * std::sqrt((1 + pan) * .5f);
            phase_ = std::fmod(phase_ + 6.2831853f * frequency / 48000.f, 6.2831853f);
            pulse_ = std::fmod(pulse_ + 1.f / (48000.f * period), 1.f);
        }
    }

  private:
    float phase_{}, pulse_{}, volume_{}, envelope_{};
    uint64_t generation_{};
};
}
