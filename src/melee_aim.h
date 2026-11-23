#pragma once
#include "shooting_beacons.h"
#include <functional>
#include <string>
#include <string_view>

// Melee rapid fire (holding Shot outside rail shooting). Verified in the
// title-update scripts and reflection (offsets below); see docs/MELEE_AIM.md.
namespace a11y {
enum class AimPriority { projectiles, enemies };
inline const char *aim_priority_name(AimPriority priority) {
    return priority == AimPriority::enemies ? "enemies" : "projectiles";
}
inline AimPriority aim_priority(std::string_view value) {
    return value == "enemies" ? AimPriority::enemies : AimPriority::projectiles;
}

// BCPawn.AttackType (+0x454): 14 is the aimed-fire charge wait, 24 rapid fire
// (BCPlayerController.StartAimShotChargeWait / StartAimShot).
inline bool melee_rapid_fire(const Memory &m, uint32_t pc) {
    auto pawn = pc ? m.u32(pc + 0x5c4) : 0;
    if (!pawn || pawn > UINT32_MAX - 0x500)
        return false;
    auto attack = m.u8(pawn + 0x454);
    return attack == 14 || attack == 24;
}

// SearchSetAimShotHomingTargetActor accepts a target when 1 - dot(camera-to-cursor,
// camera-to-target) < 0.001, about 2.56 degrees. Boosted mode widens that angle.
inline constexpr float kMeleeHomingThreshold = .001f;
inline float melee_homing_degrees(float multiplier = 1.f) {
    const float base = std::acos(1.f - kMeleeHomingThreshold) * 57.2957795f;
    if (!std::isfinite(multiplier) || multiplier < 1)
        multiplier = 1;
    return std::min(base * multiplier, 30.f);
}
inline float melee_homing_threshold(float multiplier) {
    return 1.f - std::cos(melee_homing_degrees(multiplier) / 57.2957795f);
}

// Camera up vector from the cached view rotation, as capture_aim computes it.
inline LockVector aim_camera_up(const Memory &m, uint32_t pc, LockVector forward) {
    auto cam = m.u32(pc + 0x370);
    auto pitch = float(m.u32(cam + 0x318) & 65535) * .00009587379924f;
    auto yaw = float(m.u32(cam + 0x31c) & 65535) * .00009587379924f;
    auto roll = float(m.u32(cam + 0x320) & 65535) * .00009587379924f;
    LockVector right{
        std::sin(roll) * std::sin(pitch) * std::cos(yaw) - std::cos(roll) * std::sin(yaw),
        std::sin(roll) * std::sin(pitch) * std::sin(yaw) + std::cos(roll) * std::cos(yaw),
        -std::sin(roll) * std::cos(pitch)};
    return aim_cross(forward, right);
}

using ClassTest = std::function<bool(uint32_t object, std::string_view cls)>;

// The same candidates the game's homing considers: the map's projectile cache
// (BCMapInfo.ChacheList_BCProjectile) and the world's pawns, minus the player, the
// player's own projectiles and joint-struggle pawns. Projectiles whose
// TakeDamage ignores damage (the laser family) cannot be shot down and are left
// out; pawns on the player's team (BCPawn.TeamType) and dead pawns are too.
inline AimSnapshot capture_melee_aim(const Memory &m, uint32_t pc, uint64_t tick,
                                     AimPriority priority, float window,
                                     const ClassTest &is_class) {
    AimSnapshot out;
    out.tick = tick;
    out.controller = {pc, m.u32(pc + 0x20)};
    auto pawn = m.u32(pc + 0x5c4);
    out.pawn = {pawn, m.u32(pawn + 0x20)};
    out.active = aim_context_active(m, out.controller, out.pawn);
    if (!out.active)
        return out;
    auto camera = lock_camera(m, pc);
    LockVector point;
    if (!camera.valid || !lock_vector(m, pc + 0x6b8, point))
        return out;
    auto aim = aim_unit(aim_sub(point, camera.location));
    if (!aim)
        return out;
    out.aim = *aim;
    out.camera_up = aim_camera_up(m, pc, camera.forward);
    out.maximum = 1; // No volley capacity in melee rapid fire.
    out.window_degrees = window;
    auto world = m.u32(pc + 0xd4);
    if (!world)
        return out;
    auto add = [&](uint32_t actor, bool projectile) {
        if (out.count >= AimSnapshot::capacity)
            return;
        auto index = m.u32(actor + 0x20);
        // Actor.bDeleteMe / bHidden (+0xac), bPendingDelete (+0xb0).
        if (!index || (m.u32(actor + 0xac) & 0x50000000) || (m.u32(actor + 0xb0) & 0x400))
            return;
        LockVector position;
        if (!lock_vector(m, actor + 0x54, position))
            return;
        auto delta = aim_sub(position, camera.location);
        auto distance = lock_length(delta);
        auto direction = aim_unit(delta);
        if (!direction || !std::isfinite(distance))
            return;
        // The game skips targets not in front of the camera (dot <= 0.1). The
        // melee camera turns when the cursor reaches the screen edge, so targets
        // just off screen stay reachable.
        bool front = aim_dot(camera.forward, *direction) > .1f;
        out.targets[out.count++] = {{actor, index}, *direction, false, distance, front, projectile};
    };
    if (auto map = m.u32(world + 0x4ac)) {
        auto array = m.u32(map + 0xc0), n = m.u32(map + 0xc4), cap = m.u32(map + 0xc8);
        if (n > AimSnapshot::capacity || n > cap || cap > 4096 || (!array && n))
            return out;
        for (unsigned i = 0; i < n; ++i) {
            auto projectile = m.u32(array + i * 4);
            // Owner/Instigator: the player's own shots. bExploded/bTemporaryDestroyed.
            if (!projectile || m.u32(projectile + 0x98) == pawn ||
                m.u32(projectile + 0xd0) == pawn || (m.u32(projectile + 0x298) & 0x1400000) ||
                is_class(projectile, "BCProj_Laser_Base"))
                continue;
            add(projectile, true);
        }
    }
    const auto team = m.u8(pawn + 0x468);
    unsigned walked = 0;
    for (auto other = m.u32(world + 0x3d8); other && walked < 1024;
         other = m.u32(other + 0x1e8), ++walked) {
        if (other == pawn || int32_t(m.u32(other + 0x2d8)) <= 0 || !is_class(other, "BCPawn") ||
            m.u8(other + 0x468) == team || is_class(other, "BCPawn_JointStruggleBase"))
            continue;
        add(other, false);
    }
    // Priority: while any target of the preferred kind is reachable, guide and
    // track only that kind.
    const bool wantProjectiles = priority == AimPriority::projectiles;
    bool preferred = false;
    for (unsigned i = 0; i < out.count; ++i)
        preferred |= out.targets[i].reachable && out.targets[i].projectile == wantProjectiles;
    if (preferred)
        for (unsigned i = 0; i < out.count; ++i)
            if (out.targets[i].projectile != wantProjectiles)
                out.targets[i].reachable = false;
    out.valid = true;
    return out;
}

// Spoken description of a selected target: kind and direction from the aim.
inline std::string aim_target_words(bool projectile, float pan, float elevation) {
    std::string text = projectile ? "Projectile" : "Enemy";
    // pan/elevation are the beacon's yaw/pitch over 30 degrees.
    const float yaw = pan * 30.f, pitch = elevation * 30.f;
    if (std::abs(yaw) < 5 && std::abs(pitch) < 5)
        return text + ", ahead.";
    if (std::abs(yaw) >= 5)
        text += yaw < 0 ? ", left" : ", right";
    if (pitch >= 5)
        text += ", high";
    else if (pitch <= -5)
        text += ", low";
    return text + ".";
}
}
