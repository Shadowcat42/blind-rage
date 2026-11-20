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
enum class LockTraceKind { frame, acquire, remove, marker_add, marker_remove, sound };
struct LockTraceRecord {
    LockTraceKind kind{};
    uint64_t sequence{}, tick{}, frame{};
    uint32_t controller{}, controller_index{}, target{}, target_index{}, sound{};
    CueName sound_name;
    bool result{}, accepted{}, angular{}, angle_valid{}, camera_valid{}, geometry_supported{},
        cap_applied{};
    float native_min{}, native_max{}, effective_min{}, effective_max{}, fov{}, angle{}, limit{},
        aim_offset{};
    LockVector camera_location, camera_forward, aim;
};
struct LockTraceBatch {
    std::vector<LockTraceRecord> records;
    uint64_t started{}, attempted{}, dropped{}, filtered{};
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
        filtered_ = 0;
        samples_ = {};
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
            if (acquireCheck && !record.cap_applied) {
                ++filtered_;
                return;
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
        batch.done = done_;
        batch.reason = reason_;
        return batch;
    }

  private:
    bool check(uint64_t now) {
        if (now - started_ >= 30000) {
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
    bool done_{};
    const char *reason_ = "disabled";
};
inline std::string lock_trace_line(const LockTraceRecord &r, std::string sound) {
    constexpr const char *names[]{"frame",      "acquire",       "remove",
                                  "marker_add", "marker_remove", "sound"};
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
        << ",\"cap_applied\":" << r.cap_applied;
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
    number("camera_x", r.camera_location.x);
    number("camera_y", r.camera_location.y);
    number("camera_z", r.camera_location.z);
    number("forward_x", r.camera_forward.x);
    number("forward_y", r.camera_forward.y);
    number("forward_z", r.camera_forward.z);
    number("aim_x", r.aim.x);
    number("aim_y", r.aim.y);
    number("aim_z", r.aim.z);
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
                << ",\"filtered_checks\":" << batch.filtered << "}\n";
        ended_ = true;
        stream_.flush();
    }
    static constexpr size_t limit_ = 2 * 1024 * 1024;
    std::ofstream stream_;
    size_t bytes_{};
    bool ended_{};
};
}
