#include "../src/settings_menu.h"
#include <iostream>
#include <set>
#include <stdexcept>
using namespace a11y;
static void check(bool pass, const char *message) {
    if (!pass)
        throw std::runtime_error(message);
}
int main() {
    try {
        std::map<std::string, std::string> file{{"subtitles", "0"},
                                                {"shooting_mode", "boosted"},
                                                {"audio_cue_volume", "90"},
                                                {"low_health_percent", "nonsense"}};
        auto read = [&](const std::string &key, const std::string &fallback) {
            auto it = file.find(key);
            return it == file.end() ? fallback : it->second;
        };
        SettingsMenu menu;
        auto first = menu.open(read);
        check(menu.is_open() && !menu.in_category() &&
                  first == "Blind Rage settings. Speech. 1 of 6.",
              "opening reads the first category");
        check(menu.move(-1) == "Volume. 6 of 6.", "moving up from the top category wraps");
        check(!menu.adjust(1), "the category list changes no setting");
        check(
            menu.enter() ==
                    "Volume. Sound cue volume, 90 percent. 1 of 7. Overall volume of the mod's sounds." &&
                menu.in_category(),
            "entering a category reads its first setting");
        check(
            menu.move(-1) ==
                "Synchronic count-in volume, 100 percent. 7 of 7. The share for the count-in beats before a Synchronic QTE.",
            "moving up from a category's top wraps to its last setting");
        menu.move(1);
        auto louder = menu.adjust(1);
        check(louder && louder->key == "audio_cue_volume" && louder->value == "100" &&
                  louder->speech == "100 percent",
              "right raises a number by its step");
        check(menu.adjust(1)->value == "100", "numbers stop at their maximum");
        check(menu.back() == "Volume. 6 of 6." && !menu.in_category(),
              "back returns to the category list on the same category");
        check(!menu.back() && menu.is_open(),
              "back on the category list leaves closing to the caller");
        menu.move(1);
        menu.enter();
        check(menu.current() == "Subtitles, off. 1 of 8. Reads subtitles.",
              "each category numbers its own settings");
        auto on = menu.adjust(1);
        check(on && on->value == "1" && on->speech == "on", "a toggle switches");
        menu.move(-1);
        check(
            menu.current() ==
                "Low health at, 25 percent. 8 of 8. The health percentage that counts as low health in gauge alerts.",
            "an unreadable value falls back to the default");
        check(menu.adjust(-1)->speech == "20 percent", "left lowers a number");
        menu.back();
        menu.move(2);
        menu.enter();
        check(menu.current().starts_with("Shooting assistance, boosted beacons. 1 of 3. Off,"),
              "choices use their spoken names");
        check(menu.adjust(1)->value == "off" && menu.adjust(-1)->value == "boosted",
              "choices wrap in both directions");
        menu.back();
        menu.move(2);
        menu.enter();
        check(menu.current().starts_with("QTE freeze, 0 milliseconds. 1 of 4. Pauses") &&
                  menu.adjust(1)->value == "50",
              "the QTE freeze is off by default and moves in 50 ms steps");
        check(menu.close() == "Settings closed." && !menu.is_open() && menu.current().empty(),
              "closing");
        check(!menu.adjust(1) && !menu.back(), "a closed menu changes nothing");
        check(menu.open(read) == "Blind Rage settings. Gameplay assists. 5 of 6.",
              "reopening returns to the category last used");
        std::set<std::string> keys;
        size_t count = 0;
        for (const auto &category : gameplay_settings())
            for (const auto &item : category.items) {
                keys.insert(item.key);
                ++count;
            }
        check(count == 27 && keys.size() == 27, "every setting appears exactly once");
        for (const auto &category : gameplay_settings())
            for (const auto &item : category.items)
                check(!item.description.empty(), "every setting has a description");
        std::cout << "Settings menu tests passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
