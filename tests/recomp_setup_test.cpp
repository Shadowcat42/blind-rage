#include "../src/recomp_setup.h"
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool pass, const char *message) {
    if (!pass)
        throw std::runtime_error(message);
}
int main() {
    try {
        auto remote =
            parse_manifest("asuraswrath-manifest 1\r\n"
                           "version 2026.11.01.1627\r\n"
                           "notes Something new\r\n"
                           "file aaa 100 AsurasWrath.exe https://example/a\r\n"
                           "file bbb 2000 bin/asura_wrath_recomp.exe https://example/b\r\n"
                           "file ccc 30000 data/shader_pack_d3d12.bin https://example/c\r\n");
        check(remote.version == "2026.11.01.1627" && remote.files.size() == 3 &&
                  remote.files[2].size == 30000 &&
                  remote.files[1].destination == "bin/asura_wrath_recomp.exe",
              "manifest version and file entries");
        check(pending_bytes(remote, {}) == 32100, "a fresh folder downloads everything");
        auto local =
            parse_manifest("version 2026.11.01.1626\n"
                           "file aaa 100 AsurasWrath.exe https://example/a\n"
                           "file old 2000 bin/asura_wrath_recomp.exe https://example/b\n"
                           "file ccc 30000 data/shader_pack_d3d12.bin https://example/c\n");
        check(pending_bytes(remote, local) == 2000, "an update downloads only changed entries");
        check(clean_path(L"  \"C:\\Games\\Asura's Wrath.iso\"\r\n") ==
                  L"C:\\Games\\Asura's Wrath.iso",
              "dragged paths lose their quotes and line ending");
        check(clean_path(L"D:\\disc.iso") == L"D:\\disc.iso", "plain paths are unchanged");
        check(clean_path(L"   \r\n").empty(), "an empty answer stays empty");
        check(progress_text(0, 0, 0) == "0 megabytes so far.", "no total: megabytes only");
        check(progress_text(512ull << 20, 1024ull << 20, 2) == "50 percent, 512 of 1024 megabytes.",
              "no time estimate in the first seconds");
        check(progress_text(256ull << 20, 1024ull << 20, 60) ==
                  "25 percent, 256 of 1024 megabytes, about 3 minutes left.",
              "minutes left from the average rate");
        check(
            progress_text(1000ull << 20, 1024ull << 20, 100).ends_with("less than 1 minute left."),
            "the last stretch");
        check(progress_text(2000ull << 20, 1024ull << 20, 100) ==
                  "100 percent, 1024 of 1024 megabytes.",
              "progress never passes the total");

        auto root = fs::temp_directory_path() / L"blind-rage-setup-test";
        fs::remove_all(root);
        fs::create_directories(root / L"userdata");
        check(!game_installed(root) && !program_files_present(root), "an empty folder");
        std::ofstream(root / L"userdata" / L"install.txt") << "# state\r\ngame=1\r\n";
        check(!game_installed(root), "the install state alone is not enough");
        fs::create_directories(root / L"game");
        std::ofstream(root / L"game" / L"default.xex") << "x";
        check(game_installed(root), "install state plus the game executable");
        fs::create_directories(root / L"bin_disc");
        std::ofstream(root / L"bin_disc" / L"asura_wrath_recomp.exe") << "x";
        check(program_files_present(root), "either runtime build counts as program files");
        fs::remove_all(root);
        std::cout << "Recomp setup tests passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
