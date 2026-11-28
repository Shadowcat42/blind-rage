#include "../src/settings_menu.h"
#include <iostream>
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
        check(menu.is_open() && first == "Blind Rage settings. Subtitles, off. 1 of 26.",
              "opening reads the first setting's current value");
        check(menu.move(-1) == "Synchronic count-in volume, 100 percent. 26 of 26.",
              "moving up from the top wraps to the last setting");
        menu.move(-6);
        check(menu.current() == "Sound cue volume, 90 percent. 20 of 26.",
              "each cue's volume follows the overall one");
        auto louder = menu.adjust(1);
        check(louder && louder->key == "audio_cue_volume" && louder->value == "100" &&
                  louder->speech == "100 percent",
              "right raises a number by its step");
        check(menu.adjust(1)->value == "100", "numbers stop at their maximum");
        menu.move(6);
        check(menu.move(1) == "Subtitles, off. 1 of 26.", "moving down from the bottom wraps");
        auto on = menu.adjust(1);
        check(on && on->value == "1" && on->speech == "on", "a toggle switches");
        menu.move(8);
        check(menu.current() == "Shooting assistance, boosted beacons. 9 of 26.",
              "choices use their spoken names");
        check(menu.adjust(1)->value == "off" && menu.adjust(-1)->value == "boosted",
              "choices wrap in both directions");
        menu.move(-1);
        check(menu.current() == "Low health at, 25 percent. 8 of 26.",
              "an unreadable value falls back to the default");
        check(menu.adjust(-1)->speech == "20 percent", "left lowers a number");
        check(menu.close() == "Settings closed." && !menu.is_open() && menu.current().empty(),
              "closing");
        check(!menu.adjust(1), "a closed menu changes nothing");
        std::cout << "Settings menu tests passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
