#pragma once
#include "build_profile.h"
#include "gameplay_events.h"
#include "native_hooks.h"
#include "presentation_events.h"
#include "shooting_assist.h"
#include "lock_trace.h"
#include "shooting_beacons.h"
#include <atomic>
#include <windows.h>

namespace a11y {
class GameplayHooks {
  public:
    using Function = void (*)(void *, uint8_t *);
    static constexpr std::array<const char *, 33> function_names{"RequestInputJudge",
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
                                                                 "SearchLockOnShotTarget",
                                                                 "UpdateLockOnLevel",
                                                                 "ResetLockOnLevel",
                                                                 "PlayCloseAnimation",
                                                                 "OnOpenAnimationComplete",
                                                                 "OnGetRecordInfo",
                                                                 "IterateLockOnShotTargetComp",
                                                                 "InputCheckTick",
                                                                 "CallStart",
                                                                 "CallEnd",
                                                                 "OnCloseAnimationComplete"};
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
                                reinterpret_cast<void *>(executable + profile.gameplay_rvas[1]),
                                reinterpret_cast<void *>(executable + profile.gameplay_rvas[2])});
    }
    bool install_targets(const std::array<void *, 3> &targets) {
        if (installed_)
            return true;
        if (!event_ || !hooks_.initialize())
            return false;
        void *callbacks[]{reinterpret_cast<void *>(script_call),
                          reinterpret_cast<void *>(input_window),
                          reinterpret_cast<void *>(movie_input_event)};
        for (unsigned i = 0; i < 3; ++i)
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
    void lock_diagnostics(bool enabled) {
        trace_.configure(enabled);
    }
    void shooting(ShootingMode mode) {
        mode_ = mode;
        aim_mailbox_.clear();
    }
    ShootingMode shooting() const {
        return mode_.load();
    }
    std::optional<AimSnapshot> take_aim() {
        return aim_mailbox_.take();
    }
    bool autoaim() const {
        return mode_.load() == ShootingMode::autoaim && angle_.load() > 0;
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
        aim_mailbox_.clear();
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
    // The target-position model is trusted only after several native comparisons
    // agree and never after any disagreement.
    bool position_verified() const {
        return position_matches_.load() >= 3 && position_mismatches_.load() == 0;
    }
    uint64_t position_matches() const {
        return position_matches_.load();
    }
    uint64_t position_mismatches() const {
        return position_mismatches_.load();
    }
    uint64_t failures() const {
        return failures_.load();
    }

  private:
    enum class Route {
        ignore,
        script_scene_start,
        script_scene_end,
        record_info,
        input_tick,
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
        volley_fire,
        menu_lifecycle,
        shoot_tick,
        remove,
        view,
        marker,
        marker_call,
        sound,
        search,
        level,
        level_reset
    };
    struct LockFrame {
        LockFrame *previous{};
        uint32_t pc{}, index{};
        uint64_t serial{};
        bool active{}, tracing{}, captured{};
        uint32_t search_locals{};
        LockVector aim{};
        AimSnapshot snapshot;
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
        uint32_t input_source{}, input_index{};
        uint8_t input_held{};
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
        if (script_scene_protocol(cls) && function == "CallStart")
            route = Route::script_scene_start;
        else if ((script_scene_protocol(cls) && function == "CallEnd") ||
                 ((script_scene_protocol(cls) || cls == "BCGFxMoviePlayer") &&
                  function == "OnCloseAnimationComplete"))
            route = Route::script_scene_end;
        else if (cls == "BCPlayerInput" && function == "InputCheckTick")
            route = Route::input_tick;
        else if (cls == "BCGFxUI_ChapterMenu" && function == "OnGetRecordInfo")
            route = Route::record_info;
        else if (cls == "BCSeqAct_InputDivergence" && function == "RequestInputJudge")
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
        else if (cls == "BCPlayerController_HeavyShoot" &&
                 function == "IterateLockOnShotTargetComp")
            route = Route::volley_fire;
        else if (cls == "BCGFxMoviePlayer" &&
                 (function == "PlayCloseAnimation" || function == "OnOpenAnimationComplete"))
            route = Route::menu_lifecycle;
        else if (function == "PlayerTick")
            route = Route::shoot_tick;
        else if (cls == "BCPlayerController_HeavyShoot" && function == "CheckRemoveLockOnTarget")
            route = Route::remove;
        else if (cls == "BCPlayerController_HeavyShoot" && function == "UpdateLockOnLevel")
            route = Route::level;
        else if (cls == "BCPlayerController_HeavyShoot" && function == "ResetLockOnLevel")
            route = Route::level_reset;
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
            record.level_after = int32_t(memory.u32(state.locks->pc + 0x8e8));
            record.level_target = int32_t(memory.u32(state.locks->pc + 0x8ec));
            record.level_max = int32_t(memory.u32(state.locks->pc + 0x7fc));
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
            if ((!trace_.armed() && !autoaim() && mode_.load() != ShootingMode::beacons) ||
                !subclass(memory, state.reader, source, "BCPlayerController_HeavyShoot") ||
                (state.locks && state.locks->pc == source)) {
                original_[0](context, base);
                return true;
            }
            auto world = memory.u32(source + 0xd4);
            LockFrame frame{};
            frame.previous = state.locks;
            frame.pc = source;
            frame.index = memory.u32(source + 0x20);
            frame.serial = ++lock_frames_;
            frame.active = frame.index && world && memory.u32(source + 0x5c4) &&
                           !(memory.u32(source + 0x378) & 0x8000) && !memory.u32(world + 0x368);
            frame.tracing = frame.active && trace_.begin(GetTickCount64(), false);
            state.locks = &frame;
            // The first acquisition callback supplies the authoritative aim line.
            trace_record(lock_record(memory, state, LockTraceKind::frame), state);
            original_[0](context, base);
            state.locks = frame.previous;
            if (mode_.load() == ShootingMode::beacons) {
                if (!frame.snapshot.tick)
                    frame.snapshot.tick = GetTickCount64();
                frame.snapshot.active =
                    frame.snapshot.active && enabled_.load() &&
                    aim_context_active(memory, frame.snapshot.controller, frame.snapshot.pawn);
                aim_mailbox_.put(frame.snapshot);
                SetEvent(event_);
            }
            if (trace_.pending())
                SetEvent(event_);
            return true;
        }
        if (route == Route::aim) {
            if (mode_.load() == ShootingMode::beacons && state.locks && state.locks->pc == source &&
                state.locks->active && !state.locks->captured && state.locks->search_locals &&
                position_mismatches_.load() == 0 && base_camera_view(memory, state, source)) {
                state.locks->captured = true;
                state.locks->snapshot = capture_aim(memory, source, state.locks->search_locals,
                                                    fired_.load(), GetTickCount64());
            }
            if (state.locks && state.locks->active && !state.locks->tracing && trace_.armed()) {
                state.locks->tracing = trace_.begin(GetTickCount64());
                trace_record(lock_record(memory, state, LockTraceKind::frame), state);
            }
            if (!autoaim() && mode_.load() != ShootingMode::beacons &&
                !(state.locks && state.locks->active && trace_.armed())) {
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
            auto degrees = autoaim() ? angle_.load() : 0.f;
            std::optional<float> cap;
            // The candidate's world position is computed on every check so the model
            // can be compared with the native angle after the call.
            LockVector position{};
            const bool havePosition =
                vectors && lock_target_position(memory, record.target, position);
            if (havePosition)
                if (auto computed = lock_direction_angle(origin, aim, position))
                    record.target_angle = *computed;
            record.position_verified = position_verified();
            if (autoaim()) {
                const bool geometry =
                    record.geometry_supported && vectors && state.locks && state.locks->active;
                if (geometry && record.position_verified) {
                    // Widen only where native retention would keep the lock. Unsafe
                    // candidates keep the original cone, including any native lock/drop
                    // behavior: this repair only constrains assistance added by the mod.
                    auto camera = lock_camera(memory, source);
                    LockVector gap{origin.x - camera.location.x, origin.y - camera.location.y,
                                   origin.z - camera.location.z};
                    std::optional<float> margin;
                    if (havePosition && lock_length(gap) <= .001f)
                        margin = lock_view_margin(camera, position);
                    if (margin)
                        record.view_margin = *margin;
                    if (margin && *margin < kLockViewReserve) {
                        degrees = 0;
                        record.view_gated = true;
                    } else if (!margin) {
                        // Unknown candidate geometry is not evidence for a target gate.
                        cap = stable_lock_radius(camera, origin, aim);
                        if (!cap)
                            degrees = 0;
                    }
                } else {
                    // Until the position model has agreed with the native angle (or if it
                    // ever disagrees), fall back to the conservative aim/camera bound.
                    if (geometry)
                        cap = stable_lock_radius(lock_camera(memory, source), origin, aim);
                    if (!cap)
                        degrees = 0;
                }
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
            const auto countBefore = lock_shot_count(memory, source, record.target);
            original_[0](context, base);
            if (locals) {
                record.accepted = record.target && memory.u32(locals + 0x34) == record.target;
                record.result =
                    record.accepted && countBefore < 1 && int32_t(memory.u32(locals + 0x38)) >= 1;
                LockVector line{};
                record.angle_valid =
                    lock_vector(memory, locals + 0x58, line) && lock_length(line) > .5f;
                record.angular = record.angle_valid;
                record.angle = std::bit_cast<float>(memory.u32(locals + 0x50));
                if (record.angle_valid && std::isfinite(record.target_angle)) {
                    // Self-check of the position model against the game's own angle.
                    if (std::abs(record.target_angle - record.angle) < .25f)
                        ++position_matches_;
                    else
                        ++position_mismatches_;
                }
                auto distance = std::bit_cast<float>(memory.u32(locals + 0x54));
                record.limit = std::numeric_limits<float>::quiet_NaN();
                if (std::isfinite(record.effective_min) && std::isfinite(record.effective_max) &&
                    record.effective_min <= record.effective_max && std::isfinite(distance))
                    record.limit =
                        std::clamp(record.effective_min +
                                       (record.effective_max - record.effective_min) * distance,
                                   record.effective_min, record.effective_max);
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
            if (state.locks && state.locks->tracing && locals) {
                auto record = lock_record(memory, state, LockTraceKind::sound,
                                          state.marker ? state.marker->target : 0);
                record.sound = memory.u32(locals);
                record.sound_name = {memory.u32(record.sound + 0x2c),
                                     memory.u32(record.sound + 0x30)};
                trace_record(record, state);
            }
            original_[0](context, base);
            return true;
        }
        if (route == Route::level || route == Route::level_reset) {
            const bool observe =
                trace_.armed() && state.locks && state.locks->active && state.locks->pc == source;
            auto before = observe ? int32_t(memory.u32(source + 0x8e8)) : -1;
            original_[0](context, base);
            if (observe) {
                auto after = int32_t(memory.u32(source + 0x8e8));
                if (before != after) {
                    if (!state.locks->tracing) {
                        state.locks->tracing = trace_.begin(GetTickCount64());
                        trace_record(lock_record(memory, state, LockTraceKind::frame), state);
                    }
                    auto record = lock_record(memory, state, LockTraceKind::level_change);
                    record.level_before = before;
                    // UpdateLockOnLevel's verified script dispatches this cue after
                    // increasing the level. This is path/state evidence, distinct
                    // from an observed PlayGFxUISound request (event "sound").
                    if (route == Route::level && after == before + 1 && after >= 0 && after < 5) {
                        record.sound = memory.u32(source + 0x800 + uint32_t(after) * 40 + 0x20);
                        record.sound_name = {memory.u32(record.sound + 0x2c),
                                             memory.u32(record.sound + 0x30)};
                    }
                    trace_record(record, state);
                }
            }
            return true;
        }
        if (route == Route::volley_fire) {
            auto result = ui_argument(context, 5);
            original_[0](context, base);
            if (result && memory.u32(result))
                ++fired_;
            return true;
        }
        if (route == Route::search) {
            // The script empties TargetCompList before returning, so the candidates
            // are captured on the first acquisition check, while the list is full.
            auto *lock = state.locks && state.locks->pc == source ? state.locks : nullptr;
            if (lock)
                lock->search_locals = locals;
            original_[0](context, base);
            if (lock)
                lock->search_locals = 0;
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
            state.input_source = state.input_index = 0;
            state.input_held = 0;
            state.epoch = epoch;
        }
        Memory memory(Read{base});
        auto route = self.classify(memory, base, node, name, state);
        if (route == Route::ignore) {
            self.original_[0](context, base);
            return;
        }
        auto source = ui_argument(context, 3), locals = memory.u32(frame + 0x1c);
        if (route == Route::script_scene_start || route == Route::script_scene_end) {
            auto index = memory.u32(source + 0x20), cls = memory.u32(source + 0x34);
            bool supported = script_scene_protocol(state.reader.class_name(memory, source));
            self.original_[0](context, base);
            if (supported && index && self.enabled_.load() && epoch == self.epoch_.load() &&
                memory.u32(source + 0x20) == index && memory.u32(source + 0x34) == cls) {
                GameplayEvent event;
                event.script_scene = true;
                event.scene_active = route == Route::script_scene_start;
                event.scene_movie = source;
                event.scene_index = index;
                self.publish(event);
            }
            return;
        }
        if (route == Route::input_tick) {
            // These are the game's already-updated logical button states. Empty
            // delegate handlers may never enter either script/event dispatcher.
            // Observe on the native input tick, with no OS polling or new timer.
            auto array = memory.u32(source + 0x3f0), count = memory.u32(source + 0x3f4),
                 capacity = memory.u32(source + 0x3f8), index = memory.u32(source + 0x20);
            uint8_t held = 0;
            bool valid = index && array && count >= 14 && count <= 64 && count <= capacity &&
                         capacity <= 64 &&
                         uint64_t(array) + uint64_t(count) * 0x24 <= 0x100000000ull;
            constexpr std::array<unsigned, 3> rows{11, 13, 10};
            constexpr std::array<std::string_view, 3> names{"DPAD_LEFT", "DPAD_RIGHT", "DPAD_UP"};
            for (unsigned i = 0; valid && i < rows.size(); ++i) {
                std::array<uint8_t, 0x24> row{};
                valid = memory.copy(array + rows[i] * row.size(), row.data(), row.size()) &&
                        state.reader.name(memory, cue_word(row.data())) == names[i];
                if (valid && (cue_word(row.data() + 0x18) & 0x80000000))
                    held |= uint8_t(1 << i);
            }
            if (state.input_source != source || state.input_index != index) {
                state.input_source = source;
                state.input_index = index;
                state.input_held = 0;
            }
            auto pressed = valid ? uint8_t(held & ~state.input_held) : uint8_t(0);
            state.input_held = valid ? held : uint8_t(7);
            auto before = pressed ? capture_status_context(memory, source, false) : std::nullopt;
            bool active = before && subclass(memory, state.reader, before->controller,
                                             "BCPlayerController_HeavyShoot");
            self.original_[0](context, base);
            if (active && self.enabled_.load() && epoch == self.epoch_.load() &&
                capture_status_context(memory, source, false) == before)
                for (unsigned i = 0; i < rows.size(); ++i)
                    if (pressed & (1 << i)) {
                        GameplayEvent event;
                        event.shooting_command = int(i + 1);
                        event.status = *before;
                        self.publish(event);
                    }
            return;
        }
        if (self.observe_lock(route, context, base, memory, state, source, locals))
            return;
        if (route == Route::menu_lifecycle) {
            auto index = memory.u32(source + 0x20);
            uint8_t before = 255, after = 255;
            const bool observe = index &&
                                 subclass(memory, state.reader, source, "BCGFxMovieMenu") &&
                                 memory.copy(source + 0x174, &before, 1);
            self.original_[0](context, base);
            if (observe && self.enabled_.load() && epoch == self.epoch_.load() &&
                memory.u32(source + 0x20) == index && memory.copy(source + 0x174, &after, 1) &&
                before != after) {
                GameplayEvent event;
                event.menu_changed = true;
                if (after >= 2 && script_scene_protocol(state.reader.class_name(memory, source))) {
                    event.script_scene = true;
                    event.scene_movie = source;
                    event.scene_index = index;
                }
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
        if (route == Route::record_info) {
            GameplayEvent event;
            event.record_info = true;
            event.record_movie = source;
            event.record_index = memory.u32(source + 0x20);
            event.record_difficulty = int32_t(memory.u32(locals + 0xc));
            auto read = [&](uint32_t at, void *out, size_t size) {
                return memory.copy(at, out, size);
            };
            bool valid =
                locals && event.record_index && capture_fstring(read, locals, event.record_story);
            self.original_[0](context, base);
            if (valid && self.enabled_.load() && epoch == self.epoch_.load() &&
                memory.u32(source + 0x20) == event.record_index &&
                capture_fstring(read, locals + 0x10, event.record_text))
                self.publish(event);
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
    static void movie_input_event(void *context, uint8_t *base) {
        auto &self = instance();
        auto node = ui_argument(context, 4);
        auto name = node ? frame_word(base, node + 0x2c) : UINT32_MAX;
        // ProcessEvent sees native-origin movie callbacks even when their script
        // body is undefined. ProcessInternal never runs that default empty event.
        // The original dispatcher reads this UFunction field too; exclude unrelated
        // events before memory queries, metadata lookup or queue synchronization.
        if (!self.enabled_.load(std::memory_order_relaxed) ||
            !self.configured_.load(std::memory_order_relaxed) || !node ||
            name != self.indices_[15].load(std::memory_order_relaxed)) {
            self.original_[2](context, base);
            return;
        }
        const auto epoch = self.epoch_.load();
        Memory memory(Read{base});
        auto &state = thread();
        auto source = ui_argument(context, 3), params = ui_argument(context, 5);
        auto owner = memory.u32(node + 0x28);
        auto index = memory.u32(source + 0x20), cls = memory.u32(source + 0x34);
        uint8_t inputEvent = 255;
        int recordCommand = 0;
        if (params && state.reader.object_name(memory, owner) == "GFxMoviePlayer" &&
            memory.copy(params + 0xc, &inputEvent, 1) && inputEvent == 0 && index &&
            (memory.u32(source + 0xc8) & 0x80000000) && memory.u32(params + 8) == 0 &&
            subclass(memory, state.reader, source, "BCGFxUI_ChapterMenu")) {
            auto key = state.reader.name(memory, memory.u32(params + 4));
            constexpr std::array<std::string_view, 4> directions{
                "Gamepad_RightStick_Left", "Gamepad_RightStick_Right", "Gamepad_RightStick_Up",
                "Gamepad_RightStick_Down"};
            for (unsigned i = 0; i < directions.size(); ++i)
                if (key == directions[i])
                    recordCommand = int(i + 1);
        }
        bool repeat = params && state.reader.object_name(memory, owner) == "GFxMoviePlayer" &&
                      memory.copy(params + 0xc, &inputEvent, 1) && inputEvent == 0 && index &&
                      (memory.u32(source + 0xc8) & 0x80000000) && memory.u32(params + 8) == 0 &&
                      state.reader.name(memory, memory.u32(params + 4)) == "XboxTypeS_X" &&
                      (subclass(memory, state.reader, source, "BCGFxUI_ResultMenu") ||
                       subclass(memory, state.reader, source, "BCGFxUI_ChapterMenu"));
        self.original_[2](context, base);
        if ((repeat || recordCommand) && self.enabled_.load() && epoch == self.epoch_.load() &&
            memory.u32(source + 0x20) == index && memory.u32(source + 0x34) == cls &&
            (memory.u32(source + 0xc8) & 0x80000000)) {
            GameplayEvent event;
            event.menu_repeat = repeat;
            event.record_command = recordCommand;
            event.record_movie = source;
            event.record_index = index;
            event.repeat_movie = source;
            event.repeat_index = index;
            self.publish(event);
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
    std::array<Function, 3> original_{};
    std::array<std::atomic<uint32_t>, function_names.size()> indices_{};
    HANDLE event_{};
    uint32_t names_global_ = disc_profile.layout.names_global;
    bool installed_{};
    std::atomic<bool> configured_{}, enabled_{}, qtes_{true}, status_button_{true};
    std::atomic<float> angle_{};
    std::atomic<uint64_t> epoch_{}, captured_{}, failures_{}, widenings_{}, generation_{};
    GameplayQueue queue_;
    LockTrace trace_;
    static constexpr float kLockViewReserve = .5f;
    std::atomic<uint64_t> position_matches_{}, position_mismatches_{};
    std::atomic<ShootingMode> mode_{ShootingMode::autoaim};
    AimMailbox aim_mailbox_;
    std::atomic<uint64_t> fired_{};
    std::atomic<uint64_t> lock_frames_{};
};
}
