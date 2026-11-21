#pragma once
#include <windows.h>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace a11y {
// A configured shortcut: RegisterHotKey modifiers plus a virtual-key code.
// key == 0 means the shortcut is disabled.
struct Hotkey {
    UINT modifiers{}, key{};
    bool operator==(const Hotkey &) const = default;
};
struct KeyName {
    std::string_view name;
    UINT key;
};
// Names accepted in accessibility.ini, matched case-insensitively. The first
// entry for a key is the one written back when describing a shortcut.
inline constexpr std::array<KeyName, 46> key_names{{
    {"Space", VK_SPACE},
    {"Enter", VK_RETURN},
    {"Return", VK_RETURN},
    {"Tab", VK_TAB},
    {"Escape", VK_ESCAPE},
    {"Esc", VK_ESCAPE},
    {"Backspace", VK_BACK},
    {"Insert", VK_INSERT},
    {"Delete", VK_DELETE},
    {"Del", VK_DELETE},
    {"Home", VK_HOME},
    {"End", VK_END},
    {"PageUp", VK_PRIOR},
    {"PageDown", VK_NEXT},
    {"Up", VK_UP},
    {"Down", VK_DOWN},
    {"Left", VK_LEFT},
    {"Right", VK_RIGHT},
    {"Pause", VK_PAUSE},
    {"ScrollLock", VK_SCROLL},
    {"PrintScreen", VK_SNAPSHOT},
    {"NumpadAdd", VK_ADD},
    {"NumpadPlus", VK_ADD},
    {"NumpadSubtract", VK_SUBTRACT},
    {"NumpadMinus", VK_SUBTRACT},
    {"NumpadMultiply", VK_MULTIPLY},
    {"NumpadDivide", VK_DIVIDE},
    {"NumpadDecimal", VK_DECIMAL},
    {"NumpadPeriod", VK_DECIMAL},
    {"Minus", VK_OEM_MINUS},
    {"Equals", VK_OEM_PLUS},
    {"Plus", VK_OEM_PLUS},
    {"Comma", VK_OEM_COMMA},
    {"Period", VK_OEM_PERIOD},
    {"Semicolon", VK_OEM_1},
    {"Slash", VK_OEM_2},
    {"Backtick", VK_OEM_3},
    {"Grave", VK_OEM_3},
    {"Tilde", VK_OEM_3},
    {"LeftBracket", VK_OEM_4},
    {"Backslash", VK_OEM_5},
    {"RightBracket", VK_OEM_6},
    {"Quote", VK_OEM_7},
    {"Apostrophe", VK_OEM_7},
    {"Menu", VK_APPS},
    {"Apps", VK_APPS},
}};
inline bool same_name(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}
inline std::optional<UINT> parse_number(std::string_view text, unsigned low, unsigned high) {
    if (text.empty() || text.size() > 3)
        return {};
    unsigned value = 0;
    for (char c : text) {
        if (c < '0' || c > '9')
            return {};
        value = value * 10 + unsigned(c - '0');
    }
    if (value < low || value > high)
        return {};
    return value;
}
inline std::optional<UINT> key_from_name(std::string_view name) {
    if (name.size() == 1) {
        auto c = char(std::toupper(static_cast<unsigned char>(name[0])));
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            return UINT(c);
    }
    if (name.size() >= 2 && (name[0] == 'F' || name[0] == 'f'))
        if (auto n = parse_number(name.substr(1), 1, 24))
            return UINT(VK_F1 + *n - 1);
    if (name.size() == 7 && same_name(name.substr(0, 6), "Numpad") && name[6] >= '0' &&
        name[6] <= '9')
        return UINT(VK_NUMPAD0 + (name[6] - '0'));
    for (const auto &entry : key_names)
        if (same_name(name, entry.name))
            return entry.key;
    return {};
}
inline std::string key_to_name(UINT key) {
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
        return std::string(1, char(key));
    if (key >= VK_F1 && key <= VK_F24)
        return "F" + std::to_string(key - VK_F1 + 1);
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9)
        return "Numpad" + std::to_string(key - VK_NUMPAD0);
    for (const auto &entry : key_names)
        if (entry.key == key)
            return std::string(entry.name);
    return "key " + std::to_string(key);
}
// Parses "Ctrl+Alt+F9", "Shift+Ctrl+Numpad5" or "F9". "None", "Off" or "0"
// disable the shortcut. A plain number is a legacy virtual-key code, which
// older files always combined with Ctrl+Alt. Returns nothing when invalid.
inline std::optional<Hotkey> parse_hotkey(std::string_view text) {
    auto trim = [](std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
            s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
            s.remove_suffix(1);
        return s;
    };
    text = trim(text);
    if (text.empty() || text == "0" || same_name(text, "None") || same_name(text, "Off"))
        return Hotkey{};
    if (auto code = parse_number(text, 1, 254))
        return Hotkey{MOD_CONTROL | MOD_ALT, *code};
    Hotkey result;
    while (true) {
        auto plus = text.find('+');
        auto part = trim(text.substr(0, plus));
        if (part.empty())
            return {};
        if (plus == std::string_view::npos) {
            auto key = key_from_name(part);
            if (!key)
                return {};
            result.key = *key;
            return result;
        }
        UINT modifier = same_name(part, "Ctrl") || same_name(part, "Control")  ? MOD_CONTROL
                        : same_name(part, "Alt")                               ? MOD_ALT
                        : same_name(part, "Shift")                             ? MOD_SHIFT
                        : same_name(part, "Win") || same_name(part, "Windows") ? MOD_WIN
                                                                               : 0;
        if (!modifier || (result.modifiers & modifier))
            return {};
        result.modifiers |= modifier;
        text = text.substr(plus + 1);
    }
}
// The shortcut as a person would say it, for logs and messages.
inline std::string describe_hotkey(Hotkey hotkey) {
    if (!hotkey.key)
        return "None";
    std::string text;
    if (hotkey.modifiers & MOD_CONTROL)
        text += "Ctrl+";
    if (hotkey.modifiers & MOD_ALT)
        text += "Alt+";
    if (hotkey.modifiers & MOD_SHIFT)
        text += "Shift+";
    if (hotkey.modifiers & MOD_WIN)
        text += "Win+";
    return text + key_to_name(hotkey.key);
}
}
