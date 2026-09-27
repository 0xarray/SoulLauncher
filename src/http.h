#pragma once
#include <string>
#include <utility>
#include <vector>

struct HttpResponse {
	unsigned long status = 0;
	std::string body;
	std::vector<std::string> setCookies;
	long long lastModified = 0; // unix time, 0 if absent
};

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

bool HttpRequest(const wchar_t* method, const std::wstring& url, const HttpHeaders& headers, const std::string& body, HttpResponse& out, std::string& err);

inline bool HttpPost(const std::wstring& url, const HttpHeaders& headers, const std::string& body, HttpResponse& out, std::string& err)
{
	return HttpRequest(L"POST", url, headers, body, out, err);
}

inline bool HttpGet(const std::wstring& url, HttpResponse& out, std::string& err)
{
	return HttpRequest(L"GET", url, {}, {}, out, err);
}

inline bool HttpHead(const std::wstring& url, HttpResponse& out, std::string& err)
{
	return HttpRequest(L"HEAD", url, {}, {}, out, err);
}

std::string CookieValue(const HttpResponse& resp, const std::string& name);
