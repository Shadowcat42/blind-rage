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
        word(base, disc_profile.layout.names_global + 4, 80);
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
        name(61, "L1");
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
        source.options(true, 2.f, 3.f);
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
        check(!aimObserved && std::bit_cast<float>(cue_word(base + actor + 0x7d4)) == 3.f &&
                  std::bit_cast<float>(cue_word(base + actor + 0x7d8)) == 8.f &&
                  source.widenings() == 0,
              "boost does not widen outside a verified active shooting frame");
        changeAim = true;
        context[0] = actor;
        call_script(context.data(), base);
        check(std::bit_cast<float>(cue_word(base + actor + 0x7d4)) == 60.f &&
                  std::bit_cast<float>(cue_word(base + actor + 0x7d8)) == 8.f,
              "scoped restoration preserves a game-side parameter change");
        changeAim = false;
        source.options(true, 1.f, 1.f);
        aimObserved = false;
        context[0] = actor;
        call_script(context.data(), base);
        check(!aimObserved && source.widenings() == 0,
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
        // The key's perfect time is 1.5 s on the track clock; NowTime is 1.0 s.
        word(base, 0x18000 + 8, std::bit_cast<uint32_t>(1.5f));
        driveWindow = true;
        context[0] = track;
        context[5] = 0x19000;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(events.size() == 1 && events[0].timing && events[0].sync.active &&
                  events[0].sync.window && context[0] == 0xfeed,
              "timing cue follows the actual native input predicate result");
        check(!events[0].sync.cue, "the open window alone does not cue the press");
        word(base, locals + 4, std::bit_cast<uint32_t>(1.31f));
        context[0] = track;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(events.size() == 1 && events[0].sync.window && events[0].sync.cue,
              "the press cue fires 0.2 s before the perfect time on the track clock");
        windowResult = false;
        context[0] = track;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(events.size() == 1 && !events[0].sync.window && events[0].sync.active,
              "native window closure is not guessed from ring percentage");
        {
            // BCGFxUI_Synchronic.EndSynchronic(SyncTag, Result): Result at +0xc.
            name(59, "BCGFxUI_Synchronic");
            driveWindow = false;
            word(base, owner + 0x2c, 59);
            word(base, node + 0x2c, indices[34]);
            for (uint8_t result : {uint8_t(3), uint8_t(0)}) {
                base[locals + 0xc] = result;
                context[0] = actor;
                const auto before = calls;
                call_script(context.data(), base);
                events = source.take(overflow);
                check(
                    calls == before + 1 && context[0] == 0xfeed &&
                        (result ? events.size() == 1 && events[0].sync_grade == 3 : events.empty()),
                    "the displayed synchronic grade is published once; a miss shows none");
            }
            base[locals + 0xc] = 0;
            word(base, owner + 0x2c, 4);
            word(base, node + 0x2c, indices[10]);
            driveWindow = true;
        }
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
        source.options(false, 1.f, 1.f);
        context[0] = input;
        originalCalls = calls;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(
            calls == originalCalls + 1 && context[0] == 0xfeed && events.size() == 1 &&
                events[0].status_request && events[0].status.controller == pc &&
                events[0].status.gauge == gauge,
            "Back requests status independently of QTE option and preserves original handler return");
        word(base, pc + 0x34, 0x1d800);
        word(base, 0x1d800 + 0x2c, 3);
        name(12, "DPAD_LEFT");
        name(13, "DPAD_RIGHT");
        name(60, "DPAD_UP");
        constexpr uint32_t inputRows = 0x1e000;
        word(base, input + 0x3f0, inputRows);
        word(base, input + 0x3f4, 24);
        word(base, input + 0x3f8, 24);
        std::array<unsigned, 3> dpadRows{11, 13, 10};
        for (unsigned i = 0; i < 3; ++i)
            word(base, inputRows + dpadRows[i] * 0x24, i == 2 ? 60 : 12 + i);
        word(base, node + 0x2c, indices[29]);
        for (unsigned i = 0; i < 3; ++i) {
            auto flags = inputRows + dpadRows[i] * 0x24 + 0x18;
            word(base, flags, 0);
            context[0] = input;
            call_script(context.data(), base);
            check(source.take(overflow).empty(), "neutral input tick emits no command");
            word(base, flags, 0x80000000);
            word(base, gauge + 0xc8, 0);
            context[0] = input;
            originalCalls = calls;
            call_script(context.data(), base);
            auto commands = source.take(overflow);
            check(calls == originalCalls + 1 && context[0] == 0xfeed && commands.size() == 1 &&
                      commands[0].shooting_command == int(i + 1),
                  "native button-state edge works without a delegate event or health gauge");
            context[0] = input;
            call_script(context.data(), base);
            check(source.take(overflow).empty(), "held D-pad does not repeat each input tick");
            word(base, flags, 0);
            context[0] = input;
            call_script(context.data(), base);
            check(source.take(overflow).empty(), "D-pad release emits no command");
            for (const auto &guard : std::array<std::array<uint32_t, 3>, 3>{
                     {{pc + 0x378, 0x8000, 0}, {world + 0x368, 1, 0}, {manager + 0x98, 1, 0}}}) {
                word(base, guard[0], guard[1]);
                word(base, flags, 0x80000000);
                context[0] = input;
                call_script(context.data(), base);
                check(source.take(overflow).empty(), "paused/menu/cinematic presses stay native");
                word(base, guard[0], guard[2]);
                context[0] = input;
                call_script(context.data(), base);
                check(source.take(overflow).empty(),
                      "holding across context return cannot issue a command");
                word(base, flags, 0);
                context[0] = input;
                call_script(context.data(), base);
                source.take(overflow);
            }
            word(base, gauge + 0xc8, 0x80000000);
        }
        word(base, node + 0x2c, indices[14]);
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
        source.options(false, 1.f, 1.f, false);
        context[0] = input;
        call_script(context.data(), base);
        check(source.take(overflow).empty(),
              "contextual status button can be disabled without changing native input");
        source.options(false, 1.f, 1.f, true);
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
            auto recordKeyIndex = 64 + i;
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
        name(62, "BCGFxUI_IntervalDrama");
        word(base, owner + 0x2c, 62);
        word(base, 0x1d600 + 0x2c, 62);
        word(base, 0x1d600 + 0x40, 0x1d100);
        word(base, actor + 0x34, 0x1d600);
        for (unsigned kind = 30; kind <= 32; ++kind) {
            word(base, node + 0x2c, indices[kind]);
            context[0] = actor;
            originalCalls = calls;
            call_script(context.data(), base);
            events = source.take(overflow);
            check(
                calls == originalCalls + 1 && context[0] == 0xfeed && events.size() == 1 &&
                    events[0].script_scene && events[0].scene_active == (kind == 30) &&
                    events[0].scene_movie == actor && events[0].scene_index == 7,
                "script-owned interlude start/end lifecycle is copied independently of QTE settings");
        }
        name(63, "BCGFxUI_LastCheck");
        word(base, owner + 0x2c, 63);
        word(base, 0x1d600 + 0x2c, 63);
        word(base, node + 0x2c, indices[30]);
        context[0] = actor;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(events.size() == 1 && events[0].script_scene && events[0].scene_active,
              "choice-only scene shares the script-start protocol");
        word(base, owner + 0x2c, 56);
        word(base, node + 0x2c, indices[32]);
        context[0] = actor;
        call_script(context.data(), base);
        events = source.take(overflow);
        check(events.size() == 1 && events[0].script_scene && !events[0].scene_active,
              "inherited close completion ends a script-owned choice scene");
        word(base, actor + 0x34, 0x1d400);
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
        word(base, pc + 0x7bc, 16);
        word(base, pc + 0x7d4, std::bit_cast<uint32_t>(3.6f));
        word(base, pc + 0x7d8, std::bit_cast<uint32_t>(50.f));
        // The component whose world position the lock model reads, and the angle the
        // simulated native function reports for it.
        const uint32_t lockTarget = 0x1d600;
        float simAngle = 45.f, simAimDegrees = 0.f, simDistance = 1.f;
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
                 : kind == 29               ? 6 // BCPlayerInput.InputCheckTick
                                            : 3);
            if (kind == 8) {
                word(base, nl + 4, lockTarget);
                for (unsigned off : {8u, 12u, 16u, 24u, 28u})
                    word(base, nl + off, 0);
                auto radians = simAimDegrees * 3.14159265f / 180.f;
                word(base, nl + 20, std::bit_cast<uint32_t>(std::cos(radians)));
                word(base, nl + 24, std::bit_cast<uint32_t>(std::sin(radians)));
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
        bool allowAcquisition = false, driveLevels = false, driveSearch = false,
             fireReturnsTarget = true, endSearchInMenu = false;
        constexpr uint32_t playerInput = 0x20c00;
        float seenUp = 0, seenRight = 0;
        float observedMinimum = 0;
        traceOriginal = [&](void *ctx, uint8_t *) {
            auto nf = ui_argument(ctx, 4), nl = cue_word(base + nf + 0x1c),
                 nn = cue_word(base + nf + 0x10), kind = cue_word(base + nn + 0x2c) - 14;
            if (kind == 16 && driveLevels) {
                invoke(23, pc);
                invoke(21, actor);
                invoke(24, pc);
                return;
            }
            if (kind == 28)
                word(base, ui_argument(ctx, 5), fireReturnsTarget ? lockTarget : 0);
            if (kind == 33) {
                seenUp = std::bit_cast<float>(cue_word(base + playerInput + 0x13c));
                seenRight = std::bit_cast<float>(cue_word(base + playerInput + 0x140));
            }
            if (kind == 23)
                word(base, pc + 0x8e8, 1);
            if (kind == 24)
                word(base, pc + 0x8e8, 0);
            if (kind == 16 && driveSearch) {
                invoke(22, pc);
                if (endSearchInMenu)
                    word(base, manager + 0x98, 1);
                return;
            }
            if (kind == 22 && driveSearch) {
                for (unsigned off : {0x1cu, 0x20u, 0x24u, 0x2cu, 0x30u})
                    word(base, nl + off, 0);
                word(base, nl + 0x28, std::bit_cast<uint32_t>(1.f));
                word(base, nl + 0x50, 0x1d900);
                word(base, nl + 0x54, 1);
                word(base, nl + 0x58, 1);
                word(base, 0x1d900, lockTarget);
                invoke(8, pc);
                // The script ends the search with TargetCompList.Remove(0, Length).
                word(base, nl + 0x54, 0);
            }
            if (kind == 16) {
                invoke(8, pc);
                if (cue_word(base + 0x15000 + 8 * 0x100 + 0x34)) {
                    invoke(19, actor);
                    invoke(17, pc);
                }
            }
            if (kind == 8) {
                auto minimum = std::bit_cast<float>(cue_word(base + pc + 0x7d4));
                observedMinimum = minimum;
                auto maximum = std::bit_cast<float>(cue_word(base + pc + 0x7d8));
                auto limit =
                    std::clamp(minimum + (maximum - minimum) * simDistance, minimum, maximum);
                bool accepted = allowAcquisition && simAngle < limit;
                // JustLockon is an out parameter written in the caller's frame;
                // the callee slot keeps its entry value. ResultInfo carries the
                // verified LockOnCnt increment.
                word(base, nl + 0x24, 0);
                word(base, nl + 0x34, accepted ? lockTarget : 0);
                word(base, nl + 0x38, accepted ? 1 : 0);
                word(base, nl + 0x50, std::bit_cast<uint32_t>(simAngle));
                word(base, nl + 0x54, std::bit_cast<uint32_t>(simDistance));
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
        // Episode 1 native limits are 3.6 (far) / 50 (near) degrees here.
        source.options(false, 1.f, 5.f);
        const float boostedFar = 3.6f * 5.f;
        source.lock_diagnostics(true);
        for (unsigned i = 0; i < 100; ++i)
            invoke(16, pc);
        auto waiting = source.take_lock_trace();
        check(waiting.records.size() == 101 && waiting.started != 0 && waiting.dropped == 0,
              "candidate probes start capture without flooding it with rejected checks");
        source.lock_diagnostics(true);
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
        word(base, pc + 0x7e4, 0x1d400);
        word(base, pc + 0x7e8, 1);
        word(base, 0x1d400, lockTarget);
        word(base, 0x1d404, 1);
        invoke(16, pc);
        lockBatch = source.take_lock_trace();
        check(std::none_of(lockBatch.records.begin(), lockBatch.records.end(),
                           [](const LockTraceRecord &r) {
                               return r.kind == LockTraceKind::acquire && r.result;
                           }),
              "a retained lock returned again is not reported as a new lock");
        word(base, pc + 0x7e4, 0);
        word(base, pc + 0x7e8, 0);
        source.lock_diagnostics(true);
        invoke(16, pc);
        lockBatch = source.take_lock_trace();
        check(lockBatch.records.size() == 5 && lockBatch.records[1].accepted &&
                  lockBatch.records[1].effective_min == boostedFar &&
                  lockBatch.records[1].effective_max == 50.f,
              "conservative cap preserves locks admitted by the native near cone");
        check(std::bit_cast<float>(cue_word(base + pc + 0x7d8)) == 50,
              "native observer restores Episode 1 angle after capped acquisition");
        check(!source.position_verified() && source.position_mismatches() == 0,
              "an unplaced target neither verifies nor contradicts the position model");
        // Place the component at `degrees` from the aim line and camera forward (+X).
        auto place = [&](float degrees) {
            auto radians = degrees * 3.14159265f / 180.f;
            word(base, lockTarget + 0x90 + 48, std::bit_cast<uint32_t>(1000.f * std::cos(radians)));
            word(base, lockTarget + 0x90 + 52, std::bit_cast<uint32_t>(1000.f * std::sin(radians)));
            word(base, lockTarget + 0x90 + 56, 0);
            simAngle = degrees;
        };
        source.lock_diagnostics(true);
        place(20.f);
        for (unsigned i = 0; i < 3; ++i)
            invoke(16, pc);
        source.take_lock_trace();
        check(source.position_verified() && source.position_matches() == 3,
              "three native angles agreeing with the component position verify the model");
        source.lock_diagnostics(true);
        invoke(16, pc);
        lockBatch = source.take_lock_trace();
        check(lockBatch.records.size() >= 2 && lockBatch.records[1].position_verified &&
                  !lockBatch.records[1].view_gated && !lockBatch.records[1].cap_applied &&
                  std::abs(lockBatch.records[1].view_margin - 17.5f) < .01f &&
                  lockBatch.records[1].effective_min == boostedFar &&
                  lockBatch.records[1].effective_max == 50.f,
              "a target the camera keeps is widened fully, not by the conservative bound");
        place(45.f);
        invoke(16, pc);
        lockBatch = source.take_lock_trace();
        check(lockBatch.records.size() >= 2 && lockBatch.records[1].view_gated &&
                  lockBatch.records[1].view_margin < 0 &&
                  lockBatch.records[1].effective_min == 3.6f &&
                  lockBatch.records[1].effective_max == 50.f && lockBatch.records[1].accepted,
              "unsafe target keeps native acquisition, including its original side effects");
        check(std::bit_cast<float>(cue_word(base + pc + 0x7d4)) == std::bit_cast<float>(3.6f),
              "gated candidates restore native fields after the check");
        source.lock_diagnostics(true);
        place(37.2f);
        invoke(16, pc);
        lockBatch = source.take_lock_trace();
        check(lockBatch.records.size() >= 2 && lockBatch.records[1].view_gated &&
                  lockBatch.records[1].effective_min == 3.6f &&
                  lockBatch.records[1].effective_max == 50.f,
              "the half-degree reserve disables widening without narrowing native acquisition");
        // Live regression: native acquisition accepts 3.15 degrees from the aim
        // line, while the target is 35.25 degrees from the camera (limit 35).
        word(base, 0x1b000 + 0x324, std::bit_cast<uint32_t>(65.f));
        place(35.25f);
        simAimDegrees = 32.1f;
        simAngle = 3.15f;
        simDistance = 0.f;
        source.lock_diagnostics(true);
        invoke(16, pc);
        lockBatch = source.take_lock_trace();
        check(lockBatch.records.size() == 5 && lockBatch.records[1].accepted &&
                  lockBatch.records[1].effective_min > simAngle,
              "native cone admits the live unsafe candidate when the repair is disabled");
        source.lock_diagnostics(true);
        for (unsigned i = 0; i < 7; ++i)
            invoke(16, pc);
        lockBatch = source.take_lock_trace();
        unsigned nativeLocks = 0, nativeSounds = 0;
        for (const auto &record : lockBatch.records) {
            if (record.kind == LockTraceKind::sound)
                ++nativeSounds;
            if (record.kind == LockTraceKind::acquire) {
                ++nativeLocks;
                check(record.position_verified && record.view_gated && !record.cap_applied &&
                          record.view_margin < 0 && record.accepted && record.result &&
                          record.effective_min == 3.6f && record.effective_max == 50.f,
                      "repair preserves the live native-cone lock even outside retention view");
            }
        }
        check(nativeLocks == 7 && nativeSounds == 7 &&
                  std::bit_cast<float>(cue_word(base + pc + 0x7d4)) == 3.6f &&
                  std::bit_cast<float>(cue_word(base + pc + 0x7d8)) == 50.f,
              "native repeated locks and sounds are deliberately preserved");
        word(base, 0x1b000 + 0x324, std::bit_cast<uint32_t>(70.f));
        simAimDegrees = 0.f;
        simDistance = 1.f;
        // A missing target position must use the geometric bound, not a target gate.
        word(base, lockTarget + 0xc0, std::bit_cast<uint32_t>(NAN));
        source.lock_diagnostics(true);
        invoke(16, pc);
        lockBatch = source.take_lock_trace();
        check(lockBatch.records.size() >= 2 && !lockBatch.records[1].view_gated &&
                  lockBatch.records[1].cap_applied && lockBatch.records[1].effective_max == 50.f,
              "unknown target geometry falls back to the conservative camera bound");
        place(20.f);
        word(base, lockTarget + 0x1e4, 0x40000000);
        word(base, lockTarget + 0x20, 105);
        word(base, pawn + 0x20, 44);
        source.shooting(ShootingMode::beacons);
        source.lock_diagnostics(false);
        driveSearch = true;
        invoke(16, pc);
        auto aimFrame = source.take_aim();
        check(aimFrame && aimFrame->active && aimFrame->valid && aimFrame->count == 1 &&
                  observedMinimum == 3.6f,
              "manual mode copies native search candidates without widening acquisition");
        auto serial = aimFrame->fired;
        endSearchInMenu = true;
        invoke(16, pc);
        aimFrame = source.take_aim();
        check(aimFrame && !aimFrame->active,
              "same-tick transition into a menu cancels copied guidance");
        endSearchInMenu = false;
        word(base, manager + 0x98, 0);
        invoke(28, pc);
        invoke(16, pc);
        aimFrame = source.take_aim();
        check(aimFrame && aimFrame->fired == serial + 1,
              "native volley target consumption resumes full-volley guidance");
        fireReturnsTarget = false;
        invoke(28, pc);
        invoke(16, pc);
        aimFrame = source.take_aim();
        check(aimFrame && aimFrame->fired == serial + 1,
              "empty fire iteration cannot pretend a volley was fired");
        word(base, world + 0x368, 1);
        invoke(16, pc);
        aimFrame = source.take_aim();
        check(aimFrame && !aimFrame->active, "paused native frame invalidates manual guidance");
        word(base, world + 0x368, 0);
        // Tracking: steer only while L1 is held, restore the real stick afterwards,
        // and yield entirely to real stick movement.
        source.shooting(ShootingMode::tracking);
        source.tracking(.7f);
        word(base, pc + 0x454, playerInput);
        word(base, playerInput + 0x13c, 0);
        word(base, playerInput + 0x140, 0);
        invoke(16, pc);
        source.take_aim();
        auto nudgesBefore = source.tracking_nudges();
        invoke(33, pc);
        check(seenUp == 0 && seenRight == 0 && source.tracking_nudges() == nudgesBefore,
              "tracking does not steer while L1 is released");
        word(base, inputRows + 7 * 0x24, 61);
        word(base, inputRows + 7 * 0x24 + 0x18, 0x80000000);
        invoke(29, input);
        invoke(33, pc);
        check((seenUp != 0 || seenRight != 0) && std::abs(seenRight) <= .7f &&
                  std::abs(seenUp) <= .7f && source.tracking_nudges() == nudgesBefore + 1,
              "holding L1 steers the cursor toward the unlocked target");
        check(cue_word(base + playerInput + 0x13c) == 0 &&
                  cue_word(base + playerInput + 0x140) == 0,
              "the player's real stick values are restored after the cursor update");
        word(base, playerInput + 0x140, std::bit_cast<uint32_t>(.5f));
        invoke(33, pc);
        check(seenRight == .5f && seenUp == 0 && source.tracking_nudges() == nudgesBefore + 1,
              "real stick movement takes priority over tracking");
        word(base, playerInput + 0x140, 0);
        word(base, inputRows + 7 * 0x24 + 0x18, 0);
        invoke(29, input);
        invoke(33, pc);
        check(seenUp == 0 && seenRight == 0, "releasing L1 stops tracking");
        {
            // The game declines a candidate inside its angle limit (occluded or out
            // of lock range): guidance and tracking must stop treating it as a target.
            const bool savedAllow = allowAcquisition;
            allowAcquisition = false;
            place(2.f);
            invoke(16, pc);
            source.take_aim();
            invoke(16, pc);
            auto blocked = source.take_aim();
            check(blocked && blocked->count == 1 && !blocked->targets[0].reachable,
                  "a target the game will not lock inside its cone is skipped");
            allowAcquisition = savedAllow;
            place(20.f);
            // A new capture epoch clears the blocked list; the lock range alone
            // (LockOnShotLockDistance, +0x7c8) now decides. The target is 1000 away.
            source.capture(false);
            source.capture(true);
            word(base, pc + 0x7c8, std::bit_cast<uint32_t>(500.f));
            invoke(16, pc);
            auto ranged = source.take_aim();
            check(ranged && ranged->count == 1 && !ranged->targets[0].reachable,
                  "a target beyond the game's lock distance is skipped");
            word(base, pc + 0x7c8, std::bit_cast<uint32_t>(-1.f));
            invoke(16, pc);
            ranged = source.take_aim();
            check(ranged && ranged->count == 1 && ranged->targets[0].reachable,
                  "without a lock distance the target is reachable again");
            source.lock_diagnostics(true);
            invoke(16, pc);
            auto guideBatch = source.take_lock_trace();
            check(std::any_of(guideBatch.records.begin(), guideBatch.records.end(),
                              [](const LockTraceRecord &r) {
                                  return r.kind == LockTraceKind::guide &&
                                         r.guide.candidates == 1 && r.guide.unlocked == 1 &&
                                         r.guide.unreachable == 0;
                              }),
                  "the lock trace summarizes guidance candidates");
            source.lock_diagnostics(false);
            source.take_lock_trace();
            source.take_aim();
        }
        driveSearch = false;
        source.shooting(ShootingMode::boosted);
        // A disagreement permanently falls back to the conservative bound.
        source.lock_diagnostics(true);
        simAngle = 10.f;
        word(base, lockTarget + 0x90 + 48, std::bit_cast<uint32_t>(1000.f));
        word(base, lockTarget + 0x90 + 52, std::bit_cast<uint32_t>(500.f));
        invoke(16, pc);
        source.take_lock_trace();
        check(!source.position_verified() && source.position_mismatches() == 1,
              "a native angle that disagrees with the position model is counted");
        source.lock_diagnostics(true);
        place(20.f);
        invoke(16, pc);
        lockBatch = source.take_lock_trace();
        check(lockBatch.records.size() >= 2 && !lockBatch.records[1].position_verified &&
                  !lockBatch.records[1].view_gated &&
                  lockBatch.records[1].effective_min == boostedFar &&
                  lockBatch.records[1].effective_max == 50.f,
              "after any disagreement the model is never trusted and the bound applies");
        driveLevels = true;
        word(base, pc + 0x8e8, 0);
        word(base, pc + 0x8ec, 1);
        word(base, pc + 0x7fc, 4);
        word(base, pc + 0x800 + 40 + 0x20, 0x19000);
        source.lock_diagnostics(true);
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
        source.lock_diagnostics(false);
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
