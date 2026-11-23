#pragma once
#include "build_profile.h"
#include "gameplay_events.h"
#include "native_hooks.h"
#include "presentation_events.h"
#include "shooting_assist.h"
#include "lock_trace.h"
#include "shooting_beacons.h"
#include "melee_aim.h"
#include <atomic>
#include <windows.h>

namespace a11y {
class GameplayHooks {
  public:
    using Function = void (*)(void *, uint8_t *);
    static constexpr std::array<const char *, 37> function_names{
        "RequestInputJudge",
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
        "OnCloseAnimationComplete",
        "AimShotAimingCtrl",
        "EndSynchronic",
        "PlayButtonEventPress_LockOn",
        "SearchSetAimShotHomingTargetActor"};
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
    void options(bool qtes, float pointBlank, float maxRange, bool statusButton = true) {
        qtes_ = qtes;
        point_blank_ = pointBlank;
        max_range_ = maxRange;
        status_button_ = statusButton;
    }
    void lock_diagnostics(bool enabled) {
        trace_.configure(enabled);
    }
    void tracking_focus(std::vector<AimIdentity> focus) {
        std::lock_guard lock(tracking_focus_mutex_);
        tracking_focus_ = std::move(focus);
    }
    void tracking(float strength) {
        tracking_strength_ = std::isfinite(strength) ? std::clamp(strength, 0.f, 1.f) : 0.f;
    }
    uint64_t tracking_nudges() const {
        return tracking_nudges_.load();
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
    bool boosted() const {
        return mode_.load() == ShootingMode::boosted &&
               (point_blank_.load() > 1 || max_range_.load() > 1);
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
        if (!enabled) {
            trace_.stop();
            restore_homing();
        }
        enabled_ = enabled;
    }
    void aim_priority(AimPriority priority) {
        priority_ = priority;
    }
    AimPriority aim_priority() const {
        return priority_.load();
    }
    // Boosted mode's melee homing-angle multiplier (1-10).
    void homing_boost(float multiplier) {
        homing_boost_ = std::isfinite(multiplier) ? std::clamp(multiplier, 1.f, 10.f) : 1.f;
    }
    bool boosted_melee() const {
        return mode_.load() == ShootingMode::boosted && homing_boost_.load() > 1;
    }
    uint64_t lock_on_skips() const {
        return lock_on_skips_.load();
    }
    uint64_t homing_calls() const {
        return homing_calls_.load();
    }
    uint64_t homing_hits() const {
        return homing_hits_.load();
    }
    // 1 patched to the widened angle, 0 original, -1 unrecognized code (no patch).
    int homing_state() const {
        std::lock_guard lock(homing_mutex_);
        return homing_.failed ? -1 : homing_.address && homing_.patched ? 1 : 0;
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
        aim_control,
        sync_grade,
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
        level_reset,
        lock_on_press,
        homing
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
        // Tracking runs on the game thread from the previous shooting tick's copy.
        AimSnapshot tracking;
        AimIdentity tracking_target;
        bool tracking_hold{};
        struct Blocked {
            uint32_t index{};
            uint64_t until{};
        };
        std::map<uint32_t, Blocked> blocked;
        std::optional<StickNudge> tracking_choice;
        uint64_t last_guide{};
        std::map<std::pair<uint32_t, std::string>, bool> classes;
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
        if (function == "AimShotAimingCtrl")
            route = Route::aim_control;
        else if (cls == "BCGFxUI_Synchronic" && function == "EndSynchronic")
            route = Route::sync_grade;
        else if (script_scene_protocol(cls) && function == "CallStart")
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
        else if (cls == "BCPlayerController" && function == "PlayButtonEventPress_LockOn")
            route = Route::lock_on_press;
        else if (cls == "BCPawn" && function == "SearchSetAimShotHomingTargetActor")
            route = Route::homing;
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
                bool reset = false, std::optional<float> now = {}) {
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
        if (!capture_timing(memory, source, track, window, value, now)) {
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
            // Guidance data is captured in every mode: L1 tracking works in all of
            // them, and the beacon also needs it in beacons and boosted modes.
            if (!subclass(memory, state.reader, source, "BCPlayerController_HeavyShoot") ||
                (state.locks && state.locks->pc == source)) {
                original_[0](context, base);
                // Melee rapid fire uses the same cursor (AimShotAimPoint) and cursor
                // update, so it feeds the same beacon and tracking pipeline.
                if (!(state.locks && state.locks->pc == source) &&
                    melee_rapid_fire(memory, source) &&
                    subclass(memory, state.reader, source, "BCPlayerController")) {
                    auto snapshot = capture_melee_aim(
                        memory, source, GetTickCount64(), priority_.load(),
                        melee_homing_degrees(boosted_melee() ? homing_boost_.load() : 1.f),
                        [&](uint32_t object, std::string_view cls) {
                            return cached_subclass(memory, state, object, cls);
                        });
                    snapshot.active = snapshot.active && enabled_.load();
                    aim_mailbox_.put(snapshot);
                    state.tracking = snapshot;
                    SetEvent(event_);
                }
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
            {
                if (!frame.snapshot.tick)
                    frame.snapshot.tick = GetTickCount64();
                // Targets the game declined to lock inside its angle limit are treated
                // as unreachable until the block expires.
                const auto now = GetTickCount64();
                std::erase_if(state.blocked,
                              [&](const auto &item) { return item.second.until <= now; });
                for (unsigned i = 0; i < frame.snapshot.count; ++i) {
                    auto &target = frame.snapshot.targets[i];
                    auto it = state.blocked.find(target.id.pointer);
                    if (it != state.blocked.end() && it->second.index == target.id.index)
                        target.reachable = false;
                }
                if (frame.tracing && frame.snapshot.valid && frame.snapshot.count &&
                    now - state.last_guide >= 250) {
                    state.last_guide = now;
                    auto saved = state.locks;
                    state.locks = &frame;
                    auto record = lock_record(memory, state, LockTraceKind::guide);
                    state.locks = saved;
                    auto &g = record.guide;
                    g.candidates = frame.snapshot.count;
                    const auto camera = lock_camera(memory, source);
                    auto right = aim_unit({-camera.forward.y, camera.forward.x, 0});
                    auto up =
                        right ? std::optional(aim_cross(*right, camera.forward)) : std::nullopt;
                    for (unsigned i = 0; i < frame.snapshot.count; ++i) {
                        const auto &target = frame.snapshot.targets[i];
                        if (target.locked)
                            continue;
                        ++g.unlocked;
                        if (!target.reachable) {
                            ++g.unreachable;
                            continue;
                        }
                        if (!camera.valid || !right || !up)
                            continue;
                        auto forward = aim_dot(target.direction, camera.forward);
                        auto yaw =
                            std::atan2(aim_dot(target.direction, *right), forward) * 57.29578f;
                        auto pitch =
                            std::atan2(aim_dot(target.direction, *up), forward) * 57.29578f;
                        (yaw < -5 ? g.left : yaw > 5 ? g.right : g.centre)++;
                        if (pitch > 5)
                            ++g.above;
                        else if (pitch < -5)
                            ++g.below;
                    }
                    g.tracking_hold = state.tracking_hold;
                    if (state.tracking_choice) {
                        g.tracking_target = state.tracking_choice->target.pointer;
                        g.tracking_target_index = state.tracking_choice->target.index;
                        g.tracking_focus = state.tracking_choice->focus;
                        g.tracking_yaw = state.tracking_choice->yaw;
                        g.tracking_pitch = state.tracking_choice->pitch;
                    }
                    trace_.push(record, now);
                }
                frame.snapshot.active =
                    frame.snapshot.active && enabled_.load() &&
                    aim_context_active(memory, frame.snapshot.controller, frame.snapshot.pawn);
                aim_mailbox_.put(frame.snapshot);
                state.tracking = frame.snapshot;
                SetEvent(event_);
            }
            if (trace_.pending())
                SetEvent(event_);
            return true;
        }
        if (route == Route::aim) {
            if (state.locks && state.locks->pc == source && state.locks->active &&
                !state.locks->captured && state.locks->search_locals &&
                position_mismatches_.load() == 0 && base_camera_view(memory, state, source)) {
                state.locks->captured = true;
                state.locks->snapshot = capture_aim(memory, source, state.locks->search_locals,
                                                    fired_.load(), GetTickCount64());
            }
            if (state.locks && state.locks->active && !state.locks->tracing && trace_.armed()) {
                state.locks->tracing = trace_.begin(GetTickCount64());
                trace_record(lock_record(memory, state, LockTraceKind::frame), state);
            }
            // Every mode measures candidates: tracking needs the game's verdicts
            // (declined targets) even when no cone is changed.
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
            bool boost = boosted();
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
            if (boost) {
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
                        boost = false;
                        record.view_gated = true;
                    } else if (!margin) {
                        // Unknown candidate geometry is not evidence for a target gate.
                        cap = stable_lock_radius(camera, origin, aim);
                        if (!cap)
                            boost = false;
                    }
                } else {
                    // Until the position model has agreed with the native angle (or if it
                    // ever disagrees), fall back to the conservative aim/camera bound.
                    if (geometry)
                        cap = stable_lock_radius(lock_camera(memory, source), origin, aim);
                    if (!cap)
                        boost = false;
                }
            }
            record.cap_applied = cap.has_value() && boost;
            AcquisitionScope scope(
                memory,
                [&](uint32_t at, const void *bytes, size_t n) {
                    SIZE_T wrote{};
                    return WriteProcessMemory(GetCurrentProcess(), base + at, bytes, n, &wrote) &&
                           wrote == n;
                },
                source, boost ? point_blank_.load() : 1.f, boost ? max_range_.load() : 1.f, cap);
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
                // CheckLockOnShotTarget also requires LockOnShotLockDistance and a
                // line-of-sight trace that first hits the target's owner. A candidate
                // inside the angle limit that the game still did not accept failed one
                // of those, so guidance and tracking skip it for a while instead of
                // holding the cursor on a target that cannot lock.
                if (record.target && !record.accepted && record.angle_valid &&
                    std::isfinite(record.limit) && record.angle < record.limit)
                    state.blocked[record.target] = {record.target_index,
                                                    GetTickCount64() + kBlockedMs};
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
            state.tracking = {};
            state.tracking_target = {};
            state.tracking_hold = false;
            state.blocked.clear();
            state.tracking_choice.reset();
            state.last_guide = 0;
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
        if (route == Route::aim_control) {
            // While L1 is held, steer the game's own cursor update toward a target,
            // then put the player's real stick values back. The game applies its
            // cursor speed, screen frame and camera-turn limits. Any real stick
            // movement takes priority and suspends steering for that tick.
            std::optional<StickNudge> nudge;
            const auto &shot = state.tracking;
            if (state.tracking_hold && self.enabled_.load() && epoch == self.epoch_.load() &&
                shot.controller.pointer == source &&
                shot.controller.index == memory.u32(source + 0x20) &&
                GetTickCount64() - shot.tick <= 200 &&
                aim_context_active(memory, shot.controller, shot.pawn)) {
                std::vector<AimIdentity> focus;
                {
                    std::lock_guard lock(self.tracking_focus_mutex_);
                    focus = self.tracking_focus_;
                }
                nudge = tracking_nudge(shot, state.tracking_target, self.tracking_strength_.load(),
                                       focus);
            }
            state.tracking_target = nudge ? nudge->target : AimIdentity{};
            state.tracking_choice = nudge;
            auto input = memory.u32(source + 0x454);
            const uint32_t upAt = input + 0x13c, rightAt = input + 0x140;
            auto readFloat = [&](uint32_t at) { return std::bit_cast<float>(memory.u32(at)); };
            auto writeFloat = [&](uint32_t at, float value) {
                auto bits = std::bit_cast<uint32_t>(value);
                std::array<uint8_t, 4> bytes{uint8_t(bits >> 24), uint8_t(bits >> 16),
                                             uint8_t(bits >> 8), uint8_t(bits)};
                SIZE_T wrote{};
                return WriteProcessMemory(GetCurrentProcess(), base + at, bytes.data(), 4,
                                          &wrote) &&
                       wrote == 4;
            };
            float up = 0, right = 0, steeredUp = 0, steeredRight = 0;
            bool steered = false;
            if (nudge && (nudge->up || nudge->right) && input && input < UINT32_MAX - 0x144) {
                up = readFloat(upAt);
                right = readFloat(rightAt);
                if (std::isfinite(up) && std::isfinite(right) &&
                    std::max(std::abs(up), std::abs(right)) < kStickPriority) {
                    steeredUp = nudge->up;
                    steeredRight = nudge->right;
                    steered = writeFloat(upAt, steeredUp) && writeFloat(rightAt, steeredRight);
                    if (steered)
                        ++self.tracking_nudges_;
                }
            }
            self.original_[0](context, base);
            // Restore only values the game did not change during the call.
            if (steered) {
                if (readFloat(upAt) == steeredUp)
                    writeFloat(upAt, up);
                if (readFloat(rightAt) == steeredRight)
                    writeFloat(rightAt, right);
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
            // L1 (row 7) has no rail-shooting action; holding it enables tracking.
            // Only its state is read here: the game's own L1 handling still runs.
            bool trackingHold = false;
            if (valid) {
                std::array<uint8_t, 0x24> row{};
                trackingHold = memory.copy(array + 7 * row.size(), row.data(), row.size()) &&
                               state.reader.name(memory, cue_word(row.data())) == "L1" &&
                               (cue_word(row.data() + 0x18) & 0x80000000);
            }
            state.tracking_hold = trackingHold;
            // Commands 1-4: previous/next target, shooting mode, melee aim priority.
            constexpr std::array<unsigned, 4> rows{11, 13, 10, 12};
            constexpr std::array<std::string_view, 4> names{"DPAD_LEFT", "DPAD_RIGHT", "DPAD_UP",
                                                            "DPAD_DOWN"};
            for (unsigned i = 0; valid && i < rows.size(); ++i) {
                std::array<uint8_t, 0x24> row{};
                bool named = memory.copy(array + rows[i] * row.size(), row.data(), row.size()) &&
                             state.reader.name(memory, cue_word(row.data())) == names[i];
                // The priority switch (D-pad Down) is optional; the rail commands
                // never depend on it.
                if (!named) {
                    valid = i == 3;
                    break;
                }
                if (cue_word(row.data() + 0x18) & 0x80000000)
                    held |= uint8_t(1 << i);
            }
            if (state.input_source != source || state.input_index != index) {
                state.input_source = source;
                state.input_index = index;
                state.input_held = 0;
            }
            auto pressed = valid ? uint8_t(held & ~state.input_held) : uint8_t(0);
            state.input_held = valid ? held : uint8_t(15);
            auto before = pressed ? capture_status_context(memory, source, false) : std::nullopt;
            // Rail shooting, or melee rapid fire (where only the priority switch is
            // new; the game's D-pad handlers are empty in both).
            bool rail = before && subclass(memory, state.reader, before->controller,
                                           "BCPlayerController_HeavyShoot");
            bool melee = before && !rail && melee_rapid_fire(memory, before->controller);
            bool active = rail || melee;
            if (!melee)
                pressed &= uint8_t(7);
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
        if (route == Route::lock_on_press) {
            // During melee rapid fire, holding L1 tracks (as in rail shooting), so the
            // game's lock-on press is skipped then. It is a void script function
            // with no out parameters; skipping its body changes nothing else.
            // Lock-on outside rapid fire, and a lock made before firing, are unchanged.
            if (melee_rapid_fire(memory, source)) {
                ++self.lock_on_skips_;
                return;
            }
            self.original_[0](context, base);
            return;
        }
        if (route == Route::homing) {
            self.homing_window(memory, base, node);
            auto result = ui_argument(context, 5);
            self.original_[0](context, base);
            ++self.homing_calls_;
            if (result && memory.u32(result))
                ++self.homing_hits_;
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
        if (route == Route::sync_grade) {
            // Both a press (ShowButton) and a timeout (SetResult) end the key here.
            // Result 1-3 shows GOOD/GREAT/EXCELLENT (SynchronicRankC/B/A); a miss
            // (0) shows no text.
            uint8_t result = 0;
            const bool read = locals && memory.copy(locals + 0xc, &result, 1);
            self.original_[0](context, base);
            if (read && result >= 1 && result <= 3 && self.enabled_.load() &&
                epoch == self.epoch_.load()) {
                GameplayEvent event;
                event.sync_grade = result;
                self.publish(event);
            }
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
                 route == Route::timing_end) {
            // UpdateNormal/UpdateBarrage(Idx, NowTime): NowTime is the track clock.
            std::optional<float> now;
            if (route == Route::timing_update && locals)
                now = std::bit_cast<float>(memory.u32(locals + 4));
            self.timing(memory, state, source, track, scope.window, route == Route::timing_start,
                        now);
        } else if (route == Route::animation_cross || route == Route::animation_mash ||
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
    // Class-chain test cached per class object; melee capture asks it for every
    // projectile and pawn on each rapid-fire tick.
    static bool cached_subclass(const Memory &memory, Thread &state, uint32_t object,
                                std::string_view name) {
        auto cls = memory.u32(object + 0x34);
        if (!cls)
            return false;
        auto key = std::pair{cls, std::string(name)};
        if (auto it = state.classes.find(key); it != state.classes.end())
            return it->second;
        if (state.classes.size() > 4096)
            state.classes.clear();
        return state.classes[key] = subclass(memory, state.reader, object, name);
    }
    // Boosted melee widens SearchSetAimShotHomingTargetActor's acceptance constant
    // (1 - dot < 0.001). It is a float constant in the function's own bytecode
    // (UStruct.Script at +0x4c): token EX_FloatConst (0x1e) at offset 0x388,
    // big-endian float, then EX_EndFunctionParms (0x16). The bytes are checked
    // before any write; unrecognized code is left alone. The original value is
    // written back when boosted mode ends or the mod stops.
    struct HomingPatch {
        uint8_t *base{};
        uint32_t address{}, written{};
        bool patched{}, failed{};
    };
    static constexpr uint32_t kHomingScriptSize = 1220, kHomingConstOffset = 0x388,
                              kHomingOriginal = 0x3a83126f; // 0.001f
    bool write_word(uint8_t *base, uint32_t at, uint32_t value) {
        std::array<uint8_t, 4> bytes{uint8_t(value >> 24), uint8_t(value >> 16),
                                     uint8_t(value >> 8), uint8_t(value)};
        SIZE_T wrote{};
        return WriteProcessMemory(GetCurrentProcess(), base + at, bytes.data(), 4, &wrote) &&
               wrote == 4;
    }
    void homing_window(const Memory &memory, uint8_t *base, uint32_t function) {
        std::lock_guard lock(homing_mutex_);
        if (homing_.failed)
            return;
        if (!homing_.address) {
            auto script = memory.u32(function + 0x4c), size = memory.u32(function + 0x50);
            std::array<uint8_t, 7> code{};
            if (!script || size != kHomingScriptSize ||
                !memory.copy(script + kHomingConstOffset - 1, code.data(), code.size()) ||
                code[0] != 0x16 || code[1] != 0x1e ||
                cue_word(code.data() + 2) != kHomingOriginal || code[6] != 0x16) {
                homing_.failed = true;
                return;
            }
            homing_.base = base;
            homing_.address = script + kHomingConstOffset + 1;
        }
        auto desired = boosted_melee()
                           ? std::bit_cast<uint32_t>(melee_homing_threshold(homing_boost_.load()))
                           : kHomingOriginal;
        auto current = memory.u32(homing_.address);
        if (current == desired || (current != kHomingOriginal && current != homing_.written))
            return;
        if (write_word(base, homing_.address, desired)) {
            homing_.written = desired;
            homing_.patched = desired != kHomingOriginal;
        }
    }
    void restore_homing() {
        std::lock_guard lock(homing_mutex_);
        if (!homing_.address || !homing_.patched || !homing_.base)
            return;
        Memory memory(Read{homing_.base});
        if (memory.u32(homing_.address) == homing_.written &&
            write_word(homing_.base, homing_.address, kHomingOriginal)) {
            homing_.written = kHomingOriginal;
            homing_.patched = false;
        }
    }
    mutable std::mutex homing_mutex_;
    HomingPatch homing_;
    std::atomic<AimPriority> priority_{AimPriority::projectiles};
    std::atomic<float> homing_boost_{3.f};
    std::atomic<uint64_t> lock_on_skips_{}, homing_calls_{}, homing_hits_{};
    NativeHooks hooks_;
    const BuildProfile *profile_{};
    std::array<Function, 3> original_{};
    std::array<std::atomic<uint32_t>, function_names.size()> indices_{};
    HANDLE event_{};
    uint32_t names_global_ = disc_profile.layout.names_global;
    bool installed_{};
    std::atomic<bool> configured_{}, enabled_{}, qtes_{true}, status_button_{true};
    std::atomic<float> point_blank_{1}, max_range_{1}, tracking_strength_{1.f};
    std::atomic<uint64_t> tracking_nudges_{};
    std::mutex tracking_focus_mutex_;
    std::vector<AimIdentity> tracking_focus_;
    std::atomic<uint64_t> epoch_{}, captured_{}, failures_{}, widenings_{}, generation_{};
    GameplayQueue queue_;
    LockTrace trace_;
    static constexpr float kLockViewReserve = .5f;
    // Stick deflection beyond this is the player aiming; tracking yields to it.
    static constexpr float kStickPriority = .2f;
    // How long a target the game declined to lock is skipped by guidance.
    static constexpr uint64_t kBlockedMs = 1500;
    std::atomic<uint64_t> position_matches_{}, position_mismatches_{};
    std::atomic<ShootingMode> mode_{ShootingMode::boosted};
    AimMailbox aim_mailbox_;
    std::atomic<uint64_t> fired_{};
    std::atomic<uint64_t> lock_frames_{};
};
}
