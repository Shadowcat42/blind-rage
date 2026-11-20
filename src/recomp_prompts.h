#pragma once
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
        auto mouse = binding("mnk_mouse");
        auto shown = key_name(10);
        if (mouse == "true" || mouse == "1" || shown == "move the mouse" ||
            shown.starts_with("rotate the mouse"))
            return {std::string("mouse ") + (clockwise ? "clockwise" : "counterclockwise"),
                    InputAction::rotate};
        // With mouse emulation disabled, the runtime's rotation prompt advances
        // through directional keys. Speak the full verified circular key sequence,
        // rather than only the currently drawn key and then falling silent.
        char stick = tag.ends_with("R") ? 'R' : 'L';
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
        return {keys + " in order", InputAction::sequence};
    }

  private:
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
