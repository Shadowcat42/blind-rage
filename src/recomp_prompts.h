#pragma once
#include <algorithm>
#include <map>
#include <vector>
#include "input_labels.h"
#include "build_profile.h"
#include "qte_reader.h"
#include <cstdint>
#include <cstring>
#include <windows.h>

namespace a11y {
// Read-only adapter for the fingerprinted Mark2 runtime. Its MSVC string ABI
// differs from our toolchain's std::string; never pass either across the DLL.
class RecompPrompts {
  public:
    struct NativeString {
        union {
            char text[16];
            const char *pointer;
        };
        uint64_t size{}, capacity{};
    };
    using KeyName = void *(*)(NativeString *, int);
    using Destroy = void (*)(NativeString *);
    struct NativeView {
        const char *data;
        uint64_t size;
    };
    using BindValue = void *(*)(NativeString *, const NativeView *);
    static_assert(sizeof(NativeString) == 32);
    bool initialize(HMODULE runtime, uintptr_t executable, const BuildProfile &profile) {
        auto key = reinterpret_cast<KeyName>(GetProcAddress(
            runtime,
            "?PromptKeyName@mnk@input@rex@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@H@Z"));
        auto destroy = reinterpret_cast<Destroy>(GetProcAddress(
            runtime, "??1?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QEAA@XZ"));
        auto binding = reinterpret_cast<BindValue>(GetProcAddress(
            runtime,
            "?GetBindValue@mnk@input@rex@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@V?$basic_string_view@DU?$char_traits@D@std@@@5@@Z"));
        return initialize_targets(executable, profile.prompt_state_rvas, key, destroy, binding);
    }
    bool initialize_targets(uintptr_t executable, std::array<uintptr_t, 2> flags, KeyName key,
                            Destroy destroy, BindValue binding = nullptr) {
        executable_ = executable;
        flags_ = flags;
        key_ = key;
        destroy_ = destroy;
        binding_ = binding;
        return key_ && destroy_ && device() != PromptDevice::unknown;
    }
    PromptDevice device() const {
        // Both supported executables use these same host data RVAs. The first is
        // the applied startup PS3-icon choice; the second is kbp::Run's effective
        // keyboard flag (including show_keys, device changes and temporary forcing).
        uint8_t ps{}, keyboard{};
        SIZE_T got{};
        if (!executable_ ||
            !ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void *>(executable_ + flags_[0]), &ps, 1, &got) ||
            got != 1 || ps > 1)
            return PromptDevice::unknown;
        if (!ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void *>(executable_ + flags_[1]), &keyboard, 1,
                               &got) ||
            got != 1 || keyboard > 1)
            return PromptDevice::unknown;
        return keyboard ? PromptDevice::keyboard
               : ps     ? PromptDevice::playstation
                        : PromptDevice::xbox;
    }
    std::string button(std::string_view token) const {
        auto mode = device();
        if (mode != PromptDevice::keyboard)
            return controller_button(token, mode);
        // The triggers: PromptKeyName returns a fixed "LMB"/"RMB" for kinds 7/8 under a
        // runtime context check (seen in rail shooting), whatever the binding; the
        // trigger keys (default R and F) are what the input uses, so name the binding.
        if (token == "LT" || token == "RT") {
            auto value = binding(token == "LT" ? "keybind_left_trigger" : "keybind_right_trigger");
            if (auto comma = value.find(','); comma != std::string::npos)
                value.resize(comma);
            if (!value.empty())
                return spoken_key(value);
        }
        auto kind = recomp_prompt_kind(token);
        return kind ? key_name(kind) : std::string{};
    }
    std::string key_name(int kind) const {
        if (!key_ || !destroy_ || kind < 1 || kind > 13)
            return {};
        NativeString native{};
        auto *returned = key_(&native, kind);
        return spoken_key(copy_and_destroy(native, returned));
    }
    std::string binding(std::string_view key) const {
        if (!binding_ || !destroy_)
            return {};
        NativeString native{};
        NativeView view{key.data(), key.size()};
        auto *returned = binding_(&native, &view);
        return copy_and_destroy(native, returned);
    }
    InputDescription gesture(std::string_view tag) const {
        auto mode = device();
        if (mode == PromptDevice::unknown)
            return {};
        if (mode != PromptDevice::keyboard)
            return controller_gesture(tag);
        auto physical = controller_gesture(tag);
        if (physical.label.empty())
            return {};
        if (physical.action == InputAction::move) {
            if (tag == "LRightDown") {
                auto right = direction_key('L', "right"), down = direction_key('L', "down");
                return right.empty() || down.empty()
                           ? InputDescription{}
                           : InputDescription{right + " and " + down + " together",
                                              InputAction::press};
            }
            std::string direction(tag.substr(1));
            std::transform(direction.begin(), direction.end(), direction.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            auto key = direction_key(tag[0], direction);
            return {key, InputAction::press};
        }
        bool clockwise = tag.starts_with("RRotate");
        // Tags end in the stick: L (left), R (right), Set (both). Mouse control drives
        // only the right stick; the left stick stays on its keys (WASD).
        const bool both = tag.ends_with("Set");
        char stick = tag.ends_with("R") ? 'R' : 'L';
        const std::string mouse =
            std::string("mouse ") + (clockwise ? "clockwise" : "counterclockwise");
        if (stick == 'R' && mouse_control())
            return {mouse, InputAction::rotate};
        // With mouse emulation disabled, the runtime's rotation prompt advances
        // through directional keys. Speak the full verified circular key sequence,
        // rather than only the currently drawn key and then falling silent.
        std::array<const char *, 4> directions = clockwise
                                                     ? std::array{"up", "right", "down", "left"}
                                                     : std::array{"up", "left", "down", "right"};
        std::string keys;
        for (auto direction : directions) {
            auto key = direction_key(stick, direction);
            if (key.empty())
                return {};
            if (!keys.empty())
                keys += ", ";
            keys += key;
        }
        // Both sticks: the left stick's keys, with the mouse for the right stick.
        if (both && mouse_control())
            return {keys + " in order, together with " + mouse, InputAction::sequence};
        return {keys + " in order", InputAction::sequence};
    }

    // Keyboard names for the stick tags (<LS>, <RS>, R3), from the runtime's own
    // prompt kinds: 9 is the left-stick keys (WASD), 11 the right-stick keys
    // (ARROWS). With mouse control on (mnk_mouse: "Use the mouse (camera and
    // aiming). Off means the right stick comes from the keybind_rstick_* keys
    // only") the right stick is the mouse. Empty when nothing is bound.
    // Keyboard name of a D-pad direction's first bound key; empty on a controller
    // or when the direction has no binding.
    std::string dpad_key(std::string_view direction) const {
        if (device() != PromptDevice::keyboard)
            return {};
        auto value = binding(std::string("keybind_dpad_") + std::string(direction));
        if (auto comma = value.find(','); comma != std::string::npos)
            value.resize(comma);
        return value.empty() ? std::string{} : spoken_key(value);
    }
    std::string keyboard_stick(std::string_view token) const {
        if (token == "LS")
            return key_name(9);
        if (token == "RS")
            return mouse_control() ? std::string("mouse") : key_name(11);
        if (token == "R3") {
            auto value = binding("keybind_rstick_press");
            if (auto comma = value.find(','); comma != std::string::npos)
                value.resize(comma);
            return spoken_key(value);
        }
        return {};
    }

    // The recomp's Custom controls editor: one row per controller input, its bound
    // keys selected by Windows key code. Keys are named as the runtime's bindings
    // name them (its key-name table: letters, Space, Return, Shift, LMB...), and each
    // input carries the game's action label from the controls screen (`actions`).
    struct Bind {
        const char *key, *token, *detail;
    };
    static constexpr Bind kBinds[] = {
        {"keybind_a", "A", ""},
        {"keybind_b", "B", ""},
        {"keybind_x", "X", ""},
        {"keybind_y", "Y", ""},
        {"keybind_left_shoulder", "LB", ""},
        {"keybind_right_shoulder", "RB", ""},
        {"keybind_left_trigger", "LT", ""},
        {"keybind_right_trigger", "RT", ""},
        {"keybind_lstick_up", "LS", "up"},
        {"keybind_lstick_down", "LS", "down"},
        {"keybind_lstick_left", "LS", "left"},
        {"keybind_lstick_right", "LS", "right"},
        {"keybind_rstick_up", "RS", "up"},
        {"keybind_rstick_down", "RS", "down"},
        {"keybind_rstick_left", "RS", "left"},
        {"keybind_rstick_right", "RS", "right"},
        {"keybind_lstick_press", "L3", ""},
        {"keybind_rstick_press", "R3", ""},
        {"keybind_dpad_up", "DPAD", "up"},
        {"keybind_dpad_down", "DPAD", "down"},
        {"keybind_dpad_left", "DPAD", "left"},
        {"keybind_dpad_right", "DPAD", "right"},
        {"keybind_back", "BACK", ""},
        {"keybind_start", "START", ""},
        {"keybind_guide", "GUIDE", ""},
    };
    static std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        return s;
    }
    // A binding's keys, lower-cased (the runtime's comma-separated value).
    std::vector<std::string> bound_keys(const Bind &bind) const {
        std::vector<std::string> out;
        auto value = binding(bind.key);
        for (size_t start = 0; start < value.size();) {
            auto end = value.find(',', start);
            if (end == std::string::npos)
                end = value.size();
            if (end > start)
                out.push_back(lower(value.substr(start, end - start)));
            start = end + 1;
        }
        return out;
    }
    static std::string bind_action(const Bind &bind,
                                   const std::map<std::string, std::string> &actions) {
        std::string action;
        if (auto it = actions.find(bind.token); it != actions.end())
            action = it->second;
        else if (std::string_view(bind.token) == "DPAD")
            action = "D-pad";
        else if (std::string_view(bind.token) == "GUIDE")
            action = "Guide button";
        else if (std::string_view(bind.token) == "L3")
            action = "left stick press";
        else
            action = bind.token;
        if (*bind.detail)
            action += std::string(" ") + bind.detail;
        return action;
    }
    static std::string spoken_runtime_key(const std::string &name) {
        if (name == "Return")
            return "Enter";
        if (name == "Mouse4" || name == "Mouse5")
            return "mouse button " + name.substr(5);
        return spoken_key(name);
    }
    // One key: what it is bound to.
    std::string describe_key(unsigned vk, const std::map<std::string, std::string> &actions) const {
        auto name = runtime_key_name(vk);
        if (name.empty())
            return {};
        const auto wanted = lower(name);
        std::vector<std::string> parts;
        for (const auto &bind : kBinds) {
            auto keys = bound_keys(bind);
            if (std::find(keys.begin(), keys.end(), wanted) == keys.end())
                continue;
            auto action = bind_action(bind, actions);
            if (std::find(parts.begin(), parts.end(), action) == parts.end())
                parts.push_back(action);
        }
        std::string text = spoken_runtime_key(name) + ": ";
        if (parts.empty())
            return text + "unbound.";
        for (size_t i = 0; i < parts.size(); ++i)
            text += (i ? ", " : "") + parts[i];
        return text + ".";
    }
    // One editor row: the input whose bound keys are exactly these, then the keys.
    std::string describe_row(const std::vector<unsigned> &codes,
                             const std::map<std::string, std::string> &actions) const {
        std::vector<std::string> names, wanted;
        for (auto code : codes) {
            auto name = runtime_key_name(code);
            if (name.empty() || std::find(names.begin(), names.end(), name) != names.end())
                continue;
            names.push_back(name);
            wanted.push_back(lower(name));
        }
        if (names.empty())
            return "No keys bound.";
        // The row's input is the one every listed key is bound to. A stick's
        // directions are separate bindings (W, A, S, D) but one input, and a row may
        // show only some of an input's keys (Fire: E of "E,Mouse4").
        std::vector<std::string> common;
        bool first = true;
        for (const auto &key : wanted) {
            std::vector<std::string> tokens;
            for (const auto &bind : kBinds) {
                auto keys = bound_keys(bind);
                if (std::find(keys.begin(), keys.end(), key) != keys.end() &&
                    std::find(tokens.begin(), tokens.end(), bind.token) == tokens.end())
                    tokens.push_back(bind.token);
            }
            if (first)
                common = tokens;
            else
                std::erase_if(common, [&](const std::string &token) {
                    return std::find(tokens.begin(), tokens.end(), token) == tokens.end();
                });
            first = false;
        }
        std::vector<std::string> inputs;
        for (const auto &token : common) {
            Bind whole{"", token.c_str(), ""};
            auto action = bind_action(whole, actions);
            if (std::find(inputs.begin(), inputs.end(), action) == inputs.end())
                inputs.push_back(action);
        }
        std::string keyList;
        for (size_t i = 0; i < names.size(); ++i)
            keyList += (i ? ", " : "") + spoken_runtime_key(names[i]);
        if (inputs.empty())
            return "Keys: " + keyList + ".";
        std::string text;
        for (size_t i = 0; i < inputs.size(); ++i)
            text += (i ? " / " : "") + inputs[i];
        return text + ": " + keyList + ".";
    }
    // Windows key code to the runtime's key name (its key-name table).
    static std::string runtime_key_name(unsigned vk) {
        if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
            return std::string(1, char(vk));
        if (vk >= 96 && vk <= 105)
            return "Numpad" + std::to_string(vk - 96);
        if (vk >= 112 && vk <= 135)
            return "F" + std::to_string(vk - 111);
        switch (vk) {
            case 1:
                return "LMB";
            case 2:
                return "RMB";
            case 4:
                return "MMB";
            case 5:
                return "Mouse4";
            case 6:
                return "Mouse5";
            case 8:
                return "Backspace";
            case 9:
                return "Tab";
            case 13:
                return "Return";
            case 16:
            case 160:
            case 161:
                return "Shift";
            case 17:
            case 162:
            case 163:
                return "Control";
            case 18:
            case 164:
            case 165:
                return "Alt";
            case 19:
                return "Pause";
            case 20:
                return "CapsLock";
            case 27:
                return "Escape";
            case 32:
                return "Space";
            case 33:
                return "PageUp";
            case 34:
                return "PageDown";
            case 35:
                return "End";
            case 36:
                return "Home";
            case 37:
                return "Left";
            case 38:
                return "Up";
            case 39:
                return "Right";
            case 40:
                return "Down";
            case 44:
                return "PrintScreen";
            case 45:
                return "Insert";
            case 46:
                return "Delete";
            case 106:
                return "NumpadStar";
            case 107:
                return "NumpadPlus";
            case 109:
                return "NumpadMinus";
            case 111:
                return "NumpadSlash";
            case 144:
                return "NumLock";
            case 145:
                return "ScrollLock";
            case 186:
                return "Semicolon";
            case 188:
                return "Comma";
            case 189:
                return "Minus";
            case 190:
                return "Period";
            case 191:
                return "Slash";
            case 192:
                return "Backtick";
            case 219:
                return "LBracket";
            case 220:
                return "Backslash";
            case 221:
                return "RBracket";
            case 222:
                return "Quote";
            default:
                return {};
        }
    }

  private:
    bool mouse_control() const {
        auto mouse = binding("mnk_mouse");
        auto shown = key_name(10);
        return mouse == "true" || mouse == "1" || shown == "move the mouse" ||
               shown.starts_with("rotate the mouse");
    }
    std::string direction_key(char stick, std::string_view direction) const {
        auto value = binding(std::string("keybind_") + (stick == 'L' ? "lstick_" : "rstick_") +
                             std::string(direction));
        if (auto comma = value.find(','); comma != std::string::npos)
            value.resize(comma);
        if (value.empty()) {
            auto aggregate = key_name(stick == 'L' ? 9 : 11);
            // The runtime emits these abbreviations only for the complete default
            // binding set. An empty custom binding must not invent a default key.
            if (stick == 'R' && aggregate == "arrow keys") {
                value = direction;
                value[0] = char(std::toupper(static_cast<unsigned char>(value[0])));
            } else if (stick == 'L' && aggregate == "W A S D")
                value = direction == "up"     ? "W"
                        : direction == "down" ? "S"
                        : direction == "left" ? "A"
                                              : "D";
        }
        if (value == "Up")
            return "up arrow";
        if (value == "Down")
            return "down arrow";
        if (value == "Left")
            return "left arrow";
        if (value == "Right")
            return "right arrow";
        return spoken_key(value);
    }
    std::string copy_and_destroy(NativeString &native, const void *returned) const {
        // The export and destructor were checked in isolated probes for both
        // runtime fingerprints. Bounds still protect the copy against bad payloads.
        std::string text;
        if (returned == &native && native.size <= 256 && native.capacity >= native.size) {
            const auto *data = native.capacity < 16 ? native.text : native.pointer;
            if (data) {
                char copy[256];
                SIZE_T got{};
                if (ReadProcessMemory(GetCurrentProcess(), data, copy, size_t(native.size), &got) &&
                    got == native.size)
                    text.assign(copy, size_t(native.size));
            }
        }
        destroy_(&native);
        return text;
    }
    uintptr_t executable_{};
    std::array<uintptr_t, 2> flags_{};
    KeyName key_{};
    Destroy destroy_{};
    BindValue binding_{};
};
}
