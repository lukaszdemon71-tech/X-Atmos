#include "http.hpp"

#include <cstdio>

#if IBM
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <winhttp.h>
#else
    #include <curl/curl.h>
#endif

namespace af::http {

#if IBM

namespace {

// RAII for WinHTTP handles: the original code leaked one on every early
// return path.
struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET handle) : h(handle) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

} // namespace

bool get(const std::string& url, std::string& body, std::string& error)
{
    body.clear();

    std::wstring wide(url.begin(), url.end());          // the URL is ASCII by construction
    wchar_t host[256] = {};
    wchar_t path[8192] = {};
    URL_COMPONENTS parts = {};
    parts.dwStructSize     = sizeof(parts);
    parts.lpszHostName     = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath      = path;
    parts.dwUrlPathLength  = static_cast<DWORD>(std::size(path));
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) { error = "bad URL"; return false; }

    Handle session(WinHttpOpen(L"AtmoForge/0.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) { error = "no HTTP session"; return false; }
    WinHttpSetTimeouts(session.h, 8000, 8000, 15000, 15000);

    Handle connection(WinHttpConnect(session.h, host, parts.nPort, 0));
    if (!connection) { error = "connect failed"; return false; }

    const DWORD flags = (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    Handle request(WinHttpOpenRequest(connection.h, L"GET", path, nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) { error = "request failed"; return false; }

    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        error = "no response";
        return false;
    }

    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "HTTP %lu", static_cast<unsigned long>(status));
        error = buffer;
        return false;
    }

    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request.h, &available) && available > 0) {
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.h, chunk.data(), available, &read)) break;
        body.append(chunk.data(), read);
        if (body.size() > 8u * 1024 * 1024) { error = "response too large"; return false; }
    }
    if (body.empty()) { error = "empty response"; return false; }
    return true;
}

#else   // ── libcurl: Linux and macOS ──────────────────────────────────────

namespace {

std::size_t collect(void* data, std::size_t size, std::size_t count, void* target)
{
    static_cast<std::string*>(target)->append(static_cast<char*>(data), size * count);
    return size * count;
}

} // namespace

bool get(const std::string& url, std::string& body, std::string& error)
{
    body.clear();
    CURL* handle = curl_easy_init();
    if (handle == nullptr) { error = "curl init failed"; return false; }

    char message[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, message);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "AtmoForge/0.2");
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);          // we run off the main thread

    const CURLcode result = curl_easy_perform(handle);
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(handle);

    if (result != CURLE_OK) {
        error = message[0] != '\0' ? message : curl_easy_strerror(result);
        return false;
    }
    if (status != 200) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "HTTP %ld", status);
        error = buffer;
        return false;
    }
    if (body.empty()) { error = "empty response"; return false; }
    return true;
}

#endif

} // namespace af::http
