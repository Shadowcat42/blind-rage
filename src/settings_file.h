#pragma once
// Creates and refreshes mods/accessibility/accessibility.ini from the built-in
// defaults (config/accessibility.ini, embedded at build time), so a release
// package never ships the file and extracting an update never resets it.
//
// The result follows the defaults' layout and comments. Every key already in
// the file keeps its value; keys only in the file stay in their section; keys
// new to the defaults appear with their default values; retired keys are
// dropped. Matching is case-insensitive, like GetPrivateProfile*, and the first
// occurrence of a key wins, as it does for the game's INI reader.
#include "hotkey_names.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
namespace a11y {
namespace settings_detail {
inline std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < text.size()) {
        auto end = text.find('\n', at);
        auto line = text.substr(at, end == std::string_view::npos ? text.size() - at : end - at);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        out.emplace_back(line);
        if (end == std::string_view::npos)
            break;
        at = end + 1;
    }
    return out;
}
inline std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return std::string(s);
}
inline std::string lower(std::string s) {
    for (auto &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// The value part of "key = value ; comment".
inline std::string value_of(const std::string &line) {
    auto equals = line.find('=');
    if (equals == std::string::npos)
        return {};
    auto value = line.substr(equals + 1);
    if (auto comment = value.find_first_of(";#"); comment != std::string::npos)
        value.resize(comment);
    return trim(value);
}
inline bool retired(const std::string &section, const std::string &key) {
    // The stability repair is unconditional; the boosted cone uses multipliers
    // of the game's own limits; Back/Select status is always available.
    if (section == "gameplay")
        return key == "stable_lock_cone" || key == "shooting_cone_degrees" ||
               key == "status_button";
    // Retired diagnostic commands: speech test, stop speech and reader shutdown.
    if (section == "hotkeys")
        return key == "test" || key == "stop" || key == "shutdown";
    return false;
}
struct Ini {
    std::map<std::string, std::string> values; // "section\nkey" -> original line
    std::vector<std::string> sections;
    std::map<std::string, std::vector<std::string>> keys;
    std::vector<std::string> &section(const std::string &name) {
        if (!keys.contains(name))
            sections.push_back(name);
        return keys[name];
    }
    bool has(const std::string &section, const std::string &key) const {
        return values.contains(section + "\n" + key);
    }
};
inline Ini read(const std::vector<std::string> &lines) {
    Ini ini;
    std::string section;
    ini.section(section);
    for (const auto &line : lines) {
        auto text = trim(line);
        if (text.size() > 2 && text.front() == '[' && text.back() == ']') {
            section = lower(trim(text.substr(1, text.size() - 2)));
            ini.section(section);
            continue;
        }
        if (text.empty() || text.front() == ';' || text.front() == '#')
            continue;
        auto equals = text.find('=');
        if (equals == std::string::npos || equals < 1)
            continue;
        auto key = lower(trim(text.substr(0, equals)));
        if (retired(section, key))
            continue;
        auto &order = ini.section(section);
        auto id = section + "\n" + key;
        if (!ini.values.contains(id)) {
            ini.values[id] = line;
            order.push_back(key);
        }
    }
    return ini;
}
// Older settings carried forward under their current names and values.
inline void migrate(Ini &local) {
    // shooting_assist (0/1) became shooting_mode; keep a legacy user's choice.
    if (!local.has("gameplay", "shooting_mode") && local.has("gameplay", "shooting_assist")) {
        int legacy = 0;
        try {
            legacy = std::stoi(value_of(local.values["gameplay\nshooting_assist"]));
        } catch (...) {
        }
        local.values["gameplay\nshooting_mode"] =
            std::string("shooting_mode=") + (legacy ? "boosted" : "off");
        local.section("gameplay").push_back("shooting_mode");
    }
    // beacon_volume became audio_cue_volume when the count-in started sharing it.
    if (local.has("gameplay", "beacon_volume")) {
        if (!local.has("gameplay", "audio_cue_volume")) {
            local.values["gameplay\naudio_cue_volume"] =
                "audio_cue_volume=" + trim(local.values["gameplay\nbeacon_volume"].substr(
                                          local.values["gameplay\nbeacon_volume"].find('=') + 1));
            local.section("gameplay").push_back("audio_cue_volume");
        }
        local.values.erase("gameplay\nbeacon_volume");
        std::erase(local.section("gameplay"), "beacon_volume");
    }
    // Earlier mode names: native is off, autoaim is boosted, tracking had beacons.
    if (local.has("gameplay", "shooting_mode")) {
        auto value = value_of(local.values["gameplay\nshooting_mode"]);
        const std::map<std::string, std::string> renamed{
            {"native", "off"}, {"autoaim", "boosted"}, {"tracking", "beacons"}};
        if (auto it = renamed.find(value); it != renamed.end())
            local.values["gameplay\nshooting_mode"] = "shooting_mode=" + it->second;
    }
    // Shortcuts once were virtual-key codes that always meant Ctrl+Alt.
    for (const auto &key : local.section("hotkeys")) {
        auto &line = local.values["hotkeys\n" + key];
        auto value = value_of(line);
        if (value.empty() || value.size() > 3 ||
            !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
            continue;
        auto name = trim(line.substr(0, line.find('=')));
        auto code = std::stoi(value);
        if (!code)
            line = name + "=None";
        else if (auto key_name = key_to_name(UINT(code)); !key_name.starts_with("key "))
            line = name + "=Ctrl+Alt+" + key_name;
    }
}
} // namespace settings_detail
struct SettingsUpdate {
    std::string text;
    bool created{}, changed{};
    int added{};
};
// The refreshed file for `existing` (nothing when the file does not exist yet).
inline SettingsUpdate update_settings(std::string_view defaults_text,
                                      std::optional<std::string_view> existing) {
    using namespace settings_detail;
    auto templateLines = split_lines(defaults_text);
    auto join = [](const std::vector<std::string> &lines) {
        std::string out;
        for (const auto &line : lines)
            out += line + "\r\n";
        return out;
    };
    if (!existing)
        return {join(templateLines), true, true, 0};
    auto targetLines = split_lines(*existing);
    auto local = read(targetLines);
    migrate(local);
    auto defaults = read(templateLines);
    std::set<std::string> used;
    std::vector<std::string> output;
    int added = 0;
    ptrdiff_t last = -1;
    // Keys only in the file go after their section's last setting, ahead of any
    // comments that introduce the next section.
    auto localOnly = [&](const std::string &section) {
        if (!local.keys.contains(section))
            return;
        for (const auto &key : local.keys[section]) {
            auto id = section + "\n" + key;
            if (used.insert(id).second)
                output.insert(output.begin() + ++last, local.values[id]);
        }
    };
    std::string section;
    for (const auto &line : templateLines) {
        auto text = trim(line);
        if (text.size() > 2 && text.front() == '[' && text.back() == ']') {
            localOnly(section);
            section = lower(trim(text.substr(1, text.size() - 2)));
            output.push_back(line);
            last = ptrdiff_t(output.size()) - 1;
            continue;
        }
        auto equals = text.find('=');
        if (!text.empty() && text.front() != ';' && text.front() != '#' &&
            equals != std::string::npos && equals > 0) {
            auto id = section + "\n" + lower(trim(text.substr(0, equals)));
            if (!used.insert(id).second)
                continue;
            if (local.values.contains(id))
                output.push_back(local.values[id]);
            else {
                output.push_back(line);
                ++added;
            }
            last = ptrdiff_t(output.size()) - 1;
            continue;
        }
        output.push_back(line);
    }
    localOnly(section);
    for (const auto &name : local.sections) {
        if (defaults.keys.contains(name) || local.keys[name].empty())
            continue;
        output.push_back("");
        if (!name.empty())
            output.push_back("[" + name + "]");
        last = ptrdiff_t(output.size()) - 1;
        localOnly(name);
    }
    bool changed = output != targetLines;
    return {join(output), false, changed, added};
}
} // namespace a11y
