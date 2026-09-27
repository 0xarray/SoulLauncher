#include "util.h"

#include <windows.h>
#include <fstream>

std::wstring Widen(const std::string& s)
{
	if (s.empty())
		return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring w(n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
	return w;
}

std::string Narrow(const std::wstring& w)
{
	if (w.empty())
		return {};
	int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
	return s;
}

std::wstring ExeDir()
{
	wchar_t buf[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, buf, MAX_PATH);
	std::wstring p(buf);
	return p.substr(0, p.find_last_of(L"\\/") + 1);
}

std::wstring RegString(const wchar_t* key, const wchar_t* value)
{
	wchar_t buf[1024] = {};
	DWORD size = sizeof(buf);
	if (RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
		return {};
	return buf;
}

unsigned long RegDword(const wchar_t* key, const wchar_t* value, unsigned long def)
{
	DWORD v = 0, size = sizeof(v);
	if (RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS)
		return def;
	return v;
}

bool ReadFileBytes(const std::wstring& path, std::vector<unsigned char>& out)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return false;
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}

bool WriteFileBytes(const std::wstring& path, const std::vector<unsigned char>& data)
{
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f)
		return false;
	f.write((const char*)data.data(), data.size());
	return (bool)f;
}

std::string Base64UrlDecode(const std::string& in)
{
	auto val = [](char c) -> int {
		if (c >= 'A' && c <= 'Z') return c - 'A';
		if (c >= 'a' && c <= 'z') return c - 'a' + 26;
		if (c >= '0' && c <= '9') return c - '0' + 52;
		if (c == '-' || c == '+') return 62;
		if (c == '_' || c == '/') return 63;
		return -1;
	};
	std::string out;
	int buf = 0, bits = 0;
	for (char c : in) {
		int v = val(c);
		if (v < 0)
			break;
		buf = (buf << 6) | v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back((char)((buf >> bits) & 0xFF));
		}
	}
	return out;
}
