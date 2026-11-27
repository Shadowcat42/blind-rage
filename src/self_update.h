#pragma once
// Updates a release installation of Blind Rage from the latest GitHub release.
//
// Each release publishes latest.txt beside its package:
//   version 0.3.0
//   file <sha256> <size> blind-rage-0.3.0-windows-x64.zip
// On a normal launch with no game running, BlindRage.exe reads it, and when the
// release is newer it asks before downloading. The package must match the size
// and SHA-256 in latest.txt, is extracted with Windows' own tar into a staging
// folder in the installation, and its files then replace the installed ones.
// Every replaced file is first moved aside, so any failure puts the old files
// back. The running BlindRage.exe can be renamed but not deleted, so its old
// copy is removed on the next launch. The package has no accessibility.ini, so
// settings are never touched. Development builds carry no version and never
// update; nor does a BlindRage.exe outside the installation's root.
#include "http_client.h"
#include "platform.h"
#include "recomp_setup.h"
#include "restart_support.h"
#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
namespace a11y {
struct ReleaseInfo {
    std::string version, sha256, file;
    uint64_t size{};
};
// "0.3.0" -> {0, 3, 0}; empty unless it is dot-separated numbers.
inline std::vector<int> version_parts(std::string_view text) {
    std::vector<int> parts;
    int value = 0;
    bool digit = false;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '.') {
            if (!digit)
                return {};
            parts.push_back(value);
            value = 0;
            digit = false;
        } else if (text[i] >= '0' && text[i] <= '9' && value < 100000) {
            value = value * 10 + (text[i] - '0');
            digit = true;
        } else
            return {};
    }
    return parts;
}
// True when `remote` is a later version than `local` (missing parts count as 0).
inline bool newer_version(std::string_view remote, std::string_view local) {
    auto a = version_parts(remote), b = version_parts(local);
    if (a.empty() || b.empty())
        return false;
    a.resize(std::max(a.size(), b.size()));
    b.resize(a.size());
    return a > b;
}
inline std::optional<ReleaseInfo> parse_release_info(std::string_view text) {
    ReleaseInfo info;
    std::istringstream in{std::string(text)};
    std::string line;
    bool file = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::istringstream fields(line);
        std::string kind;
        fields >> kind;
        if (kind == "version")
            fields >> info.version;
        else if (kind == "file")
            file = bool(fields >> info.sha256 >> info.size >> info.file);
    }
    auto hex = info.sha256.size() == 64 &&
               std::all_of(info.sha256.begin(), info.sha256.end(),
                           [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    auto name =
        !info.file.empty() && info.file.front() != '.' && info.file.ends_with(".zip") &&
        std::all_of(info.file.begin(), info.file.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_';
        });
    if (!file || version_parts(info.version).empty() || !hex || !name || !info.size ||
        info.size > 200ull * 1024 * 1024)
        return std::nullopt;
    return info;
}
// The changelog sections ("## 0.3.0" headings) for versions after `current` up
// to `latest`, newest first, as plain lines: Markdown emphasis and code marks
// are removed and list items start with "- ". Empty when there are none.
inline std::string changelog_since(std::string_view text, std::string_view current,
                                   std::string_view latest) {
    std::string out, line;
    std::istringstream in{std::string(text)};
    bool keep = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.starts_with("## ")) {
            std::istringstream heading(line.substr(3));
            std::string version;
            heading >> version;
            keep = !version_parts(version).empty() && newer_version(version, current) &&
                   !newer_version(version, latest);
            if (keep)
                out +=
                    (out.empty() ? "" : "\n") + std::string("Changes in ") + line.substr(3) + ":\n";
            continue;
        }
        if (!keep || line.starts_with("# "))
            continue;
        std::string plain;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '`' || (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '*')) {
                if (line[i] == '*')
                    ++i;
                continue;
            }
            plain += line[i];
        }
        // Links: keep the text of [text](target).
        for (size_t open; (open = plain.find('[')) != std::string::npos;) {
            auto mid = plain.find("](", open),
                 close = mid == std::string::npos ? std::string::npos : plain.find(')', mid);
            if (close == std::string::npos)
                break;
            plain = plain.substr(0, open) + plain.substr(open + 1, mid - open - 1) +
                    plain.substr(close + 1);
        }
        while (!plain.empty() && plain.back() == ' ')
            plain.pop_back();
        if (!plain.empty())
            out += plain + "\n";
    }
    return out;
}
// Moves every file under `staging` to the same place under `root`. Existing
// files are moved aside to "<name>.old" first and restored if anything fails.
inline bool apply_staged(const fs::path &staging, const fs::path &root, std::string &error) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(staging, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_directory(ec))
            continue;
        if (!it->is_regular_file(ec) || it->is_symlink(ec)) {
            error = "The package contains something other than files.";
            return false;
        }
        auto relative = it->path().lexically_relative(staging);
        for (const auto &part : relative)
            if (part == L".." || part == L".") {
                error = "The package contains an unsafe path.";
                return false;
            }
        files.push_back(relative);
    }
    if (ec) {
        error = "Could not read the extracted package.";
        return false;
    }
    for (auto required : {L"BlindRage.exe", L"mods/accessibility/dist/asura_accessibility.dll",
                          L"mods/accessibility/dist/prism.dll"})
        if (!fs::is_regular_file(staging / required)) {
            error = "The package is missing " + utf8(fs::path(required).wstring()) + ".";
            return false;
        }
    struct Step {
        fs::path target, backup;
        bool placed{};
    };
    std::vector<Step> done;
    auto rollback = [&] {
        for (auto step = done.rbegin(); step != done.rend(); ++step) {
            if (step->placed)
                DeleteFileW(step->target.c_str());
            if (!step->backup.empty())
                MoveFileExW(step->backup.c_str(), step->target.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    };
    for (const auto &relative : files) {
        Step step{root / relative, {}, false};
        fs::create_directories(step.target.parent_path(), ec);
        if (fs::exists(step.target)) {
            step.backup = step.target;
            step.backup += L".old";
            DeleteFileW(step.backup.c_str());
            if (!MoveFileExW(step.target.c_str(), step.backup.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                error = "Could not replace " + utf8(relative.wstring()) + "; is it in use?";
                rollback();
                return false;
            }
        }
        done.push_back(step);
        if (!MoveFileExW((staging / relative).c_str(), step.target.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            error = "Could not write " + utf8(relative.wstring()) + ".";
            rollback();
            return false;
        }
        done.back().placed = true;
    }
    // The running launcher's old copy cannot be deleted yet; the next launch does.
    for (const auto &step : done)
        if (!step.backup.empty())
            DeleteFileW(step.backup.c_str());
    return true;
}
class SelfUpdate {
  public:
    explicit SelfUpdate(fs::path root) : root_(std::move(root)) {}
    // Checks for a newer release and offers it. Returns the exit code of the
    // updated BlindRage.exe it started, or nothing to carry on launching.
    std::optional<int> run(std::string_view current, const std::vector<std::wstring> &arguments) {
        auto exe = root_ / L"BlindRage.exe";
        std::error_code ec;
        if (version_parts(current).empty() || !fs::equivalent(module_path(), exe, ec))
            return std::nullopt;
        cleanup();
        auto text = http_get(wide(kBase) + L"latest/download/latest.txt", 4096, 5000);
        auto info = text ? parse_release_info(*text) : std::nullopt;
        if (!info || !newer_version(info->version, current))
            return std::nullopt;
        // What changed, from the new release's own CHANGELOG.md, including any
        // versions skipped in between. Missing notes never stop the update.
        if (auto changelog =
                http_get(wide(std::string(kSource) + "v" + info->version + "/CHANGELOG.md"),
                         512 * 1024, 5000)) {
            auto notes = changelog_since(*changelog, current, info->version);
            std::istringstream lines(notes);
            for (std::string line; std::getline(lines, line);)
                say(line);
        }
        auto answer =
            ask("Blind Rage " + info->version + " is available; you have " + std::string(current) +
                ". Type y and press Enter to update now, or just press Enter to skip. "
                "Your settings are kept.");
        if (answer != L"y" && answer != L"Y") {
            say("Not updating. Set update_check=0 in accessibility.ini to stop these checks.");
            return std::nullopt;
        }
        if (!install(*info)) {
            cleanup();
            say("The update did not complete; nothing was changed. Starting Blind Rage " +
                std::string(current) + ".");
            return std::nullopt;
        }
        cleanup();
        say("Updated to Blind Rage " + info->version +
            ". The full changelog is CHANGELOG.md in mods/accessibility.");
        auto line = windows_argument(exe.wstring()) + L" --skip-update";
        for (const auto &argument : arguments)
            line += L" " + windows_argument(argument);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            root_.c_str(), &si, &pi)) {
            say("Could not start the updated BlindRage.exe. Run it again.");
            return 1;
        }
        CloseHandle(pi.hThread);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        return int(code);
    }
    static constexpr const char *kBase = "https://github.com/Shadowcat42/blind-rage/releases/";
    static constexpr const char *kSource =
        "https://raw.githubusercontent.com/Shadowcat42/blind-rage/";

  private:
    fs::path staging() const {
        return root_ / L"blind_rage_update";
    }
    void cleanup() const {
        std::error_code ec;
        fs::remove_all(staging(), ec);
        fs::remove(root_ / L"BlindRage.exe.old", ec);
    }
    bool install(const ReleaseInfo &info) {
        std::error_code ec;
        auto folder = staging(), zip = folder / L"package.zip", files = folder / L"files";
        fs::remove_all(folder, ec);
        if (!fs::create_directories(files, ec)) {
            say("Could not create the update folder.");
            return false;
        }
        say("Downloading Blind Rage " + info.version + ", " + megabytes(info.size) + " megabytes.");
        uint64_t received = 0;
        bool downloaded;
        {
            std::ofstream out(zip, std::ios::binary);
            downloaded =
                out && http_fetch(
                           wide(std::string(kBase) + "download/v" + info.version + "/" + info.file),
                           [&](const char *data, size_t size) {
                               received += size;
                               return received <= info.size &&
                                      bool(out.write(data, std::streamsize(size)));
                           },
                           30000);
            out.close();
            downloaded = downloaded && out;
        }
        if (!downloaded || received != info.size) {
            say("The download failed. Check your internet connection.");
            return false;
        }
        if (sha256(zip) != info.sha256) {
            say("The download was damaged: its checksum does not match the release.");
            return false;
        }
        std::wstring system(MAX_PATH, L'\0');
        system.resize(GetSystemDirectoryW(system.data(), MAX_PATH));
        auto tar = fs::path(system) / L"tar.exe";
        auto line = windows_argument(tar.wstring()) + L" -x -f " + windows_argument(zip.wstring()) +
                    L" -C " + windows_argument(files.wstring());
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(tar.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, folder.c_str(), &si, &pi)) {
            say("Could not start Windows' tar to extract the update.");
            return false;
        }
        CloseHandle(pi.hThread);
        DWORD code = 1;
        if (WaitForSingleObject(pi.hProcess, 120000) == WAIT_OBJECT_0)
            GetExitCodeProcess(pi.hProcess, &code);
        else
            TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        if (code) {
            say("The update could not be extracted.");
            return false;
        }
        std::string error;
        if (!apply_staged(files, root_, error)) {
            say(error);
            return false;
        }
        return true;
    }
    fs::path root_;
};
} // namespace a11y
