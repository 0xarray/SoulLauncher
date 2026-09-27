#include "steam.h"
#include "util.h"

#include <windows.h>
#include <shellapi.h>

#include <fstream>
#include <sstream>
#include <vector>

namespace steam {

namespace {

// Flat list of the quoted tokens in a VDF file; structure isn't needed for the keys read here.
std::vector<std::string> VdfTokens(const std::wstring& path)
{
	std::ifstream f(path, std::ios::binary);
	std::stringstream ss;
	ss << f.rdbuf();
	std::string s = ss.str();

	std::vector<std::string> tokens;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] != '"')
			continue;
		std::string t;
		for (i++; i < s.size() && s[i] != '"'; i++) {
			if (s[i] == '\\' && i + 1 < s.size())
				i++;
			t += s[i];
		}
		tokens.push_back(t);
	}
	return tokens;
}

std::string VdfValue(const std::vector<std::string>& tokens, const char* key)
{
	for (size_t i = 0; i + 1 < tokens.size(); i++)
		if (_stricmp(tokens[i].c_str(), key) == 0)
			return tokens[i + 1];
	return {};
}

std::wstring WithSlash(std::wstring p)
{
	for (auto& c : p)
		if (c == L'/')
			c = L'\\';
	if (!p.empty() && p.back() != L'\\')
		p += L'\\';
	return p;
}

}

bool FindInstall(unsigned appId, Install& out)
{
	std::wstring steam = RegString(L"Software\\Valve\\Steam", L"SteamPath");
	if (steam.empty())
		return false;
	steam = WithSlash(steam);

	std::vector<std::wstring> libraries = { steam };
	auto tokens = VdfTokens(steam + L"steamapps\\libraryfolders.vdf");
	for (size_t i = 0; i + 1 < tokens.size(); i++)
		if (tokens[i] == "path")
			libraries.push_back(WithSlash(Widen(tokens[i + 1])));

	std::wstring manifest = L"steamapps\\appmanifest_" + std::to_wstring(appId) + L".acf";
	for (const auto& lib : libraries) {
		auto acf = VdfTokens(lib + manifest);
		std::string installDir = VdfValue(acf, "installdir");
		if (installDir.empty())
			continue;
		out.dir = WithSlash(lib + L"steamapps\\common\\" + Widen(installDir));
		out.stateFlags = strtoul(VdfValue(acf, "StateFlags").c_str(), nullptr, 10);
		out.lastUpdated = _strtoi64(VdfValue(acf, "LastUpdated").c_str(), nullptr, 10);
		out.buildId = VdfValue(acf, "buildid");
		out.targetBuildId = VdfValue(acf, "TargetBuildID");
		return GetFileAttributesW((out.dir + L"SoulWorker.exe").c_str()) != INVALID_FILE_ATTRIBUTES;
	}
	return false;
}

bool ClientInstalled()
{
	std::wstring steam = RegString(L"Software\\Valve\\Steam", L"SteamPath");
	return !steam.empty() && GetFileAttributesW((WithSlash(steam) + L"steam.exe").c_str()) != INVALID_FILE_ATTRIBUTES;
}

void OpenLibraryPage(unsigned appId)
{
	std::wstring url = L"steam://nav/games/details/" + std::to_wstring(appId);
	ShellExecuteW(nullptr, nullptr, url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void OpenInstall(unsigned appId)
{
	std::wstring url = ClientInstalled() ? L"steam://install/" + std::to_wstring(appId)
		: L"https://store.steampowered.com/app/" + std::to_wstring(appId) + L"/";
	ShellExecuteW(nullptr, nullptr, url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}
