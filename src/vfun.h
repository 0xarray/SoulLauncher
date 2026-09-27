#pragma once
#include <string>
#include <vector>

namespace vfun {

struct Env {
	std::wstring vfunDir;   // VFUN\VLauncher\, empty when VFUN is not installed
	std::string deviceId;   // Win32_ComputerSystemProduct.UUID, sent as Device-Id
	std::string version;    // "<yymmdd of VLauncher build>.<launcher VERSION>"
};

struct Result {
	int code = -1;          // API "result"; 1 = success
	std::string msg;
	bool ok() const { return code == 1; }
};

struct SavedLogin {
	std::string userId;
	std::string refreshToken;
	unsigned long long lastUsed = 0;
};

bool InitEnv(Env& env, std::string& err);

Result Login(const Env& env, const std::string& id, const std::string& password, std::string& access, std::string& refresh);
Result Refresh(const Env& env, const std::string& refresh, std::string& access, std::string& newRefresh);
Result MakeAuthCode(const Env& env, const std::string& access, std::string& authCode);

// Accounts VFUN keeps for auto-login (UserData\AutoLoginUser.dat), newest first.
std::vector<SavedLogin> ReadVfunLogins(const Env& env);

// SoulWorker GL "LaunchParams" from VFUN's Info\GameData.bin, or the last known one.
std::wstring LaunchTemplate(const Env& env);
// Language VFUN would pass as <PLAN>.
std::wstring LaunchLanguage(const Env& env);

// Newest client version in the patch server's [useropen] list, 0 if it can't be read.
// VFUN compares it against HKCU\Software\Valofe\SoulWorker VERSION.
unsigned long LatestClientVersion(const Env& env);
// When the patch server published a version's first chunk (unix time), 0 if unknown.
long long PatchPublishTime(const Env& env, unsigned long version);

std::string TokenUserId(const std::string& jwt);

}
