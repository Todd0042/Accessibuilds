#include "http_client.h"
#include "api_rate_limiter.h"
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <vector>

namespace Http {

static Response DoGet(const std::wstring& host, const std::wstring& path,
                      const std::wstring& extra_headers)
{
    APIRateLimit::WaitAndAcquire();   /* respect GW2 API rate limit */
    APIRateLimit::g_InFlight++;
    struct InFlightGuard { ~InFlightGuard() { APIRateLimit::g_InFlight--; } } _guard;

    Response resp;

    HINTERNET hSession = WinHttpOpen(
        L"BuildCoach/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        resp.error = "WinHttpOpen failed";
        return resp;
    }

    HINTERNET hConnect = WinHttpConnect(
        hSession, host.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
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
        WinHttpCloseHandle(hSession);
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
        WinHttpCloseHandle(hSession);
        resp.error = "WinHttpSendRequest failed";
        return resp;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
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
    WinHttpCloseHandle(hSession);
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

} /* namespace Http */
