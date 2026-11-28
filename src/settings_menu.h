#pragma once
// The mod's spoken settings menu (Ctrl+O, or both stick clicks). It edits the
// [gameplay] settings of accessibility.ini; the mod applies each change at once
// and saves it to the file. Settings are grouped in categories: the menu opens
// on the category list, and back returns to it. This file holds only the menu
// model and its speech.
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
    // Read after the setting's position; moving on cuts it off.
    std::string description;
};
struct SettingCategory {
    std::string name;
    std::vector<SettingItem> items;
};
inline std::vector<SettingCategory> gameplay_settings() {
    const std::vector<std::pair<std::string, std::string>> onOff{{"1", "on"}, {"0", "off"}};
    return {
        {"Speech",
         {
             {"subtitles", "Subtitles", onOff, 0, 0, 1, "", "1", "Reads subtitles."},
             {"tutorials", "Game tutorials", onOff, 0, 0, 1, "", "1",
              "Reads the game's own tutorial boxes."},
             {"interrupt", "Game text interrupts speech", onOff, 0, 0, 1, "", "0",
              "New subtitles and tutorials cut off whatever the screen reader is saying, instead of waiting."},
             {"qtes", "QTE announcements", onOff, 0, 0, 1, "", "1", "Announces quick-time events."},
             {"qte_interrupt", "QTEs interrupt speech", onOff, 0, 0, 1, "", "1",
              "A new QTE prompt cuts off whatever the screen reader is saying."},
             {"qte_wording",
              "QTE wording",
              {{"verbose", "verbose"}, {"quick", "quick"}},
              0,
              0,
              1,
              "",
              "verbose",
              "Quick says prompts briefly, like LS right RS left; verbose spells them out."},
             {"status_warnings", "Gauge alerts", onOff, 0, 0, 1, "", "1",
              "Announces low health, when Burst is ready, and other changes to your gauges."},
             {"low_health_percent",
              "Low health at",
              {},
              5,
              50,
              5,
              "percent",
              "25",
              "The health percentage that counts as low health in gauge alerts."},
         }},
        {"Tutorials and tips",
         {
             {"mod_tutorials", "Tutorials", onOff, 0, 0, 1, "", "1",
              "The mod's own tutorial boxes. They pause the game."},
             {"mod_tips", "Fight tips", onOff, 0, 0, 1, "", "1",
              "A short pausing box when an enemy whose fight isn't obvious appears."},
         }},
        {"Shooting and aiming",
         {
             {"shooting_mode",
              "Shooting assistance",
              {{"off", "off"}, {"beacons", "beacons"}, {"boosted", "boosted beacons"}},
              0,
              0,
              1,
              "",
              "beacons",
              "Off, audio beacons toward targets, or beacons with a larger lock-on area (boosted)."},
             {"aim_priority",
              "Melee rapid fire aims at",
              {{"projectiles", "projectiles first"}, {"enemies", "enemies first"}},
              0,
              0,
              1,
              "",
              "projectiles",
              "Whether melee rapid fire goes for projectiles or enemies first. D-pad Down switches it during rapid fire."},
             {"tracking_strength",
              "L1 steering speed",
              {},
              0,
              100,
              10,
              "percent",
              "100",
              "How fast holding L1 steers your aim onto targets, as a share of the game's own cursor speed."},
         }},
        {"Beacons",
         {
             {"walk_beacon", "Walking beacon", onOff, 0, 0, 1, "", "1",
              "A tone toward where to go next when walking. Quiet during fights."},
             {"melee_beacon", "Melee beacon", onOff, 0, 0, 1, "", "1",
              "A tone toward the enemy your attacks will turn to. D-pad Up toggles it in melee."},
             {"look_beacon", "Look-around beacon", onOff, 0, 0, 1, "", "1",
              "In scenes where the right stick turns the camera, leads your view to things the game reacts to."},
         }},
        {"Gameplay assists",
         {
             {"qte_freeze",
              "QTE freeze",
              {},
              0,
              300,
              50,
              "milliseconds",
              "0",
              "Pauses the game briefly when a new QTE appears, so you hear it sooner. Presses during the freeze don't count."},
             {"boost_point_blank",
              "Boosted lock-on, close range",
              {},
              1,
              10,
              1,
              "times",
              "2",
              "How much larger the lock-on area is up close. Only with boosted shooting assistance."},
             {"boost_max_range",
              "Boosted lock-on, long range",
              {},
              1,
              10,
              1,
              "times",
              "3",
              "How much larger the lock-on area is far away. Only with boosted shooting assistance."},
             {"boost_homing",
              "Boosted rapid fire homing",
              {},
              1,
              10,
              1,
              "times",
              "3",
              "How much more melee rapid fire homes in. Only with boosted shooting assistance."},
         }},
        {"Volume",
         {
             {"audio_cue_volume",
              "Sound cue volume",
              {},
              0,
              100,
              10,
              "percent",
              "60",
              "Overall volume of the mod's sounds."},
             {"shooting_beacon_volume",
              "Shooting beacon volume",
              {},
              0,
              100,
              10,
              "percent",
              "100",
              "The shooting beacon's share of the overall volume."},
             {"walk_beacon_volume",
              "Walking beacon volume",
              {},
              0,
              100,
              10,
              "percent",
              "100",
              "The walking beacon's share of the overall volume."},
             {"melee_beacon_volume",
              "Melee beacon volume",
              {},
              0,
              100,
              10,
              "percent",
              "100",
              "The melee beacon's share of the overall volume."},
             {"look_beacon_volume",
              "Look-around beacon volume",
              {},
              0,
              100,
              10,
              "percent",
              "100",
              "The look-around beacon's share of the overall volume."},
             {"fire_warning_volume",
              "Fire warning volume",
              {},
              0,
              100,
              10,
              "percent",
              "100",
              "The share for the mod's danger warnings, such as when to jump."},
             {"count_in_volume",
              "Synchronic count-in volume",
              {},
              0,
              100,
              10,
              "percent",
              "100",
              "The share for the count-in beats before a Synchronic QTE."},
         }},
    };
}
class SettingsMenu {
  public:
    using Read = std::function<std::string(const std::string &key, const std::string &fallback)>;
    explicit SettingsMenu(std::vector<SettingCategory> categories = gameplay_settings())
        : categories_(std::move(categories)) {}
    bool is_open() const {
        return open_;
    }
    // True inside a category, false on the category list.
    bool in_category() const {
        return open_ && inside_;
    }
    // Opens on the category list, on the category last used, reading current
    // values. Returns the speech.
    std::string open(const Read &read) {
        values_.clear();
        for (const auto &category : categories_)
            for (const auto &item : category.items)
                values_[item.key] = normalize(item, read(item.key, item.fallback));
        open_ = true;
        inside_ = false;
        index_ = 0;
        if (category_ >= categories_.size())
            category_ = 0;
        return "Blind Rage settings. " + current();
    }
    std::string close() {
        open_ = inside_ = false;
        return "Settings closed.";
    }
    // Up/down: the next category on the list, or the next setting inside one.
    // Both wrap.
    std::string move(int delta) {
        if (!open_ || categories_.empty())
            return {};
        auto wrap = [delta](size_t at, size_t size) {
            auto n = int(size);
            return size_t((int(at) + (delta % n) + n) % n);
        };
        if (inside_) {
            if (items().empty())
                return {};
            index_ = wrap(index_, items().size());
        } else
            category_ = wrap(category_, categories_.size());
        return current();
    }
    // Right or confirm on the category list: opens the selected category.
    std::string enter() {
        if (!open_ || inside_ || categories_.empty() || items().empty())
            return {};
        inside_ = true;
        index_ = 0;
        return categories_[category_].name + ". " + current();
    }
    // Back: from a setting to the category list. Returns nothing on the list
    // itself, where back closes the menu.
    std::optional<std::string> back() {
        if (!in_category())
            return std::nullopt;
        inside_ = false;
        return current();
    }
    struct Change {
        std::string key, value, speech;
    };
    // Left/right (or confirm, which steps forward) inside a category. Wraps for
    // choices, stops at the ends for numbers.
    std::optional<Change> adjust(int delta) {
        if (!in_category() || items().empty())
            return std::nullopt;
        const auto &item = items()[index_];
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
            value = std::to_string(number);
        }
        return Change{item.key, value, spoken(item, value)};
    }
    std::string current() const {
        if (!open_ || categories_.empty())
            return {};
        if (!inside_)
            return categories_[category_].name + ". " + std::to_string(category_ + 1) + " of " +
                   std::to_string(categories_.size()) + ".";
        if (index_ >= items().size())
            return {};
        const auto &item = items()[index_];
        auto text = item.label + ", " + spoken(item, values_.at(item.key)) + ". " +
                    std::to_string(index_ + 1) + " of " + std::to_string(items().size()) + ".";
        return item.description.empty() ? text : text + " " + item.description;
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
    const std::vector<SettingItem> &items() const {
        return categories_[category_].items;
    }
    std::vector<SettingCategory> categories_;
    std::map<std::string, std::string> values_;
    size_t category_ = 0, index_ = 0;
    bool open_ = false, inside_ = false;
};
} // namespace a11y
