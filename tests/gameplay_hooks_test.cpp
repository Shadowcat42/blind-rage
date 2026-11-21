#include "../src/gameplay_hooks.h"
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool pass, const char *message) {
    if (!pass)
        throw std::runtime_error(message);
}
static unsigned calls{};
static bool driveWindow{}, windowResult = true, judgeAfterInput{}, aimObserved{}, changeAim{},
                           enterCinematic{};
static std::function<void(void *, uint8_t *)> traceOriginal;
static void word(uint8_t *base, uint32_t at, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        base[at + i] = uint8_t(value >> (24 - 8 * i));
}
extern "C" __declspec(noinline) void window_target(void *context, uint8_t *base) {
    word(base, ui_argument(context, 5), windowResult ? 1 : 0);
    static_cast<uint64_t *>(context)[0] = 0x999;
}
static GameplayHooks::Function volatile call_window = window_target;
extern "C" __declspec(noinline) void script_target(void *context, uint8_t *base) {
    ++calls;
    auto source = ui_argument(context, 3);
    if (traceOriginal) {
        traceOriginal(context, base);
        static_cast<uint64_t *>(context)[0] = 0xfeed;
        return;
    }
    if (driveWindow) {
        call_window(context, base);
        if (judgeAfterInput)
            word(base, 0x18000 + 20, 0xc0000000);
    } else {
        aimObserved = std::bit_cast<float>(cue_word(base + source + 0x7d4)) == 45.f;
        if (changeAim)
            word(base, source + 0x7d4, std::bit_cast<uint32_t>(60.f));
    }
    if (enterCinematic)
        word(base, 0x3000 + 0x378, 0x8000);
    static_cast<uint64_t *>(context)[0] = 0xfeed;
}
// Simulates ProcessEvent's early return for an event with no defined script body.
// It deliberately never invokes script_target/ProcessInternal.
extern "C" __declspec(noinline) void movie_event_target(void *context, uint8_t *) {
    ++calls;
    static_cast<uint64_t *>(context)[0] = 0xfeed;
}
int main() {
    try {
        auto *base = static_cast<uint8_t *>(
            VirtualAlloc(nullptr, 0x100000000ull, MEM_RESERVE, PAGE_NOACCESS));
        check(base, "reserve synthetic guest address space");
        check(VirtualAlloc(base + 0x1000, 0x20000, MEM_COMMIT, PAGE_READWRITE),
              "commit synthetic data");
        check(VirtualAlloc(base + 0x832a9000, 0x2000, MEM_COMMIT, PAGE_READWRITE),
              "commit synthetic globals");
        const uint32_t names = 0x10000, entries = 0x11000, node = 0x8000, owner = 0x9000,
                       frame = 0xa000, locals = 0xb000, actor = 0x1000, pc = 0x3000, judge = 0x4000,
                       row = 0x5000, requests = 0x6000, buttons = 0x7000, info = 0xc000,
                       track = 0x2000;
        word(base, disc_profile.layout.names_global, names);
        word(base, disc_profile.layout.names_global + 4, 64);
        auto name = [&](unsigned index, const char *text) {
            word(base, names + index * 4, entries + index * 0x80);
            std::memcpy(base + entries + index * 0x80 + 8, text, std::strlen(text) + 1);
        };
        name(0, "None");
        name(1, "ByteProperty");
        name(2, "BCSeqAct_InputDivergence");
        name(3, "BCPlayerController_HeavyShoot");
        name(4, "BCInterpTrackSynchronic");
        name(5, "BCSeqAct_AnimFadeByButtonRep");
        std::array<uint32_t, GameplayHooks::function_names.size()> indices{};
        for (unsigned i = 0; i < indices.size(); ++i) {
            indices[i] = i + 14;
            name(i + 14, GameplayHooks::function_names[i]);
        }
        word(base, node + 0x28, owner);
        word(base, node + 0x2c, indices[0]);
        word(base, owner + 0x2c, 2);
        word(base, actor + 0x34, owner);
        word(base, actor + 0x20, 7);
        word(base, frame + 0x10, node);
        word(base, frame + 0x14, actor);
        word(base, frame + 0x1c, locals);
        word(base, locals, pc);
        word(base, actor + 0xc4, 0x18800000);
        word(base, actor + 0xe4, 9);
        word(base, actor + 0xf0, requests);
        word(base, actor + 0xf4, 2);
        word(base, actor + 0xf8, 2);
        base[actor + 0x114] = 3;
        word(base, actor + 0x118, 1);
        word(base, pc + 0x760, judge);
        word(base, judge + 0x20, 8);
        word(base, judge + 0x1c4, row);
        word(base, judge + 0x1c8, 1);
        word(base, judge + 0x1cc, 1);
        word(base, row, buttons);
        word(base, row + 4, 2);
        word(base, row + 8, 2);
        base[row + 0xc] = 3;
        word(base, row + 0x10, 1);
        word(base, row + 0x20, info);
        word(base, row + 0x24, 2);
        word(base, row + 0x28, 2);
        word(base, row + 0x48, 0x80000000);
        word(base, row + 0x50, 9);
        for (unsigned i = 0; i < 2; ++i) {
            base[buttons + i] = base[requests + i * 0x30] = uint8_t(17 + i);
            word(base, requests + i * 0x30 + 0x24, 10 + i);
            word(base, requests + i * 0x30 + 0x2c, 0x80000000);
        }
        word(base, info, 0x40000000);
        auto &source = GameplayHooks::instance();
        check(source.install_targets({reinterpret_cast<void *>(script_target),
                                      reinterpret_cast<void *>(window_target),
                                      reinterpret_cast<void *>(movie_event_target)}),
              "install native gameplay detours");
        source.configure_indices(indices);
        source.options(true, 45);
        source.capture(true);
        GameplayHooks::Function volatile call_script = script_target;
        std::array<uint64_t, 40> context{};
        context[0] = actor;
        context[4] = frame;
        call_script(context.data(), base);
        bool overflow = false;
        auto events = source.take(overflow);
        check(!overflow && events.size() == 1 && events[0].group.active &&
                  events[0].group.count == 2 && context[0] == 0xfeed && calls == 1,
              "script-return snapshot preserves original behavior and grouped commands");
        word(base, node + 0x2c, indices[2]);
        context[0] = actor;
        call_script(context.data(), base);
        check(source.take(overflow).empty(), "unchanged snapshots do not wake the worker");
        ++button_revision;
        context[0] = actor;
        call_script(context.data(), base);
        check(source.take(overflow).size() == 1,
              "new presentation revision resynchronizes semantic snapshot");
        word(base, node + 0x2c, indices[0]);
        context[0] = actor;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(events.size() == 1 && events[0].group.generation > 1,
              "new request increments lifecycle despite reused identifier/tag");
        word(base, node + 0x2c, indices[8]);
        word(base, owner + 0x2c, 3);
        word(base, actor + 0x7d4, std::bit_cast<uint32_t>(3.f));
        word(base, actor + 0x7d8, std::bit_cast<uint32_t>(8.f));
        context[0] = actor;
        call_script(context.data(), base);
        check(aimObserved && std::bit_cast<float>(cue_word(base + actor + 0x7d4)) == 3.f &&
                  std::bit_cast<float>(cue_word(base + actor + 0x7d8)) == 8.f &&
                  source.widenings() == 1,
              "native angular parameters widen only while the original test executes");
        changeAim = true;
        context[0] = actor;
        call_script(context.data(), base);
        check(std::bit_cast<float>(cue_word(base + actor + 0x7d4)) == 60.f &&
                  std::bit_cast<float>(cue_word(base + actor + 0x7d8)) == 8.f,
              "scoped restoration preserves a game-side parameter change");
        changeAim = false;
        source.options(true, 0);
        aimObserved = false;
        context[0] = actor;
        call_script(context.data(), base);
        check(!aimObserved && source.widenings() == 2,
              "disabled shooting aid does not modify the game");
        word(base, node + 0x2c, indices[10]);
        word(base, owner + 0x2c, 4);
        word(base, frame + 0x14, track);
        word(base, track + 0x20, 12);
        word(base, track + 0x2c, 13);
        word(base, track + 0x8c, 0x18000);
        word(base, track + 0x90, 1);
        word(base, track + 0x94, 1);
        word(base, 0x18000 + 20, 0x40000000);
        word(base, locals, 0);
        word(base, locals + 4, std::bit_cast<uint32_t>(1.f));
        driveWindow = true;
        context[0] = track;
        context[5] = 0x19000;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(events.size() == 1 && events[0].timing && events[0].sync.active &&
                  events[0].sync.window && context[0] == 0xfeed,
              "timing cue follows the actual native input predicate result");
        windowResult = false;
        context[0] = track;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(events.size() == 1 && !events[0].sync.window && events[0].sync.active,
              "native window closure is not guessed from ring percentage");
        windowResult = true;
        judgeAfterInput = true;
        context[0] = track;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(events.size() == 1 && !events[0].sync.active && !events[0].sync.window,
              "button consumed in the same game call invalidates its unsent timing instruction");
        driveWindow = false;
        judgeAfterInput = false;
        word(base, node + 0x2c, indices[3]);
        word(base, owner + 0x2c, 5);
        word(base, frame + 0x14, actor);
        word(base, actor + 0x11c, 0x20000000);
        base[actor + 0xc4] = 30;
        word(base, actor + 0xe8, 10);
        word(base, actor + 0xec, 0);
        word(base, actor + 0xf0, 0);
        context[0] = actor;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(events.size() == 1 && !events[0].timing && events[0].group.active &&
                  events[0].group.mode == 1,
              "animation-driven mash uses shared hook without a BCInputJudgeSystem request");
        word(base, actor + 0x11c, 0x60000000);
        word(base, node + 0x2c, indices[4]);
        context[0] = actor;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(events.size() == 1 && !events[0].group.active,
              "animation success invalidates its instruction");
        {
            CueTransaction outer;
            word(base, actor + 0x11c, 0x20000000);
            context[0] = actor;
            call_script(context.data(), base);
            word(base, actor + 0x11c, 0x60000000);
            context[0] = actor;
            call_script(context.data(), base);
            check(source.take(overflow).empty(),
                  "nested action does not expose an intermediate actionable QTE");
        }
        events = source.take(overflow);
        check(events.size() == 2 && events[0].group.active && !events[1].group.active,
              "the complete action lifecycle is committed as one batch");
        auto captures = source.captured();
        auto originalCalls = calls;
        word(base, node + 0x2c, 0);
        for (unsigned i = 0; i < 1000; ++i) {
            context[0] = actor;
            call_script(context.data(), base);
        }
        check(calls == originalCalls + 1000 && source.captured() == captures &&
                  source.take(overflow).empty(),
              "unrelated script calls retain original execution without cue traffic");
        // Real Back/Select dispatch: one native press in gameplay, never consume or
        // replace the game's handler, including its cutscene and debug-camera paths.
        const uint32_t input = 0xd000, pawn = 0xe000, hud = 0xf000, world = 0x1a000,
                       manager = 0x1b000, gauge = 0x1c000;
        name(6, "BCPlayerInput");
        name(7, "BCDebugCameraInput");
        word(base, owner + 0x2c, 6);
        word(base, node + 0x2c, indices[14]);
        word(base, frame + 0x14, input);
        word(base, locals, 0);
        word(base, input + 0x20, 41);
        word(base, input + 0x28, pc);
        word(base, pc + 0x20, 40);
        word(base, pc + 0x5c4, pawn);
        word(base, pawn + 0x1e4, pc);
        word(base, pc + 0x378, 0);
        word(base, pc + 0xd4, world);
        word(base, pc + 0x3c8, hud);
        word(base, hud + 0x478, manager);
        word(base, hud + 0x488, gauge);
        word(base, gauge + 0x20, 42);
        word(base, gauge + 0xc8, 0x80000000);
        word(base, gauge + 0x1e0, 0x80000000);
        source.options(false, 0);
        context[0] = input;
        originalCalls = calls;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(
            calls == originalCalls + 1 && context[0] == 0xfeed && events.size() == 1 &&
                events[0].status_request && events[0].status.controller == pc &&
                events[0].status.gauge == gauge,
            "Back requests status independently of QTE option and preserves original handler return");
        auto capturedStatus = events[0].status;
        Memory memory([&](uint32_t at, void *out, size_t size) {
            if (uint64_t(at) + size > 0x21000)
                return false;
            std::memcpy(out, base + at, size);
            return true;
        });
        check(capture_status_context(memory, input) == capturedStatus,
              "delivery revalidates the captured gameplay context");
        word(base, gauge + 0x20, 43);
        check(capture_status_context(memory, input) != capturedStatus,
              "reused gauge cannot authorize an old status request");
        word(base, gauge + 0x20, 42);
        word(base, gauge + 0x1e0, 0);
        context[0] = input;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(events.size() == 1 && events[0].status_request,
              "hidden gauge display must not disable gameplay status button");
        for (const auto &guard :
             std::array<std::array<uint32_t, 3>, 6>{{{pc + 0x378, 0x8000, 0},
                                                     {world + 0x368, 1, 0},
                                                     {hud + 0x44c, 0x10000000, 0},
                                                     {manager + 0x98, 1, 0},
                                                     {gauge + 0xc8, 0, 0x80000000},
                                                     {pawn + 0x1e4, 0, pc}}}) {
            word(base, guard[0], guard[1]);
            context[0] = input;
            originalCalls = calls;
            call_script(context.data(), base);
            check(
                calls == originalCalls + 1 && source.take(overflow).empty(),
                "cinematic/paused/menu/closed/no-pawn Back always reaches original without status");
            word(base, guard[0], guard[2]);
        }
        base[locals] = 1;
        context[0] = input;
        call_script(context.data(), base);
        check(source.take(overflow).empty(), "Back release/held event does not repeat status");
        base[locals] = 0;
        enterCinematic = true;
        context[0] = input;
        call_script(context.data(), base);
        check(source.take(overflow).empty(),
              "transition into a cinematic inside the original handler cancels status");
        enterCinematic = false;
        word(base, pc + 0x378, 0);
        source.options(false, 0, false);
        context[0] = input;
        call_script(context.data(), base);
        check(source.take(overflow).empty(),
              "contextual status button can be disabled without changing native input");
        source.options(false, 0, true);
        word(base, owner + 0x2c, 7);
        context[0] = input;
        call_script(context.data(), base);
        check(source.take(overflow).empty(), "debug camera Back is not a gameplay status shortcut");
        Memory unreadable([](uint32_t, void *, size_t) { return false; });
        check(!capture_status_context(unreadable, input), "unreadable context fails closed");
        name(8, "GFxMoviePlayer");
        name(9, "BCGFxUI_ResultMenu");
        name(10, "XboxTypeS_X");
        name(11, "XboxTypeS_Y");
        word(base, owner + 0x2c, 8);
        word(base, node + 0x2c, indices[15]);
        word(base, actor + 0x34, 0x1d000);
        word(base, 0x1d000 + 0x2c, 9);
        word(base, 0x1d000 + 0x40, 0x1d100);
        word(base, 0x1d100 + 0x2c, 8);
        word(base, actor + 0xc8, 0x80000000);
        word(base, frame + 0x14, actor);
        word(base, locals + 4, 10);
        word(base, locals + 8, 0);
        base[locals + 0xc] = 0;
        GameplayHooks::Function volatile call_movie_event = movie_event_target;
        context[4] = node;
        context[5] = locals;
        context[0] = actor;
        originalCalls = calls;
        call_movie_event(context.data(), base);
        events = source.take(overflow);
        check(calls == originalCalls + 1 && context[0] == 0xfeed && events.size() == 1 &&
                  events[0].menu_repeat && events[0].repeat_movie == actor &&
                  events[0].repeat_index == 7,
              "Square/X repeats results even when ProcessEvent skips the undefined script body");
        for (uint8_t inputEvent : {uint8_t(1), uint8_t(2), uint8_t(3)}) {
            base[locals + 0xc] = inputEvent;
            context[0] = actor;
            call_movie_event(context.data(), base);
            check(source.take(overflow).empty(),
                  "release/repeat/double-click do not flood result narration");
        }
        base[locals + 0xc] = 0;
        word(base, locals + 4, 11);
        context[0] = actor;
        call_movie_event(context.data(), base);
        check(source.take(overflow).empty(), "other face buttons do not repeat results");
        word(base, locals + 4, 10);
        word(base, actor + 0xc8, 0);
        context[0] = actor;
        call_movie_event(context.data(), base);
        check(source.take(overflow).empty(), "closed results movie cannot request a repeat");
        word(base, actor + 0xc8, 0x80000000);
        word(base, actor + 0x34, 0x1d100);
        context[0] = actor;
        call_movie_event(context.data(), base);
        check(source.take(overflow).empty(), "Square/X outside results remains native input only");
        name(54, "BCGFxUI_ChapterMenu");
        word(base, 0x1d400 + 0x2c, 54);
        word(base, 0x1d400 + 0x40, 0x1d100);
        word(base, actor + 0x34, 0x1d400);
        context[0] = actor;
        originalCalls = calls;
        call_movie_event(context.data(), base);
        events = source.take(overflow);
        check(calls == originalCalls + 1 && context[0] == 0xfeed && events.size() == 1 &&
                  events[0].menu_repeat && events[0].repeat_movie == actor,
              "episode intro repeat shares the input observer and preserves native input");
        constexpr std::array<const char *, 4> recordKeys{
            "Gamepad_RightStick_Left", "Gamepad_RightStick_Right", "Gamepad_RightStick_Up",
            "Gamepad_RightStick_Down"};
        for (unsigned i = 0; i < recordKeys.size(); ++i) {
            auto recordKeyIndex = i < 2 ? 48 + i : 55 + i;
            name(recordKeyIndex, recordKeys[i]);
            word(base, locals + 4, recordKeyIndex);
            context[0] = actor;
            call_movie_event(context.data(), base);
            events = source.take(overflow);
            check(
                events.size() == 1 && events[0].record_command == int(i + 1) &&
                    events[0].record_movie == actor && !events[0].menu_repeat,
                "chapter right-stick press publishes a record browse command without consuming input");
            base[locals + 0xc] = 1;
            context[0] = actor;
            call_movie_event(context.data(), base);
            check(source.take(overflow).empty(), "right-stick release cannot browse records");
            base[locals + 0xc] = 0;
        }
        word(base, locals + 4, 10);
        // Record ranks are sent as text before Flash selects its rank timeline.
        word(base, owner + 0x2c, 54);
        word(base, node + 0x2c, indices[27]);
        auto fstring = [&](uint32_t at, uint32_t bytes, std::string_view value) {
            word(base, at, bytes);
            word(base, at + 4, uint32_t(value.size() + 1));
            word(base, at + 8, uint32_t(value.size() + 1));
            std::memset(base + bytes, 0, (value.size() + 1) * 2);
            for (unsigned i = 0; i < value.size(); ++i)
                base[bytes + i * 2 + 1] = value[i];
        };
        fstring(locals, 0x14000, "000");
        word(base, locals + 0xc, 1);
        fstring(locals + 0x10, 0x14200, "Time|1|A/Battle|2|B/Sync|3|C");
        context[4] = frame;
        context[0] = actor;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(events.size() == 1 && events[0].record_info && events[0].record_difficulty == 1 &&
                  events[0].record_story.utf8() == "000" &&
                  events[0].record_text.utf8() == "Time|1|A/Battle|2|B/Sync|3|C",
              "record callback copies resolved rank data even with QTE narration disabled");
        word(base, owner + 0x2c, 8);
        word(base, node + 0x2c, indices[15]);
        // The interpreter path must not publish a second copy if a future
        // movie subclass defines a FilterButtonInput implementation.
        context[4] = frame;
        context[5] = 0;
        context[0] = actor;
        call_script(context.data(), base);
        check(source.take(overflow).empty(), "movie script body cannot duplicate the input event");
        name(55, "BCGFxMovieMenu");
        name(56, "BCGFxMoviePlayer");
        word(base, 0x1d100 + 0x2c, 55);
        word(base, owner + 0x2c, 56);
        word(base, node + 0x2c, indices[25]);
        base[actor + 0x174] = 1;
        traceOriginal = [&](void *, uint8_t *) { base[actor + 0x174] = 2; };
        context[0] = actor;
        originalCalls = calls;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(calls == originalCalls + 1 && events.size() == 1 && events[0].menu_changed &&
                  context[0] == 0xfeed,
              "shared close lifecycle wakes the reader even without a UI-tree mutation");
        context[0] = actor;
        call_script(context.data(), base);
        check(source.take(overflow).empty(), "unchanged lifecycle does not wake the reader");
        word(base, node + 0x2c, indices[26]);
        traceOriginal = [&](void *, uint8_t *) { base[actor + 0x174] = 1; };
        context[0] = actor;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(events.size() == 1 && events[0].menu_changed,
              "shared open completion restores event-driven menu discovery");
        traceOriginal = {};
        name(50, "BCGFxUI_Hud");
        name(51, "Camera");
        name(52, "BCGFxMoviePlayer");
        name(53, "SE_UI_lockon1_Cue");
        word(base, pc + 0x34, 0x1d000);
        word(base, 0x1d000 + 0x2c, 3);
        word(base, pc + 0x370, 0x1b000);
        word(base, 0x1b000 + 0x324, std::bit_cast<uint32_t>(70.f));
        word(base, 0x19000 + 0x2c, 53);
        word(base, 0x19000 + 0x20, 90);
        word(base, 0x1b000 + 0x34, 0x1d200);
        word(base, 0x1d200 + 0x20, 101);
        word(base, 0x1d200 + 0x2c, 51);
        word(base, 0x1d200 + 0x44, 0x1d300);
        word(base, 0x1d300 + 0x28, 0x1d200);
        word(base, 0x1d300 + 0x2c, indices[18]);
        word(base, 0x1d300 + 0x88, 0x82260d78);
        word(base, pc + 0x7d4, std::bit_cast<uint32_t>(3.6f));
        word(base, pc + 0x7d8, std::bit_cast<uint32_t>(50.f));
        std::function<void(unsigned, uint32_t)> invoke;
        invoke = [&](unsigned kind, uint32_t object) {
            uint32_t nf = 0x14000 + kind * 0x40, nn = 0x1e000 + kind * 0x40,
                     no = 0x1f000 + kind * 0x40, nl = 0x15000 + kind * 0x100;
            word(base, nf + 0x10, nn);
            word(base, nf + 0x14, object);
            word(base, nf + 0x1c, nl);
            word(base, nn + 0x28, no);
            word(base, nn + 0x2c, indices[kind]);
            word(base, no + 0x2c,
                 kind == 18                 ? 51
                 : kind == 19 || kind == 20 ? 50
                 : kind == 21               ? 52
                                            : 3);
            if (kind == 8) {
                word(base, nl + 4, gauge);
                for (unsigned off : {8u, 12u, 16u, 24u, 28u})
                    word(base, nl + off, 0);
                word(base, nl + 20, std::bit_cast<uint32_t>(1.f));
            }
            if (kind == 17)
                word(base, nl, gauge);
            if (kind == 19)
                word(base, nl + 4, gauge);
            if (kind == 20)
                word(base, nl + 12, 1);
            if (kind == 21)
                word(base, nl, 0x19000);
            std::array<uint64_t, 40> ctx{};
            ctx[0] = object;
            ctx[4] = nf;
            ctx[5] = 0x18000 + kind * 8;
            call_script(ctx.data(), base);
            check(ctx[0] == 0xfeed, "diagnostic observer preserves original return context");
        };
        bool allowAcquisition = false, driveLevels = false;
        traceOriginal = [&](void *ctx, uint8_t *) {
            auto nf = ui_argument(ctx, 4), nl = cue_word(base + nf + 0x1c),
                 nn = cue_word(base + nf + 0x10), kind = cue_word(base + nn + 0x2c) - 14;
            if (kind == 16 && driveLevels) {
                invoke(23, pc);
                invoke(21, actor);
                invoke(24, pc);
                return;
            }
            if (kind == 23)
                word(base, pc + 0x8e8, 1);
            if (kind == 24)
                word(base, pc + 0x8e8, 0);
            if (kind == 16) {
                invoke(8, pc);
                if (cue_word(base + 0x15000 + 8 * 0x100 + 0x34)) {
                    invoke(19, actor);
                    invoke(17, pc);
                }
            }
            if (kind == 8) {
                bool accepted =
                    allowAcquisition && std::bit_cast<float>(cue_word(base + pc + 0x7d8)) > 45;
                word(base, nl + 0x24, accepted ? 1 : 0);
                word(base, nl + 0x34, accepted ? gauge : 0);
                word(base, nl + 0x50, std::bit_cast<uint32_t>(45.f));
                word(base, nl + 0x54, std::bit_cast<uint32_t>(1.f));
                word(base, nl + 0x58, std::bit_cast<uint32_t>(.7071068f));
                word(base, nl + 0x5c, std::bit_cast<uint32_t>(.7071068f));
            }
            if (kind == 19) {
                invoke(20, actor);
                invoke(21, actor);
            }
            if (kind == 17) {
                invoke(18, 0x1b000);
                word(base, ui_argument(ctx, 5), 1);
                word(base, nl + 0xc, std::bit_cast<uint32_t>(.7071068f));
                word(base, nl + 0x10, std::bit_cast<uint32_t>(.7071068f));
                word(base, nl + 0x18, std::bit_cast<uint32_t>(1.f));
            }
        };
        source.options(false, 20);
        source.lock_diagnostics(true, false);
        for (unsigned i = 0; i < 100; ++i)
            invoke(16, pc);
        auto waiting = source.take_lock_trace();
        check(waiting.records.size() == 101 && waiting.started != 0 && waiting.dropped == 0,
              "candidate probes start capture without flooding it with rejected checks");
        source.lock_diagnostics(true, false);
        allowAcquisition = true;
        invoke(16, pc);
        invoke(16, pc);
        auto lockBatch = source.take_lock_trace();
        check(lockBatch.records.size() == 10 && source.take(overflow).empty(),
              "nested native lock trace uses a separate queue with no narration traffic");
        check(
            lockBatch.records[1].kind == LockTraceKind::acquire && lockBatch.records[1].result &&
                lockBatch.records[1].accepted &&
                lockBatch.records[2].kind == LockTraceKind::marker_add &&
                lockBatch.records[3].kind == LockTraceKind::sound &&
                lockBatch.records[3].sound_name.index == 53 &&
                lockBatch.records[4].kind == LockTraceKind::remove &&
                lockBatch.records[4].angular && lockBatch.records[4].result &&
                lockBatch.records[4].angle > lockBatch.records[4].limit,
            "trace preserves acquisition, actual sound request and angular removal in frame order");
        check(lockBatch.records[0].frame + 1 == lockBatch.records[5].frame,
              "successive shooting ticks have distinct frame identities");
        source.lock_diagnostics(true, true);
        invoke(16, pc);
        lockBatch = source.take_lock_trace();
        check(lockBatch.records.size() == 2 && !lockBatch.records[1].accepted &&
                  std::abs(lockBatch.records[1].effective_max - 37) < .001f,
              "opt-in stable cap acts before the simulated native marker/sound side effects");
        check(std::bit_cast<float>(cue_word(base + pc + 0x7d8)) == 50,
              "native observer restores Episode 1 angle after capped acquisition");
        driveLevels = true;
        word(base, pc + 0x8e8, 0);
        word(base, pc + 0x8ec, 1);
        word(base, pc + 0x7fc, 4);
        word(base, pc + 0x800 + 40 + 0x20, 0x19000);
        source.lock_diagnostics(true, false);
        invoke(16, pc);
        lockBatch = source.take_lock_trace();
        check(
            lockBatch.records.size() == 4 &&
                lockBatch.records[1].kind == LockTraceKind::level_change &&
                lockBatch.records[1].level_before == 0 && lockBatch.records[1].level_after == 1 &&
                lockBatch.records[1].sound_name.index == 53 &&
                lockBatch.records[2].kind == LockTraceKind::sound &&
                lockBatch.records[2].target == 0 &&
                lockBatch.records[3].kind == LockTraceKind::level_change &&
                lockBatch.records[3].level_after == 0 && !lockBatch.records[3].sound,
            "level cues, resets and sound requests outside marker callbacks remain distinguishable");
        source.lock_diagnostics(false, false);
        traceOriginal = {};
        source.capture(false);
        context[0] = track;
        call_script(context.data(), base);
        check(source.take(overflow).empty() && context[0] == 0xfeed,
              "capture shutdown retains original execution without notifications");
        VirtualFree(base, 0, MEM_RELEASE);
        std::cout << "Gameplay native hook, QTE, timing and scoped acquisition tests passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
