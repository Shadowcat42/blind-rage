#pragma once
#include "build_profile.h"
#include "gameplay_events.h"
#include "native_hooks.h"
#include "presentation_events.h"
#include "shooting_assist.h"
#include "lock_trace.h"
#include <atomic>
#include <windows.h>

namespace a11y {
class GameplayHooks {
  public:
    using Function = void (*)(void *, uint8_t *);
    static constexpr std::array<const char *, 23> function_names{"RequestInputJudge",
                                                                 "RequestButtonIcon",
                                                                 "UpdateButtonIcon",
                                                                 "CheckActivate",
                                                                 "Update",
                                                                 "Deactivated",
                                                                 "EndJudge",
                                                                 "DestroyRequest",
                                                                 "CheckLockOnShotTarget",
                                                                 "StartSynchronicTime",
                                                                 "UpdateNormal",
                                                                 "UpdateBarrage",
                                                                 "EndSynchronicTime",
                                                                 "InitTracks",
                                                                 "HandleButtonInput_Back",
                                                                 "FilterButtonInput",
                                                                 "PlayerTick",
                                                                 "CheckRemoveLockOnTarget",
                                                                 "GetCameraViewPoint",
                                                                 "ShowLockonMarker",
                                                                 "CallShowLockonMarker",
                                                                 "PlayGFxUISound",
                                                                 "SearchLockOnShotTarget"};
    static GameplayHooks &instance() {
        static auto *value = new GameplayHooks;
        return *value;
    }
    bool install(uintptr_t executable, const BuildProfile &profile) {
        if (installed_)
            return profile_ == &profile;
        profile_ = &profile;
        names_global_ = profile.layout.names_global;
        return install_targets({reinterpret_cast<void *>(executable + profile.gameplay_rvas[0]),
                                reinterpret_cast<void *>(executable + profile.gameplay_rvas[1])});
    }
    bool install_targets(const std::array<void *, 2> &targets) {
        if (installed_)
            return true;
        if (!event_ || !hooks_.initialize())
            return false;
        void *callbacks[]{reinterpret_cast<void *>(script_call),
                          reinterpret_cast<void *>(input_window)};
        for (unsigned i = 0; i < 2; ++i)
            if (!hooks_.add(targets[i], callbacks[i], reinterpret_cast<void **>(&original_[i]))) {
                hooks_.clear();
                return false;
            }
        if (!hooks_.enable()) {
            hooks_.clear();
            return false;
        }
        installed_ = true;
        return true;
    }
    bool configure_names(const Memory &memory, Reader &reader) {
        if (configured_)
            return true;
        if (!reader.ready(memory))
            return false;
        std::array<uint32_t, function_names.size()> indices{};
        indices.fill(UINT32_MAX);
        auto count = memory.u32(names_global_ + 4);
        if (count > 250000)
            return false;
        for (uint32_t i = 0; i < count; ++i) {
            auto name = reader.name(memory, i);
            for (size_t j = 0; j < function_names.size(); ++j)
                if (name == function_names[j])
                    indices[j] = i;
        }
        if (std::any_of(indices.begin(), indices.end(),
                        [](uint32_t id) { return id == UINT32_MAX; }))
            return false;
        configure_indices(indices);
        return true;
    }
    void configure_indices(const std::array<uint32_t, function_names.size()> &indices) {
        configured_ = false;
        for (size_t i = 0; i < indices.size(); ++i)
            indices_[i] = indices[i];
        configured_ = true;
    }
    void options(bool qtes, float shootingDegrees, bool statusButton = true) {
        qtes_ = qtes;
        angle_ = shootingDegrees;
        status_button_ = statusButton;
    }
    void lock_diagnostics(bool enabled, bool stableCone = false) {
        trace_.configure(enabled);
        stable_cone_ = stableCone;
    }
    LockTraceBatch take_lock_trace() {
        return trace_.take();
    }
    void stop_lock_trace() {
        trace_.stop();
    }
    void capture(bool enabled) {
        enabled_ = false;
        ++epoch_;
        queue_.clear();
        if (!enabled)
            trace_.stop();
        enabled_ = enabled;
    }
    HANDLE event() const {
        return event_;
    }
    std::vector<GameplayEvent> take(bool &overflow) {
        return queue_.take(overflow);
    }
    uint64_t captured() const {
        return captured_.load();
    }
    uint64_t widenings() const {
        return widenings_.load();
    }
    uint64_t failures() const {
        return failures_.load();
    }

  private:
    enum class Route {
        ignore,
        qte_bind,
        qte_update,
        judge_update,
        aim,
        timing_start,
        timing_update,
        timing_end,
        timing_reset,
        animation_cross,
        animation_mash,
        animation_hold,
        status_button,
        menu_repeat,
        shoot_tick,
        remove,
        view,
        marker,
        marker_call,
        sound,
        search
    };
    struct LockFrame {
        LockFrame *previous{};
        uint32_t pc{}, index{};
        uint64_t serial{};
        bool active{}, tracing{};
        LockVector aim{};
    };
    struct Removal {
        Removal *previous{};
        uint32_t pc{};
        bool angular{};
    };
    struct Marker {
        Marker *previous{};
        uint32_t movie{}, target{};
    };
    struct CameraViewCache {
        uint32_t cls{}, index{};
        bool supported{};
    };
    struct Binding {
        uint32_t source{}, index{}, pc{}, cls{};
        QteSchema schema = QteSchema::divergence;
        uint64_t generation{}, observed_revision{};
        bool published{};
        QteGroup last;
    };
    struct TimingBinding {
        uint32_t source{}, index{}, track{};
        uint64_t generation{};
        bool published{};
        TimingState last;
    };
    struct TimingScope {
        TimingScope *previous{};
        uint32_t source{}, frame{};
        int32_t track{};
        bool window{};
    };
    struct Cache {
        uint32_t node{}, owner{}, owner_name{}, name{};
        Route route = Route::ignore;
    };
    struct Thread {
        uint64_t epoch = UINT64_MAX;
        uint32_t pc{};
        Reader reader;
        std::array<Binding, 32> bindings{};
        std::array<TimingBinding, 32> timings{};
        std::array<Cache, 128> routes{};
        std::array<CameraViewCache, 16> cameras{};
        TimingScope *scope{};
        LockFrame *locks{};
        Removal *removal{};
        Marker *marker{};
    };
    static Thread &thread() {
        static thread_local Thread state;
        return state;
    }
    struct Read {
        uint8_t *base;
        bool operator()(uint32_t at, void *out, size_t size) const {
            SIZE_T got{};
            return at && uint64_t(at) + size <= 0x100000000ull &&
                   ReadProcessMemory(GetCurrentProcess(), base + at, out, size, &got) &&
                   got == size;
        }
    };
    // The original interpreter immediately reads this same FFrame and UFunction.
    // Only those guaranteed call-frame fields use direct loads. Unrelated script
    // calls take this small name-ID filter without RPM, locks or object scans.
    static uint32_t frame_word(uint8_t *base, uint32_t at) {
        if (!at || uint64_t(at) + 4 > 0x100000000ull)
            return 0;
        return cue_word(base + at);
    }
    bool selected(uint32_t name) const {
        for (const auto &index : indices_)
            if (index.load(std::memory_order_relaxed) == name)
                return true;
        return false;
    }
    static bool subclass(const Memory &memory, Reader &reader, uint32_t object,
                         std::string_view name) {
        for (auto cls = memory.u32(object + 0x34), depth = 0u; cls && depth < 24;
             cls = memory.u32(cls + 0x40), ++depth)
            if (reader.object_name(memory, cls) == name)
                return true;
        return false;
    }
    Route classify(const Memory &memory, uint8_t *base, uint32_t node, uint32_t name,
                   Thread &state) {
        auto owner = frame_word(base, node + 0x28),
             ownerName = owner ? frame_word(base, owner + 0x2c) : 0;
        auto &cached = state.routes[(node >> 4) % state.routes.size()];
        if (cached.node == node && cached.owner == owner && cached.owner_name == ownerName &&
            cached.name == name)
            return cached.route;
        auto cls = state.reader.name(memory, ownerName), function = state.reader.name(memory, name);
        Route route = Route::ignore;
        if (cls == "BCSeqAct_InputDivergence" && function == "RequestInputJudge")
            route = Route::qte_bind;
        else if ((cls == "BCSeqAct_InputDivergence" &&
                  (function == "RequestButtonIcon" || function == "UpdateButtonIcon")) ||
                 ((cls == "BCSeqAct_InputDivergenceBase" || cls == "BCSeqAct_ButtonDivergence") &&
                  (function == "CheckActivate" || function == "Deactivated")))
            route = Route::qte_update;
        else if (cls == "BCInputJudgeSystem" &&
                 (function == "Update" || function == "EndJudge" || function == "DestroyRequest"))
            route = Route::judge_update;
        else if (cls == "BCPlayerController_HeavyShoot" && function == "CheckLockOnShotTarget")
            route = Route::aim;
        else if (cls == "BCPlayerInput" && function == "HandleButtonInput_Back")
            route = Route::status_button;
        else if (cls == "GFxMoviePlayer" && function == "FilterButtonInput")
            route = Route::menu_repeat;
        else if (function == "PlayerTick")
            route = Route::shoot_tick;
        else if (cls == "BCPlayerController_HeavyShoot" && function == "CheckRemoveLockOnTarget")
            route = Route::remove;
        else if (cls == "BCPlayerController_HeavyShoot" && function == "SearchLockOnShotTarget")
            route = Route::search;
        else if (cls == "Camera" && function == "GetCameraViewPoint")
            route = Route::view;
        else if (cls == "BCGFxUI_Hud" && function == "ShowLockonMarker")
            route = Route::marker;
        else if (cls == "BCGFxUI_Hud" && function == "CallShowLockonMarker")
            route = Route::marker_call;
        else if (cls == "BCGFxMoviePlayer" && function == "PlayGFxUISound")
            route = Route::sound;
        else if (function == "RequestButtonIcon" || function == "CheckActivate" ||
                 function == "Update" || function == "Deactivated") {
            if (cls == "BCSeqAct_AnimCrossFadeButtonRep")
                route = Route::animation_cross;
            else if (cls == "BCSeqAct_AnimFadeByButtonRep")
                route = Route::animation_mash;
            else if (cls == "BCSeqAct_AnimFadeByHoldTime")
                route = Route::animation_hold;
        }
        if (cls == "BCInterpTrackSynchronic") {
            if (function == "StartSynchronicTime")
                route = Route::timing_start;
            else if (function == "UpdateNormal" || function == "UpdateBarrage")
                route = Route::timing_update;
            else if (function == "EndSynchronicTime")
                route = Route::timing_end;
            else if (function == "InitTracks")
                route = Route::timing_reset;
        }
        cached = {node, owner, ownerName, name, route};
        return route;
    }
    struct Staged {
        GameplayQueue queue;
        uint64_t epoch{};
    };
    static Staged &staged() {
        static thread_local Staged value;
        return value;
    }
    static void flush(void *context) {
        auto &self = *static_cast<GameplayHooks *>(context);
        auto &local = staged();
        bool overflow = false;
        auto events = local.queue.take(overflow);
        if (!self.enabled_.load() || local.epoch != self.epoch_.load())
            return;
        for (const auto &event : events)
            self.queue_.push(event);
        if (overflow)
            self.queue_.mark_overflow();
        if (overflow || !events.empty())
            SetEvent(self.event_);
    }
    void publish(GameplayEvent event) {
        event.tick = GetTickCount64();
        event.order = ++cue_order;
        ++captured_;
        if (cue_transaction.depth) {
            auto &local = staged();
            auto epoch = epoch_.load();
            if (local.epoch != epoch) {
                local.queue.clear();
                local.epoch = epoch;
            }
            local.queue.push(event);
            defer_cue_flush(flush, this);
        } else {
            queue_.push(event);
            SetEvent(event_);
        }
    }
    Binding *bind(const Memory &memory, Thread &state, uint32_t source, uint32_t pc, bool fresh,
                  QteSchema schema = QteSchema::divergence) {
        auto index = memory.u32(source + 0x20);
        if (!source || !index)
            return nullptr;
        auto it =
            std::find_if(state.bindings.begin(), state.bindings.end(), [&](const Binding &item) {
                return item.source == source && item.index == index;
            });
        if (it == state.bindings.end()) {
            if (schema == QteSchema::divergence &&
                !subclass(memory, state.reader, source, "BCSeqAct_InputDivergence"))
                return nullptr;
            it =
                std::find_if(state.bindings.begin(), state.bindings.end(),
                             [](const Binding &item) { return !item.source || !item.last.active; });
            if (it == state.bindings.end()) {
                ++failures_;
                return nullptr;
            }
            *it = {};
            it->source = source;
            it->index = index;
            it->cls = memory.u32(source + 0x34);
            it->schema = schema;
            it->generation = ++generation_;
        }
        if (it->schema != schema || it->cls != memory.u32(source + 0x34)) {
            it->schema = schema;
            it->cls = memory.u32(source + 0x34);
            it->published = false;
            it->generation = ++generation_;
        }
        if (pc)
            it->pc = pc;
        else if (state.pc)
            it->pc = state.pc;
        it->observed_revision = button_revision.load();
        if (fresh)
            it->generation = ++generation_;
        return &*it;
    }
    void snapshot(const Memory &memory, Binding &binding) {
        QteGroup group;
        bool valid = memory.u32(binding.source + 0x20) == binding.index &&
                     memory.u32(binding.source + 0x34) == binding.cls &&
                     (binding.schema == QteSchema::divergence
                          ? capture_qte_group(memory, binding.source, binding.pc, group)
                          : capture_animation_qte(memory, binding.source, binding.schema, group));
        if (!valid) {
            group = {};
            group.source = binding.source;
            group.index = binding.index;
            ++failures_;
        }
        group.generation = binding.generation;
        group.visual_revision = binding.observed_revision;
        if (!binding.published || group != binding.last) {
            binding.last = group;
            binding.published = true;
            GameplayEvent event;
            event.group = group;
            publish(event);
        }
    }
    void timing(const Memory &memory, Thread &state, uint32_t source, int32_t track, bool window,
                bool reset = false) {
        if (track < 0) {
            for (auto &binding : state.timings)
                if (binding.source == source) {
                    TimingState ended = binding.last;
                    ended.active = ended.window = false;
                    ended.generation = binding.generation;
                    if (!binding.published || ended != binding.last) {
                        binding.last = ended;
                        binding.published = true;
                        GameplayEvent event;
                        event.timing = true;
                        event.sync = ended;
                        publish(event);
                    }
                }
            return;
        }
        TimingState value;
        if (!capture_timing(memory, source, track, window, value)) {
            ++failures_;
            return;
        }
        auto it = std::find_if(state.timings.begin(), state.timings.end(),
                               [&](const TimingBinding &item) {
                                   return item.source == source && item.index == value.index &&
                                          item.track == uint32_t(track);
                               });
        if (it == state.timings.end()) {
            it = std::find_if(
                state.timings.begin(), state.timings.end(),
                [](const TimingBinding &item) { return !item.source || !item.last.active; });
            if (it == state.timings.end()) {
                ++failures_;
                return;
            }
            *it = {};
            it->source = source;
            it->index = value.index;
            it->track = uint32_t(track);
            it->generation = ++generation_;
        }
        if (reset || (!it->last.active && value.active))
            it->generation = ++generation_;
        value.generation = it->generation;
        if (!it->published || value != it->last) {
            it->last = value;
            it->published = true;
            GameplayEvent event;
            event.timing = true;
            event.sync = value;
            publish(event);
        }
    }
    LockTraceRecord lock_record(const Memory &memory, Thread &state, LockTraceKind kind,
                                uint32_t target = 0) {
        LockTraceRecord record;
        record.kind = kind;
        record.target = target;
        record.target_index = target ? memory.u32(target + 0x20) : 0;
        if (state.locks) {
            record.frame = state.locks->serial;
            record.controller = state.locks->pc;
            record.controller_index = state.locks->index;
            record.aim = state.locks->aim;
            auto camera = lock_camera(memory, state.locks->pc);
            record.camera_valid = camera.valid;
            record.fov = camera.fov;
            record.camera_location = camera.location;
            record.camera_forward = camera.forward;
            if (auto offset = lock_angle(record.aim, camera.forward))
                record.aim_offset = *offset;
        }
        return record;
    }
    void trace_record(const LockTraceRecord &record, Thread &state) {
        if (state.locks && state.locks->tracing)
            trace_.push(record, GetTickCount64());
    }
    bool base_camera_view(const Memory &memory, Thread &state, uint32_t pc) {
        auto camera = memory.u32(pc + 0x370), type = memory.u32(camera + 0x34),
             index = memory.u32(type + 0x20);
        if (!camera || !type || !index)
            return false;
        auto &cached = state.cameras[(type >> 4) % state.cameras.size()];
        if (cached.cls == type && cached.index == index)
            return cached.supported;
        bool supported = false, found = false;
        auto viewName = indices_[18].load();
        for (auto cls = type, depth = 0u; cls && !found && depth < 24;
             cls = memory.u32(cls + 0x40), ++depth) {
            auto field = memory.u32(cls + 0x44);
            for (unsigned count = 0; field && count < 1024;
                 field = memory.u32(field + 0x3c), ++count)
                if (memory.u32(field + 0x2c) == viewName) {
                    found = true;
                    supported = memory.u32(field + 0x28) == cls &&
                                state.reader.object_name(memory, cls) == "Camera" &&
                                memory.u32(field + 0x88) == 0x82260d78;
                    break;
                }
        }
        cached = {type, index, supported};
        return supported;
    }
    bool observe_lock(Route route, void *context, uint8_t *base, const Memory &memory,
                      Thread &state, uint32_t source, uint32_t locals) {
        if (route == Route::shoot_tick) {
            if ((!trace_.armed() && !stable_cone_.load()) ||
                !subclass(memory, state.reader, source, "BCPlayerController_HeavyShoot") ||
                (state.locks && state.locks->pc == source)) {
                original_[0](context, base);
                return true;
            }
            auto world = memory.u32(source + 0xd4);
            LockFrame frame{state.locks, source, memory.u32(source + 0x20), ++lock_frames_};
            frame.active = frame.index && world && memory.u32(source + 0x5c4) &&
                           !(memory.u32(source + 0x378) & 0x8000) && !memory.u32(world + 0x368);
            frame.tracing = frame.active && trace_.begin(GetTickCount64(), false);
            state.locks = &frame;
            // The first acquisition callback supplies the authoritative aim line.
            trace_record(lock_record(memory, state, LockTraceKind::frame), state);
            original_[0](context, base);
            state.locks = frame.previous;
            if (trace_.pending())
                SetEvent(event_);
            return true;
        }
        if (route == Route::aim) {
            if (!stable_cone_.load() && !(state.locks && state.locks->active && trace_.armed())) {
                AcquisitionScope scope(
                    memory,
                    [&](uint32_t at, const void *bytes, size_t n) {
                        SIZE_T wrote{};
                        return WriteProcessMemory(GetCurrentProcess(), base + at, bytes, n,
                                                  &wrote) &&
                               wrote == n;
                    },
                    source, angle_.load());
                if (scope.changed())
                    ++widenings_;
                original_[0](context, base);
                return true;
            }
            LockVector origin{}, aim{};
            bool vectors = locals && lock_vector(memory, locals + 8, origin) &&
                           lock_vector(memory, locals + 0x14, aim);
            if (state.locks && state.locks->pc == source && vectors)
                state.locks->aim = aim;
            auto record = lock_record(memory, state, LockTraceKind::acquire,
                                      locals ? memory.u32(locals + 4) : 0);
            record.geometry_supported = base_camera_view(memory, state, source);
            record.native_min = std::bit_cast<float>(memory.u32(source + 0x7d4));
            record.native_max = std::bit_cast<float>(memory.u32(source + 0x7d8));
            auto degrees = angle_.load();
            std::optional<float> cap;
            if (stable_cone_.load()) {
                if (record.geometry_supported && vectors && state.locks && state.locks->active)
                    cap = stable_lock_radius(lock_camera(memory, source), origin, aim);
                if (!cap)
                    degrees = 0;
            }
            record.cap_applied = cap.has_value() && degrees > 0;
            AcquisitionScope scope(
                memory,
                [&](uint32_t at, const void *bytes, size_t n) {
                    SIZE_T wrote{};
                    return WriteProcessMemory(GetCurrentProcess(), base + at, bytes, n, &wrote) &&
                           wrote == n;
                },
                source, degrees, cap);
            record.effective_min = std::bit_cast<float>(memory.u32(source + 0x7d4));
            record.effective_max = std::bit_cast<float>(memory.u32(source + 0x7d8));
            if (scope.changed())
                ++widenings_;
            original_[0](context, base);
            if (locals) {
                record.result = memory.u32(locals + 0x24) == 1;
                record.accepted = record.target && memory.u32(locals + 0x34) == record.target;
                LockVector line{};
                record.angle_valid =
                    lock_vector(memory, locals + 0x58, line) && lock_length(line) > .5f;
                record.angular = record.angle_valid;
                record.angle = std::bit_cast<float>(memory.u32(locals + 0x50));
                auto distance = std::bit_cast<float>(memory.u32(locals + 0x54));
                record.limit = std::numeric_limits<float>::quiet_NaN();
                if (std::isfinite(record.effective_min) && std::isfinite(record.effective_max) &&
                    record.effective_min <= record.effective_max && std::isfinite(distance))
                    record.limit =
                        std::clamp(record.effective_min +
                                       (record.effective_max - record.effective_min) * distance,
                                   record.effective_min, record.effective_max);
            }
            // Wait for a real new lock instead of spending the capture on the
            // thousands of rejected targets searched during the opening shot.
            // A capped candidate is also useful evidence when the candidate fix
            // prevents every lock; its repeated failures are sampled by LockTrace.
            if (state.locks && state.locks->active && !state.locks->tracing &&
                ((record.accepted && record.result) || record.cap_applied)) {
                state.locks->tracing = trace_.begin(GetTickCount64());
                trace_record(lock_record(memory, state, LockTraceKind::frame), state);
            }
            trace_record(record, state);
            return true;
        }
        if (route == Route::remove) {
            if (!state.locks || !state.locks->tracing || source != state.locks->pc) {
                original_[0](context, base);
                return true;
            }
            auto target = locals ? memory.u32(locals) : 0, result = ui_argument(context, 5);
            Removal scope{state.removal, source, false};
            state.removal = &scope;
            original_[0](context, base);
            state.removal = scope.previous;
            auto record = lock_record(memory, state, LockTraceKind::remove, target);
            record.result = result && memory.u32(result) != 0;
            record.angular = scope.angular;
            LockVector toTarget{}, forward{};
            if (scope.angular && lock_vector(memory, locals + 0xc, toTarget) &&
                lock_vector(memory, locals + 0x18, forward))
                if (auto angle = lock_angle(toTarget, forward)) {
                    record.angle = *angle;
                    record.angle_valid = true;
                    record.camera_forward = forward;
                    lock_vector(memory, locals + 0x24, record.camera_location);
                }
            record.limit = (record.fov + 5.f) / 2.f;
            trace_record(record, state);
            return true;
        }
        if (route == Route::view) {
            original_[0](context, base);
            if (state.removal && source == memory.u32(state.removal->pc + 0x370))
                state.removal->angular = true;
            return true;
        }
        if (route == Route::marker) {
            if (!state.locks || !state.locks->tracing) {
                original_[0](context, base);
                return true;
            }
            Marker scope{state.marker, source, locals ? memory.u32(locals + 4) : 0};
            state.marker = &scope;
            original_[0](context, base);
            state.marker = scope.previous;
            return true;
        }
        if (route == Route::marker_call) {
            if (state.marker && state.marker->movie == source && locals) {
                auto record = lock_record(memory, state,
                                          memory.u32(locals + 0xc) ? LockTraceKind::marker_add
                                                                   : LockTraceKind::marker_remove,
                                          state.marker->target);
                trace_record(record, state);
            }
            original_[0](context, base);
            return true;
        }
        if (route == Route::sound) {
            if (state.marker && locals) {
                auto record =
                    lock_record(memory, state, LockTraceKind::sound, state.marker->target);
                record.sound = memory.u32(locals);
                record.sound_name = {memory.u32(record.sound + 0x2c),
                                     memory.u32(record.sound + 0x30)};
                trace_record(record, state);
            }
            original_[0](context, base);
            return true;
        }
        if (route == Route::search) {
            original_[0](context, base);
            return true;
        }
        return false;
    }
    static void script_call(void *context, uint8_t *base) {
        auto &self = instance();
        if (!self.enabled_.load(std::memory_order_relaxed) ||
            !self.configured_.load(std::memory_order_relaxed)) {
            self.original_[0](context, base);
            return;
        }
        auto frame = ui_argument(context, 4), node = frame_word(base, frame + 0x10),
             name = node ? frame_word(base, node + 0x2c) : UINT32_MAX;
        if (!node || !self.selected(name)) {
            self.original_[0](context, base);
            return;
        }
        auto epoch = self.epoch_.load();
        auto &state = thread();
        if (state.epoch != epoch) {
            state.bindings = {};
            state.timings = {};
            state.pc = 0;
            state.scope = nullptr;
            state.locks = nullptr;
            state.removal = nullptr;
            state.marker = nullptr;
            state.epoch = epoch;
        }
        Memory memory(Read{base});
        auto route = self.classify(memory, base, node, name, state);
        if (route == Route::ignore) {
            self.original_[0](context, base);
            return;
        }
        auto source = ui_argument(context, 3), locals = memory.u32(frame + 0x1c);
        if (self.observe_lock(route, context, base, memory, state, source, locals))
            return;
        if (route == Route::menu_repeat) {
            // UE3's existing movie input event: XboxTypeS_X is physical Square/X.
            // Observe only IE_Pressed (0), never repeats/releases or gameplay input.
            uint8_t inputEvent = 255;
            auto index = memory.u32(source + 0x20), cls = memory.u32(source + 0x34);
            bool repeat = locals && memory.copy(locals + 0xc, &inputEvent, 1) && inputEvent == 0 &&
                          index && (memory.u32(source + 0xc8) & 0x80000000) &&
                          memory.u32(locals + 8) == 0 &&
                          state.reader.name(memory, memory.u32(locals + 4)) == "XboxTypeS_X" &&
                          (subclass(memory, state.reader, source, "BCGFxUI_ResultMenu") ||
                           subclass(memory, state.reader, source, "BCGFxUI_ChapterMenu"));
            self.original_[0](context, base);
            if (repeat && self.enabled_.load() && epoch == self.epoch_.load() &&
                memory.u32(source + 0x20) == index && memory.u32(source + 0x34) == cls &&
                (memory.u32(source + 0xc8) & 0x80000000)) {
                GameplayEvent event;
                event.menu_repeat = true;
                event.repeat_movie = source;
                event.repeat_index = index;
                self.publish(event);
            }
            return;
        }
        if (route == Route::status_button) {
            uint8_t buttonEvent = 255;
            auto before = self.status_button_.load() && locals &&
                                  memory.copy(locals, &buttonEvent, 1) && buttonEvent == 0
                              ? capture_status_context(memory, source)
                              : std::nullopt;
            // Always execute the original, including all cinematic-skip behavior.
            self.original_[0](context, base);
            if (before && self.enabled_.load() && epoch == self.epoch_.load() &&
                capture_status_context(memory, source) == before) {
                GameplayEvent event;
                event.status_request = true;
                event.status = *before;
                self.publish(event);
            }
            return;
        }
        if (!self.qtes_.load()) {
            self.original_[0](context, base);
            return;
        }
        CueTransaction transaction;
        int32_t track = locals ? int32_t(memory.u32(locals)) : -1;
        TimingScope scope{state.scope, source, frame, track, false};
        if (route == Route::timing_update)
            state.scope = &scope;
        self.original_[0](context, base);
        if (route == Route::timing_update)
            state.scope = scope.previous;
        if (!self.enabled_.load() || epoch != self.epoch_.load())
            return;
        if (route == Route::qte_bind || route == Route::qte_update) {
            auto pc = locals ? memory.u32(locals) : 0;
            if (auto *binding = self.bind(memory, state, source, pc, route == Route::qte_bind))
                self.snapshot(memory, *binding);
        } else if (route == Route::judge_update) {
            auto pc = memory.u32(source + 0x1d0);
            if (pc)
                state.pc = pc;
            for (auto &binding : state.bindings)
                if (binding.schema == QteSchema::divergence &&
                    (binding.last.active || binding.last.identifier.index) && binding.source &&
                    binding.pc && memory.u32(binding.pc + 0x760) == source)
                    self.snapshot(memory, binding);
        } else if (route == Route::timing_reset)
            self.timing(memory, state, source, -1, false);
        else if (route == Route::timing_start || route == Route::timing_update ||
                 route == Route::timing_end)
            self.timing(memory, state, source, track, scope.window, route == Route::timing_start);
        else if (route == Route::animation_cross || route == Route::animation_mash ||
                 route == Route::animation_hold) {
            auto schema = route == Route::animation_cross  ? QteSchema::animation_cross
                          : route == Route::animation_mash ? QteSchema::animation_mash
                                                           : QteSchema::animation_hold;
            if (auto *binding =
                    self.bind(memory, state, source, 0, name == self.indices_[1].load(), schema))
                self.snapshot(memory, *binding);
        }
    }
    static void input_window(void *context, uint8_t *base) {
        auto &self = instance();
        auto *scope = self.enabled_.load(std::memory_order_relaxed) &&
                              self.qtes_.load(std::memory_order_relaxed)
                          ? thread().scope
                          : nullptr;
        bool observe = scope && scope->source == ui_argument(context, 3) &&
                       scope->frame == ui_argument(context, 4);
        auto result = observe ? ui_argument(context, 5) : 0;
        self.original_[1](context, base);
        if (observe && result) {
            uint8_t bytes[4]{};
            SIZE_T got{};
            if (ReadProcessMemory(GetCurrentProcess(), base + result, bytes, 4, &got) && got == 4)
                scope->window = cue_word(bytes) != 0;
        }
    }
    GameplayHooks() : event_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {
        for (auto &index : indices_)
            index = UINT32_MAX;
    }
    NativeHooks hooks_;
    const BuildProfile *profile_{};
    std::array<Function, 2> original_{};
    std::array<std::atomic<uint32_t>, function_names.size()> indices_{};
    HANDLE event_{};
    uint32_t names_global_ = disc_profile.layout.names_global;
    bool installed_{};
    std::atomic<bool> configured_{}, enabled_{}, qtes_{true}, status_button_{true};
    std::atomic<float> angle_{};
    std::atomic<uint64_t> epoch_{}, captured_{}, failures_{}, widenings_{}, generation_{};
    GameplayQueue queue_;
    LockTrace trace_;
    std::atomic<bool> stable_cone_{};
    std::atomic<uint64_t> lock_frames_{};
};
}
