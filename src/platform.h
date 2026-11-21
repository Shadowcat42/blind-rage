#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>
#include "build_profile.h"
namespace a11y {
namespace fs = std::filesystem;
inline fs::path module_path(HMODULE m = nullptr) {
    std::wstring b(32768, L'\0');
    auto n = GetModuleFileNameW(m, b.data(), DWORD(b.size()));
    b.resize(n);
    return b;
}
inline std::string utf8(const std::wstring &v) {
    int n = WideCharToMultiByte(CP_UTF8, 0, v.data(), int(v.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, v.data(), int(v.size()), s.data(), n, nullptr, nullptr);
    return s;
}
inline std::wstring wide(const std::string &v) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, v.data(), int(v.size()), nullptr, 0);
    std::wstring s(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, v.data(), int(v.size()), s.data(), n);
    return s;
}
inline std::string sha256(const fs::path &file) {
    BCRYPT_ALG_HANDLE alg{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return {};
    DWORD size{}, got{};
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&size, sizeof(size), &got, 0);
    std::vector<unsigned char> object(size);
    std::string result;
    if (BCryptCreateHash(alg, &hash, object.data(), size, nullptr, 0, 0) >= 0) {
        std::ifstream in(file, std::ios::binary);
        std::vector<char> buffer(1024 * 1024);
        bool ok = bool(in);
        while (in) {
            in.read(buffer.data(), buffer.size());
            auto n = in.gcount();
            if (n && BCryptHashData(hash, (PUCHAR)buffer.data(), ULONG(n), 0) < 0)
                ok = false;
        }
        if (!in.eof())
            ok = false;
        unsigned char digest[32]{};
        if (ok && BCryptFinishHash(hash, digest, 32, 0) >= 0) {
            const char *hex = "0123456789abcdef";
            for (auto b : digest) {
                result += hex[b >> 4];
                result += hex[b & 15];
            }
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return result;
}
inline const BuildProfile *identify_build(const fs::path &bin, std::string &error) {
    std::array<std::string, 3> hashes{sha256(bin / L"asura_wrath_recomp.exe"),
                                      sha256(bin / L"rexruntime.dll"),
                                      sha256(bin / L"mark2_nr.dll")};
    auto profile = profile_fingerprints(hashes);
    if (!profile)
        error =
            "Unsupported or unreadable recomp files. Expected a verified Mark2 disc or title-update build 2026.11.01.1627. Accessibility was not loaded.";
    return profile;
}
inline bool supported(const fs::path &bin, std::string &error) {
    return identify_build(bin, error) != nullptr;
}
inline bool supported_game_data(const fs::path &root, const BuildProfile &profile,
                                std::string &error) {
    if (profile.patch_fingerprint &&
        sha256(root / L"game" / L"default.xexp") != profile.patch_fingerprint) {
        error =
            "The installed title-update patch does not match the verified update. Reinstall the supported title update or select --build disc.";
        return false;
    }
    return true;
}
struct Module {
    uintptr_t base{};
    fs::path path;
};
inline Module remote_module(DWORD pid, const wchar_t *name) {
    auto h = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (h == INVALID_HANDLE_VALUE)
        return {};
    MODULEENTRY32W m{};
    m.dwSize = sizeof(m);
    Module out;
    if (Module32FirstW(h, &m))
        do {
            if (!_wcsicmp(name, m.szModule)) {
                out = {(uintptr_t)m.modBaseAddr, m.szExePath};
                break;
            }
        } while (Module32NextW(h, &m));
    CloseHandle(h);
    return out;
}
inline std::wstring event_name(DWORD pid, const std::wstring &command) {
    return L"Local\\AsuraA11y." + std::to_wstring(pid) + L"." + command;
}
}
