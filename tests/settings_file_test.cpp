#include "../src/settings_file.h"
#include "default_settings.h"
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool pass, const char *message) {
    if (!pass)
        throw std::runtime_error(message);
}
static bool has_line(const std::string &text, const std::string &line) {
    return text.find(line + "\r\n") != std::string::npos;
}
int main() {
    try {
        const std::string defaults = "; Shortcuts.\n"
                                     "[hotkeys]\n"
                                     "repeat=Backtick\n"
                                     "status=H\n"
                                     "\n"
                                     "; Narration.\n"
                                     "[gameplay]\n"
                                     "subtitles=1\n"
                                     "shooting_mode=beacons\n"
                                     "audio_cue_volume=60\n";
        auto created = update_settings(defaults, std::nullopt);
        check(created.created && created.text.starts_with("; Shortcuts.\r\n[hotkeys]\r\n") &&
                  has_line(created.text, "audio_cue_volume=60"),
              "a missing file is created with every default and comment");
        auto same = update_settings(defaults, defaults);
        check(!same.created && !same.changed, "an up-to-date file is left alone");
        auto crlf = update_settings(defaults, created.text);
        check(!crlf.changed, "line endings alone never cause a rewrite");

        const std::string old = "[hotkeys]\n"
                                "repeat=Ctrl+Alt+F2\n"
                                "test=Ctrl+Alt+F1\n"
                                "custom=Ctrl+Alt+Q\n"
                                "[gameplay]\n"
                                "SUBTITLES=0\n"
                                "status_button=1\n"
                                "audio_cue_volume=90\n"
                                "[diagnostics]\n"
                                "lock_trace=1\n";
        auto merged = update_settings(defaults, old);
        check(merged.changed && merged.added == 2, "new settings are counted");
        check(has_line(merged.text, "repeat=Ctrl+Alt+F2") && has_line(merged.text, "SUBTITLES=0") &&
                  has_line(merged.text, "audio_cue_volume=90"),
              "values already set are kept, matched case-insensitively");
        check(has_line(merged.text, "status=H") && has_line(merged.text, "shooting_mode=beacons"),
              "settings new to the defaults are added with their default values");
        check(merged.text.find("status_button") == std::string::npos &&
                  merged.text.find("test=") == std::string::npos,
              "retired settings are removed");
        check(merged.text.find("status=H\r\ncustom=Ctrl+Alt+Q\r\n\r\n; Narration.") !=
                  std::string::npos,
              "a setting only in the file stays at the end of its section, before the next "
              "section's comments");
        check(merged.text.ends_with("\r\n[diagnostics]\r\nlock_trace=1\r\n"),
              "a section only in the file is kept");
        check(!update_settings(defaults, merged.text).changed, "the refreshed file is stable");

        const std::string legacy = "[hotkeys]\n"
                                   "repeat=113\n"
                                   "status=0\n"
                                   "[gameplay]\n"
                                   "shooting_assist=1\n"
                                   "beacon_volume=40\n";
        auto migrated = update_settings(defaults, legacy);
        check(has_line(migrated.text, "repeat=Ctrl+Alt+F2") &&
                  has_line(migrated.text, "status=None"),
              "numeric shortcut codes become key names");
        check(has_line(migrated.text, "shooting_mode=boosted") &&
                  has_line(migrated.text, "audio_cue_volume=40") &&
                  migrated.text.find("beacon_volume") == std::string::npos,
              "old setting names carry their values forward");
        check(has_line(update_settings(defaults, "[gameplay]\nshooting_mode=autoaim\n").text,
                       "shooting_mode=boosted"),
              "old shooting mode names are renamed");

        // The real defaults the launcher embeds.
        auto real = update_settings(kDefaultSettings, std::nullopt);
        check(real.created && has_line(real.text, "[hotkeys]") &&
                  has_line(real.text, "[gameplay]") &&
                  !update_settings(kDefaultSettings, real.text).changed,
              "the built-in defaults create a stable file");
        std::cout << "Settings file tests passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
