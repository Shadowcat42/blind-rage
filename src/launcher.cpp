#include "platform.h"
#include "restart_support.h"
#include <iostream>
using namespace a11y;
static DWORD remote_call(HANDLE process, uintptr_t fn, void *arg) {
    HANDLE thread =
        CreateRemoteThread(process, nullptr, 0, (LPTHREAD_START_ROUTINE)fn, arg, 0, nullptr);
    if (!thread)
        throw std::runtime_error("Cannot create initialization thread");
    auto wait = WaitForSingleObject(thread, 15000);
    DWORD result{};
    if (wait == WAIT_OBJECT_0)
        GetExitCodeThread(thread, &result);
    CloseHandle(thread);
    if (wait != WAIT_OBJECT_0)
        throw std::runtime_error("Initialization timed out; game left running");
    return result;
}
int wmain(int argc, wchar_t **argv) {
    try {
        auto dir = module_path().parent_path();
        auto root = fs::exists(dir / L"mods" / L"accessibility")
                        ? dir
                        : dir.parent_path().parent_path().parent_path();
        auto dist = root / L"mods" / L"accessibility" / L"dist";
        fs::path bin;
        std::string error;
        DWORD pid = 0, restartParent = 0;
        std::wstring command;
        std::optional<std::wstring> restartCommand;
        bool checkOnly = false;
        const BuildProfile *requested = nullptr;
        const BuildProfile *selected = nullptr;
        for (int i = 1; i < argc; i++) {
            std::wstring arg = argv[i];
            if (arg == L"--attach" && i + 1 < argc) {
                wchar_t *end{};
                pid = wcstoul(argv[++i], &end, 10);
                if (!pid || *end)
                    throw std::runtime_error("Invalid process ID");
            } else if (arg == L"--command" && i + 1 < argc)
                command = argv[++i];
            else if (arg == L"--check")
                checkOnly = true;
            else if (arg == L"--restart-parent" && i + 1 < argc)
                restartParent = restart_parent_id(argv[++i]);
            else if (arg == L"--restart-command" && i + 1 < argc)
                restartCommand = argv[++i];
            else if (arg == L"--build" && i + 1 < argc) {
                auto name = utf8(argv[++i]);
                requested = name == "auto" ? nullptr : profile_named(name);
                if (name != "auto" && !requested)
                    throw std::runtime_error("Unknown build; choose auto, disc, or updated");
            } else
                throw std::runtime_error(
                    "Usage: BlindRage.exe [--build auto|disc|updated] [--check] [--attach PID] [--command repeat|mute|dump|status|aimmode|aimprev|aimnext|aimpriority|rsup|rsdown|rsleft|rsright]");
        }
        if (restartCommand && !restartParent)
            throw std::runtime_error("Restart arguments require a restart parent");
        if (restartParent) {
            if (pid || checkOnly || !command.empty() || !requested)
                throw std::runtime_error(
                    "Restart requires one explicit build and no attach/check/command mode");
            wait_for_restart_parent(restartParent,
                                    root / requested->directory / L"asura_wrath_recomp.exe");
        }
        auto installation_build = [&](const fs::path &path) -> const BuildProfile * {
            for (auto profile : build_profiles) {
                auto expected = root / profile->directory / L"asura_wrath_recomp.exe";
                if (fs::exists(expected) && fs::equivalent(path, expected))
                    return profile;
            }
            return nullptr;
        };
        if (pid) {
            auto module = remote_module(pid, L"asura_wrath_recomp.exe");
            if (module.path.empty() || !(selected = installation_build(module.path)))
                throw std::runtime_error("Target is not a game process from this installation");
            bin = module.path.parent_path();
        } else if (!restartParent && (!checkOnly || !requested)) {
            auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            PROCESSENTRY32W p{};
            p.dwSize = sizeof(p);
            if (Process32FirstW(snapshot, &p)) {
                do {
                    if (!_wcsicmp(p.szExeFile, L"asura_wrath_recomp.exe")) {
                        auto module = remote_module(p.th32ProcessID, L"asura_wrath_recomp.exe");
                        auto profile =
                            module.path.empty() ? nullptr : installation_build(module.path);
                        if (profile) {
                            if (pid) {
                                CloseHandle(snapshot);
                                throw std::runtime_error(
                                    "Multiple game processes; use --attach PID");
                            }
                            pid = p.th32ProcessID;
                            selected = profile;
                            bin = module.path.parent_path();
                        }
                    }
                } while (Process32NextW(snapshot, &p));
            }
            CloseHandle(snapshot);
        }
        if (selected && requested && selected != requested)
            throw std::runtime_error(
                "A different game build is running. Close it or select its matching --build profile.");
        if (!selected) {
            selected = requested;
            if (!selected) {
                std::ifstream installation(root / L"userdata" / L"install.txt");
                std::optional<bool> updated;
                if (installation) {
                    updated = installed_title_update(installation);
                    if (!updated)
                        throw std::runtime_error(
                            "Cannot determine installed title update; use --build disc or --build updated");
                } else
                    updated = fs::exists(root / L"game" / L"default.xexp");
                selected = *updated ? &updated_profile : &disc_profile;
            }
            bin = root / selected->directory;
        }
        auto verified = identify_build(bin, error);
        if (!verified)
            throw std::runtime_error(error);
        if (verified != selected)
            throw std::runtime_error(
                "The selected build directory contains files from another profile");
        if (!supported_game_data(root, *verified, error))
            throw std::runtime_error(error);
        if (checkOnly) {
            std::cout << "Verified build profile: " << verified->id
                      << "\nExecutable: " << utf8((bin / L"asura_wrath_recomp.exe").wstring())
                      << "\n";
            return 0;
        }
        if (!pid) {
            if (!command.empty())
                throw std::runtime_error("No running game");
            auto exe = bin / L"asura_wrath_recomp.exe";
            auto line = restartCommand ? *restartCommand : windows_argument(exe.wstring());
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                bin.c_str(), &si, &pi))
                throw std::runtime_error("Could not launch game");
            pid = pi.dwProcessId;
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        Module game;
        for (int i = 0; i < 150; i++) {
            game = remote_module(pid, L"asura_wrath_recomp.exe");
            if (game.base && remote_module(pid, L"rexruntime.dll").base)
                break;
            Sleep(100);
        }
        if (game.path.empty() || !fs::equivalent(game.path, bin / L"asura_wrath_recomp.exe"))
            throw std::runtime_error("Target is not the selected game build");
        if (!command.empty()) {
            auto event = OpenEventW(EVENT_MODIFY_STATE, FALSE, event_name(pid, command).c_str());
            if (!event)
                throw std::runtime_error("Accessibility command unavailable");
            SetEvent(event);
            CloseHandle(event);
            return 0;
        }
        HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                         PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                     FALSE, pid);
        if (!process)
            throw std::runtime_error("Cannot open game process");
        auto mod = remote_module(pid, L"asura_accessibility.dll");
        auto dll = dist / L"asura_accessibility.dll";
        if (!mod.base) {
            auto load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
            HMODULE owner{};
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)load, &owner);
            auto target = remote_module(pid, module_path(owner).filename().c_str());
            if (!target.base)
                throw std::runtime_error("Target Windows loader unavailable");
            auto path = dll.wstring();
            auto size = (path.size() + 1) * sizeof(wchar_t);
            void *remote =
                VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            SIZE_T written{};
            if (!remote || !WriteProcessMemory(process, remote, path.c_str(), size, &written) ||
                written != size)
                throw std::runtime_error("Cannot pass DLL path to game");
            remote_call(process, target.base + uintptr_t(load) - uintptr_t(owner), remote);
            VirtualFreeEx(process, remote, 0, MEM_RELEASE);
            mod = remote_module(pid, L"asura_accessibility.dll");
            if (!mod.base)
                throw std::runtime_error("DLL failed to load");
        }
        HMODULE local = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
        if (!local)
            throw std::runtime_error("Cannot inspect accessibility DLL");
        auto init = GetProcAddress(local, "A11yInitialize");
        if (!init)
            throw std::runtime_error("Initialization export missing");
        auto rva = uintptr_t(init) - uintptr_t(local);
        FreeLibrary(local);
        auto result = remote_call(process, mod.base + rva, nullptr);
        CloseHandle(process);
        if (!result)
            throw std::runtime_error("Accessibility worker could not start");
        std::cout << "Accessibility attached to PID " << pid << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        MessageBoxW(nullptr, wide(e.what()).c_str(), L"Blind Rage", MB_OK | MB_ICONERROR);
        return 1;
    }
}
