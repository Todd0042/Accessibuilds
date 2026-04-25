#pragma once
#include <string>
#include <functional>

/*
 * Thin WinHTTP wrapper.  All calls happen synchronously on the caller's thread —
 * always invoke from a std::thread to avoid blocking the render thread.
 */
namespace Http {

struct Response {
    int         status_code = 0;
    std::string body;
    std::string error;
    bool        ok() const { return status_code >= 200 && status_code < 300; }
};

/* Blocking GET over HTTPS */
Response Get(const std::wstring& host, const std::wstring& path,
             const std::wstring& extra_headers = L"");

/* Blocking GET with Authorization: Bearer header */
Response GetWithBearer(const std::wstring& host, const std::wstring& path,
                       const std::string& api_key);

} /* namespace Http */
