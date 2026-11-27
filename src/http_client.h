#pragma once
// Minimal HTTPS GET over WinHTTP, shared by the setup front end and the
// updater. Redirects are followed (GitHub release downloads redirect to its
// file host); only HTTPS URLs are accepted.
#include "platform.h"
#include <winhttp.h>
#include <functional>
#include <optional>
#include <string>
namespace a11y {
// Streams the body of a 200 response to `sink`, which returns false to stop.
// Returns true only when the whole body arrived and `sink` accepted it.
inline bool http_fetch(const std::wstring &url,
                       const std::function<bool(const char *, size_t)> &sink,
                       int timeout_ms = 30000) {
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256]{}, path[2048]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = DWORD(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = DWORD(std::size(path));
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
        return false;
    bool ok = false;
    auto session = WinHttpOpen(L"BlindRage/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session)
        WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    auto connection = session ? WinHttpConnect(session, host, parts.nPort, 0) : nullptr;
    auto request = connection
                       ? WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                       : nullptr;
    DWORD status = 0, size = sizeof(status);
    if (request &&
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                           0) &&
        WinHttpReceiveResponse(request, nullptr) &&
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                            WINHTTP_NO_HEADER_INDEX) &&
        status == 200) {
        ok = true;
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) {
                ok = false;
                break;
            }
            if (!available)
                break;
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read) || !read ||
                !sink(chunk.data(), read)) {
                ok = false;
                break;
            }
        }
    }
    for (auto handle : {request, connection, session})
        if (handle)
            WinHttpCloseHandle(handle);
    return ok;
}
// A small text body, or nothing on any failure or when it exceeds `limit`.
inline std::optional<std::string> http_get(const std::wstring &url, size_t limit = 1024 * 1024,
                                           int timeout_ms = 30000) {
    std::string body;
    bool ok = http_fetch(
        url,
        [&](const char *data, size_t size) {
            if (body.size() + size > limit)
                return false;
            body.append(data, size);
            return true;
        },
        timeout_ms);
    if (!ok)
        return std::nullopt;
    return body;
}
} // namespace a11y
