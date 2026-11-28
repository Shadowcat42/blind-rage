#include "platform.h"
#include "menu_reader.h"
#include "speech.h"
#include "ui_hook_source.h"
#include "window_notifications.h"
#include "diagnostics.h"
#include "presentation_hooks.h"
#include "gameplay_text.h"
#include "recomp_prompts.h"
#include "gameplay_status.h"
#include "gameplay_dump.h"
#include "gameplay_hooks.h"
#include "synchronic_reader.h"
#include "beacon_audio.h"
#include "restart_hook.h"
#include <atomic>
#include <mutex>
#include <objbase.h>
using namespace a11y;
static HMODULE self;
static std::mutex lifecycle;
static HANDLE worker;
static std::atomic<bool> stopping;
static std::atomic<bool> diagnosticsStarted;
struct Page {
    std::array<unsigned char, 4096> bytes{};
    bool valid{};
};
struct CaptureGuard {
    UiHookSource &source;
    ~CaptureGuard() {
        source.capture(false);
    }
};
struct PresentationGuard {
    PresentationHooks &source;
    ~PresentationGuard() {
        source.capture(false);
    }
};
struct GameplayGuard {
    GameplayHooks &source;
    ~GameplayGuard() {
        source.capture(false);
    }
};
struct RestartGuard {
    RestartHook &source;
    ~RestartGuard() {
        source.capture(false);
    }
};
static DWORD WINAPI run(void *) {
    try {
        auto dir = module_path(self).parent_path();
        auto bin = module_path().parent_path();
        auto logs = bin.parent_path() / L"userdata" / L"accessibility";
        fs::create_directories(logs);
        DiagnosticCleanup cleanup;
        if (!diagnosticsStarted.exchange(true))
            cleanup = clear_previous_diagnostics(logs);
        std::ofstream log(logs / L"accessibility.log", std::ios::app);
        auto write = [&](const std::string &s) {
            log << GetTickCount64() << " " << s << std::endl;
        };
        write("SESSION pid=" + std::to_string(GetCurrentProcessId()) +
              " previous_diagnostics_removed=" + std::to_string(cleanup.removed) +
              " cleanup_failures=" + std::to_string(cleanup.failed));
        std::string error;
        auto profile = identify_build(bin, error);
        if (!profile || !supported_game_data(bin.parent_path(), *profile, error)) {
            write(error);
            return 1;
        }
        write(std::string("BUILD profile=") + profile->id);
        auto runtime = GetModuleHandleW(L"rexruntime.dll");
        auto instance = reinterpret_cast<void *(*)()>(
            GetProcAddress(runtime, "?instance@Runtime@rex@@SAPEAV12@XZ"));
        auto membase = reinterpret_cast<unsigned char *(*)(void *)>(
            GetProcAddress(runtime, "?virtual_membase@Runtime@rex@@QEBAPEAEXZ"));
        if (!instance || !membase) {
            write("Runtime accessors unavailable");
            return 2;
        }
        auto &source = UiHookSource::instance();
        if (!source.install(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), *profile)) {
            write("UI hook installation failed; event reader not started");
            return 4;
        }
        CaptureGuard captureGuard{source};
        auto &presentations = PresentationHooks::instance();
        bool textHooks =
            presentations.install(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), *profile);
        if (!textHooks)
            write("Gameplay text hooks unavailable; menu reader remains active");
        PresentationGuard presentationGuard{presentations};
        auto &gameHooks = GameplayHooks::instance();
        bool nativeGameplay =
            gameHooks.install(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), *profile);
        if (!nativeGameplay)
            write("Gameplay state hooks unavailable; QTEs and shooting aid not started");
        if (!gameHooks.install_input(runtime))
            write("Controller input hook unavailable; the settings menu uses the keyboard only");
        GameplayGuard gameplayGuard{gameHooks};
        auto &restart = RestartHook::instance();
        RestartGuard restartGuard{restart};
        bool restartReady =
            restart.install(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), *profile,
                            bin.parent_path() / L"BlindRage.exe", module_path());
        restart.capture(restartReady);
        write(
            restartReady
                ? "Settings restart redirect installed"
                : "Settings restart redirect unavailable; restart through BlindRage.exe manually");
        auto co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        Speech speech(write);
        bool speaking = speech.init(dir);
        if (speaking)
            speech.speak("Blind Rage ready.");
        // One settings file: mods/accessibility/accessibility.ini, the parent of dist.
        auto ini = (dir.parent_path() / L"accessibility.ini").wstring();
        write("CONFIG " + utf8(ini) + (fs::exists(ini) ? "" : " (missing; defaults in use)"));
        std::vector<std::wstring> commands = {
            L"repeat",      L"mute", L"dump",   L"status", L"aimmode", L"aimprev", L"aimnext",
            L"aimpriority", L"rsup", L"rsdown", L"rsleft", L"rsright", L"settings"};
        std::vector<HANDLE> events;
        for (auto &c : commands)
            events.push_back(
                CreateEventW(nullptr, FALSE, FALSE, event_name(GetCurrentProcessId(), c).c_str()));
        // Shortcuts are written as key names ("Ctrl+Alt+F9"); legacy numeric
        // virtual-key codes still mean Ctrl+Alt plus that key. A missing or empty
        // value uses the default; "None" disables the shortcut.
        // Plain keys are registered only while the game has focus, so they never
        // reach other applications; the game does not receive them either.
        constexpr UINT chord = MOD_CONTROL | MOD_ALT;
        const Hotkey defaults[] = {{0, VK_OEM_3},
                                   {chord, VK_F5},
                                   {0, VK_F5},
                                   {0, 'H'},
                                   {0, 'M'},
                                   {0, VK_OEM_COMMA},
                                   {0, VK_OEM_PERIOD},
                                   {0, VK_OEM_2},
                                   // Keyboard right stick.
                                   {MOD_CONTROL, VK_UP},
                                   {MOD_CONTROL, VK_DOWN},
                                   {MOD_CONTROL, VK_LEFT},
                                   {MOD_CONTROL, VK_RIGHT},
                                   // The settings menu.
                                   {MOD_CONTROL, 'O'}};
        Hotkey hotkeys[16]{};
        for (size_t i = 0; i < commands.size(); i++) {
            wchar_t text[64]{};
            GetPrivateProfileStringW(L"hotkeys", commands[i].c_str(), L"", text, 64, ini.c_str());
            auto value = utf8(text);
            auto parsed = value.empty() ? std::optional<Hotkey>(defaults[i]) : parse_hotkey(value);
            if (!parsed)
                write("Hotkey " + utf8(commands[i]) + ": unrecognized \"" + value + "\"; using " +
                      describe_hotkey(defaults[i]));
            hotkeys[i] = parsed.value_or(defaults[i]);
        }
        gameHooks.keyboard_stick({hotkeys[8], hotkeys[9], hotkeys[10], hotkeys[11]});
        WindowNotifications notifications;
        if (!notifications.initialize(hotkeys)) {
            write("Foreground notifications unavailable; reader stopped");
            for (auto h : events)
                CloseHandle(h);
            speech.close();
            if (SUCCEEDED(co))
                CoUninitialize();
            return 5;
        }
        for (unsigned i = 0; i < commands.size(); i++)
            if (notifications.focused() && hotkeys[i].key && !notifications.hotkey_registered(i))
                write("Hotkey unavailable: " + utf8(commands[i]) + " (" +
                      describe_hotkey(hotkeys[i]) + ")");
        events.push_back(source.event());
        source.capture(true);
        Reader reader(profile->layout);
        std::string last, pending, lastSpeech, lastContext, spoken, announcement;
        std::optional<Menu> lastMenu;
        DialogReturn dialogReturn;
        bool muted = false, forceContext = notifications.focused(), scheduled = false,
             discovered = false, metadataReady = false;
        unsigned stable = 0, retries = 0;
        auto setting = [&](const wchar_t *key, int fallback) {
            return GetPrivateProfileIntW(L"gameplay", key, fallback, ini.c_str()) != 0;
        };
        RecompPrompts prompts;
        if (!prompts.initialize(runtime, reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),
                                *profile))
            write("Recomp prompt labels unavailable");
        // One label source for menus and tutorials. On keyboard the sticks take the
        // runtime's own names: the left-stick keys, and the mouse or right-stick keys.
        auto buttonLabel = [&](std::string_view token) {
            if (token == "LS" || token == "RS" || token == "R3") {
                if (prompts.device() == PromptDevice::unknown)
                    return std::string{};
                if (prompts.device() == PromptDevice::keyboard)
                    return prompts.keyboard_stick(token);
                return std::string(token == "LS"   ? "Left stick"
                                   : token == "RS" ? "Right stick"
                                                   : "Right stick press");
            }
            return prompts.button(token);
        };
        reader.button_labels(buttonLabel);
        reader.key_descriptions([&](const std::vector<unsigned> &keys,
                                    const std::map<std::string, std::string> &actions) {
            return prompts.describe_row(keys, actions);
        });
        GameplayText::Options textOptions{setting(L"subtitles", 1), setting(L"tutorials", 1)};
        GameplayText gameplay(textOptions, buttonLabel);
        GameplayStatus status(
            [&](std::string_view tag) { return prompts.button(tag); },
            GetPrivateProfileIntW(L"gameplay", L"low_health_percent", 25, ini.c_str()));
        bool statusWarnings = setting(L"status_warnings", 1);
        bool interruptText = setting(L"interrupt", 0);
        bool qteEnabled = setting(L"qtes", 1), interruptQte = setting(L"qte_interrupt", 1);
        // Boosted mode scales the game's own lock cone: point_blank multiplies the
        // near limit (LockOnDegMaxVal), max_range the far limit (LockOnDegMinVal).
        auto multiplier = [&](const wchar_t *key, float fallback) {
            wchar_t text[32]{};
            GetPrivateProfileStringW(L"gameplay", key, L"", text, 32, ini.c_str());
            wchar_t *end = nullptr;
            float value = text[0] ? std::wcstof(text, &end) : fallback;
            if (text[0] && (end == text || !std::isfinite(value))) {
                write("SHOOTING_BOOST unrecognized " + utf8(key) + "; using default");
                value = fallback;
            }
            return std::clamp(value, 1.f, 10.f);
        };
        float boostPointBlank = multiplier(L"boost_point_blank", 2.f),
              boostMaxRange = multiplier(L"boost_max_range", 3.f);
        float trackingStrength =
            float(std::clamp<int>(
                GetPrivateProfileIntW(L"gameplay", L"tracking_strength", 100, ini.c_str()), 0,
                100)) /
            100.f;
        wchar_t modeText[32]{};
        GetPrivateProfileStringW(L"gameplay", L"shooting_mode", L"", modeText, 32, ini.c_str());
        auto legacy = int(GetPrivateProfileIntW(L"gameplay", L"shooting_assist", -1, ini.c_str()));
        auto shootingMode = shooting_mode(utf8(modeText), legacy);
        // Legacy choices carry over and new configurations default to beacons. The
        // file is only touched when the key is absent; a typo is logged and never
        // overwritten, so the user's text survives for them to correct.
        if (modeText[0] && !shooting_mode_recognized(utf8(modeText)))
            write("SHOOTING_MODE unrecognized value ignored; using " +
                  std::string(shooting_mode_name(shootingMode)));
        else if (!modeText[0])
            WritePrivateProfileStringW(L"gameplay", L"shooting_mode",
                                       wide(shooting_mode_name(shootingMode)).c_str(), ini.c_str());
        gameHooks.options(textHooks && qteEnabled, boostPointBlank, boostMaxRange);
        gameHooks.tracking(trackingStrength);
        gameHooks.shooting(shootingMode);
        gameHooks.homing_boost(multiplier(L"boost_homing", 3.f));
        wchar_t priorityText[32]{};
        GetPrivateProfileStringW(L"gameplay", L"aim_priority", L"projectiles", priorityText, 32,
                                 ini.c_str());
        auto aimPriority = aim_priority(utf8(priorityText));
        gameHooks.aim_priority(aimPriority);
        bool lockTraceEnabled =
            GetPrivateProfileIntW(L"diagnostics", L"lock_trace", 0, ini.c_str()) != 0;
        bool stableCone = shootingMode == ShootingMode::boosted;
        write("SHOOTING_MODE " + std::string(shooting_mode_name(shootingMode)) + " configured");
        gameHooks.lock_diagnostics(lockTraceEnabled);
        ShootingBeacons beacons;
        BeaconAudio beaconAudio;
        std::optional<AimSnapshot> lastAim;
        float cueGain =
            float(std::clamp<int>(
                GetPrivateProfileIntW(L"gameplay", L"audio_cue_volume", 60, ini.c_str()), 0, 100)) *
            .004f;
        // Each cue's share of the overall cue volume, 0-100 percent.
        auto cue_share = [&](const wchar_t *key) {
            return float(std::clamp<int>(GetPrivateProfileIntW(L"gameplay", key, 100, ini.c_str()),
                                         0, 100)) /
                   100.f;
        };
        float shootShare = cue_share(L"shooting_beacon_volume"),
              walkShare = cue_share(L"walk_beacon_volume"),
              meleeShare = cue_share(L"melee_beacon_volume"),
              lookShare = cue_share(L"look_beacon_volume"),
              fireShare = cue_share(L"fire_warning_volume"),
              countInShare = cue_share(L"count_in_volume");
        AudioRetry audioRetry;
        bool audioReady = false, audioAnnounced = false;
        auto beacon_output = [&](BeaconCue cue) {
            // A running mod tutorial holds the live beacon back: its demonstrations
            // play between boxes and must not be covered.
            if (gameHooks.tutorial_running())
                return;
            if (!notifications.focused() || muted || !shooting_assisted(shootingMode))
                cue.active = false;
            // Start on first need and rebuild after a device error, with backoff.
            if (cue.active && !(audioReady && beaconAudio.healthy()) &&
                audioRetry.due(GetTickCount64())) {
                if (audioReady)
                    write("BEACON_AUDIO lost; restarting");
                beaconAudio.close();
                audioReady = beaconAudio.start(cueGain * shootShare);
                if (audioReady)
                    audioRetry.success();
                else
                    audioRetry.failure(GetTickCount64());
                write(audioReady ? "BEACON_AUDIO ready"
                                 : "BEACON_AUDIO unavailable; aiming remains manual");
                if (!audioReady && !audioAnnounced && speaking) {
                    audioAnnounced = true;
                    speech.speak("Beacon audio unavailable. Aiming remains manual.", false);
                }
            }
            if (audioReady && beaconAudio.healthy())
                beaconAudio.update(cue);
        };
        // Walking guidance to the nearest touch trigger not yet entered.
        // The mod's own tutorial boxes (Episode 1's rail-shooting introduction).
        bool modTutorials = setting(L"mod_tutorials", 1);
        gameHooks.mod_tutorials(modTutorials);
        // Fight tips: a short box when an enemy whose fight isn't obvious appears.
        bool modTips = setting(L"mod_tips", 1);
        gameHooks.mod_tips(modTips);
        uint64_t tutorialLevel = 0, chaseLevel = 0, meleeLevel = 0, beaconDemo = 0;
        // Lookups from the first tutorial, reused: script classes and the SE_UI cues
        // stay loaded for the whole session.
        std::optional<GameplayHooks::TutorialRun> tutorialBase;
        // The tutorial box's script functions and the lock cues, looked up by name.
        auto tutorial_run = [&](const Memory &memory) {
            GameplayHooks::TutorialRun run;
            const auto &layout = reader.layout();
            auto advertise =
                ScriptLookup::find(memory, reader, layout, "BCGFxUI_Advertise", "Class");
            run.advertise_class = advertise;
            run.create = ScriptLookup::function(memory, reader, advertise, "CallCreate");
            run.show = ScriptLookup::function(memory, reader, advertise, "ShowAdvertise");
            run.sound = ScriptLookup::function(memory, reader, advertise, "PlayGFxUISound");
            auto [offset, mask] = ScriptLookup::property(memory, reader, advertise, "bUsePause");
            run.use_pause_offset = offset;
            run.use_pause_mask = mask;
            run.lock_tone = ScriptLookup::find(memory, reader, layout, "SE_UI_lockon1_Cue",
                                               "SoundCue", "SE_UI");
            run.full_lock_tone = ScriptLookup::find(memory, reader, layout, "SE_UI_lockon2_Cue",
                                                    "SoundCue", "SE_UI");
            auto names = memory.u32(layout.names_global + 4);
            for (uint32_t i = 0; i < names && i < 250000 && !run.tick_name; ++i)
                if (reader.name(memory, i) == "Tick")
                    run.tick_name = i;
            return run;
        };
        // The QTE note holds Episode 1's first QTE announcement until its box is
        // confirmed. If the box does not open promptly, the QTE is announced anyway.
        uint64_t qteNoteLevel = 0, qteNoteStarted = 0;
        bool qteNoteActive = false, qteNoteOpened = false;
        constexpr uint64_t kQteNoteOpenMs = 400;
        auto beacon_demo = [&] {
            if (!notifications.focused() || muted)
                return;
            if (!(audioReady && beaconAudio.healthy())) {
                beaconAudio.close();
                audioReady = beaconAudio.start(cueGain * shootShare);
            }
            if (!audioReady)
                return;
            // On target: centred, middle pitch, continuous, for one second.
            BeaconCue cue;
            cue.active = true;
            cue.tick = GetTickCount64();
            cue.hold_ms = 1000;
            cue.generation = ~0ull - ++beaconDemo;
            beaconAudio.update(cue);
        };
        bool walkEnabled = setting(L"walk_beacon", 1);
        gameHooks.walk_beacon(walkEnabled);
        BeaconAudio walkAudio;
        walkAudio.synth().voice(BeaconVoice::sine);
        AudioRetry walkRetry;
        bool walkReady = false;
        uint64_t walkTarget = 0;
        auto walk_output = [&](BeaconCue cue) {
            if (!notifications.focused() || muted || !walkEnabled)
                cue.active = false;
            if (cue.active && !(walkReady && walkAudio.healthy()) &&
                walkRetry.due(GetTickCount64())) {
                walkAudio.close();
                walkReady = walkAudio.start(cueGain * walkShare);
                if (walkReady)
                    walkRetry.success();
                else
                    walkRetry.failure(GetTickCount64());
                write(walkReady ? "WALK_AUDIO ready" : "WALK_AUDIO unavailable");
            }
            if (walkReady && walkAudio.healthy())
                walkAudio.update(cue);
        };
        // Melee beacon: the enemy the game would attack, or the nearest one.
        bool meleeEnabled = setting(L"melee_beacon", 1);
        BeaconAudio meleeAudio;
        // An octave below the other beacons: 262.5 Hz ahead, 220 Hz behind.
        meleeAudio.synth().voice(BeaconVoice::square, kMeleeBeaconPitch);
        AudioRetry meleeRetry;
        bool meleeReady = false;
        uint64_t meleeTarget = 0, meleeSeen = 0;
        uint32_t lastController = 0; // For F5 dumps' gameplay section.
        bool meleeReach = false;
        auto melee_output = [&](BeaconCue cue) {
            if (!notifications.focused() || muted || !meleeEnabled)
                cue.active = false;
            if (cue.active && !(meleeReady && meleeAudio.healthy()) &&
                meleeRetry.due(GetTickCount64())) {
                meleeAudio.close();
                meleeReady = meleeAudio.start(cueGain * meleeShare);
                if (meleeReady)
                    meleeRetry.success();
                else
                    meleeRetry.failure(GetTickCount64());
                write(meleeReady ? "MELEE_AUDIO ready" : "MELEE_AUDIO unavailable");
            }
            if (meleeReady && meleeAudio.healthy())
                meleeAudio.update(cue);
        };
        // Look-around beacon: camera scenes the right stick turns (the hot spring).
        bool lookEnabled = setting(L"look_beacon", 1);
        BeaconAudio lookAudio;
        AudioRetry lookRetry;
        bool lookReady = false;
        int lookGroup = -1;
        unsigned lookFound = 0;
        auto look_output = [&](BeaconCue cue) {
            if (!notifications.focused() || muted || !lookEnabled)
                cue.active = false;
            if (cue.active && !(lookReady && lookAudio.healthy()) &&
                lookRetry.due(GetTickCount64())) {
                lookAudio.close();
                lookReady = lookAudio.start(cueGain * lookShare);
                if (lookReady)
                    lookRetry.success();
                else
                    lookRetry.failure(GetTickCount64());
                write(lookReady ? "LOOK_AUDIO ready" : "LOOK_AUDIO unavailable");
            }
            if (lookReady && lookAudio.healthy())
                lookAudio.update(cue);
        };
        // Jump cue for wide sword waves: a quick 500-1000 Hz sweep.
        JumpAudio jumpAudio;
        AudioRetry jumpRetry;
        bool jumpReady = false;
        uint64_t jumpLogged = 0;
        auto jump_output = [&](JumpCue cue) {
            if (!notifications.focused() || muted)
                cue.active = false;
            if (cue.active && !(jumpReady && jumpAudio.healthy()) &&
                jumpRetry.due(GetTickCount64())) {
                jumpAudio.close();
                jumpReady = jumpAudio.start(cueGain * fireShare);
                if (jumpReady)
                    jumpRetry.success();
                else
                    jumpRetry.failure(GetTickCount64());
                write(jumpReady ? "JUMP_AUDIO ready" : "JUMP_AUDIO unavailable");
            }
            if (jumpReady && jumpAudio.healthy())
                jumpAudio.update(cue);
        };
        // Incoming-fire warning in shooting sections without the game's lock-on.
        FireAudio fireAudio;
        AudioRetry fireRetry;
        bool fireReady = false;
        auto fire_output = [&](FireCue cue, bool demo = false) {
            // A running tutorial holds live warnings back; its demonstration plays.
            if (gameHooks.tutorial_running() && !demo)
                return;
            if (!notifications.focused() || muted)
                cue.active = false;
            if (cue.active && !(fireReady && fireAudio.healthy()) &&
                fireRetry.due(GetTickCount64())) {
                fireAudio.close();
                fireReady = fireAudio.start(cueGain * fireShare);
                if (fireReady)
                    fireRetry.success();
                else
                    fireRetry.failure(GetTickCount64());
                write(fireReady ? "FIRE_AUDIO ready" : "FIRE_AUDIO unavailable");
            }
            if (fireReady && fireAudio.healthy())
                fireAudio.update(cue);
        };
        auto drain_beacons = [&] {
            if (auto fire = gameHooks.take_fire())
                fire_output(*fire);
            for (const auto &line : gameHooks.take_wave_log())
                write(line);
            if (auto waves = gameHooks.take_waves()) {
                fire_output(waves->step);
                const auto &lastJump = waves->jump;
                if (lastJump.active && lastJump.generation != jumpLogged) {
                    write("WAVE_JUMP in " +
                          std::to_string(int64_t(lastJump.at) - int64_t(GetTickCount64())) + "ms");
                    jumpLogged = lastJump.generation;
                }
                jump_output(lastJump);
            }
            if (auto sample = gameHooks.take_aim()) {
                lastAim = *sample;
                beacon_output(beacons.update(*sample));
                gameHooks.tracking_focus(beacons.focus());
            }
            if (auto walk = gameHooks.take_walk()) {
                auto cue = walk_cue(*walk);
                if (cue.active && cue.generation != walkTarget)
                    write("WALK_TARGET distance=" + std::to_string(int(walk->distance)) +
                          " bearing=" + std::to_string(int(walk->bearing)) +
                          " remaining=" + std::to_string(walk->remaining) +
                          (walk->burst_guide ? " burst_guide" : ""));
                if (cue.active)
                    walkTarget = cue.generation;
                walk_output(cue);
            }
            if (auto look = gameHooks.take_look()) {
                if (look->active && look->found && look->found != lookFound)
                    write("LOOK_FOUND groups=" + std::to_string(look->found) +
                          " remaining=" + std::to_string(look->remaining));
                if (look->active && look->target && look->group != lookGroup)
                    write("LOOK_TARGET group=" + std::to_string(look->group) +
                          " angle=" + std::to_string(int(look->angle)) +
                          " cone=" + std::to_string(int(look->cone)) +
                          " remaining=" + std::to_string(look->remaining));
                lookFound = look->active ? look->found : 0;
                lookGroup = look->active && look->target ? look->group : -1;
                look_output(look_cue(*look));
            }
            if (auto melee = gameHooks.take_melee()) {
                if (melee->controller.pointer)
                    lastController = melee->controller.pointer;
                if (melee->active)
                    meleeSeen = melee->tick;
                auto cue = melee_cue(*melee);
                if (meleeEnabled && cue.active &&
                    (cue.generation != meleeTarget || melee->reach != meleeReach))
                    write("MELEE_TARGET distance=" + std::to_string(int(melee->distance)) +
                          " bearing=" + std::to_string(int(melee->bearing)) + " locked=" +
                          std::to_string(melee->locked) + " reach=" + std::to_string(melee->reach));
                if (cue.active) {
                    meleeTarget = cue.generation;
                    meleeReach = melee->reach;
                }
                melee_output(cue);
            }
        };
        auto shooting_command = [&](int command) {
            if (command == 5) {
                // Melee outside rapid fire: D-pad Up (or the aim mode key).
                meleeEnabled = !meleeEnabled;
                if (!meleeEnabled)
                    meleeAudio.silence();
                WritePrivateProfileStringW(L"gameplay", L"melee_beacon", meleeEnabled ? L"1" : L"0",
                                           ini.c_str());
                std::string text = meleeEnabled ? "Melee beacon on." : "Melee beacon off.";
                write("MELEE_BEACON " + std::to_string(meleeEnabled));
                if (notifications.focused() && !muted && speaking)
                    speech.speak(text, false);
            } else if (command == 3) {
                gameHooks.stop_lock_trace();
                shootingMode = next_shooting_mode(shootingMode);
                gameHooks.shooting(shootingMode);
                beacons.reset();
                lastAim.reset();
                beaconAudio.silence();
                WritePrivateProfileStringW(L"gameplay", L"shooting_mode",
                                           wide(shooting_mode_name(shootingMode)).c_str(),
                                           ini.c_str());
                std::string text = shootingMode == ShootingMode::beacons   ? "Beacons."
                                   : shootingMode == ShootingMode::boosted ? "Boosted beacons."
                                                                           : "Beacons off.";
                write("SHOOTING_MODE " + std::string(shooting_mode_name(shootingMode)));
                if (notifications.focused() && !muted && speaking)
                    speech.speak(text, false);
            } else if (command == 4) {
                // Melee rapid fire: guide and track projectiles or enemies first.
                aimPriority = aimPriority == AimPriority::projectiles ? AimPriority::enemies
                                                                      : AimPriority::projectiles;
                gameHooks.aim_priority(aimPriority);
                beacons.reset();
                WritePrivateProfileStringW(L"gameplay", L"aim_priority",
                                           wide(aim_priority_name(aimPriority)).c_str(),
                                           ini.c_str());
                std::string text =
                    aimPriority == AimPriority::enemies ? "Enemies first." : "Projectiles first.";
                write("AIM_PRIORITY " + std::string(aim_priority_name(aimPriority)));
                if (notifications.focused() && !muted && speaking)
                    speech.speak(text, false);
            } else if (shooting_assisted(shootingMode) && lastAim &&
                       GetTickCount64() - lastAim->tick <= 200) {
                auto cue = beacons.update(*lastAim, command == 1 ? -1 : 1);
                gameHooks.tracking_focus(beacons.focus());
                beacon_output(cue);
                const bool melee = lastAim->window_degrees > 0;
                bool projectile = false;
                for (unsigned i = 0; i < lastAim->count; ++i)
                    if (lastAim->targets[i].id == beacons.guiding())
                        projectile = lastAim->targets[i].projectile;
                std::string text =
                    melee ? (cue.active ? aim_target_words(projectile, cue.pan, cue.elevation)
                                        : "No targets.")
                    : lastAim->locks >= lastAim->maximum ? "Volley full. Fire to continue."
                    : cue.active                         ? "Target cluster selected."
                                                         : "No unlocked target clusters.";
                write("BEACON_CLUSTER " + text);
                if (notifications.focused() && !muted && speaking)
                    speech.speak(text, false);
            }
        };
        std::optional<LockTraceWriter> lockWriter;
        if (lockTraceEnabled) {
            lockWriter.emplace(logs / L"lock-trace.jsonl", profile->id, 0, stableCone,
                               GetCurrentProcessId());
            if (!lockWriter->valid()) {
                write("LOCK_TRACE cannot open diagnostic file");
                gameHooks.lock_diagnostics(false);
            }
        }
        write("LOCK_DIAGNOSTICS trace=" + std::to_string(lockTraceEnabled) +
              " stable_cone=" + std::to_string(stableCone));
        auto drain_lock_trace = [&] {
            if (!lockWriter || lockWriter->finished())
                return;
            auto runtimeInstance = instance();
            auto base = runtimeInstance ? membase(runtimeInstance) : nullptr;
            Memory memory([&](uint32_t at, void *out, size_t n) {
                SIZE_T got{};
                return base && ReadProcessMemory(GetCurrentProcess(), base + at, out, n, &got) &&
                       got == n;
            });
            lockWriter->drain(gameHooks.take_lock_trace(), [&](CueName value) {
                if (!value.index)
                    return std::string{};
                auto name = reader.name(memory, value.index);
                if (value.number && !name.empty())
                    name += "_" + std::to_string(value.number - 1);
                return name;
            });
            if (lockWriter->finished())
                gameHooks.stop_lock_trace();
        };
        QteReader qtes([&](std::string_view tag) { return prompts.button(tag); },
                       [&](std::string_view tag) { return prompts.gesture(tag); });
        {
            wchar_t wording[32]{};
            GetPrivateProfileStringW(L"gameplay", L"qte_wording", L"verbose", wording, 32,
                                     ini.c_str());
            qtes.quick(utf8(wording) == "quick");
        }
        SynchronicReader timing([&](std::string_view tag) { return prompts.button(tag); });
        if (textHooks) {
            events.push_back(presentations.event());
            presentations.capture(true);
            write("Gameplay text observer started");
        }
        if (nativeGameplay) {
            events.push_back(gameHooks.event());
            gameHooks.capture(true);
            write("Gameplay state observer started; qtes=" + std::to_string(qteEnabled) +
                  " boost_point_blank=" + std::to_string(boostPointBlank) +
                  " boost_max_range=" + std::to_string(boostMaxRange) +
                  " tracking_strength=" + std::to_string(trackingStrength));
        }
        auto narrate_text = [&] {
            for (const auto &line : gameplay.take()) {
                write("GAME_TEXT [" + line.channel + "] " + line.text);
                if (notifications.focused() && !muted && speaking)
                    speech.speak(line.text, interruptText);
            }
        };
        auto narrate_status = [&] {
            for (const auto &line : status.take()) {
                write("GAME_STATUS " + line);
                if (statusWarnings && notifications.focused() && !muted && speaking)
                    speech.speak(line, false);
            }
        };
        auto movie_class = [&](const Memory &memory, uint32_t movie, std::string_view expected) {
            for (auto cls = memory.u32(movie + 0x34), depth = 0u; cls && depth < 20;
                 cls = memory.u32(cls + 0x40), ++depth)
                if (reader.object_name(memory, cls) == expected)
                    return true;
            return false;
        };
        auto validate_status = [&](const Memory &memory) {
            for (auto movie : status.movies())
                if (memory.u32(movie.pointer + 0x20) != movie.index ||
                    !(memory.u32(movie.pointer + 0xc8) & 0x80000000) ||
                    (!movie_class(memory, movie.pointer, "BCGFxUI_Gauge") &&
                     !movie_class(memory, movie.pointer, "BCGFxMovieButton")))
                    status.closed(movie);
        };
        auto validate_qtes = [&](const Memory &memory) {
            auto validate = [&](auto &cueReader, std::string_view expected) {
                for (auto movie : cueReader.movies()) {
                    if (memory.u32(movie.pointer + 0x20) != movie.index ||
                        !(memory.u32(movie.pointer + 0xc8) & 0x80000000) ||
                        !movie_class(memory, movie.pointer, expected)) {
                        cueReader.closed(movie);
                        continue;
                    }
                    auto gfx = memory.u32(movie.pointer + 0x180);
                    if (memory.u32(gfx + 0x40) == 0x48) {
                        auto root = memory.u32(memory.u32(gfx + 0x44) + 4);
                        if (root)
                            cueReader.visibility(movie, reader.visible(memory, root));
                    }
                }
            };
            validate(qtes, "BCGFxMovieButton");
            validate(timing, "BCGFxUI_Synchronic");
        };
        // The synchronic count-in: started on first use, rebuilt after a device error.
        CountInAudio countInAudio;
        // The count-in follows the synchronic track's clock and stops while it pauses.
        countInAudio.synth().clock(gameHooks.timing_clock());
        AudioRetry countInRetry;
        bool countInReady = false;
        int64_t countInPerfect = 0, countInSpacing = kCountInBeatNs;
        std::string countInLogged;
        auto count_in_output = [&] {
            auto cue = timing.take_count_in();
            if (!cue)
                return;
            if (!qteEnabled || !notifications.focused() || muted)
                cue->active = false;
            if (cue->active && !(countInReady && countInAudio.healthy()) &&
                countInRetry.due(GetTickCount64())) {
                countInAudio.close();
                countInReady = countInAudio.start(cueGain * countInShare);
                if (countInReady)
                    countInRetry.success();
                else
                    countInRetry.failure(GetTickCount64());
                write(countInReady ? "COUNT_IN_AUDIO ready" : "COUNT_IN_AUDIO unavailable");
            }
            if (countInReady && countInAudio.healthy())
                countInAudio.update(*cue);
            if (cue->active) {
                countInPerfect = cue->perfect;
                countInSpacing = cue->spacing;
            }
        };
        // Log each beat once it has been rendered, relative to the perfect time.
        auto count_in_log = [&] {
            if (!countInReady)
                return;
            auto played = countInAudio.synth().played();
            std::string text;
            for (int beat = 0; beat < kCountInBeats; ++beat) {
                auto at = played.at[size_t(beat)];
                text += " beat" + std::to_string(beat + 1) + "=";
                text += at ? std::to_string((at - countInPerfect) / 1'000'000) + "ms" : "-";
            }
            text = "COUNT_IN_PLAYED generation=" + std::to_string(played.generation) +
                   " spacing=" + std::to_string(countInSpacing / 1'000'000) + "ms" + text;
            if (text != countInLogged) {
                countInLogged = text;
                write(text);
            }
        };
        // After the QTE note: announce the QTE that is current now.
        auto release_qte = [&](const char *why) {
            qteNoteActive = false;
            auto text = timing.repeat();
            if (text.empty())
                text = qtes.repeat();
            write(std::string("MOD_TUTORIAL QTE note ") + why +
                  (text.empty() ? "; no QTE still active" : "; announcing: " + text));
            if (qteEnabled && !text.empty() && notifications.focused() && !muted && speaking)
                speech.speak(text, interruptQte);
        };
        auto narrate_qtes = [&] {
            auto lines = timing.take();
            auto normal = qtes.take();
            lines.insert(lines.end(), normal.begin(), normal.end());
            std::string combined;
            for (const auto &line : lines) {
                write("GAME_QTE " + line);
                append_phrase(combined, line);
            }
            if (qteNoteActive && !qteNoteOpened &&
                GetTickCount64() - qteNoteStarted > kQteNoteOpenMs) {
                gameHooks.cancel_tutorial();
                release_qte("box did not open");
                return;
            }
            // Held while the note is up; announced once it is confirmed.
            if (qteNoteActive)
                return;
            // Episode 1's map must still be the one loaded: its object at the same
            // address, with the same index and name.
            auto episode1Loaded = [&] {
                auto runtimeInstance = instance();
                auto base = runtimeInstance ? membase(runtimeInstance) : nullptr;
                Memory memory([&](uint32_t at, void *out, size_t size) {
                    SIZE_T got{};
                    return base &&
                           ReadProcessMemory(GetCurrentProcess(), base + at, out, size, &got) &&
                           got == size;
                });
                auto level = uint32_t(tutorialLevel >> 32);
                return level && memory.u32(level + 0x20) == uint32_t(tutorialLevel) &&
                       reader.object_name(memory, level) == "BC-S000";
            };
            if (!combined.empty() && qteEnabled && modTutorials && tutorialBase && tutorialLevel &&
                qteNoteLevel != tutorialLevel && !gameHooks.tutorial_running() &&
                episode1Loaded()) {
                qteNoteLevel = tutorialLevel;
                auto run = *tutorialBase;
                run.boxes = qte_note();
                gameHooks.start_tutorial(std::move(run));
                qteNoteActive = true;
                qteNoteOpened = false;
                qteNoteStarted = GetTickCount64();
                write("MOD_TUTORIAL QTE note before: " + combined);
                return;
            }
            if (qteEnabled && !combined.empty() && notifications.focused() && !muted && speaking)
                speech.speak(combined, interruptQte);
        };
        ULONGLONG due = 0, burst = 0, emptySince = 0;
        uint64_t reads = 0;
        auto changed = [&] {
            auto now = GetTickCount64();
            if (!scheduled)
                burst = now;
            scheduled = true;
            due = std::min(now + 20, burst + 80);
            retries = 10;
        };
        auto inspect = [&](bool dumpOnly) {
            auto runtimeInstance = instance();
            auto base = runtimeInstance ? membase(runtimeInstance) : nullptr;
            std::unordered_map<uint32_t, Page> pages;
            Memory memory([&](uint32_t at, void *out, size_t size) {
                if (!base)
                    return false;
                auto *dest = (unsigned char *)out;
                while (size) {
                    auto page = at & ~4095u;
                    auto [it, added] = pages.try_emplace(page);
                    if (added) {
                        SIZE_T got{};
                        it->second.valid = ReadProcessMemory(GetCurrentProcess(), base + page,
                                                             it->second.bytes.data(), 4096, &got) &&
                                           got == 4096;
                    }
                    if (!it->second.valid)
                        return false;
                    auto count = std::min<size_t>(size, 4096 - (at & 4095));
                    memcpy(dest, it->second.bytes.data() + (at & 4095), count);
                    size -= count;
                    at += uint32_t(count);
                    dest += count;
                }
                return true;
            });
            // Movie lifecycle and explicit requests refresh the registry. Cursor/value
            // changes reuse validated movie handles and only inspect their UI trees.
            bool discover = source.take_discovery() || dumpOnly || !discovered;
            if (base && discover) {
                reader.discover(memory);
                discovered = reader.ready(memory);
            }
            if (base && nativeGameplay && discover && !metadataReady &&
                gameHooks.configure_names(memory, reader)) {
                metadataReady = true;
                write("GAMEPLAY_METADATA verified function-name filter ready");
            }
            if (dumpOnly) {
                auto stem = L"dump-" + std::to_wstring(GetTickCount64());
                std::ofstream dump(logs / (stem + L".txt"));
                reader.diagnostic(memory, dump);
                write_gameplay_dump(memory, reader, lastController, dump);
                dump.close();
                std::ofstream raw(logs / (stem + L".pages"), std::ios::binary);
                uint32_t magic = 0x41575031, count = 0;
                for (const auto &[address, page] : pages)
                    if (page.valid)
                        count++;
                raw.write((char *)&magic, 4);
                raw.write((char *)&count, 4);
                for (const auto &[address, page] : pages)
                    if (page.valid) {
                        raw.write((char *)&address, 4);
                        raw.write((const char *)page.bytes.data(), 4096);
                    }
                write("Saved " + utf8(stem) + ".txt and .pages (raw UI diagnostics)");
                std::string counts;
                for (unsigned i = 0; i < UiHookSource::hook_count; i++)
                    counts += " " + std::to_string(source.calls(i));
                write("EVENT_STATS reads=" + std::to_string(reads) +
                      " mutations=" + std::to_string(source.changes()) + " hook_calls=" + counts);
                counts.clear();
                for (unsigned i = 0; i < UiHookSource::hook_count; i++)
                    counts += " " + std::to_string(source.notifications(i));
                write("EVENT_SOURCES" + counts);
                write(
                    "GAMEPLAY_STATS captured=" + std::to_string(gameHooks.captured()) +
                    " capture_failures=" + std::to_string(gameHooks.failures()) +
                    " widened_checks=" + std::to_string(gameHooks.widenings()) +
                    " lock_position_matches=" + std::to_string(gameHooks.position_matches()) +
                    " lock_position_mismatches=" + std::to_string(gameHooks.position_mismatches()) +
                    " tracking_nudges=" + std::to_string(gameHooks.tracking_nudges()) +
                    " lock_on_skips=" + std::to_string(gameHooks.lock_on_skips()) +
                    " homing_calls=" + std::to_string(gameHooks.homing_calls()) +
                    " homing_hits=" + std::to_string(gameHooks.homing_hits()) +
                    " homing_patch=" + std::to_string(gameHooks.homing_state()) +
                    " keyboard_stick_writes=" + std::to_string(gameHooks.stick_writes()));
                return;
            }
            auto menus = base ? reader.menus(memory) : std::vector<Menu>{};
            validate_status(memory);
            validate_qtes(memory);
            narrate_qtes();
            for (auto movie : gameplay.movies())
                if (memory.u32(movie.pointer + 0x20) != movie.index ||
                    !(memory.u32(movie.pointer + 0xc8) & 0x80000000))
                    gameplay.closed(movie);
            for (auto movie : gameplay.boxes()) {
                if (memory.u32(movie.pointer + 0x20) != movie.index ||
                    !(memory.u32(movie.pointer + 0xc8) & 0x80000000)) {
                    gameplay.closed(movie);
                    continue;
                }
                // Box tutorials are authored inside the movie rather than delivered as a
                // FString. Reuse the visible-tree decoder only after actual UI events.
                auto gfx = memory.u32(movie.pointer + 0x180);
                if (memory.u32(gfx + 0x40) != 0x48)
                    continue;
                auto root = memory.u32(memory.u32(gfx + 0x44) + 4);
                std::string text;
                for (const auto &line : reader.text(memory, root)) {
                    if (text.size() + line.size() > 8192)
                        break;
                    append_phrase(text, line);
                }
                gameplay.box_text(movie, std::move(text));
            }
            narrate_text();
            for (const auto &menu : menus)
                if (reader.class_name(memory, menu.movie) == "BCGFxUI_TitleMenu")
                    source.watch_title(menu.movie);
            std::string current, context;
            bool incomplete = !menus.empty() && menus.back().pending;
            if (!menus.empty() && !incomplete) {
                auto &menu = menus.back();
                context = menu_context(menu);
                current = menu_selection_state(menu);
            }
            dialogReturn.observe(current, menus.empty() ? nullptr : &menus.back());
            // A game menu (the pause menu) taking over during a mod tutorial ends the
            // tutorial: its box can no longer be confirmed, and the beacon must not
            // stay held back.
            if (!current.empty() && gameHooks.tutorial_running()) {
                gameHooks.cancel_tutorial();
                write("MOD_TUTORIAL interrupted by a game menu");
                if (qteNoteActive)
                    release_qte("interrupted");
            }
            if (current == pending)
                stable++;
            else {
                pending = current;
                stable = 0;
            }
            if (!current.empty())
                emptySince = 0;
            else if (!emptySince)
                emptySince = GetTickCount64();
            bool settled = !incomplete &&
                           (current.empty() ? GetTickCount64() - emptySince >= 250 : stable >= 1);
            if (settled) {
                scheduled = false;
                if (current != last) {
                    bool entered = lastContext != context;
                    lastContext = context;
                    last = current;
                    if (!menus.empty()) {
                        // Repeat keeps the full line; a changed setting value alone is
                        // announced.
                        lastSpeech = describe(menus.back(), entered, true);
                        announcement = entered || !lastMenu
                                           ? std::string{}
                                           : setting_value_change(*lastMenu, menus.back());
                        write("MENU " + lastSpeech +
                              (announcement.empty() ? "" : " (value: " + announcement + ")"));
                        if (announcement.empty())
                            announcement = lastSpeech;
                        lastMenu = menus.back();
                    }
                    // Losing a menu clears our narration state, but must not cancel NVDA
                    // speech that may belong to another application or notification.
                    else {
                        lastSpeech.clear();
                        announcement.clear();
                        spoken.clear();
                        lastMenu.reset();
                    }
                }
                if (notifications.focused() && !muted && speaking && !current.empty() &&
                    (spoken != current || forceContext)) {
                    if (forceContext)
                        announcement = lastSpeech = describe(menus.back(), true, true);
                    if (forceContext || !dialogReturn.quiet(current))
                        speech.speak(announcement, menu_interrupts(menus.back(), interruptText));
                    else
                        write("MENU_RETURN unchanged after a confirmed dialog; not announced");
                    spoken = current;
                }
                forceContext = false;
            } else if (retries) {
                --retries;
                scheduled = true;
                due = GetTickCount64() + (current.empty() && !incomplete ? 250 : 33);
            } else
                scheduled = false;
            burst = GetTickCount64();
            // Counted for EVENT_STATS in dumps; each read is no longer logged.
            ++reads;
        };
        // The spoken settings menu (Ctrl+O or both stick clicks). Each change applies
        // at once and is saved to accessibility.ini.
        SettingsMenu settingsMenu;
        bool menuPaused = false;
        auto menu_say = [&](const std::string &text) {
            write("SETTINGS " + text);
            if (speaking && !text.empty())
                speech.speak(text, true);
        };
        auto set_cue_gains = [&] {
            beaconAudio.set_gain(cueGain * shootShare);
            walkAudio.set_gain(cueGain * walkShare);
            meleeAudio.set_gain(cueGain * meleeShare);
            lookAudio.set_gain(cueGain * lookShare);
            countInAudio.set_gain(cueGain * countInShare);
            fireAudio.set_gain(cueGain * fireShare);
            jumpAudio.set_gain(cueGain * fireShare);
        };
        auto apply_setting = [&](const std::string &key, const std::string &value) {
            WritePrivateProfileStringW(L"gameplay", wide(key).c_str(), wide(value).c_str(),
                                       ini.c_str());
            const bool on = value == "1";
            const float number = float(std::atof(value.c_str()));
            if (key == "subtitles" || key == "tutorials") {
                (key == "subtitles" ? textOptions.subtitles : textOptions.tutorials) = on;
                gameplay.options(textOptions);
            } else if (key == "qtes") {
                qteEnabled = on;
                gameHooks.options(textHooks && qteEnabled, boostPointBlank, boostMaxRange);
            } else if (key == "qte_interrupt")
                interruptQte = on;
            else if (key == "interrupt")
                interruptText = on;
            else if (key == "status_warnings")
                statusWarnings = on;
            else if (key == "low_health_percent")
                status.low_health(int(number));
            else if (key == "shooting_mode") {
                gameHooks.stop_lock_trace();
                shootingMode = shooting_mode(value, -1);
                gameHooks.shooting(shootingMode);
                beacons.reset();
                lastAim.reset();
                beaconAudio.silence();
            } else if (key == "aim_priority")
                gameHooks.aim_priority(aim_priority(value));
            else if (key == "boost_point_blank" || key == "boost_max_range") {
                (key == "boost_point_blank" ? boostPointBlank : boostMaxRange) =
                    std::clamp(number, 1.f, 10.f);
                gameHooks.options(textHooks && qteEnabled, boostPointBlank, boostMaxRange);
            } else if (key == "boost_homing")
                gameHooks.homing_boost(std::clamp(number, 1.f, 10.f));
            else if (key == "tracking_strength")
                gameHooks.tracking(std::clamp(number, 0.f, 100.f) / 100.f);
            else if (key == "walk_beacon") {
                walkEnabled = on;
                gameHooks.walk_beacon(on);
                if (!on)
                    walkAudio.silence();
            } else if (key == "look_beacon") {
                lookEnabled = on;
                if (!on)
                    lookAudio.silence();
            } else if (key == "melee_beacon") {
                meleeEnabled = on;
                if (!on)
                    meleeAudio.silence();
            } else if (key == "qte_wording") {
                qtes.quick(value == "quick");
            } else if (key == "mod_tips") {
                modTips = on;
                gameHooks.mod_tips(on);
            } else if (key == "mod_tutorials") {
                modTutorials = on;
                gameHooks.mod_tutorials(on);
            } else if (key == "audio_cue_volume") {
                cueGain = std::clamp(number, 0.f, 100.f) * .004f;
                set_cue_gains();
            } else if (key.ends_with("_volume")) {
                auto share = std::clamp(number, 0.f, 100.f) / 100.f;
                if (key == "shooting_beacon_volume")
                    shootShare = share;
                else if (key == "walk_beacon_volume")
                    walkShare = share;
                else if (key == "melee_beacon_volume")
                    meleeShare = share;
                else if (key == "look_beacon_volume")
                    lookShare = share;
                else if (key == "fire_warning_volume")
                    fireShare = share;
                else if (key == "count_in_volume")
                    countInShare = share;
                set_cue_gains();
            }
        };
        // Pausing uses BCGFxMovieScene.SetGamePause, resolved once.
        auto pause_ready = [&] {
            if (gameHooks.pause_configured())
                return true;
            auto runtimeInstance = instance();
            auto base = runtimeInstance ? membase(runtimeInstance) : nullptr;
            Memory memory([&](uint32_t at, void *out, size_t size) {
                SIZE_T got{};
                return base && ReadProcessMemory(GetCurrentProcess(), base + at, out, size, &got) &&
                       got == size;
            });
            if (!reader.ready(memory))
                return false;
            const auto &layout = reader.layout();
            auto scene = ScriptLookup::find(memory, reader, layout, "BCGFxMovieScene", "Class");
            auto setPause = ScriptLookup::function(memory, reader, scene, "SetGamePause");
            uint32_t tick = 0;
            auto names = memory.u32(layout.names_global + 4);
            for (uint32_t i = 0; i < names && i < 250000 && !tick; ++i)
                if (reader.name(memory, i) == "Tick")
                    tick = i;
            gameHooks.configure_pause(setPause, scene, tick);
            return gameHooks.pause_configured();
        };
        auto open_menu = [&] {
            auto text = settingsMenu.open([&](const std::string &key, const std::string &fallback) {
                wchar_t value[64]{};
                GetPrivateProfileStringW(L"gameplay", wide(key).c_str(), wide(fallback).c_str(),
                                         value, 64, ini.c_str());
                return utf8(value);
            });
            gameHooks.menu_open(true);
            notifications.menu_keys(true);
            // Pause gameplay only: a game menu or the pause menu already stops it.
            if (last.empty() && pause_ready()) {
                gameHooks.request_pause(true);
                menuPaused = true;
            }
            menu_say(text);
        };
        auto close_menu = [&] {
            if (menuPaused)
                gameHooks.request_pause(false);
            menuPaused = false;
            gameHooks.menu_open(false);
            notifications.menu_keys(false);
            menu_say(settingsMenu.close());
        };
        auto menu_input = [&](int input) {
            if (input == GameplayHooks::menu_toggle) {
                settingsMenu.is_open() ? close_menu() : open_menu();
                return;
            }
            if (!settingsMenu.is_open())
                return;
            if (input == GameplayHooks::menu_up || input == GameplayHooks::menu_down)
                menu_say(settingsMenu.move(input == GameplayHooks::menu_up ? -1 : 1));
            else if (input == GameplayHooks::menu_back)
                close_menu();
            else if (auto change =
                         settingsMenu.adjust(input == GameplayHooks::menu_left ? -1 : 1)) {
                apply_setting(change->key, change->value);
                menu_say(change->speech);
            }
        };
        auto command = [&](unsigned i) {
            if (i >= commands.size())
                return;
            const auto &name = commands[i];
            if (name == L"repeat" && speaking && !muted) {
                auto text = lastSpeech;
                if (last.empty()) {
                    text = timing.repeat();
                    if (text.empty())
                        text = qtes.repeat();
                    if (text.empty())
                        text = gameplay.repeat();
                }
                speech.speak(text.empty() ? "No supported text is active." : text);
            }
            if (name == L"mute") {
                muted = !muted;
                speech.stop();
                if (speaking)
                    speech.speak(muted ? "Narration off." : "Narration on.");
            }
            if (name == L"dump")
                inspect(true);
            if (name == L"settings")
                menu_input(GameplayHooks::menu_toggle);
            // Keyboard equivalents of the D-pad shooting controls.
            // In melee outside rapid fire it toggles the melee beacon, like D-pad Up.
            if (name == L"aimmode")
                shooting_command(meleeSeen && GetTickCount64() - meleeSeen <= 300 ? 5 : 3);
            if (name == L"aimprev")
                shooting_command(1);
            if (name == L"aimnext")
                shooting_command(2);
            if (name == L"aimpriority")
                shooting_command(4);
            // Keyboard right stick in menus: the records and controls browsers.
            if (name == L"rsup" || name == L"rsdown" || name == L"rsleft" || name == L"rsright") {
                if (auto movie = reader.right_stick_movie()) {
                    auto runtimeInstance = instance();
                    auto base = runtimeInstance ? membase(runtimeInstance) : nullptr;
                    Memory memory([&](uint32_t at, void *out, size_t size) {
                        SIZE_T got{};
                        return base &&
                               ReadProcessMemory(GetCurrentProcess(), base + at, out, size, &got) &&
                               got == size;
                    });
                    GameplayEvent event;
                    event.record_command = name == L"rsleft"    ? 1
                                           : name == L"rsright" ? 2
                                           : name == L"rsup"    ? 3
                                                                : 4;
                    event.record_movie = movie;
                    event.record_index = memory.u32(movie + 0x20);
                    gameHooks.inject(event);
                }
            }
            if (name == L"status" && speaking && !muted) {
                auto runtimeInstance = instance();
                auto base = runtimeInstance ? membase(runtimeInstance) : nullptr;
                Memory memory([&](uint32_t at, void *out, size_t size) {
                    SIZE_T got{};
                    return base &&
                           ReadProcessMemory(GetCurrentProcess(), base + at, out, size, &got) &&
                           got == size;
                });
                validate_status(memory);
                auto text = status.status();
                write("GAME_STATUS_REQUEST " + text);
                speech.speak(text, false);
            }
        };
        write(
            "Event reader started: assignment, raw assignment, movie open/close, text, display flags, title script assignment");
        while (!stopping) {
            auto now = GetTickCount64();
            DWORD timeout = scheduled ? DWORD(due > now ? due - now : 0) : INFINITE;
            if (scheduled && due <= now) {
                inspect(false);
                continue;
            }
            auto wait = MsgWaitForMultipleObjectsEx(DWORD(events.size()), events.data(), timeout,
                                                    QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (stopping)
                break;
            if (wait == WAIT_TIMEOUT) {
                inspect(false);
                continue;
            }
            if (wait >= WAIT_OBJECT_0 && wait < WAIT_OBJECT_0 + events.size()) {
                auto index = wait - WAIT_OBJECT_0;
                if (index == commands.size())
                    changed();
                else if (index > commands.size()) {
                    drain_lock_trace();
                    drain_beacons();
                    bool overflow = false, nativeOverflow = false;
                    auto batch = presentations.take(overflow);
                    auto native = gameHooks.take(nativeOverflow);
                    if (batch.empty() && native.empty() && !overflow && !nativeOverflow)
                        continue;
                    auto now = GetTickCount64();
                    bool stale =
                        std::any_of(batch.begin(), batch.end(),
                                    [&](const PresentationEvent &event) {
                                        return now - event.tick > 2000;
                                    }) ||
                        std::any_of(native.begin(), native.end(), [&](const GameplayEvent &event) {
                            return now - event.tick > 200;
                        });
                    if (overflow || nativeOverflow || stale) {
                        gameplay.reset();
                        status.reset();
                        qtes.reset();
                        timing.reset();
                        countInAudio.silence();
                        beacons.reset();
                        beaconAudio.silence();
                        walkAudio.silence();
                        meleeAudio.silence();
                        lookAudio.silence();
                        lastAim.reset();
                        reader.clear_script_scenes();
                        if (textHooks)
                            presentations.capture(true);
                        if (nativeGameplay)
                            gameHooks.capture(true);
                        write(
                            "GAME_CUE_RESET discarded stale/overflowed batch; resynchronizing producers");
                    } else {
                        auto runtimeInstance = instance();
                        auto base = runtimeInstance ? membase(runtimeInstance) : nullptr;
                        Memory memory([&](uint32_t at, void *out, size_t n) {
                            SIZE_T got{};
                            return base &&
                                   ReadProcessMemory(GetCurrentProcess(), base + at, out, n,
                                                     &got) &&
                                   got == n;
                        });
                        auto name = [&](CueName value) {
                            auto text = reader.name(memory, value.index);
                            if (value.number && !text.empty())
                                text += "_" + std::to_string(value.number - 1);
                            return text;
                        };
                        std::optional<StatusContext> statusRequest;
                        std::optional<std::pair<uint32_t, uint32_t>> menuRepeat;
                        struct Ordered {
                            uint64_t order;
                            bool native;
                            size_t index;
                        };
                        std::vector<Ordered> order;
                        for (size_t i = 0; i < batch.size(); ++i)
                            order.push_back({batch[i].order, false, i});
                        for (size_t i = 0; i < native.size(); ++i)
                            order.push_back({native[i].order, true, i});
                        std::sort(order.begin(), order.end(),
                                  [](const Ordered &left, const Ordered &right) {
                                      return left.order < right.order;
                                  });
                        for (const auto &item : order) {
                            if (!item.native) {
                                const auto &event = batch[item.index];
                                gameplay.apply(event);
                                status.apply(event);
                                qtes.apply(event);
                                timing.apply(event);
                                if (event.kind == PresentationKind::button_create)
                                    write("QTE_ICON tag=" + event.tag.utf8() +
                                          " input=" + event.type.utf8() +
                                          " revision=" + std::to_string(event.revision));
                            } else {
                                const auto &event = native[item.index];
                                if (event.script_scene) {
                                    reader.script_scene(memory, event.scene_movie,
                                                        event.scene_index, event.scene_active);
                                    changed();
                                } else if (event.record_info) {
                                    if (memory.u32(event.record_movie + 0x20) ==
                                            event.record_index &&
                                        movie_class(memory, event.record_movie,
                                                    "BCGFxUI_ChapterMenu")) {
                                        reader.record_info(event.record_movie, event.record_index,
                                                           event.record_story.utf8(),
                                                           event.record_difficulty,
                                                           event.record_text.utf8());
                                        changed();
                                    }
                                } else if (event.record_command &&
                                           memory.u32(event.record_movie + 0x20) ==
                                               event.record_index &&
                                           movie_class(memory, event.record_movie,
                                                       "BCGFxUI_OptionMenu")) {
                                    // Control Settings: the right stick reads one slot.
                                    auto line = reader.navigate_controls(memory, event.record_movie,
                                                                         event.record_command);
                                    if (!line.empty()) {
                                        write("MENU_CONTROL " + line);
                                        lastSpeech = line;
                                        if (notifications.focused() && !muted && speaking)
                                            speech.speak(line);
                                    }
                                } else if (event.record_command) {
                                    if (notifications.focused() &&
                                        memory.u32(event.record_movie + 0x20) ==
                                            event.record_index &&
                                        movie_class(memory, event.record_movie,
                                                    "BCGFxUI_ChapterMenu"))
                                        if (auto menu = reader.navigate_record(
                                                memory, event.record_movie, event.record_command)) {
                                            // This cursor belongs to the mod: its new state is
                                            // already complete, so no animation settling delay is needed.
                                            lastContext = menu_context(*menu);
                                            last = pending = menu_selection_state(*menu);
                                            dialogReturn.observe(last, nullptr);
                                            stable = 1;
                                            announcement = lastSpeech = record_navigation_text(
                                                *menu, event.record_command <= 2);
                                            lastMenu = *menu;
                                            write("MENU_RECORD age_ms=" +
                                                  std::to_string(GetTickCount64() - event.tick) +
                                                  " " + lastSpeech);
                                            if (!muted && speaking) {
                                                speech.speak(lastSpeech);
                                                spoken = last;
                                                forceContext = false;
                                            }
                                        }
                                } else if (event.menu_input) {
                                    menu_input(event.menu_input);
                                } else if (event.tutorial_level) {
                                    auto level = reader.object_name(memory, event.tutorial_level);
                                    write(std::string(event.tutorial_melee
                                                          ? "MOD_TUTORIAL melee in "
                                                          : "MOD_TUTORIAL rail shooting in ") +
                                          level);
                                    uint64_t key = uint64_t(event.tutorial_level) << 32 |
                                                   event.tutorial_level_index;
                                    // Episode 1's first rail shooting, or Episode 8's chase (a
                                    // shooting section with the game's lock-on off).
                                    // Another map: the QTE note belongs only to the Episode 1
                                    // run whose rail shooting was reported.
                                    if (level != "BC-S000")
                                        tutorialLevel = 0;
                                    // Episode 2's first melee section gets the melee one.
                                    const bool episode1 = !event.tutorial_melee &&
                                                          level == "BC-S000" &&
                                                          key != tutorialLevel,
                                               chase = !event.tutorial_melee &&
                                                       level == "BC-S070" &&
                                                       event.tutorial_no_lock && key != chaseLevel,
                                               melee = event.tutorial_melee && level == "BC-S010" &&
                                                       key != meleeLevel;
                                    if (episode1 || chase || melee) {
                                        (episode1 ? tutorialLevel
                                         : chase  ? chaseLevel
                                                  : meleeLevel) = key;
                                        auto run = tutorial_run(memory);
                                        run.boxes = qte_note();
                                        if (run.valid())
                                            tutorialBase = run;
                                        TutorialLabels labels;
                                        auto label = [&](std::string_view token, std::string &out) {
                                            if (auto name = prompts.button(token); !name.empty())
                                                out = name;
                                        };
                                        label("A", labels.cross);
                                        label("X", labels.square);
                                        label("Y", labels.triangle);
                                        label("LB", labels.l1);
                                        label("B", labels.circle);
                                        label("RT", labels.r2);
                                        auto up = prompts.dpad_key("up"),
                                             left = prompts.dpad_key("left"),
                                             right = prompts.dpad_key("right");
                                        if (!up.empty())
                                            labels.dpad_up = up;
                                        if (!left.empty() && !right.empty())
                                            labels.dpad_left_right = left + " or " + right;
                                        run.boxes = episode1 ? rail_shooting_tutorial(labels)
                                                    : chase  ? chase_tutorial(labels)
                                                             : melee_tutorial(labels);
                                        if (run.valid()) {
                                            gameHooks.start_tutorial(std::move(run));
                                            write(episode1 ? "MOD_TUTORIAL rail shooting started"
                                                  : chase  ? "MOD_TUTORIAL chase started"
                                                           : "MOD_TUTORIAL melee started");
                                        } else
                                            write("MOD_TUTORIAL unavailable: lookup incomplete");
                                    }
                                } else if (event.fight_tip) {
                                    write("MOD_TIP " + std::to_string(event.fight_tip) + " for " +
                                          reader.class_name(memory, event.tip_enemy));
                                    if (modTips && !gameHooks.tutorial_running()) {
                                        auto run =
                                            tutorialBase ? *tutorialBase : tutorial_run(memory);
                                        if (run.valid() && !tutorialBase)
                                            tutorialBase = run;
                                        run.boxes = fight_tip(FightTip(event.fight_tip));
                                        if (run.valid() && !run.boxes.empty()) {
                                            gameHooks.start_tutorial(std::move(run));
                                            write("MOD_TIP started");
                                        } else
                                            write("MOD_TIP unavailable: lookup incomplete");
                                    }
                                } else if (event.tutorial_step) {
                                    constexpr const char *steps[] = {"",         "opened", "sound",
                                                                     "finished", "failed", "beacon",
                                                                     "fire"};
                                    write("MOD_TUTORIAL box " +
                                          std::to_string(event.tutorial_box + 1) + " " +
                                          (event.tutorial_step < 7 ? steps[event.tutorial_step]
                                                                   : "unknown"));
                                    if (event.tutorial_step == GameplayHooks::tutorial_beacon)
                                        beacon_demo();
                                    // The fire warning: centred, sliding to the right.
                                    if (event.tutorial_step == GameplayHooks::tutorial_fire) {
                                        FireCue demo{true, 0.f, 1.f, GetTickCount64(), 2000, 1.f};
                                        fire_output(demo, true);
                                    }
                                    if (qteNoteActive &&
                                        event.tutorial_step == GameplayHooks::tutorial_opened)
                                        qteNoteOpened = true;
                                    if (qteNoteActive &&
                                        (event.tutorial_step == GameplayHooks::tutorial_finished ||
                                         event.tutorial_step == GameplayHooks::tutorial_failed))
                                        release_qte(event.tutorial_step ==
                                                            GameplayHooks::tutorial_finished
                                                        ? "confirmed"
                                                        : "failed");
                                } else if (event.shooting_command) {
                                    if (notifications.focused() &&
                                        capture_status_context(memory, event.status.input, false) ==
                                            event.status)
                                        shooting_command(event.shooting_command);
                                } else if (event.menu_changed)
                                    changed();
                                else if (event.menu_repeat)
                                    menuRepeat = {{event.repeat_movie, event.repeat_index}};
                                else if (event.status_request)
                                    statusRequest = event.status;
                                else if (event.sync_grade) {
                                    timing.grade(event.sync_grade);
                                    write("SYNC_GRADE " + std::to_string(event.sync_grade));
                                } else if (event.timing) {
                                    timing.apply(event.sync, event.order, name);
                                    write(
                                        "TIMING_STATE source=" + std::to_string(event.sync.source) +
                                        " track=" + std::to_string(event.sync.track) +
                                        " active=" + std::to_string(event.sync.active) +
                                        " window=" + std::to_string(event.sync.window) +
                                        " cue=" + std::to_string(event.sync.cue) +
                                        " until=" + std::to_string(event.sync.until));
                                    count_in_output();
                                    count_in_log();
                                } else {
                                    qtes.apply(event.group, event.order, name);
                                    unsigned visible = 0;
                                    for (unsigned i = 0; i < event.group.count; ++i)
                                        visible += event.group.commands[i].visible;
                                    write("QTE_STATE source=" + std::to_string(event.group.source) +
                                          " active=" + std::to_string(event.group.active) +
                                          " mode=" + std::to_string(event.group.mode) +
                                          " visible=" + std::to_string(visible) +
                                          " required=" + std::to_string(event.group.required));
                                }
                            }
                        }
                        validate_status(memory);
                        validate_qtes(memory);
                        narrate_text();
                        narrate_status();
                        if (statusRequest &&
                            capture_status_context(memory, statusRequest->input) == statusRequest &&
                            movie_class(memory, statusRequest->input, "BCPlayerInput") &&
                            movie_class(memory, statusRequest->controller, "BCPlayerController")) {
                            auto text =
                                status.status({statusRequest->gauge, statusRequest->gauge_index});
                            if (!text.empty()) {
                                write("GAME_STATUS_REQUEST Back " + text);
                                if (notifications.focused() && !muted && speaking)
                                    speech.speak(text, false);
                            }
                        }
                        if (menuRepeat) {
                            auto [movie, index] = *menuRepeat;
                            if (memory.u32(movie + 0x20) == index &&
                                (memory.u32(movie + 0xc8) & 0x80000000) &&
                                (movie_class(memory, movie, "BCGFxUI_ResultMenu") ||
                                 movie_class(memory, movie, "BCGFxUI_ChapterMenu"))) {
                                auto text = menu_repeat_text(reader.menus(memory), movie);
                                if (!text.empty()) {
                                    write("MENU_REPEAT " + text);
                                    if (notifications.focused() && !muted && speaking)
                                        speech.speak(text);
                                }
                            }
                        }
                        narrate_qtes();
                        if (!gameplay.boxes().empty())
                            changed();
                    }
                } else
                    command(index);
            } else if (wait == WAIT_OBJECT_0 + events.size()) {
                MSG message;
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message == WindowNotifications::focus_message) {
                        bool was = notifications.focused();
                        bool focused = notifications.update();
                        if (!focused) {
                            beaconAudio.silence();
                            walkAudio.silence();
                            meleeAudio.silence();
                            lookAudio.silence();
                        }
                        if (focused && !was) {
                            forceContext = true;
                            source.request_discovery();
                            changed();
                        }
                    } else if (message.message == WM_HOTKEY && message.wParam >= 100 &&
                               message.wParam < 100 + commands.size() && notifications.focused())
                        command(unsigned(message.wParam - 100));
                    else if (message.message == WM_HOTKEY &&
                             message.wParam >= WindowNotifications::kMenuKeyBase &&
                             message.wParam < WindowNotifications::kMenuKeyBase + 6) {
                        constexpr int keys[]{GameplayHooks::menu_up,      GameplayHooks::menu_down,
                                             GameplayHooks::menu_left,    GameplayHooks::menu_right,
                                             GameplayHooks::menu_confirm, GameplayHooks::menu_back};
                        menu_input(keys[message.wParam - WindowNotifications::kMenuKeyBase]);
                    } else {
                        TranslateMessage(&message);
                        DispatchMessageW(&message);
                    }
                }
            } else {
                write("Event wait failed: " + std::to_string(GetLastError()));
                break;
            }
        }
        gameHooks.capture(false);
        beaconAudio.close();
        walkAudio.close();
        meleeAudio.close();
        lookAudio.close();
        jumpAudio.close();
        countInAudio.close();
        drain_lock_trace();
        presentations.capture(false);
        source.capture(false);
        speech.stop();
        speech.close();
        for (size_t i = 0; i < commands.size(); i++)
            CloseHandle(events[i]);
        if (SUCCEEDED(co))
            CoUninitialize();
        write("Accessibility stopped");
    } catch (...) {
        OutputDebugStringA("Blind Rage worker failed\n");
        return 3;
    }
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI A11yInitialize(void *) {
    std::lock_guard lock(lifecycle);
    if (worker && WaitForSingleObject(worker, 0) == WAIT_TIMEOUT)
        return 1;
    if (worker)
        CloseHandle(worker);
    stopping = false;
    worker = CreateThread(nullptr, 0, run, nullptr, 0, nullptr);
    return worker ? 1 : 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI A11yShutdown(void *) {
    std::lock_guard lock(lifecycle);
    stopping = true;
    SetEvent(UiHookSource::instance().event());
    if (worker) {
        if (WaitForSingleObject(worker, 5000) != WAIT_OBJECT_0)
            return 0;
        CloseHandle(worker);
        worker = nullptr;
    }
    return 1;
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
