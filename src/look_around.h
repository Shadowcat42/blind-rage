#pragma once
// Look-around beacon: scenes where the right stick turns a fixed camera
// (BCCameraActorViewCheck, such as Episode 10's hot spring) and the game reacts to
// what is in view. BCCameraActorViewCheck.Tick counts a group of targets
// (ViewCheckTargetList[i].Targets) as viewed once the camera's centre line stays
// within FOV / 2 * CheckAngleRate of one of them for StartTriggerTime seconds,
// while bEnableViewCheck is set and the camera is the player's view target.
// BCCameraActor.ViewModifyRotation turns the view with the right stick within
// set limits and eases back when it is released. See docs/LOOK_AROUND.md.
#include "lock_trace.h"
#include "melee_aim.h"
#include <set>
#include <string>
#include <vector>
namespace a11y {
// Offsets resolved by name at runtime (0 when absent).
struct LookFields {
    uint32_t view_target{};                  // Camera.ViewTarget (Target is its first member)
    uint32_t enable_offset{}, enable_mask{}; // BCCameraActorViewCheck.bEnableViewCheck
    uint32_t angle_rate{};                   // CheckAngleRate
    uint32_t list{};                         // ViewCheckTargetList
    // BCViewCheckTargetParams members and element size.
    uint32_t group_name{}, targets{}, trigger_time{}, element_size{};
    bool valid() const {
        return view_target && enable_offset && enable_mask && angle_rate && list && element_size &&
               element_size <= 256;
    }
};
struct LookSnapshot {
    uint64_t tick{};
    bool active{};    // A look-around camera is checking views.
    bool target{};    // A group is left to find.
    bool inside{};    // The view is on that group's nearest target.
    int group = -1;   // Index in the camera's list.
    unsigned found{}; // Groups completed so far with this camera.
    unsigned remaining{};
    float yaw{}, pitch{}; // Degrees from the view centre: right and up are positive.
    float angle{}, cone{};
};
// Reads a guest FString (big-endian UTF-16) for the reset check.
inline std::u16string guest_fstring(const Memory &m, uint32_t at) {
    std::u16string out;
    auto data = m.u32(at), count = m.u32(at + 4);
    if (!data || !count || count > 256)
        return out;
    for (uint32_t i = 0; i + 1 < count; ++i)
        out.push_back(char16_t(m.u32(data + i * 2) >> 16));
    return out;
}
// "リセット" (reset): a group the level uses to clear its own counters, not
// something to find.
inline bool reset_group(const std::u16string &name) {
    return name.find(u"リセット") != std::u16string::npos;
}
class LookTracker {
  public:
    void reset() {
        camera_ = 0;
        done_.clear();
        dwell_ = 0;
        dwell_group_ = -1;
        last_ = 0;
    }
    LookSnapshot update(const Memory &m, uint32_t pc, const LookFields &fields,
                        const ClassTest &is_class, uint64_t now) {
        LookSnapshot out;
        out.tick = now;
        auto cameraManager = m.u32(pc + 0x370);
        auto actor =
            cameraManager && fields.valid() ? m.u32(cameraManager + fields.view_target) : 0;
        if (!actor || !is_class(actor, "BCCameraActorViewCheck") ||
            !(m.u32(actor + fields.enable_offset) & fields.enable_mask)) {
            last_ = 0;
            return out;
        }
        if (actor != camera_) {
            reset();
            camera_ = actor;
        }
        auto view = lock_camera(m, pc);
        auto rate = std::bit_cast<float>(m.u32(actor + fields.angle_rate));
        if (!view.valid || !std::isfinite(rate) || rate <= 0)
            return out;
        out.active = true;
        out.cone = view.fov / 2 * rate;
        const auto dt = last_ && now > last_ ? float(now - last_) / 1000.f : 0.f;
        last_ = now;
        // The view's own axes, for the offsets heard as stereo and pitch.
        LockVector forward = view.forward;
        LockVector right{-forward.y, forward.x, 0};
        auto rightLength = lock_length(right);
        if (!(rightLength > 1e-4f))
            return out;
        right = {right.x / rightLength, right.y / rightLength, 0};
        auto array = m.u32(actor + fields.list), count = m.u32(actor + fields.list + 4);
        if (count > 64)
            return out;
        float best = 1e9f;
        for (uint32_t i = 0; i < count && array; ++i) {
            auto entry = array + i * fields.element_size;
            if (done_.count(int(i)) || reset_group(guest_fstring(m, entry + fields.group_name)))
                continue;
            ++out.remaining;
            auto targets = m.u32(entry + fields.targets),
                 targetCount = m.u32(entry + fields.targets + 4);
            for (uint32_t t = 0; t < targetCount && t < 32 && targets; ++t) {
                LockVector at;
                auto target = m.u32(targets + t * 4);
                if (!target || !lock_vector(m, target + 0x54, at))
                    continue;
                LockVector line{at.x - view.location.x, at.y - view.location.y,
                                at.z - view.location.z};
                auto angle = lock_angle(line, forward);
                if (!angle || *angle >= best)
                    continue;
                best = *angle;
                auto length = lock_length(line);
                auto side = (line.x * right.x + line.y * right.y) / length;
                auto ahead =
                    (line.x * forward.x + line.y * forward.y + line.z * forward.z) / length;
                out.group = int(i);
                out.angle = *angle;
                out.yaw = std::atan2(side, ahead) * 57.2957795f;
                out.pitch = (std::asin(std::clamp(line.z / length, -1.f, 1.f)) -
                             std::asin(std::clamp(forward.z, -1.f, 1.f))) *
                            57.2957795f;
            }
        }
        out.target = out.group >= 0;
        out.inside = out.target && out.angle < out.cone;
        // A group counts once the view has held on it a little longer than the
        // game needs, then guidance moves on to the next one.
        if (out.inside && out.group == dwell_group_)
            dwell_ += dt;
        else {
            dwell_group_ = out.inside ? out.group : -1;
            dwell_ = 0;
        }
        if (out.inside) {
            auto need = std::bit_cast<float>(
                m.u32(array + uint32_t(out.group) * fields.element_size + fields.trigger_time));
            if (!std::isfinite(need) || need < 0 || need > 30)
                need = 1;
            if (dwell_ >= need + .2f) {
                done_.insert(out.group);
                dwell_ = 0;
                dwell_group_ = -1;
            }
        }
        out.found = unsigned(done_.size());
        return out;
    }

  private:
    uint32_t camera_{};
    std::set<int> done_;
    float dwell_{};
    int dwell_group_ = -1;
    uint64_t last_{};
};
// Like the shooting beacon: stereo for left and right, pitch for up and down,
// pulses quickening as the view closes in, steady inside the game's cone.
inline BeaconCue look_cue(const LookSnapshot &look) {
    BeaconCue cue;
    cue.tick = look.tick;
    if (!look.active || !look.target)
        return cue;
    cue.active = true;
    cue.pan = std::clamp(look.yaw / 30.f, -1.f, 1.f);
    cue.elevation = std::clamp(look.pitch / 20.f, -1.f, 1.f);
    // BeaconSynth is continuous at 4 or less: steady from the cone's edge.
    cue.error = look.cone > 0 ? std::clamp(look.angle / look.cone * 4.f, 0.f, 45.f) : 45.f;
    cue.generation = uint64_t(look.group + 1);
    return cue;
}
} // namespace a11y
