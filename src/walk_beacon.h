#pragma once
#include "melee_aim.h"
#include <mutex>
#include <optional>
#include <vector>

// Walking guidance: points to the nearest Kismet touch trigger (SeqEvent_Touch)
// not yet entered in the loaded levels, or to the game's burst guide target. Walking sections advance through these
// (Episode 4's opening section: two scenery triggers, then GameEvent_Proceed).
// See docs/WALK_BEACON.md.
namespace a11y {
// Title-update reflection (SequenceEvent, Sequence, LevelStreaming, WorldInfo) and,
// for Level.GameSequences, the native layout found in the saved snapshot.
struct WalkLayout {
    static constexpr uint32_t level_sequences = 0xa0;   // Level.GameSequences
    static constexpr uint32_t streaming_levels = 0x334; // WorldInfo.StreamingLevels
    static constexpr uint32_t loaded_level = 0x44;      // LevelStreaming.LoadedLevel
    static constexpr uint32_t streaming_flags = 0x60;   // bIsVisible 0x80000000
    static constexpr uint32_t sequence_objects = 0xb0;  // Sequence.SequenceObjects
    static constexpr uint32_t originator = 0xb8;        // SequenceEvent.Originator
    static constexpr uint32_t trigger_count = 0xc4;     // SequenceEvent.TriggerCount
    static constexpr uint32_t event_flags = 0xd0;       // bEnabled 0x80000000
};

// Touch events of the persistent level and every visible streamed level, found by
// walking their Kismet sequences (bounded; nested sequences included).
inline std::vector<uint32_t> walk_touch_events(const Memory &m, uint32_t world,
                                               const ClassTest &is_class) {
    std::vector<uint32_t> levels, events, todo;
    if (!world)
        return events;
    if (auto persistent = m.u32(world + 0x28))
        levels.push_back(persistent);
    auto array = m.u32(world + WalkLayout::streaming_levels),
         n = m.u32(world + WalkLayout::streaming_levels + 4);
    for (unsigned i = 0; array && i < n && i < 256; ++i) {
        auto streaming = m.u32(array + i * 4);
        auto level = streaming ? m.u32(streaming + WalkLayout::loaded_level) : 0;
        if (level && (m.u32(streaming + WalkLayout::streaming_flags) & 0x80000000))
            levels.push_back(level);
    }
    for (auto level : levels) {
        auto seqs = m.u32(level + WalkLayout::level_sequences),
             count = m.u32(level + WalkLayout::level_sequences + 4);
        for (unsigned i = 0; seqs && i < count && i < 16; ++i)
            todo.push_back(m.u32(seqs + i * 4));
    }
    std::vector<uint32_t> seen;
    unsigned visited = 0;
    while (!todo.empty() && visited < 8192) {
        auto sequence = todo.back();
        todo.pop_back();
        if (!sequence || std::find(seen.begin(), seen.end(), sequence) != seen.end())
            continue;
        seen.push_back(sequence);
        auto items = m.u32(sequence + WalkLayout::sequence_objects),
             count = m.u32(sequence + WalkLayout::sequence_objects + 4);
        if (count > 4096)
            continue;
        for (unsigned i = 0; items && i < count && visited < 8192; ++i, ++visited) {
            auto object = m.u32(items + i * 4);
            if (!object)
                continue;
            if (is_class(object, "SeqEvent_Touch"))
                events.push_back(object);
            else if (is_class(object, "Sequence"))
                todo.push_back(object);
        }
    }
    return events;
}

// Kismet fields for telling route triggers from repeatable ones, looked up by
// name from the touch event's class (SequenceEvent.MaxTriggerCount; SequenceOp's
// OutputLinks {Links {LinkedOp, InputLinkIdx}} and EventLinks {LinkedEvents}).
// Array element sizes are the structs' PropertiesSize. Zero means not found, and
// every trigger then counts as a route trigger, as before.
struct WalkLinkLayout {
    uint32_t max_trigger_count{};
    uint32_t output_links{}, output_size{}, links{}, link_size{}, linked_op{}, input_index{};
    uint32_t event_links{}, event_size{}, linked_events{};
    bool links_found{}; // Every link field exists (offsets may legitimately be 0).
    bool counts() const {
        return max_trigger_count != 0;
    }
    bool links_known() const {
        return links_found && output_links && output_size >= 12 && output_size <= 256 &&
               link_size >= 8 && link_size <= 64 && linked_op + 4 <= link_size &&
               input_index + 4 <= link_size && links + 12 <= output_size && event_links &&
               event_size >= 12 && event_size <= 256 && linked_events + 12 <= event_size;
    }
};

// A touch trigger and what it does to the route. Most route triggers fire once
// (MaxTriggerCount 1, the default). Triggers that fire any number of times
// (MaxTriggerCount 0) change camera angles, play scenery or NPC reactions or
// stream levels (a survey of every playable package); Episode 12's opening
// street switches such triggers on behind the player as they walk on. One kind
// is still a route step: a repeatable trigger that switches on (SeqAct_Toggle
// input 0 Turn On or 2 Toggle) a one-shot trigger that is off, as Episode 10's
// gate does before the enemies' trigger.
struct WalkTrigger {
    uint32_t event{};
    bool repeatable{};
    std::vector<uint32_t> switches_on; // Touch events this trigger can switch on.
};

inline std::vector<WalkTrigger> walk_triggers(const Memory &m, const std::vector<uint32_t> &events,
                                              const WalkLinkLayout &layout,
                                              const ClassTest &is_class) {
    std::vector<WalkTrigger> out;
    out.reserve(events.size());
    for (auto event : events) {
        WalkTrigger trigger;
        trigger.event = event;
        trigger.repeatable = layout.counts() && m.u32(event + layout.max_trigger_count) == 0;
        if (trigger.repeatable && layout.links_known()) {
            auto outputs = m.u32(event + layout.output_links),
                 outputCount = m.u32(event + layout.output_links + 4);
            for (unsigned o = 0; outputs && o < outputCount && o < 16; ++o) {
                auto output = outputs + o * layout.output_size;
                auto links = m.u32(output + layout.links),
                     linkCount = m.u32(output + layout.links + 4);
                for (unsigned l = 0; links && l < linkCount && l < 32; ++l) {
                    auto link = links + l * layout.link_size;
                    auto op = m.u32(link + layout.linked_op);
                    auto input = m.u32(link + layout.input_index);
                    if (!op || (input != 0 && input != 2) || !is_class(op, "SeqAct_Toggle"))
                        continue;
                    auto targets = m.u32(op + layout.event_links),
                         targetCount = m.u32(op + layout.event_links + 4);
                    for (unsigned t = 0; targets && t < targetCount && t < 8; ++t) {
                        auto entry = targets + t * layout.event_size;
                        auto list = m.u32(entry + layout.linked_events),
                             listCount = m.u32(entry + layout.linked_events + 4);
                        for (unsigned e = 0; list && e < listCount && e < 64; ++e)
                            if (auto target = m.u32(list + e * 4);
                                target &&
                                std::find(events.begin(), events.end(), target) != events.end())
                                trigger.switches_on.push_back(target);
                    }
                }
            }
        }
        out.push_back(std::move(trigger));
    }
    return out;
}

struct WalkSnapshot {
    AimIdentity controller, pawn, target;
    uint64_t tick{};
    bool active{}, valid{}, enemies{};
    bool burst_guide{}; // The target is the game's burst guide target.
    float bearing{};    // Degrees from the camera's facing; positive is to the right.
    float distance{};   // Native world units, horizontal.
    unsigned remaining{};
};

// The nearest enabled touch trigger that has not fired yet, relative to the
// camera's horizontal facing. While a live enemy pawn exists the snapshot is
// marked so guidance can stay quiet during the fight.
//
// A burst guide (BCSeqAct_BurstGuide, Episode 9's last door) takes priority: it
// stores its target on the controller (BCPlayerController.BurstGuideTargetActor,
// at `guide_offset`), and the game shows the R2 prompt only once the target is
// in sight and near; until then sighted players follow an on-screen arrow.
//
// Route triggers come first: one-shot triggers, and repeatable ones that switch on
// a one-shot trigger that is still off and unfired. Other repeatable triggers are
// used only when no route trigger is waiting.
inline WalkSnapshot capture_walk(const Memory &m, uint32_t pc, uint64_t tick,
                                 const std::vector<WalkTrigger> &events, const ClassTest &is_class,
                                 uint32_t guide_offset = 0) {
    WalkSnapshot out;
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
    auto world = m.u32(pc + 0xd4);
    const auto team = m.u8(pawn + 0x468);
    unsigned walked = 0;
    for (auto other = world ? m.u32(world + 0x3d8) : 0; other && walked < 1024;
         other = m.u32(other + 0x1e8), ++walked)
        if (other != pawn && int32_t(m.u32(other + 0x2d8)) > 0 && is_class(other, "BCPawn") &&
            m.u8(other + 0x468) != team && !(m.u32(other + 0xac) & 0x50000000)) {
            out.enemies = true;
            break;
        }
    const float facing = std::atan2(camera.forward.y, camera.forward.x);
    auto aim = [&](const LockVector &position, AimIdentity target) {
        auto dx = position.x - at.x, dy = position.y - at.y;
        auto bearing = (std::atan2(dy, dx) - facing) * 57.2957795f;
        while (bearing > 180)
            bearing -= 360;
        while (bearing < -180)
            bearing += 360;
        // Unreal's world is left-handed: positive yaw turns to the right.
        out.bearing = bearing;
        out.distance = std::hypot(dx, dy);
        out.target = target;
    };
    if (auto guide = guide_offset ? m.u32(pc + guide_offset) : 0;
        guide && m.u32(guide + 0x20) && !(m.u32(guide + 0xac) & 0x10000000)) {
        LockVector position;
        if (lock_vector(m, guide + 0x54, position) &&
            std::isfinite(std::hypot(position.x - at.x, position.y - at.y))) {
            aim(position, {guide, m.u32(guide + 0x20)});
            out.burst_guide = true;
            out.remaining = 1;
            out.valid = true;
            return out;
        }
    }
    auto enabled = [&](uint32_t event) {
        return (m.u32(event + WalkLayout::event_flags) & 0x80000000) != 0;
    };
    auto fired = [&](uint32_t event) { return m.u32(event + WalkLayout::trigger_count) != 0; };
    auto route = [&](const WalkTrigger &trigger) {
        if (!trigger.repeatable)
            return true;
        for (auto target : trigger.switches_on) {
            auto it = std::find_if(events.begin(), events.end(),
                                   [&](const WalkTrigger &t) { return t.event == target; });
            if (it != events.end() && !it->repeatable && m.u32(target + 0x20) && !enabled(target) &&
                !fired(target))
                return true;
        }
        return false;
    };
    // Best route trigger [0] and best other trigger [1].
    float best[2] = {std::numeric_limits<float>::infinity(),
                     std::numeric_limits<float>::infinity()};
    std::optional<std::pair<LockVector, AimIdentity>> chosen[2];
    for (const auto &item : events) {
        auto event = item.event;
        auto trigger = m.u32(event + WalkLayout::originator);
        if (!trigger || !m.u32(event + 0x20) || fired(event) || !enabled(event) ||
            !m.u32(trigger + 0x20) || (m.u32(trigger + 0xac) & 0x10000000))
            continue;
        LockVector position;
        if (!lock_vector(m, trigger + 0x54, position))
            continue;
        auto dx = position.x - at.x, dy = position.y - at.y;
        auto distance = std::hypot(dx, dy);
        if (!std::isfinite(distance))
            continue;
        ++out.remaining;
        auto kind = route(item) ? 0 : 1;
        if (distance < best[kind]) {
            best[kind] = distance;
            chosen[kind] = {position, {event, m.u32(event + 0x20)}};
        }
    }
    if (auto &pick = chosen[0] ? chosen[0] : chosen[1])
        aim(pick->first, pick->second);
    out.valid = true;
    return out;
}

// Steady pitch: 525 Hz ahead, 440 Hz when the trigger is behind (beyond 90
// degrees). Stereo shows the side; pulses quicken with proximity, turning
// continuous within about 270 units.
inline constexpr float kWalkBeaconRange = 3000.f;
inline BeaconCue walk_cue(const WalkSnapshot &walk) {
    BeaconCue cue;
    cue.tick = walk.tick;
    if (!walk.active || !walk.valid || walk.enemies || !walk.target.pointer)
        return cue;
    cue.active = true;
    cue.pan = std::clamp(walk.bearing / 90.f, -1.f, 1.f);
    // BeaconSynth pitch is 525 * 2^(elevation * 0.65): 0 gives 525 Hz, this 440 Hz.
    static const float behind = std::log2(440.f / 525.f) / .65f;
    cue.elevation = std::abs(walk.bearing) > 90 ? behind : 0.f;
    cue.error = std::clamp(walk.distance / kWalkBeaconRange * 45.f, 0.f, 45.f);
    cue.generation = (uint64_t(walk.target.pointer) << 8) ^ walk.target.index;
    return cue;
}

template <class T> class LatestMailbox {
  public:
    void put(const T &value) {
        std::lock_guard lock(mutex_);
        latest_ = value;
    }
    std::optional<T> take() {
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
    std::optional<T> latest_;
};
}
