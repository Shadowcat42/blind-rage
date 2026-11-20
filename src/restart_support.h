#pragma once
#include "platform.h"
#include <stdexcept>

namespace a11y {
// Quote one argv element for the Windows C runtime, including embedded quotes
// and trailing backslashes. No shell or command interpreter is involved.
inline std::wstring windows_argument(std::wstring_view value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (auto ch : value) {
        if (ch == L'\\') {
            ++slashes;
            continue;
        }
        result.append(ch == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        result += ch;
    }
    result.append(slashes * 2, L'\\');
    result += L'\"';
    return result;
}
inline DWORD restart_parent_id(std::wstring_view value) {
    uint64_t number = 0;
    if (value.empty())
        throw std::runtime_error("Invalid restart parent");
    for (auto ch : value) {
        if (ch < L'0' || ch > L'9' || number > (MAXDWORD - uint64_t(ch - L'0')) / 10)
            throw std::runtime_error("Invalid restart parent");
        number = number * 10 + uint64_t(ch - L'0');
    }
    if (!number)
        throw std::runtime_error("Invalid restart parent");
    return DWORD(number);
}
inline void wait_for_restart_parent(DWORD pid, const fs::path &expected, DWORD timeout = 30000) {
    if (pid == GetCurrentProcessId())
        throw std::runtime_error("Cannot restart the launcher itself");
    HANDLE parent = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!parent) {
        if (GetLastError() == ERROR_INVALID_PARAMETER)
            return;
        throw std::runtime_error("Cannot wait for the previous game process");
    }
    struct Close {
        HANDLE handle;
        ~Close() {
            CloseHandle(handle);
        }
    } close{parent};
    if (WaitForSingleObject(parent, 0) == WAIT_OBJECT_0)
        return;
    std::wstring image(32768, L'\0');
    DWORD length = DWORD(image.size());
    if (!QueryFullProcessImageNameW(parent, 0, image.data(), &length)) {
        if (WaitForSingleObject(parent, 0) == WAIT_OBJECT_0)
            return;
        throw std::runtime_error("Cannot verify the restarting game process");
    }
    image.resize(length);
    std::error_code error;
    if (!fs::equivalent(fs::path(image), expected, error) || error)
        throw std::runtime_error("Restart parent is not the selected game from this installation");
    if (WaitForSingleObject(parent, timeout) != WAIT_OBJECT_0)
        throw std::runtime_error("The previous game has not exited; restart cancelled");
}
}
