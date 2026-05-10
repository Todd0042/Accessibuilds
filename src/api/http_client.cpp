#include "http_client.h"
#include "api_rate_limiter.h"
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <vector>

namespace Http {

/* One session per User-Agent, created at AddonLoad, destroyed at AddonUnload.
 * WinHttpOpen triggers the Windows thread pool; creating it once at startup
 * instead of per-request prevents a spike of ~20 pool threads on login that
 * was pushing GW2's D3D allocator to fail with ERROR_NOT_ENOUGH_MEMORY. */
static HINTERNET s_session      = nullptr; /* Accessibuilds/1.0 — GW2 API calls  */
static HINTERNET s_session_page = nullptr; /* browser UA       — web-page fetches */

void Init()
{
    s_session = WinHttpOpen(
        L"Accessibuilds/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);

    s_session_page = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        L"AppleWebKit/537.36 (KHTML, like Gecko) "
        L"Chrome/124.0.0.0 Safari/537.36",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
}

void Shutdown()
{
    if (s_session_page) { WinHttpCloseHandle(s_session_page); s_session_page = nullptr; }
    if (s_session)      { WinHttpCloseHandle(s_session);      s_session = nullptr; }
}

static Response DoGet(const std::wstring& host, const std::wstring& path,
                      const std::wstring& extra_headers)
{
    APIRateLimit::WaitAndAcquire();
    APIRateLimit::g_InFlight++;
    struct InFlightGuard { ~InFlightGuard() { APIRateLimit::g_InFlight--; } } _guard;

    Response resp;
    if (!s_session) { resp.error = "Http not initialized"; return resp; }

    HINTERNET hConnect = WinHttpConnect(
        s_session, host.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        resp.error = "WinHttpConnect failed";
        return resp;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        resp.error = "WinHttpOpenRequest failed";
        return resp;
    }

    if (!extra_headers.empty()) {
        WinHttpAddRequestHeaders(hRequest, extra_headers.c_str(),
                                 (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    }

    if (!WinHttpSendRequest(hRequest,
                            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        resp.error = "WinHttpSendRequest failed";
        return resp;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        resp.error = "WinHttpReceiveResponse failed";
        return resp;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &status_size, WINHTTP_NO_HEADER_INDEX);
    resp.status_code = (int)status;

    std::string body;
    DWORD bytes_available = 0;
    do {
        bytes_available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &bytes_available)) break;
        if (bytes_available == 0) break;

        std::vector<char> buf(bytes_available + 1, 0);
        DWORD bytes_read = 0;
        WinHttpReadData(hRequest, buf.data(), bytes_available, &bytes_read);
        body.append(buf.data(), bytes_read);
    } while (bytes_available > 0);

    resp.body = std::move(body);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    return resp;
}

Response Get(const std::wstring& host, const std::wstring& path,
             const std::wstring& extra_headers)
{
    return DoGet(host, path, extra_headers);
}

Response GetWithBearer(const std::wstring& host, const std::wstring& path,
                       const std::string& api_key)
{
    std::wstring auth_header = L"Authorization: Bearer ";
    auth_header.append(api_key.begin(), api_key.end());
    return DoGet(host, path, auth_header);
}

Response GetPage(const std::wstring& host, const std::wstring& path)
{
    Response resp;
    if (!s_session_page) { resp.error = "Http not initialized"; return resp; }

    HINTERNET hConnect = WinHttpConnect(s_session_page, host.c_str(),
                                        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { resp.error = "WinHttpConnect failed"; return resp; }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        resp.error = "WinHttpOpenRequest failed";
        return resp;
    }

    WinHttpAddRequestHeaders(hRequest,
        L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
        L"Accept-Language: en-US,en;q=0.9\r\n",
        (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        resp.error = "WinHttpSendRequest/ReceiveResponse failed";
        return resp;
    }

    DWORD status = 0; DWORD ssz = sizeof(status);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &ssz, WINHTTP_NO_HEADER_INDEX);
    resp.status_code = (int)status;

    /* Read until WinHttpReadData signals end-of-data with 0 bytes.
     * WinHttpQueryDataAvailable can return 0 between chunks on large pages
     * (SC HTML is ~500 KB), so drive the loop off the read result instead. */
    char read_buf[65536];
    DWORD bytes_read = 0;
    do {
        bytes_read = 0;
        if (!WinHttpReadData(hRequest, read_buf, sizeof(read_buf), &bytes_read))
            break;
        if (bytes_read > 0)
            resp.body.append(read_buf, bytes_read);
    } while (bytes_read > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    return resp;
}

} /* namespace Http */
