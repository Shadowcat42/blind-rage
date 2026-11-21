#include "../src/hotkey_names.h"
#include <cstdio>
#include <stdexcept>
using namespace a11y;
static void check(bool ok, const char *what) {
    if (!ok)
        throw std::runtime_error(what);
}
int main() {
    try {
        constexpr UINT chord = MOD_CONTROL | MOD_ALT;
        check(parse_hotkey("Ctrl+Alt+F9") == Hotkey{chord, VK_F9}, "named chord");
        check(parse_hotkey(" ctrl + alt + f12 ") == Hotkey{chord, VK_F12},
              "names ignore case and spacing");
        check(parse_hotkey("Alt+Control+F1") == Hotkey{chord, VK_F1}, "modifier order is free");
        check(parse_hotkey("Shift+Win+Numpad5") == Hotkey{MOD_SHIFT | MOD_WIN, VK_NUMPAD5},
              "shift, win and numpad digits");
        check(parse_hotkey("Ctrl+Shift+A") == Hotkey{MOD_CONTROL | MOD_SHIFT, 'A'}, "letters");
        check(parse_hotkey("Ctrl+Alt+7") == Hotkey{chord, '7'}, "digit keys are not codes");
        check(parse_hotkey("F9") == Hotkey{0, VK_F9}, "a bare key is taken as written");
        check(parse_hotkey("Ctrl+Alt+PageDown") == Hotkey{chord, VK_NEXT}, "named keys");
        check(parse_hotkey("Ctrl+Alt+Slash") == Hotkey{chord, VK_OEM_2}, "punctuation names");
        check(parse_hotkey("120") == Hotkey{chord, VK_F9}, "legacy codes keep Ctrl+Alt");
        check(parse_hotkey("0") == Hotkey{} && parse_hotkey("None") == Hotkey{} &&
                  parse_hotkey("off") == Hotkey{},
              "disable words");
        for (auto bad : {"Ctrl+Alt+", "Ctrl+Alt+F25", "Ctrl+Ctrl+F1", "Hyper+F1", "Ctrl+Alt+Foo",
                         "Ctrl++F1", "999", "F0"})
            check(!parse_hotkey(bad), "invalid text is rejected");
        check(describe_hotkey({chord, VK_F10}) == "Ctrl+Alt+F10" &&
                  describe_hotkey({MOD_SHIFT, VK_NUMPAD0}) == "Shift+Numpad0" &&
                  describe_hotkey({chord, VK_OEM_3}) == "Ctrl+Alt+Backtick" &&
                  describe_hotkey({}) == "None",
              "description uses the canonical name");
        for (UINT key = 1; key < 255; ++key) {
            auto name = key_to_name(key);
            if (name.rfind("key ", 0) == 0)
                continue;
            auto back = parse_hotkey("Ctrl+" + name);
            check(back && back->key == key, "every described key parses back");
        }
        std::puts("Hotkey name parsing tests passed.");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
