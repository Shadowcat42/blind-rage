#pragma once
// Gameplay state for F5 dumps: where the player is, what the walking beacon is
// aiming at, the touch triggers, live enemies, the game's burst guide target and
// blocking volumes near the line to the target. The .pages file only holds the
// memory the menu reader touched, so these values are written out as text.
#include "script_calls.h"
#include "walk_beacon.h"
#include <cmath>
#include <iomanip>
#include <ostream>
#include <sstream>
namespace a11y {
inline std::string dump_vector(const LockVector &v) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(0) << "(" << v.x << ", " << v.y << ", " << v.z << ")";
    return out.str();
}
// Horizontal distance from point p to the segment a-b.
inline float segment_distance(const LockVector &p, const LockVector &a, const LockVector &b) {
    float dx = b.x - a.x, dy = b.y - a.y, length = dx * dx + dy * dy;
    float t = length > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length, 0.f, 1.f) : 0;
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}
inline void write_gameplay_dump(const Memory &m, Reader &reader, uint32_t pc, std::ostream &out) {
    auto is_class = [&](uint32_t object, std::string_view cls) {
        for (auto c = object ? m.u32(object + 0x34) : 0, depth = 0u; c && depth < 32;
             c = m.u32(c + 0x40), ++depth)
            if (reader.object_name(m, c) == cls)
                return true;
        return false;
    };
    auto name = [&](uint32_t object) {
        return object ? reader.class_name(m, object) + " " + reader.object_name(m, object)
                      : std::string("none");
    };
    out << "\nGAMEPLAY\n";
    if (!pc || !is_class(pc, "BCPlayerController")) {
        out << "No player controller seen yet.\n";
        return;
    }
    auto pawn = m.u32(pc + 0x5c4), world = m.u32(pc + 0xd4);
    LockVector at{};
    bool located = pawn && lock_vector(m, pawn + 0x54, at);
    out << "controller " << name(pc) << "\npawn " << name(pawn)
        << " pde=" << int(m.u8(pawn + 0x466)) << " health=" << int32_t(m.u32(pawn + 0x2d8))
        << " team=" << int(m.u8(pawn + 0x468));
    if (located)
        out << " location=" << dump_vector(at);
    auto pawnClass = pawn ? m.u32(pawn + 0x34) : 0;
    if (auto velocity = ScriptLookup::property(m, reader, pawnClass, "Velocity").first) {
        LockVector v;
        if (lock_vector(m, pawn + velocity, v))
            out << " velocity=" << dump_vector(v);
    }
    out << " cinematic=" << ((m.u32(pc + 0x378) & 0x8000) ? 1 : 0) << "\n";
    auto camera = lock_camera(m, pc);
    if (camera.valid)
        out << "camera location=" << dump_vector(camera.location)
            << " yaw=" << int(std::atan2(camera.forward.y, camera.forward.x) * 57.2957795f)
            << " pitch=" << int(std::asin(std::clamp(camera.forward.z, -1.f, 1.f)) * 57.2957795f)
            << " fov=" << int(camera.fov) << "\n";
    // The game's burst guide target, which the walking beacon prefers.
    LockVector target{};
    bool targeted = false;
    if (auto guide =
            ScriptLookup::property(m, reader, m.u32(pc + 0x34), "BurstGuideTargetActor").first) {
        auto actor = m.u32(pc + guide);
        out << "burst_guide " << name(actor);
        if (actor && lock_vector(m, actor + 0x54, target)) {
            targeted = true;
            out << " location=" << dump_vector(target);
        }
        out << "\n";
    }
    // Touch triggers, nearest first, as the walking beacon sees them.
    auto events = walk_touch_events(m, world, is_class);
    struct Trigger {
        uint32_t event, origin;
        LockVector at;
        float distance;
    };
    std::vector<Trigger> triggers;
    for (auto event : events) {
        auto origin = m.u32(event + WalkLayout::originator);
        LockVector position{};
        if (!origin || !lock_vector(m, origin + 0x54, position))
            continue;
        triggers.push_back({event, origin, position,
                            located ? std::hypot(position.x - at.x, position.y - at.y) : 0.f});
    }
    std::sort(triggers.begin(), triggers.end(),
              [](const Trigger &a, const Trigger &b) { return a.distance < b.distance; });
    for (const auto &t : triggers) {
        bool fired = m.u32(t.event + WalkLayout::trigger_count) != 0,
             enabled = (m.u32(t.event + WalkLayout::event_flags) & 0x80000000) != 0;
        out << "trigger " << reader.object_name(m, t.event)
            << " area=" << reader.object_name(m, t.origin) << " location=" << dump_vector(t.at)
            << " distance=" << int(t.distance) << " fired=" << fired << " enabled=" << enabled
            << "\n";
        if (!targeted && !fired && enabled) {
            target = t.at;
            targeted = true;
        }
    }
    // Live pawns other than the player.
    unsigned walked = 0;
    for (auto other = world ? m.u32(world + 0x3d8) : 0; other && walked < 1024;
         other = m.u32(other + 0x1e8), ++walked) {
        if (other == pawn || !is_class(other, "BCPawn"))
            continue;
        LockVector position{};
        lock_vector(m, other + 0x54, position);
        out << "pawn " << name(other) << " health=" << int32_t(m.u32(other + 0x2d8))
            << " team=" << int(m.u8(other + 0x468)) << " flags=" << std::hex << m.u32(other + 0xac)
            << std::dec << " location=" << dump_vector(position);
        if (located)
            out << " distance=" << int(std::hypot(position.x - at.x, position.y - at.y));
        out << "\n";
    }
    // Blocking volumes near the line from the player to the beacon's target: the
    // volume's origin, its flag words and how far that origin is from the line.
    if (located && targeted) {
        out << "path from=" << dump_vector(at) << " to=" << dump_vector(target) << "\n";
        auto table = m.u32(reader.layout().objects_global),
             count = m.u32(reader.layout().objects_global + 4);
        for (uint32_t i = 0; table && i < count && i < 250000; ++i) {
            auto object = m.u32(table + i * 4);
            if (!object || reader.class_name(m, object).find("BlockingVolume") == std::string::npos)
                continue;
            LockVector position{};
            if (!lock_vector(m, object + 0x54, position))
                continue;
            auto off = segment_distance(position, at, target);
            if (off > 4000)
                continue;
            out << "volume " << name(object) << " location=" << dump_vector(position)
                << " from_path=" << int(off) << " flags=" << std::hex << m.u32(object + 0xac) << " "
                << m.u32(object + 0xb0) << std::dec << "\n";
        }
    }
}
} // namespace a11y
