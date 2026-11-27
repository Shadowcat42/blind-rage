#include "../src/self_update.h"
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool pass, const char *message) {
    if (!pass)
        throw std::runtime_error(message);
}
static void write_file(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
static std::string read_file(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}
int main() {
    try {
        check(newer_version("0.3.0", "0.2.0"), "a later minor version is newer");
        check(newer_version("0.10.0", "0.9.5"), "parts compare as numbers");
        check(newer_version("1.0", "0.9.9"), "a later major version is newer");
        check(!newer_version("0.2", "0.2.0"), "missing parts count as zero");
        check(!newer_version("0.2.0", "0.3.0"), "an older release is not offered");
        check(!newer_version("0.3.0", ""), "a development build never updates");
        check(!newer_version("0.3.0-beta", "0.2.0") && !newer_version("0..3", "0.2.0"),
              "malformed versions are ignored");

        const char *changelog = "# Changelog\r\n\r\n## 0.4.0\r\n\r\n"
                                "- A **melee** beacon; `melee_beacon=0` turns it off.\r\n"
                                "- See [melee aiming](docs/MELEE_AIM.md).\r\n\r\n"
                                "## 0.3.0\r\n\r\n- Updates itself.\r\n\r\n"
                                "## 0.2.0\r\n\r\n- Setup.\r\n";
        check(changelog_since(changelog, "0.2.0", "0.4.0") ==
                  "Changes in 0.4.0:\n- A melee beacon; melee_beacon=0 turns it off.\n"
                  "- See melee aiming.\n\nChanges in 0.3.0:\n- Updates itself.\n",
              "changelog covers every skipped version, as plain text");
        check(changelog_since(changelog, "0.3.0", "0.3.0").empty(), "nothing newer: no notes");
        check(changelog_since(changelog, "0.2.0", "0.3.0") ==
                  "Changes in 0.3.0:\n- Updates itself.\n",
              "versions after the offered release are left out");

        const std::string hash(64, 'a');
        auto info = parse_release_info("version 0.3.0\r\nfile " + hash +
                                       " 1234567 blind-rage-0.3.0-windows-x64.zip\r\n");
        check(info && info->version == "0.3.0" && info->sha256 == hash && info->size == 1234567 &&
                  info->file == "blind-rage-0.3.0-windows-x64.zip",
              "release info fields");
        check(!parse_release_info("version 0.3.0\n"), "release info needs a file line");
        check(!parse_release_info("version 0.3.0\nfile abc 10 a.zip\n"), "short hashes rejected");
        check(!parse_release_info("version 0.3.0\nfile " + hash + " 10 ../a.zip\n"),
              "file names with paths rejected");
        check(!parse_release_info("version 0.3.0\nfile " + hash + " 10 a.exe\n"),
              "only zip packages are accepted");
        check(!parse_release_info("version 0.3.0\nfile " + std::string(64, 'A') + " 10 a.zip\n"),
              "hashes are lowercase hex");
        check(!parse_release_info("version 0.3.0\nfile " + hash + " 0 a.zip\n"),
              "an empty package is rejected");
        check(!parse_release_info("version next\nfile " + hash + " 10 a.zip\n"),
              "the version must be numeric");

        wchar_t temp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, temp);
        auto base =
            fs::path(temp) / (L"blind_rage_update_test_" + std::to_wstring(GetCurrentProcessId()));
        fs::remove_all(base);
        auto root = base / L"root", staging = base / L"staging";
        auto dist = fs::path(L"mods") / L"accessibility" / L"dist";
        write_file(root / L"BlindRage.exe", "old launcher");
        write_file(root / dist / L"asura_accessibility.dll", "old dll");
        write_file(root / L"mods" / L"accessibility" / L"accessibility.ini", "settings");
        write_file(staging / L"BlindRage.exe", "new launcher");
        write_file(staging / dist / L"asura_accessibility.dll", "new dll");
        write_file(staging / dist / L"prism.dll", "new prism");
        write_file(staging / L"mods" / L"accessibility" / L"docs" / L"ROADMAP.md", "roadmap");
        std::string error;
        check(apply_staged(staging, root, error), "a complete package installs");
        check(read_file(root / L"BlindRage.exe") == "new launcher" &&
                  read_file(root / dist / L"asura_accessibility.dll") == "new dll" &&
                  read_file(root / dist / L"prism.dll") == "new prism" &&
                  read_file(root / L"mods" / L"accessibility" / L"docs" / L"ROADMAP.md") ==
                      "roadmap",
              "files replaced and added");
        check(read_file(root / L"mods" / L"accessibility" / L"accessibility.ini") == "settings",
              "settings are untouched");
        check(!fs::exists(root / L"BlindRage.exe.old") &&
                  !fs::exists(root / dist / L"asura_accessibility.dll.old"),
              "old copies removed when not in use");

        // A missing file stops the update before anything changes.
        fs::remove_all(staging);
        write_file(staging / L"BlindRage.exe", "newer launcher");
        check(!apply_staged(staging, root, error) && error.find("missing") != std::string::npos,
              "an incomplete package is refused");
        check(read_file(root / L"BlindRage.exe") == "new launcher", "nothing changed");

        // A file that cannot be replaced puts every earlier file back.
        write_file(staging / dist / L"asura_accessibility.dll", "newer dll");
        write_file(staging / dist / L"prism.dll", "newer prism");
        HANDLE locked = CreateFileW((root / dist / L"prism.dll").c_str(), GENERIC_READ, 0, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
        check(locked != INVALID_HANDLE_VALUE, "lock a file for the rollback test");
        bool applied = apply_staged(staging, root, error);
        CloseHandle(locked);
        check(!applied && error.find("in use") != std::string::npos, "a locked file fails");
        check(read_file(root / L"BlindRage.exe") == "new launcher" &&
                  read_file(root / dist / L"asura_accessibility.dll") == "new dll" &&
                  read_file(root / dist / L"prism.dll") == "new prism",
              "a failed update restores the old files");
        check(!fs::exists(root / L"BlindRage.exe.old"), "no leftovers after a rollback");

        fs::remove_all(base);
        std::cout << "Self-update tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
