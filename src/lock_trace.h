#pragma once
#include "qte_events.h"
#include <mutex>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <filesystem>
#include <limits>

namespace a11y {
struct LockVector {
    float x{}, y{}, z{};
};
inline bool lock_vector(const Memory &m, uint32_t at, LockVector &out) {
    uint8_t data[12]{};
    if (!m.copy(at, data, 12))
        return false;
    out = {std::bit_cast<float>(cue_word(data)), std::bit_cast<float>(cue_word(data + 4)),
           std::bit_cast<float>(cue_word(data + 8))};
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}
inline float lock_length(LockVector v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
inline std::optional<float> lock_angle(LockVector a, LockVector b) {
    auto length = lock_length(a) * lock_length(b);
    if (!std::isfinite(length) || length < .000001f)
        return {};
    auto dot = (a.x * b.x + a.y * b.y + a.z * b.z) / length;
    if (!std::isfinite(dot))
        return {};
    return std::acos(std::clamp(dot, -1.f, 1.f)) * 57.29577951308232f;
}
struct LockCamera {
    LockVector location, forward;
    float fov{};
    bool valid{};
};
inline LockCamera lock_camera(const Memory &m, uint32_t pc) {
    LockCamera result;
    auto camera = m.u32(pc + 0x370);
    uint8_t data[28]{};
    if (!camera || !m.copy(camera + 0x30c, data, 28))
        return result;
    result.location = {std::bit_cast<float>(cue_word(data)),
                       std::bit_cast<float>(cue_word(data + 4)),
                       std::bit_cast<float>(cue_word(data + 8))};
    auto pitch = float(cue_word(data + 12) & 65535) * .00009587379924285257f,
         yaw = float(cue_word(data + 16) & 65535) * .00009587379924285257f;
    result.forward = {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw),
                      std::sin(pitch)};
    result.fov = std::bit_cast<float>(cue_word(data + 24));
    result.valid = std::isfinite(lock_length(result.location)) && std::isfinite(result.fov) &&
                   result.fov > 0 && result.fov < 180;
    return result;
}
inline std::optional<float> stable_lock_radius(const LockCamera &camera, LockVector origin,
                                               LockVector aim) {
    LockVector delta{origin.x - camera.location.x, origin.y - camera.location.y,
                     origin.z - camera.location.z};
    if (!camera.valid || !std::isfinite(lock_length(delta)) || lock_length(delta) > .001f)
        return {};
    auto offset = lock_angle(aim, camera.forward);
    if (!offset)
        return {};
    return std::max(0.f, (camera.fov + 5.f) / 2.f - *offset - .5f);
}
// PrimitiveComponent.LocalToWorld (reflected +0x90) is a row-major FMatrix and the
// component's world origin is its fourth row. Treated as a hypothesis: callers
// must confirm it against the native angle before relying on it.
inline bool lock_target_position(const Memory &m, uint32_t component, LockVector &out) {
    return component && component < UINT32_MAX - 0x100 &&
           lock_vector(m, component + 0x90 + 48, out);
}
// Angle between a direction and the vector from origin to a world position.
inline std::optional<float> lock_direction_angle(LockVector origin, LockVector direction,
                                                 LockVector target) {
    return lock_angle(direction,
                      LockVector{target.x - origin.x, target.y - origin.y, target.z - origin.z});
}
// The native retention predicate (CheckRemoveLockOnTarget) rejects a target when
// its angle from camera forward exceeds (FOV + 5) / 2 degrees. This is the
// remaining margin; negative means the game would drop the lock on its next check.
inline std::optional<float> lock_view_margin(const LockCamera &camera, LockVector target) {
    if (!camera.valid)
        return {};
    auto angle = lock_direction_angle(camera.location, camera.forward, target);
    if (!angle)
        return {};
    return (camera.fov + 5.f) / 2.f - *angle;
}
// CheckLockOnShotTarget writes JustLockon through an out parameter (token 0x48),
// so its own frame slot never changes. A completed lock is instead the verified
// script transition of BCLockOnShotTargetInfo.LockOnCnt from below one to at
// least one. Read the controller's LockOnShotTargetList (+0x7e4, 12-byte
// {Target, LockOnCnt, LockOnTimeToGo} elements) before the call; FindStruct
// uses the first matching target, and an absent target starts at zero.
inline int32_t lock_shot_count(const Memory &m, uint32_t pc, uint32_t target) {
    auto data = m.u32(pc + 0x7e4), count = m.u32(pc + 0x7e8);
    if (!target || !data || count > 64)
        return 0;
    for (uint32_t i = 0; i < count; ++i)
        if (m.u32(data + i * 12) == target)
            return int32_t(m.u32(data + i * 12 + 4));
    return 0;
}
enum class LockTraceKind {
    frame,
    acquire,
    remove,
    marker_add,
    marker_remove,
    sound,
    level_change,
    guide
};
// Shooting guidance summary: where unlocked candidates are on screen, which of
// them guidance may use, and what tracking chose.
struct GuideInfo {
    uint32_t candidates{}, unlocked{}, unreachable{}, left{}, centre{}, right{}, above{}, below{};
    uint32_t tracking_target{}, tracking_target_index{};
    bool tracking_hold{}, tracking_focus{};
    float tracking_yaw{std::numeric_limits<float>::quiet_NaN()},
        tracking_pitch{std::numeric_limits<float>::quiet_NaN()};
};
struct LockTraceRecord {
    LockTraceKind kind{};
    uint64_t sequence{}, tick{}, frame{};
    uint32_t controller{}, controller_index{}, target{}, target_index{}, sound{};
    CueName sound_name;
    int32_t level_before{-1}, level_after{-1}, level_target{-1}, level_max{-1};
    bool result{}, accepted{}, angular{}, angle_valid{}, camera_valid{}, geometry_supported{},
        cap_applied{}, view_gated{}, position_verified{};
    float target_angle{std::numeric_limits<float>::quiet_NaN()},
        view_margin{std::numeric_limits<float>::quiet_NaN()};
    float native_min{}, native_max{}, effective_min{}, effective_max{}, fov{}, angle{}, limit{},
        aim_offset{};
    LockVector camera_location, camera_forward, aim;
    GuideInfo guide;
};
struct LockTraceBatch {
    std::vector<LockTraceRecord> records;
    uint64_t started{}, attempted{}, dropped{}, filtered{}, active{};
    bool done{};
    const char *reason = "waiting";
};
// A separate bounded diagnostic queue; overflow never touches narration state.
class LockTrace {
  public:
    void configure(bool enabled) {
        std::lock_guard lock(mutex_);
        enabled_ = enabled;
        started_ = attempted_ = dropped_ = sequence_ = 0;
        filtered_ = active_ = last_active_ = 0;
        samples_ = {};
        last_acquire_sample_ = 0;
        count_ = 0;
        done_ = false;
        reason_ = enabled ? "waiting" : "disabled";
    }
    bool armed() const {
        return enabled_.load(std::memory_order_relaxed);
    }
    bool begin(uint64_t now, bool startAllowed = true) {
        now = std::max<uint64_t>(now, 1);
        std::lock_guard lock(mutex_);
        if (!enabled_ || done_)
            return false;
        if (!started_ && !startAllowed)
            return false;
        if (!started_)
            started_ = now;
        // Callers begin only from active, unpaused shooting ticks. Count time
        // between consecutive ticks; a pause or hitch longer than the gap limit
        // does not spend the capture window.
        if (last_active_ && now >= last_active_ && now - last_active_ <= max_tick_gap_)
            active_ += now - last_active_;
        last_active_ = now;
        return check(now);
    }
    void push(LockTraceRecord record, uint64_t now) {
        now = std::max<uint64_t>(now, 1);
        std::lock_guard lock(mutex_);
        if (!enabled_ || !started_ || done_ || !check(now))
            return;
        const bool acquireCheck =
            record.kind == LockTraceKind::acquire && !(record.accepted && record.result);
        const bool retainCheck = record.kind == LockTraceKind::remove && !record.result;
        if (acquireCheck || retainCheck) {
            if (acquireCheck) {
                // One rejected candidate per second globally: a large candidate
                // set must not defeat a bounded identity cache by cycling it.
                if (last_acquire_sample_ && now - last_acquire_sample_ < 1000) {
                    ++filtered_;
                    return;
                }
                last_acquire_sample_ = now;
            }
            // Keep one diagnostic sample per identity and kind each second.
            // Real acquisitions, removals and sounds are always recorded.
            Sample *slot = &samples_[0];
            for (auto &sample : samples_) {
                if (sample.valid && sample.controller == record.controller &&
                    sample.controller_index == record.controller_index &&
                    sample.target == record.target && sample.target_index == record.target_index &&
                    sample.kind == record.kind) {
                    if (now - sample.tick < 1000) {
                        ++filtered_;
                        return;
                    }
                    slot = &sample;
                    break;
                }
                if (!sample.valid || sample.tick < slot->tick)
                    slot = &sample;
            }
            *slot = {record.controller,
                     record.controller_index,
                     record.target,
                     record.target_index,
                     record.kind,
                     now,
                     true};
        }
        ++attempted_;
        record.sequence = ++sequence_;
        record.tick = now - started_;
        if (count_ == records_.size())
            ++dropped_;
        else
            records_[count_++] = record;
        if (attempted_ >= 8192) {
            done_ = true;
            reason_ = "record_limit";
            enabled_ = false;
        }
    }
    bool pending() {
        std::lock_guard lock(mutex_);
        return count_ || done_;
    }
    void stop() {
        std::lock_guard lock(mutex_);
        if (started_ && !done_) {
            done_ = true;
            reason_ = "stopped";
        }
        enabled_ = false;
    }
    LockTraceBatch take() {
        std::lock_guard lock(mutex_);
        LockTraceBatch batch;
        batch.records.assign(records_.begin(), records_.begin() + count_);
        count_ = 0;
        batch.started = started_;
        batch.attempted = attempted_;
        batch.dropped = dropped_;
        batch.filtered = filtered_;
        batch.active = active_;
        batch.done = done_;
        batch.reason = reason_;
        return batch;
    }

  private:
    bool check(uint64_t) {
        if (active_ >= 30000) {
            done_ = true;
            reason_ = "time_limit";
            enabled_ = false;
            return false;
        }
        return true;
    }
    std::atomic<bool> enabled_{};
    std::mutex mutex_;
    std::array<LockTraceRecord, 512> records_{};
    size_t count_{};
    struct Sample {
        uint32_t controller{}, controller_index{}, target{}, target_index{};
        LockTraceKind kind{};
        uint64_t tick{};
        bool valid{};
    };
    std::array<Sample, 64> samples_{};
    uint64_t started_{}, attempted_{}, dropped_{}, sequence_{}, filtered_{};
    uint64_t last_acquire_sample_{}, active_{}, last_active_{};
    static constexpr uint64_t max_tick_gap_ = 250;
    bool done_{};
    const char *reason_ = "disabled";
};
inline std::string lock_trace_line(const LockTraceRecord &r, std::string sound) {
    constexpr const char *names[]{"frame",         "acquire", "remove",       "marker_add",
                                  "marker_remove", "sound",   "level_change", "guide"};
    // Cue names are metadata identifiers. Encode unusual characters without
    // permitting a cue name to inject JSON or extra log lines.
    std::string escaped;
    for (unsigned char ch : sound) {
        if (ch == '"' || ch == '\\') {
            escaped += '\\';
            escaped += char(ch);
        } else if (ch >= 32 && ch < 127)
            escaped += char(ch);
        else
            escaped += '?';
    }
    std::ostringstream out;
    out << std::setprecision(7) << "{\"event\":\"" << names[unsigned(r.kind)]
        << "\",\"seq\":" << r.sequence << ",\"ms\":" << r.tick << ",\"frame\":" << r.frame
        << ",\"pc\":" << r.controller << ",\"pc_index\":" << r.controller_index
        << ",\"target\":" << r.target << ",\"target_index\":" << r.target_index
        << ",\"result\":" << r.result << ",\"angular_reached\":" << r.angular
        << ",\"angle_valid\":" << r.angle_valid << ",\"camera_valid\":" << r.camera_valid;
    out << ",\"accepted\":" << r.accepted << ",\"geometry_supported\":" << r.geometry_supported
        << ",\"cap_applied\":" << r.cap_applied << ",\"view_gated\":" << r.view_gated
        << ",\"position_verified\":" << r.position_verified;
    out << ",\"level_before\":" << r.level_before << ",\"level_after\":" << r.level_after
        << ",\"level_target\":" << r.level_target << ",\"level_max\":" << r.level_max;
    auto number = [&](const char *key, float value) {
        out << ",\"" << key << "\":";
        if (std::isfinite(value))
            out << value;
        else
            out << "null";
    };
    number("native_min", r.native_min);
    number("native_max", r.native_max);
    number("effective_min", r.effective_min);
    number("effective_max", r.effective_max);
    number("fov", r.fov);
    number("angle", r.angle);
    number("limit", r.limit);
    number("aim_offset", r.aim_offset);
    number("target_angle", r.target_angle);
    number("view_margin", r.view_margin);
    number("camera_x", r.camera_location.x);
    number("camera_y", r.camera_location.y);
    number("camera_z", r.camera_location.z);
    number("forward_x", r.camera_forward.x);
    number("forward_y", r.camera_forward.y);
    number("forward_z", r.camera_forward.z);
    number("aim_x", r.aim.x);
    number("aim_y", r.aim.y);
    number("aim_z", r.aim.z);
    if (r.kind == LockTraceKind::guide) {
        const auto &g = r.guide;
        out << ",\"candidates\":" << g.candidates << ",\"unlocked\":" << g.unlocked
            << ",\"unreachable\":" << g.unreachable << ",\"left\":" << g.left
            << ",\"centre\":" << g.centre << ",\"right\":" << g.right << ",\"above\":" << g.above
            << ",\"below\":" << g.below << ",\"tracking_hold\":" << g.tracking_hold
            << ",\"tracking_target\":" << g.tracking_target
            << ",\"tracking_target_index\":" << g.tracking_target_index
            << ",\"tracking_focus\":" << g.tracking_focus;
        number("tracking_yaw", g.tracking_yaw);
        number("tracking_pitch", g.tracking_pitch);
    }
    out << ",\"sound\":" << r.sound << ",\"cue\":\"" << escaped << "\"}";
    return out.str();
}
class LockTraceWriter {
  public:
    explicit LockTraceWriter(const std::filesystem::path &path, std::string_view profile = "",
                             int degrees = 0, bool stable = false, uint32_t pid = 0)
        : stream_(path, std::ios::trunc | std::ios::binary) {
        if (!profile.empty()) {
            std::ostringstream out;
            out << "{\"event\":\"session\",\"schema\":1,\"profile\":\"" << profile
                << "\",\"shooting_degrees\":" << degrees << ",\"stable_cone\":" << stable
                << ",\"pid\":" << pid << "}";
            auto line = out.str();
            stream_ << line << '\n';
            bytes_ = line.size() + 1;
        }
    }
    bool valid() const {
        return bool(stream_);
    }
    bool finished() const {
        return ended_;
    }
    template <class Name> void drain(LockTraceBatch batch, Name name) {
        if (ended_ || !stream_)
            return;
        for (const auto &record : batch.records) {
            auto line = lock_trace_line(record, name(record.sound_name));
            if (bytes_ + line.size() + 1 > limit_ - 512) {
                end("byte_limit", batch);
                return;
            }
            stream_ << line << '\n';
            bytes_ += line.size() + 1;
        }
        if (batch.done)
            end(batch.reason, batch);
        stream_.flush();
    }

  private:
    void end(const char *reason, const LockTraceBatch &batch) {
        if (ended_)
            return;
        stream_ << "{\"event\":\"end\",\"reason\":\"" << reason
                << "\",\"attempted\":" << batch.attempted << ",\"dropped\":" << batch.dropped
                << ",\"filtered_checks\":" << batch.filtered << ",\"active_ms\":" << batch.active
                << "}\n";
        ended_ = true;
        stream_.flush();
    }
    static constexpr size_t limit_ = 2 * 1024 * 1024;
    std::ofstream stream_;
    size_t bytes_{};
    bool ended_{};
};
}
