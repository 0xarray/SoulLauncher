#pragma once
#include <string>

namespace steam {

// Valofe's Steam release of the Global client; same binaries and data as the VFUN install.
constexpr unsigned kSoulWorkerGB = 1377580;

struct Install {
	std::wstring dir;          // ...\steamapps\common\Soulworker_GB\ (trailing slash)
	unsigned stateFlags = 0;
	long long lastUpdated = 0; // unix time of Steam's last update of the app
	std::string buildId;
	std::string targetBuildId;
	// StateFlags 4 = fully installed; anything else means an update is pending or running.
	bool upToDate() const { return stateFlags == 4 && (targetBuildId.empty() || targetBuildId == "0" || targetBuildId == buildId); }
};

bool FindInstall(unsigned appId, Install& out);
bool ClientInstalled();
void OpenLibraryPage(unsigned appId);
// Steam's install dialog, or the store page in a browser when Steam isn't installed.
void OpenInstall(unsigned appId);

}
