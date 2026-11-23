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
        std::vector<std::wstring> commands = {L"repeat",  L"mute",    L"dump",    L"status",
                                              L"aimmode", L"aimprev", L"aimnext", L"aimpriority"};
        std::vector<HANDLE> events;
        for (auto &c : commands)
            events.push_back(
                CreateEventW(nullptr, FALSE, FALSE, event_name(GetCurrentProcessId(), c).c_str()));
        // Shortcuts are written as key names ("Ctrl+Alt+F9"); legacy numeric
        // virtual-key codes still mean Ctrl+Alt plus that key. A missing or empty
        // value uses the default; "None" disables the shortcut.
        constexpr UINT chord = MOD_CONTROL | MOD_ALT;
        const Hotkey defaults[] = {{chord, VK_F2},  {chord, VK_F5}, {chord, VK_F8},
                                   {chord, VK_F4},  {chord, VK_F9}, {chord, VK_F10},
                                   {chord, VK_F12}, {chord, VK_F11}};
        Hotkey hotkeys[10]{};
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
        std::string last, pending, lastSpeech, lastContext, spoken;
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
        // One label source for menus and tutorials. The recomp's prompt names do
        // not cover the sticks, so keyboard stick tags stay unlabeled.
        auto buttonLabel = [&](std::string_view token) {
            if (token == "LS" || token == "RS" || token == "R3") {
                if (prompts.device() == PromptDevice::keyboard ||
                    prompts.device() == PromptDevice::unknown)
                    return std::string{};
                return std::string(token == "LS"   ? "Left stick"
                                   : token == "RS" ? "Right stick"
                                                   : "Right stick press");
            }
            return prompts.button(token);
        };
        reader.button_labels(buttonLabel);
        GameplayText gameplay({setting(L"subtitles", 1), setting(L"tutorials", 1)}, buttonLabel);
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
        gameHooks.options(textHooks && qteEnabled, boostPointBlank, boostMaxRange,
                          setting(L"status_button", 1));
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
                GetPrivateProfileIntW(L"gameplay", L"audio_cue_volume", 30, ini.c_str()), 0, 100)) *
            .004f;
        AudioRetry audioRetry;
        bool audioReady = false, audioAnnounced = false;
        auto beacon_output = [&](BeaconCue cue) {
            if (!notifications.focused() || muted || !shooting_assisted(shootingMode))
                cue.active = false;
            // Start on first need and rebuild after a device error, with backoff.
            if (cue.active && !(audioReady && beaconAudio.healthy()) &&
                audioRetry.due(GetTickCount64())) {
                if (audioReady)
                    write("BEACON_AUDIO lost; restarting");
                beaconAudio.close();
                audioReady = beaconAudio.start(cueGain);
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
        const bool walkEnabled = setting(L"walk_beacon", 1);
        gameHooks.walk_beacon(walkEnabled);
        BeaconAudio walkAudio;
        AudioRetry walkRetry;
        bool walkReady = false;
        uint64_t walkTarget = 0;
        auto walk_output = [&](BeaconCue cue) {
            if (!notifications.focused() || muted || !walkEnabled)
                cue.active = false;
            if (cue.active && !(walkReady && walkAudio.healthy()) &&
                walkRetry.due(GetTickCount64())) {
                walkAudio.close();
                walkReady = walkAudio.start(cueGain);
                if (walkReady)
                    walkRetry.success();
                else
                    walkRetry.failure(GetTickCount64());
                write(walkReady ? "WALK_AUDIO ready" : "WALK_AUDIO unavailable");
            }
            if (walkReady && walkAudio.healthy())
                walkAudio.update(cue);
        };
        auto drain_beacons = [&] {
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
                          " remaining=" + std::to_string(walk->remaining));
                if (cue.active)
                    walkTarget = cue.generation;
                walk_output(cue);
            }
        };
        auto shooting_command = [&](int command) {
            if (command == 3) {
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
        SynchronicReader timing([&](std::string_view tag) { return prompts.button(tag); });
        if (textHooks) {
            events.push_back(presentations.event());
            presentations.capture(true);
            write("Gameplay text observer started (prototype)");
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
        int64_t countInPerfect = 0;
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
                countInReady = countInAudio.start(cueGain);
                if (countInReady)
                    countInRetry.success();
                else
                    countInRetry.failure(GetTickCount64());
                write(countInReady ? "COUNT_IN_AUDIO ready" : "COUNT_IN_AUDIO unavailable");
            }
            if (countInReady && countInAudio.healthy())
                countInAudio.update(*cue);
            if (cue->active)
                countInPerfect = cue->perfect;
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
            text = "COUNT_IN_PLAYED generation=" + std::to_string(played.generation) + text;
            if (text != countInLogged) {
                countInLogged = text;
                write(text);
            }
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
            auto started = GetTickCount64();
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
                    " homing_patch=" + std::to_string(gameHooks.homing_state()));
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
                        lastSpeech = describe(menus.back(), entered, true);
                        write("MENU " + lastSpeech);
                    }
                    // Losing a menu clears our narration state, but must not cancel NVDA
                    // speech that may belong to another application or notification.
                    else {
                        lastSpeech.clear();
                        spoken.clear();
                    }
                }
                if (notifications.focused() && !muted && speaking && !current.empty() &&
                    (spoken != current || forceContext)) {
                    if (forceContext)
                        lastSpeech = describe(menus.back(), true, true);
                    if (forceContext || !dialogReturn.quiet(current))
                        speech.speak(lastSpeech, menu_interrupts(menus.back(), interruptText));
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
            ++reads;
            write("EVENT_READ " + std::to_string(reads) +
                  " ms=" + std::to_string(GetTickCount64() - started) + " pages=" +
                  std::to_string(pages.size()) + " discover=" + std::to_string(discover) +
                  " mutations=" + std::to_string(source.changes()));
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
            // Keyboard equivalents of the D-pad shooting controls.
            if (name == L"aimmode")
                shooting_command(3);
            if (name == L"aimprev")
                shooting_command(1);
            if (name == L"aimnext")
                shooting_command(2);
            if (name == L"aimpriority")
                shooting_command(4);
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
                                            lastSpeech = record_navigation_text(
                                                *menu, event.record_command <= 2);
                                            write("MENU_RECORD age_ms=" +
                                                  std::to_string(GetTickCount64() - event.tick) +
                                                  " " + lastSpeech);
                                            if (!muted && speaking) {
                                                speech.speak(lastSpeech);
                                                spoken = last;
                                                forceContext = false;
                                            }
                                        }
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
                        }
                        if (focused && !was) {
                            forceContext = true;
                            source.request_discovery();
                            changed();
                        }
                    } else if (message.message == WM_HOTKEY && message.wParam >= 100 &&
                               message.wParam < 100 + commands.size() && notifications.focused())
                        command(unsigned(message.wParam - 100));
                    else {
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
