#pragma once
// Accessible console front end for the recomp's own installer (AsurasWrath.exe).
// Its command-line modes do all downloading, validation and extraction; this
// only asks for files, starts them and reports progress and results as plain
// console text. Its command-line modes were verified locally against the
// installer; that research is kept out of the repository.
//
// Those modes print no progress, so progress comes from the installer's own
// work: the bytes it writes (process I/O counters, which unlike directory sizes
// are current while a file is open), against the download size in the
// release manifest or the size of the disc image. Results come from the lines
// it appends to userdata/install_log.txt and update_log.txt.
#include "http_client.h"
#include "platform.h"
#include <windows.h>
#include <tlhelp32.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
namespace a11y {
struct ManifestFile {
    std::string hash, destination, url;
    uint64_t size{};
};
struct Manifest {
    std::string version;
    std::vector<ManifestFile> files;
};
inline Manifest parse_manifest(std::string_view text) {
    Manifest out;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::istringstream fields(line);
        std::string kind;
        fields >> kind;
        if (kind == "version")
            fields >> out.version;
        else if (kind == "file") {
            ManifestFile file;
            if (fields >> file.hash >> file.size >> file.destination >> file.url)
                out.files.push_back(std::move(file));
        }
    }
    return out;
}
// What the updater will download: files whose entry differs from the installed
// manifest (it compares manifests, not the files on disk).
inline uint64_t pending_bytes(const Manifest &remote, const Manifest &local) {
    std::map<std::string, std::string> installed;
    for (const auto &file : local.files)
        installed[file.destination] = file.hash;
    uint64_t total = 0;
    for (const auto &file : remote.files)
        if (auto it = installed.find(file.destination);
            it == installed.end() || it->second != file.hash)
            total += file.size;
    return total;
}
// A typed, pasted or dragged path: surrounding spaces and quotes removed.
inline std::wstring clean_path(std::wstring text) {
    auto trim = [](std::wstring &s) {
        while (!s.empty() && iswspace(s.back()))
            s.pop_back();
        size_t start = 0;
        while (start < s.size() && iswspace(s[start]))
            ++start;
        s.erase(0, start);
    };
    trim(text);
    if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"')
        text = text.substr(1, text.size() - 2);
    trim(text);
    return text;
}
inline std::string megabytes(uint64_t bytes) {
    return std::to_string((bytes + 512 * 1024) / (1024 * 1024));
}
// "34 percent, 560 of 1592 megabytes, about 3 minutes left."
inline std::string progress_text(uint64_t done, uint64_t total, double seconds) {
    if (!total)
        return megabytes(done) + " megabytes so far.";
    done = std::min(done, total);
    auto percent = int(done * 100 / total);
    std::string out = std::to_string(percent) + " percent, " + megabytes(done) + " of " +
                      megabytes(total) + " megabytes";
    if (done && seconds >= 5 && done < total) {
        auto left = (total - done) * seconds / double(done);
        if (left >= 90)
            out += ", about " + std::to_string(int(std::lround(left / 60))) + " minutes left";
        else
            out += ", less than " + std::to_string(left < 45 ? 1 : 2) + " minute" +
                   (left < 45 ? "" : "s") + " left";
    }
    return out + ".";
}
inline bool game_installed(const fs::path &root) {
    std::ifstream state(root / L"userdata" / L"install.txt");
    std::string line;
    bool game = false;
    while (std::getline(state, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line == "game=1")
            game = true;
    }
    return game && fs::exists(root / L"game" / L"default.xex");
}
inline bool program_files_present(const fs::path &root) {
    return fs::exists(root / L"bin" / L"asura_wrath_recomp.exe") ||
           fs::exists(root / L"bin_disc" / L"asura_wrath_recomp.exe");
}
// Console text NVDA reads as it appears.
inline void say(const std::string &text) {
    auto out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode{}, written{};
    auto line = wide(text) + L"\r\n";
    if (GetConsoleMode(out, &mode))
        WriteConsoleW(out, line.data(), DWORD(line.size()), &written, nullptr);
    else
        std::cout << text << "\n" << std::flush;
}
inline std::wstring ask(const std::string &prompt) {
    say(prompt);
    auto in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode{};
    if (GetConsoleMode(in, &mode)) {
        std::wstring line;
        wchar_t buffer[1024];
        DWORD read{};
        while (ReadConsoleW(in, buffer, DWORD(std::size(buffer)), &read, nullptr) && read) {
            line.append(buffer, read);
            if (line.find(L'\n') != std::wstring::npos)
                break;
        }
        return clean_path(line);
    }
    std::string line;
    std::getline(std::cin, line);
    return clean_path(wide(line));
}
class RecompSetup {
  public:
    explicit RecompSetup(fs::path root) : root_(std::move(root)) {}
    // First launch: program files, then the game, then the update/DLC choice.
    bool first_time() {
        auto installer = root_ / L"AsurasWrath.exe";
        if (!fs::exists(installer)) {
            say("AsurasWrath.exe is not in this folder. Put Blind Rage in the same folder as the "
                "Asura's Wrath PC version's AsurasWrath.exe, then run BlindRage.exe again.");
            return false;
        }
        say("Asura's Wrath is not installed yet. Blind Rage will set it up using the PC "
            "version's own installer.");
        if (!program_files_present(root_) && !download_program_files())
            return false;
        if (!install_disc())
            return false;
        extras();
        create_setup_shortcut();
        say("Setup is complete. Starting the game.");
        return true;
    }
    // Later: the same choice of adding the title update or DLC.
    void extras() {
        for (;;) {
            auto choice = ask("Do you want to add the title update or downloadable content now? "
                              "Type a number and press Enter. 1, add the title update. 2, add a "
                              "DLC package. 3, no.");
            if (choice == L"1")
                add_package(L"--update", "the title update");
            else if (choice == L"2")
                add_package(L"--dlc", "the DLC package");
            else if (choice == L"3" || choice.empty())
                return;
            else
                say("Please type 1, 2 or 3.");
        }
    }

  private:
    struct Run {
        DWORD code = 1;
        bool started = false;
    };
    bool download_program_files() {
        say("Downloading the PC version's program files. This is about 1.6 gigabytes and only "
            "happens once.");
        uint64_t total = 0;
        if (auto remote = fetch_manifest()) {
            Manifest local;
            if (std::ifstream in(root_ / L"manifest.txt", std::ios::binary); in) {
                std::ostringstream text;
                text << in.rdbuf();
                local = parse_manifest(text.str());
            }
            total = pending_bytes(*remote, local);
        } else
            say("Could not read the download size; progress will be shown in megabytes.");
        auto run =
            launch({L"--self-update"}, "Downloading", total, L"update_log.txt", "update_tmp");
        if (!run.started)
            return false;
        if (run.code || !program_files_present(root_)) {
            say("The download did not complete. Check your internet connection and run "
                "BlindRage.exe again.");
            return false;
        }
        say("Program files downloaded.");
        return true;
    }
    bool install_disc() {
        for (;;) {
            auto path = ask("Type or paste the full path to your Asura's Wrath Xbox 360 disc "
                            "image, a .iso file, or a .zip, .7z or .rar archive containing one, "
                            "then press Enter. You can also drag the file onto this window. "
                            "Leave it empty to stop.");
            if (path.empty()) {
                say("Setup stopped. Run BlindRage.exe again to continue.");
                return false;
            }
            fs::path file = fs::absolute(fs::path(path));
            if (!fs::is_regular_file(file)) {
                say("That file was not found. Please check the path.");
                continue;
            }
            say("Checking the disc image.");
            auto check = launch({L"--check", file.wstring()}, "", 0, L"install_log.txt");
            if (!check.started)
                return false;
            if (check.code) {
                say("That disc image was not accepted. Please try another file.");
                continue;
            }
            say("The disc image is valid. Installing the game; this copies several gigabytes.");
            std::error_code ignored;
            auto size = fs::file_size(file, ignored);
            bool archive = file.extension() != L".iso" && file.extension() != L".ISO";
            auto install = launch({L"--install", file.wstring()}, "Installing", archive ? 0 : size,
                                  L"install_log.txt");
            if (!install.started)
                return false;
            if (install.code || !game_installed(root_)) {
                say("The installation did not complete. See the messages above.");
                return false;
            }
            say("The game is installed.");
            return true;
        }
    }
    void add_package(const wchar_t *option, const std::string &what) {
        auto path = ask("Type or paste the full path to " + what +
                        ", then press Enter. Leave it empty to go back.");
        if (path.empty())
            return;
        fs::path file = fs::absolute(fs::path(path));
        if (!fs::is_regular_file(file)) {
            say("That file was not found.");
            return;
        }
        auto check = launch({L"--check", L"-", option, file.wstring()}, "", 0, L"install_log.txt");
        if (!check.started)
            return;
        if (check.code) {
            say("That file was not accepted as " + what + ".");
            return;
        }
        auto install =
            launch({L"--install", L"-", option, file.wstring()}, "Adding", 0, L"install_log.txt");
        if (install.started)
            say(install.code ? "Adding " + what + " did not complete." : "Added " + what + ".");
    }
    static std::wstring quote(const std::wstring &argument) {
        if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos)
            return argument;
        std::wstring out = L"\"";
        for (auto c : argument)
            out += c == L'"' ? std::wstring(L"\\\"") : std::wstring(1, c);
        return out + L"\"";
    }
    static uint64_t written(HANDLE process) {
        IO_COUNTERS counters{};
        return GetProcessIoCounters(process, &counters) ? counters.WriteTransferCount : 0;
    }
    // Bytes written by the installer's child processes (Windows tar unpacking
    // an archive), which the installer's own counters do not include.
    static uint64_t children_written(DWORD parent) {
        uint64_t total = 0;
        auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (snapshot != INVALID_HANDLE_VALUE && Process32FirstW(snapshot, &entry))
            do
                if (entry.th32ParentProcessID == parent)
                    if (auto child = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                                 entry.th32ProcessID)) {
                        total += written(child);
                        CloseHandle(child);
                    }
            while (Process32NextW(snapshot, &entry));
        if (snapshot != INVALID_HANDLE_VALUE)
            CloseHandle(snapshot);
        return total;
    }
    // The newest staged download, named "<index>_<file>.part".
    std::string current_download(const char *folder) const {
        std::error_code error;
        int best = -1;
        std::string name;
        for (const auto &entry : fs::directory_iterator(root_ / folder, error)) {
            auto file = entry.path().filename().string();
            auto underscore = file.find('_');
            if (underscore == std::string::npos || !file.ends_with(".part"))
                continue;
            int index = std::atoi(file.substr(0, underscore).c_str());
            if (index > best) {
                best = index;
                name = file.substr(underscore + 1, file.size() - underscore - 6);
            }
        }
        return name;
    }
    Run launch(const std::vector<std::wstring> &arguments, const std::string &label, uint64_t total,
               const wchar_t *log, const char *staging = nullptr) {
        Run run;
        auto exe = root_ / L"AsurasWrath.exe";
        auto logPath = root_ / L"userdata" / log;
        std::error_code ignored;
        auto before = fs::exists(logPath) ? fs::file_size(logPath, ignored) : 0;
        std::wstring line = quote(exe.wstring());
        for (const auto &argument : arguments)
            line += L" " + quote(argument);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            root_.c_str(), &si, &pi)) {
            say("Could not start AsurasWrath.exe.");
            return run;
        }
        run.started = true;
        CloseHandle(pi.hThread);
        auto started = GetTickCount64(), lastSaid = started;
        int lastTenth = -1;
        std::string lastFile;
        while (WaitForSingleObject(pi.hProcess, 1000) == WAIT_TIMEOUT) {
            if (label.empty())
                continue;
            auto now = GetTickCount64();
            auto done = written(pi.hProcess) + children_written(pi.dwProcessId);
            auto tenth = total ? int(std::min<uint64_t>(done, total) * 10 / total) : -1;
            auto file = staging ? current_download(staging) : std::string{};
            bool newFile = !file.empty() && file != lastFile && file.find("shader_pack") == 0;
            if (tenth != lastTenth || now - lastSaid >= 15000 || newFile) {
                std::string text =
                    label + ": " + progress_text(done, total, double(now - started) / 1000);
                if (newFile)
                    text += " Now " + file + ".";
                say(text);
                lastSaid = now;
                lastTenth = tenth;
            }
            if (!file.empty())
                lastFile = file;
        }
        GetExitCodeProcess(pi.hProcess, &run.code);
        CloseHandle(pi.hProcess);
        // The installer's own result lines, added during this run.
        if (std::ifstream in(logPath, std::ios::binary); in) {
            in.seekg(std::streamoff(before));
            std::string text;
            while (std::getline(in, text)) {
                if (!text.empty() && text.back() == '\r')
                    text.pop_back();
                if (!text.empty())
                    say("Installer: " + text);
            }
        }
        return run;
    }
    std::optional<Manifest> fetch_manifest() const {
        std::string url =
            "https://github.com/omar2300/asuras-wrath-pc/releases/latest/download/manifest.txt";
        if (std::ifstream source(root_ / L"update_source.txt"); source) {
            std::string line;
            if (std::getline(source, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                    line.pop_back();
                if (line.starts_with("https://"))
                    url = line;
            }
        }
        auto text = http_get(wide(url));
        if (!text)
            return std::nullopt;
        auto manifest = parse_manifest(*text);
        if (manifest.files.empty())
            return std::nullopt;
        return manifest;
    }
    // "Blind Rage setup" in the installation folder, like the PC version's own
    // "Add update or DLC" shortcut.
    void create_setup_shortcut() const {
        auto link = root_ / L"Blind Rage setup.lnk";
        if (fs::exists(link))
            return;
        bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        IShellLinkW *shell = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_IShellLinkW, reinterpret_cast<void **>(&shell)))) {
            shell->SetPath((root_ / L"BlindRage.exe").c_str());
            shell->SetArguments(L"--setup");
            shell->SetWorkingDirectory(root_.c_str());
            shell->SetDescription(L"Add the title update or downloadable content");
            IPersistFile *file = nullptr;
            if (SUCCEEDED(
                    shell->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&file)))) {
                if (SUCCEEDED(file->Save(link.c_str(), TRUE)))
                    say("Created the Blind Rage setup shortcut in this folder, for adding the "
                        "title update or DLC later.");
                file->Release();
            }
            shell->Release();
        }
        if (com)
            CoUninitialize();
    }
    fs::path root_;
};
} // namespace a11y
