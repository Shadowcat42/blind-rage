#pragma once
#include "hotkey_names.h"
#include <windows.h>
#include <array>
#include <atomic>
namespace a11y {
class WindowNotifications {
  public:
    static constexpr UINT focus_message = WM_APP + 17;
    bool initialize(const Hotkey *keys) {
        MSG message;
        PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
        thread_ = GetCurrentThreadId();
        for (unsigned i = 0; i < keys_.size(); i++)
            keys_[i] = keys[i];
        hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, focus, 0,
                                0, WINEVENT_OUTOFCONTEXT);
        update();
        return hook_ != nullptr;
    }
    bool update() {
        DWORD pid{};
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        bool focused = pid == GetCurrentProcessId();
        if (focused != focused_)
            for (unsigned i = 0; i < keys_.size(); i++) {
                if (registered_[i]) {
                    UnregisterHotKey(nullptr, 100 + int(i));
                    registered_[i] = false;
                }
                if (focused && keys_[i].key)
                    registered_[i] = RegisterHotKey(
                        nullptr, 100 + int(i), keys_[i].modifiers | MOD_NOREPEAT, keys_[i].key);
            }
        focused_ = focused;
        register_menu(menu_ && focused_);
        return focused_;
    }
    bool focused() const {
        return focused_;
    }
    // The settings menu borrows Up, Down, Left, Right, Enter and Escape while
    // it is open (hotkey IDs 300-305); the game does not receive them then.
    static constexpr int kMenuKeyBase = 300;
    void menu_keys(bool on) {
        menu_ = on;
        register_menu(on && focused_);
    }
    bool hotkey_registered(unsigned i) const {
        return i < registered_.size() && registered_[i];
    }
    ~WindowNotifications() {
        if (hook_)
            UnhookWinEvent(hook_);
        for (unsigned i = 0; i < keys_.size(); i++)
            if (registered_[i])
                UnregisterHotKey(nullptr, 100 + int(i));
        register_menu(false);
        thread_ = 0;
    }

  private:
    void register_menu(bool on) {
        static constexpr UINT keys[]{VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_ESCAPE};
        for (int i = 0; i < 6; ++i) {
            if (menu_registered_[size_t(i)] && !on) {
                UnregisterHotKey(nullptr, kMenuKeyBase + i);
                menu_registered_[size_t(i)] = false;
            } else if (on && !menu_registered_[size_t(i)])
                menu_registered_[size_t(i)] = RegisterHotKey(nullptr, kMenuKeyBase + i, 0, keys[i]);
        }
    }
    static void CALLBACK focus(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
        auto thread = thread_.load();
        if (thread)
            PostThreadMessageW(thread, focus_message, 0, 0);
    }
    inline static std::atomic<DWORD> thread_{};
    HWINEVENTHOOK hook_{};
    std::array<Hotkey, 16> keys_{};
    std::array<bool, 16> registered_{};
    std::array<bool, 6> menu_registered_{};
    bool focused_{}, menu_{};
};
}
