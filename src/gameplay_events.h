#pragma once
#include "qte_events.h"
#include <mutex>
#include <optional>

namespace a11y {
struct StatusContext {
    uint32_t input{}, input_index{}, controller{}, controller_index{}, gauge{}, gauge_index{};
    bool operator==(const StatusContext &) const = default;
};
// Mirror the Back handler's pawn/controller relationship, then require an
// unpaused, noncinematic game with an open gauge and no active menu scene.
// The game keeps updating gauge values even when its display is hidden.
// Called only on a button press and before delivery, never on a timer.
inline std::optional<StatusContext> capture_status_context(const Memory &m, uint32_t input) {
    auto word = [&](uint32_t object, uint32_t offset, uint32_t &out) {
        std::array<uint8_t, 4> bytes{};
        if (!object || uint64_t(object) + offset + 4 > 0x100000000ull ||
            !m.copy(object + offset, bytes.data(), 4))
            return false;
        out = cue_word(bytes.data());
        return true;
    };
    StatusContext value;
    value.input = input;
    uint32_t outer{}, pawn{}, flags{}, world{}, paused{}, hud{}, manager{}, scene{};
    if (!word(input, 0x20, value.input_index) || !value.input_index || !word(input, 0x28, outer) ||
        !word(outer, 0x5c4, pawn) || !word(pawn, 0x1e4, value.controller) ||
        !word(value.controller, 0x20, value.controller_index) || !value.controller_index ||
        !word(value.controller, 0x378, flags) || (flags & 0x8000) ||
        !word(value.controller, 0xd4, world) || !word(world, 0x368, paused) || paused ||
        !word(value.controller, 0x3c8, hud) || !word(hud, 0x44c, flags) || (flags & 0x10000000) ||
        !word(hud, 0x478, manager))
        return {};
    if (manager && (!word(manager, 0x98, scene) || scene))
        return {};
    if (!word(hud, 0x488, value.gauge) || !word(value.gauge, 0x20, value.gauge_index) ||
        !value.gauge_index || !word(value.gauge, 0xc8, flags) || !(flags & 0x80000000))
        return {};
    return value;
}
struct TimingState {
    uint32_t source{}, index{}, track{};
    CueName name;
    uint64_t generation{};
    bool active{}, window{}, mash{};
    bool operator==(const TimingState &) const = default;
};
inline bool capture_timing(const Memory &m, uint32_t source, int32_t track, bool window,
                           TimingState &state) {
    state = {};
    state.source = source;
    state.index = m.u32(source + 0x20);
    state.track = uint32_t(track);
    state.name = {m.u32(source + 0x2c), m.u32(source + 0x30)};
    std::array<uint8_t, 12> array{};
    if (!source || !state.index || track < 0 || !m.copy(source + 0x8c, array.data(), array.size()))
        return false;
    auto rows = cue_word(array.data()), count = cue_word(array.data() + 4),
         capacity = cue_word(array.data() + 8);
    if (!rows || count > 256 || uint32_t(track) >= count || count > capacity || capacity > 4096)
        return false;
    std::array<uint8_t, 24> row{};
    if (!m.copy(rows + uint32_t(track) * 24, row.data(), row.size()) || row[0] > 1)
        return false;
    auto flags = cue_word(row.data() + 20);
    state.active = (flags & 0xc0000000) == 0x40000000;
    if (track > 0)
        state.active = state.active && (m.u32(rows + uint32_t(track - 1) * 24 + 20) & 0x80000000);
    state.mash = row[0] == 1;
    state.window = state.active && window;
    return true;
}
struct GameplayEvent {
    bool timing{}, status_request{}, results_repeat{};
    uint32_t repeat_movie{}, repeat_index{};
    QteGroup group;
    TimingState sync;
    StatusContext status;
    uint64_t tick{}, order{};
};
class GameplayQueue {
  public:
    void push(const GameplayEvent &event) {
        std::lock_guard lock(mutex_);
        if (count_ == events_.size()) {
            overflow_ = true;
            return;
        }
        events_[count_++] = event;
    }
    std::vector<GameplayEvent> take(bool &overflow) {
        std::lock_guard lock(mutex_);
        std::vector<GameplayEvent> out(events_.begin(), events_.begin() + count_);
        count_ = 0;
        overflow = overflow_;
        overflow_ = false;
        return out;
    }
    void clear() {
        std::lock_guard lock(mutex_);
        count_ = 0;
        overflow_ = false;
    }
    void mark_overflow() {
        std::lock_guard lock(mutex_);
        overflow_ = true;
    }

  private:
    std::mutex mutex_;
    std::array<GameplayEvent, 64> events_{};
    size_t count_{};
    bool overflow_{};
};
}
