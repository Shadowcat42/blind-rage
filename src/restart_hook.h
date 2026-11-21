#pragma once
#include "restart_support.h"
#include <atomic>

namespace a11y {
class RestartHook {
  public:
    using Function = decltype(&CreateProcessW);
    static RestartHook &instance() {
        static auto *value = new RestartHook;
        return *value;
    }
    bool install(uintptr_t executable, const BuildProfile &profile, const fs::path &launcher,
                 const fs::path &game) {
        auto slot = executable + profile.restart_rvas[0],
             caller = executable + profile.restart_rvas[1];
        // Verify that the selected instruction really calls the selected IAT slot.
        uint8_t instruction[6]{};
        SIZE_T got{};
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(caller - 6),
                               instruction, 6, &got) ||
            got != 6 || instruction[0] != 0xff || instruction[1] != 0x15)
            return false;
        int32_t displacement{};
        std::memcpy(&displacement, instruction + 2, 4);
        if (uintptr_t(intptr_t(caller) + displacement) != slot || !fs::is_regular_file(launcher))
            return false;
        return install_slot(reinterpret_cast<void **>(slot), caller, launcher, game, profile.id);
    }
    bool install_slot(void **slot, uintptr_t caller, const fs::path &launcher, const fs::path &game,
                      std::string_view profile) {
        if (slot_)
            return slot_ == slot;
        if (!slot || !*slot || !caller)
            return false;
        launcher_ = launcher.wstring();
        game_ = game.wstring();
        profile_ = wide(std::string(profile));
        caller_ = caller;
        original_ = reinterpret_cast<Function>(*slot);
        DWORD protection{};
        if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection))
            return false;
        auto previous = InterlockedCompareExchangePointer(
            slot, reinterpret_cast<void *>(create_process), reinterpret_cast<void *>(original_));
        DWORD ignored{};
        VirtualProtect(slot, sizeof(void *), protection, &ignored);
        if (previous != reinterpret_cast<void *>(original_))
            return false;
        slot_ = slot;
        return true;
    }
    void capture(bool enabled) {
        enabled_ = enabled;
    }
    uint64_t redirects() const {
        return redirects_.load();
    }

  private:
    static BOOL WINAPI create_process(LPCWSTR application, LPWSTR command,
                                      LPSECURITY_ATTRIBUTES processSecurity,
                                      LPSECURITY_ATTRIBUTES threadSecurity, BOOL inherit,
                                      DWORD flags, LPVOID environment, LPCWSTR directory,
                                      LPSTARTUPINFOW startup, LPPROCESS_INFORMATION process) {
        auto &self = instance();
        auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
        if (!self.enabled_.load() || caller != self.caller_ || !application ||
            _wcsicmp(application, self.game_.c_str()))
            return self.original_(application, command, processSecurity, threadSecurity, inherit,
                                  flags, environment, directory, startup, process);
        BOOL result = FALSE;
        DWORD error = ERROR_NOT_ENOUGH_MEMORY;
        try {
            auto line = windows_argument(self.launcher_) + L" --build " + self.profile_ +
                        L" --restart-parent " + std::to_wstring(GetCurrentProcessId());
            if (command)
                line += L" --restart-command " + windows_argument(command);
            result = self.original_(self.launcher_.c_str(), line.data(), processSecurity,
                                    threadSecurity, inherit, flags | CREATE_NO_WINDOW, environment,
                                    directory, startup, process);
            error = GetLastError();
            if (result)
                ++self.redirects_;
        } catch (...) { /* Keep the current game alive through its normal failure path. */
        }
        SetLastError(error);
        return result;
    }
    // Process-lifetime import shim: callbacks may already be in flight at shutdown.
    void **slot_{};
    uintptr_t caller_{};
    Function original_{};
    std::wstring launcher_, game_, profile_;
    std::atomic<bool> enabled_{};
    std::atomic<uint64_t> redirects_{};
};
}
