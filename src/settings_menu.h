#pragma once
// The mod's spoken settings menu (Ctrl+O, or both stick clicks). It edits the
// [gameplay] settings of accessibility.ini; the mod applies each change at once
// and saves it to the file. This file holds only the menu model and its speech.
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace a11y {
struct SettingItem {
    std::string key, label;
    // Choices: {stored value, spoken name}. A number setting has none and uses
    // minimum..maximum in steps.
    std::vector<std::pair<std::string, std::string>> choices;
    int minimum = 0, maximum = 0, step = 1;
    std::string unit;
    std::string fallback;
};
inline std::vector<SettingItem> gameplay_settings() {
    const std::vector<std::pair<std::string, std::string>> onOff{{"1", "on"}, {"0", "off"}};
    return {
        {"subtitles", "Subtitles", onOff, 0, 0, 1, "", "1"},
        {"tutorials", "Game tutorials", onOff, 0, 0, 1, "", "1"},
        {"qtes", "QTE announcements", onOff, 0, 0, 1, "", "1"},
        {"qte_interrupt", "QTEs interrupt speech", onOff, 0, 0, 1, "", "1"},
        {"interrupt", "Game text interrupts speech", onOff, 0, 0, 1, "", "0"},
        {"status_warnings", "Low health warning", onOff, 0, 0, 1, "", "1"},
        {"low_health_percent", "Low health at", {}, 5, 50, 5, "percent", "25"},
        {"shooting_mode",
         "Shooting assistance",
         {{"off", "off"}, {"beacons", "beacons"}, {"boosted", "boosted beacons"}},
         0,
         0,
         1,
         "",
         "beacons"},
        {"aim_priority",
         "Melee rapid fire aims at",
         {{"projectiles", "projectiles first"}, {"enemies", "enemies first"}},
         0,
         0,
         1,
         "",
         "projectiles"},
        {"boost_point_blank", "Boosted lock-on, close range", {}, 1, 10, 1, "times", "2"},
        {"boost_max_range", "Boosted lock-on, long range", {}, 1, 10, 1, "times", "3"},
        {"boost_homing", "Boosted rapid fire homing", {}, 1, 10, 1, "times", "3"},
        {"tracking_strength", "L1 steering speed", {}, 0, 100, 10, "percent", "100"},
        {"walk_beacon", "Walking beacon", onOff, 0, 0, 1, "", "1"},
        {"melee_beacon", "Melee beacon", onOff, 0, 0, 1, "", "1"},
        {"look_beacon", "Look-around beacon", onOff, 0, 0, 1, "", "1"},
        {"mod_tutorials", "Blind Rage tutorials", onOff, 0, 0, 1, "", "1"},
        {"audio_cue_volume", "Sound cue volume", {}, 0, 100, 10, "percent", "60"},
        {"shooting_beacon_volume", "Shooting beacon volume", {}, 0, 100, 10, "percent", "100"},
        {"walk_beacon_volume", "Walking beacon volume", {}, 0, 100, 10, "percent", "100"},
        {"melee_beacon_volume", "Melee beacon volume", {}, 0, 100, 10, "percent", "100"},
        {"look_beacon_volume", "Look-around beacon volume", {}, 0, 100, 10, "percent", "100"},
        {"fire_warning_volume", "Fire warning volume", {}, 0, 100, 10, "percent", "100"},
        {"count_in_volume", "Synchronic count-in volume", {}, 0, 100, 10, "percent", "100"},
    };
}
class SettingsMenu {
  public:
    using Read = std::function<std::string(const std::string &key, const std::string &fallback)>;
    explicit SettingsMenu(std::vector<SettingItem> items = gameplay_settings())
        : items_(std::move(items)) {}
    bool is_open() const {
        return open_;
    }
    // Opens on the first item, reading current values. Returns the speech.
    std::string open(const Read &read) {
        values_.clear();
        for (const auto &item : items_)
            values_[item.key] = normalize(item, read(item.key, item.fallback));
        open_ = true;
        index_ = 0;
        return "Blind Rage settings. " + describe(index_);
    }
    std::string close() {
        open_ = false;
        return "Settings closed.";
    }
    std::string move(int delta) {
        if (!open_ || items_.empty())
            return {};
        auto n = int(items_.size());
        index_ = size_t((int(index_) + (delta % n) + n) % n);
        return describe(index_);
    }
    struct Change {
        std::string key, value, speech;
    };
    // Left/right (or confirm, which steps forward). Wraps for choices, stops at
    // the ends for numbers.
    std::optional<Change> adjust(int delta) {
        if (!open_ || items_.empty())
            return std::nullopt;
        const auto &item = items_[index_];
        auto &value = values_[item.key];
        if (!item.choices.empty()) {
            auto n = int(item.choices.size());
            int at = 0;
            for (int i = 0; i < n; ++i)
                if (item.choices[size_t(i)].first == value)
                    at = i;
            at = ((at + delta) % n + n) % n;
            value = item.choices[size_t(at)].first;
        } else {
            int number = std::stoi(value) + delta * item.step;
            number = std::clamp(number, item.minimum, item.maximum);
            auto next = std::to_string(number);
            if (next == value)
                return Change{item.key, value, spoken(item, value)};
            value = next;
        }
        return Change{item.key, value, spoken(item, value)};
    }
    std::string describe(size_t index) const {
        if (index >= items_.size())
            return {};
        const auto &item = items_[index];
        return item.label + ", " + spoken(item, values_.at(item.key)) + ". " +
               std::to_string(index + 1) + " of " + std::to_string(items_.size()) + ".";
    }
    std::string current() const {
        return open_ ? describe(index_) : std::string{};
    }

  private:
    static std::string normalize(const SettingItem &item, std::string value) {
        if (!item.choices.empty()) {
            for (const auto &[stored, name] : item.choices)
                if (stored == value)
                    return value;
            // Booleans accept any number; nonzero is on.
            if (item.choices.size() == 2 && item.choices[0].first == "1") {
                try {
                    return std::stoi(value) ? "1" : "0";
                } catch (...) {
                }
            }
            return item.fallback;
        }
        try {
            auto number = int(std::lround(std::stod(value)));
            return std::to_string(std::clamp(number, item.minimum, item.maximum));
        } catch (...) {
            return item.fallback;
        }
    }
    static std::string spoken(const SettingItem &item, const std::string &value) {
        for (const auto &[stored, name] : item.choices)
            if (stored == value)
                return name;
        return item.unit.empty() ? value : value + " " + item.unit;
    }
    std::vector<SettingItem> items_;
    std::map<std::string, std::string> values_;
    size_t index_ = 0;
    bool open_ = false;
};
} // namespace a11y
