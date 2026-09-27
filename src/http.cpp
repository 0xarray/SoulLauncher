#include "http.h"
#include "util.h"

#include <windows.h>
#include <winhttp.h>

namespace {

struct Handle {
	HINTERNET h = nullptr;
	Handle(HINTERNET v) : h(v) {}
	~Handle() { if (h) WinHttpCloseHandle(h); }
	operator HINTERNET() const { return h; }
};

std::string LastError(const char* what)
{
	return std::string(what) + " failed (" + std::to_string(GetLastError()) + ")";
}

}

bool HttpRequest(const wchar_t* method, const std::wstring& url, const HttpHeaders& headers, const std::string& body, HttpResponse& out, std::string& err)
{
	URL_COMPONENTS uc = { sizeof(uc) };
	wchar_t host[256] = {}, path[2048] = {};
	uc.lpszHostName = host;
	uc.dwHostNameLength = _countof(host);
	uc.lpszUrlPath = path;
	uc.dwUrlPathLength = _countof(path);
	if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
		err = LastError("WinHttpCrackUrl");
		return false;
	}

	// VFUN talks through libcurl, which sends no User-Agent.
	Handle session = WinHttpOpen(nullptr, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) {
		err = LastError("WinHttpOpen");
		return false;
	}
	Handle connect = WinHttpConnect(session, host, uc.nPort, 0);
	if (!connect) {
		err = LastError("WinHttpConnect");
		return false;
	}
	DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
	Handle request = WinHttpOpenRequest(connect, method, path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (!request) {
		err = LastError("WinHttpOpenRequest");
		return false;
	}
	DWORD noCookies = WINHTTP_DISABLE_COOKIES;
	WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &noCookies, sizeof(noCookies));

	std::string raw;
	for (const auto& [k, v] : headers)
		raw += k + ": " + v + "\r\n";
	std::wstring wraw = Widen(raw);

	if (!WinHttpSendRequest(request, wraw.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wraw.c_str(), wraw.empty() ? 0 : (DWORD)-1L, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) ||
		!WinHttpReceiveResponse(request, nullptr)) {
		err = LastError("HTTP request");
		return false;
	}

	DWORD status = 0, size = sizeof(status);
	WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
	out.status = status;

	SYSTEMTIME modified = {};
	size = sizeof(modified);
	if (WinHttpQueryHeaders(request, WINHTTP_QUERY_LAST_MODIFIED | WINHTTP_QUERY_FLAG_SYSTEMTIME, WINHTTP_HEADER_NAME_BY_INDEX, &modified, &size, WINHTTP_NO_HEADER_INDEX)) {
		FILETIME ft;
		if (SystemTimeToFileTime(&modified, &ft))
			out.lastModified = (long long)((((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000000ULL) - 11644473600LL;
	}

	for (DWORD index = 0;;) {
		DWORD len = 0;
		WinHttpQueryHeaders(request, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &len, &index);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
			break;
		std::wstring value(len / sizeof(wchar_t), L'\0');
		if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &len, &index))
			break;
		value.resize(len / sizeof(wchar_t));
		out.setCookies.push_back(Narrow(value));
	}

	for (;;) {
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0)
			break;
		size_t off = out.body.size();
		out.body.resize(off + avail);
		DWORD read = 0;
		if (!WinHttpReadData(request, out.body.data() + off, avail, &read))
			break;
		out.body.resize(off + read);
	}
	return true;
}

std::string CookieValue(const HttpResponse& resp, const std::string& name)
{
	for (const auto& c : resp.setCookies) {
		if (c.compare(0, name.size(), name) != 0 || c.size() <= name.size() || c[name.size()] != '=')
			continue;
		size_t start = name.size() + 1;
		return c.substr(start, c.find(';', start) - start);
	}
	return {};
}
