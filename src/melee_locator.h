#pragma once
// Melee beacon: in melee sections outside rapid fire, points to the enemy the
// game would attack, or the nearest one when none is in reach.
//
// The game keeps its current target on the controller's BCTargetLockControl
// (BCPlayerController.TargetControl): SetTargetInformation stores LockedActor,
// and bFixLock is set while L1 lock-on holds it. Without a lock,
// UpdateTargetingActor re-picks LockedActor every 0.2 s from enemies within
// LockOnDistance (500) in line of sight, favouring those in front, and attacks
// turn Asura toward it. So an unlocked LockedActor means an enemy in reach. See
// docs/MELEE_AIM.md.
#include "walk_beacon.h"
namespace a11y {
// Offsets resolved by name at runtime (0 when absent).
struct MeleeFields {
    uint32_t target_control{};         // BCPlayerController.TargetControl
    uint32_t locked_actor{};           // BCTargetLockControl.LockedActor
    uint32_t fix_offset{}, fix_mask{}; // BCTargetLockControl.bFixLock
};
struct MeleeSnapshot {
    AimIdentity controller, pawn, target;
    uint64_t tick{};
    bool active{}, valid{};
    bool locked{}; // Held by L1 lock-on.
    bool reach{};  // The game's own target: attacks turn to it.
    float bearing{}, distance{};
};
// The game's auto-targeting radius (BCPlayerParam.LockOnDistance); also used as
// "in reach" while locked on, where the game keeps the target at any distance.
inline constexpr float kMeleeReach = 500.f, kMeleeBeaconRange = 3000.f;
// Middle pitch of the melee beacon's square wave, an octave below the others.
inline constexpr float kMeleeBeaconPitch = 262.5f;
inline bool live_enemy(const Memory &m, uint32_t other, uint32_t pawn, uint8_t team,
                       const ClassTest &is_class) {
    return other && other != pawn && m.u32(other + 0x20) && int32_t(m.u32(other + 0x2d8)) > 0 &&
           is_class(other, "BCPawn") && m.u8(other + 0x468) != team &&
           !(m.u32(other + 0xac) & 0x50000000);
}
inline MeleeSnapshot capture_melee(const Memory &m, uint32_t pc, uint64_t tick,
                                   const MeleeFields &fields, const ClassTest &is_class) {
    MeleeSnapshot out;
    out.tick = tick;
    out.controller = {pc, m.u32(pc + 0x20)};
    auto pawn = m.u32(pc + 0x5c4);
    out.pawn = {pawn, m.u32(pawn + 0x20)};
    out.active = aim_context_active(m, out.controller, out.pawn) && m.u8(pawn + 0x466) == 1 &&
                 !melee_rapid_fire(m, pc);
    if (!out.active)
        return out;
    auto camera = lock_camera(m, pc);
    LockVector at;
    if (!camera.valid || !lock_vector(m, pawn + 0x54, at))
        return out;
    const auto team = m.u8(pawn + 0x468);
    uint32_t target = 0;
    if (auto control = fields.target_control ? m.u32(pc + fields.target_control) : 0;
        control && fields.locked_actor) {
        auto actor = m.u32(control + fields.locked_actor);
        if (live_enemy(m, actor, pawn, team, is_class)) {
            target = actor;
            out.locked = fields.fix_offset && fields.fix_mask &&
                         (m.u32(control + fields.fix_offset) & fields.fix_mask);
            out.reach = !out.locked;
        }
    }
    LockVector position;
    if (!target) {
        auto world = m.u32(pc + 0xd4);
        float best = std::numeric_limits<float>::infinity();
        unsigned walked = 0;
        for (auto other = world ? m.u32(world + 0x3d8) : 0; other && walked < 1024;
             other = m.u32(other + 0x1e8), ++walked) {
            if (!live_enemy(m, other, pawn, team, is_class) ||
                !lock_vector(m, other + 0x54, position))
                continue;
            auto distance = std::hypot(position.x - at.x, position.y - at.y);
            if (std::isfinite(distance) && distance < best) {
                best = distance;
                target = other;
            }
        }
    }
    out.valid = true;
    if (!target || !lock_vector(m, target + 0x54, position))
        return out;
    auto dx = position.x - at.x, dy = position.y - at.y;
    out.distance = std::hypot(dx, dy);
    if (!std::isfinite(out.distance)) {
        out.distance = 0;
        return out;
    }
    auto bearing =
        (std::atan2(dy, dx) - std::atan2(camera.forward.y, camera.forward.x)) * 57.2957795f;
    while (bearing > 180)
        bearing -= 360;
    while (bearing < -180)
        bearing += 360;
    out.bearing = bearing; // Unreal's world is left-handed: positive is to the right.
    out.target = {target, m.u32(target + 0x20)};
    if (out.locked)
        out.reach = out.distance <= kMeleeReach;
    return out;
}
// Like the walking beacon: stereo for the side, a higher pitch ahead than behind
// (262.5 and 220 Hz with kMeleeBeaconPitch), pulses quickening with distance. It turns steady only in reach; out of reach
// it always pulses.
inline BeaconCue melee_cue(const MeleeSnapshot &melee) {
    BeaconCue cue;
    cue.tick = melee.tick;
    if (!melee.active || !melee.valid || !melee.target.pointer)
        return cue;
    cue.active = true;
    cue.pan = std::clamp(melee.bearing / 90.f, -1.f, 1.f);
    static const float behind = std::log2(440.f / 525.f) / .65f;
    cue.elevation = std::abs(melee.bearing) > 90 ? behind : 0.f;
    // BeaconSynth is continuous at 4 or less and fully pulsed from 8.
    cue.error =
        melee.reach ? 0.f : std::clamp(8.f + melee.distance / kMeleeBeaconRange * 37.f, 8.f, 45.f);
    cue.generation = (uint64_t(melee.target.pointer) << 8) ^ melee.target.index;
    return cue;
}
} // namespace a11y
