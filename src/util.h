#pragma once
#include <string>
#include <vector>

std::wstring Widen(const std::string& s);
std::string Narrow(const std::wstring& s);

std::wstring ExeDir();
std::wstring RegString(const wchar_t* key, const wchar_t* value);
unsigned long RegDword(const wchar_t* key, const wchar_t* value, unsigned long def);
bool ReadFileBytes(const std::wstring& path, std::vector<unsigned char>& out);
bool WriteFileBytes(const std::wstring& path, const std::vector<unsigned char>& data);

std::string Base64UrlDecode(const std::string& in);
