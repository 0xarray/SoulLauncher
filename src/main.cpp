#include "steam.h"
#include "util.h"
#include "vfun.h"

#include <windows.h>
#include <dpapi.h>
#include <shellapi.h>

#include <chrono>
#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

const wchar_t* kGameKey = L"Software\\Valofe\\SoulWorker";
const wchar_t* kLauncherKey = L"SOFTWARE\\Valofe\\Vlauncher";
const wchar_t* kTitle = L"SoulLauncher";

struct Options {
	std::string loginId;
	bool login = false;
	bool vfun = false;
	std::string vfunId;
	bool logout = false;
	bool dryRun = false;
	bool skipVersionCheck = false;
	bool status = false;
	bool useSteam = false;
	bool useVfunInstall = false;
	std::wstring gameDir;
	std::wstring lang;
};

bool g_console = false; // stdout goes somewhere (parent console or redirected)
bool g_silent = false;  // no message boxes

// Windows-subsystem exe: only print when started from a terminal or with redirected output.
void InitOutput()
{
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	if (out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN) {
		g_console = true;
		return;
	}
	if (AttachConsole(ATTACH_PARENT_PROCESS)) {
		FILE* f = nullptr;
		freopen_s(&f, "CONOUT$", "w", stdout);
		freopen_s(&f, "CONOUT$", "w", stderr);
		g_console = true;
	}
}

void Print(const std::string& s)
{
	if (g_console)
		printf("%s\n", s.c_str());
}

void Message(const std::string& s, UINT icon)
{
	if (g_console)
		fprintf(icon == MB_ICONERROR ? stderr : stdout, "%s\n", s.c_str());
	if (!g_silent)
		MessageBoxW(nullptr, Widen(s).c_str(), kTitle, MB_OK | icon | MB_SETFOREGROUND);
}

int Fail(const std::string& s)
{
	Message(s, MB_ICONERROR);
	return 1;
}

const char* kUsage =
	"SoulLauncher - starts SoulWorker (Global) through VFUN's login, without the VFUN launcher.\n\n"
	"  --login [id]            sign in with a VFUN id and password\n"
	"  --vfun [id]             reuse a login saved by the VFUN launcher's auto-login\n"
	"  --logout                forget the login saved by SoulLauncher\n"
	"  --steam                 use the Steam install of SoulWorker (default when there is one)\n"
	"  --vfun-install          use the VFUN install even if Steam has one\n"
	"  --game <dir>            use this SoulWorker folder\n"
	"  --lang <name>           game language: English, Chinese or Taiwan\n"
	"  --skip-version-check    start even if the client looks out of date\n"
	"  --status                show which install, version and login would be used, without signing in\n"
	"  --dry-run               get an auth code and show the command line, don't start the game\n"
	"  --silent                no message boxes\n\n"
	"With no options it uses the saved login, falling back to VFUN's auto-login, then a sign-in prompt.";

bool ParseArgs(Options& o)
{
	int argc = 0;
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	bool ok = true;
	for (int i = 1; i < argc && ok; i++) {
		std::wstring a = argv[i];
		auto next = [&](std::wstring& out) {
			if (i + 1 >= argc || argv[i + 1][0] == L'-')
				return false;
			out = argv[++i];
			return true;
		};
		std::wstring v;
		if (a == L"--login") {
			o.login = true;
			if (next(v))
				o.loginId = Narrow(v);
		}
		else if (a == L"--vfun") {
			o.vfun = true;
			if (next(v))
				o.vfunId = Narrow(v);
		}
		else if (a == L"--logout") o.logout = true;
		else if (a == L"--dry-run") o.dryRun = true;
		else if (a == L"--skip-version-check") o.skipVersionCheck = true;
		else if (a == L"--status") o.status = true;
		else if (a == L"--steam") o.useSteam = true;
		else if (a == L"--vfun-install") o.useVfunInstall = true;
		else if (a == L"--silent") g_silent = true;
		else if (a == L"--game" && next(v)) o.gameDir = v;
		else if (a == L"--lang" && next(v)) o.lang = v;
		else ok = false;
	}
	LocalFree(argv);
	if (!ok)
		Message(kUsage, MB_ICONINFORMATION);
	return ok;
}

std::wstring StorePath() { return ExeDir() + L"SoulLauncher.dat"; }

bool LoadStore(vfun::SavedLogin& s)
{
	std::vector<unsigned char> enc;
	if (!ReadFileBytes(StorePath(), enc) || enc.empty())
		return false;
	DATA_BLOB in = { (DWORD)enc.size(), enc.data() }, out = {};
	if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
		return false;
	json j = json::parse(out.pbData, out.pbData + out.cbData, nullptr, false);
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);
	if (j.is_discarded())
		return false;
	s.userId = j.value("user_id", "");
	s.refreshToken = j.value("refresh_token", "");
	return !s.refreshToken.empty();
}

bool SaveStore(const vfun::SavedLogin& s)
{
	std::string plain = json{ { "user_id", s.userId }, { "refresh_token", s.refreshToken } }.dump();
	DATA_BLOB in = { (DWORD)plain.size(), (BYTE*)plain.data() }, out = {};
	if (!CryptProtectData(&in, L"SoulLauncher", nullptr, nullptr, nullptr, 0, &out))
		return false;
	bool ok = WriteFileBytes(StorePath(), std::vector<unsigned char>(out.pbData, out.pbData + out.cbData));
	LocalFree(out.pbData);
	return ok;
}

void Replace(std::wstring& s, const std::wstring& from, const std::wstring& to)
{
	for (size_t p = 0; (p = s.find(from, p)) != std::wstring::npos; p += to.size())
		s.replace(p, from.size(), to);
}

// Gives a double-clicked launcher a console for the sign-in prompt only.
struct PromptConsole {
	bool owned = false;
	PromptConsole()
	{
		if (g_console || !AllocConsole())
			return;
		owned = true;
		SetConsoleTitleW(kTitle);
		FILE* f = nullptr;
		freopen_s(&f, "CONIN$", "r", stdin);
		freopen_s(&f, "CONOUT$", "w", stdout);
		freopen_s(&f, "CONOUT$", "w", stderr);
	}
	~PromptConsole()
	{
		if (owned)
			FreeConsole();
	}
};

std::string ReadLine(bool hidden)
{
	HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
	DWORD mode = 0;
	bool console = GetConsoleMode(in, &mode) != 0;
	if (hidden && console)
		SetConsoleMode(in, mode & ~ENABLE_ECHO_INPUT);
	std::wstring line;
	wchar_t buf[512] = {};
	if (console) {
		DWORD read = 0;
		if (ReadConsoleW(in, buf, _countof(buf) - 1, &read, nullptr))
			line.assign(buf, read);
	}
	else if (fgetws(buf, _countof(buf), stdin)) {
		line = buf;
	}
	SecureZeroMemory(buf, sizeof(buf));
	if (hidden && console) {
		SetConsoleMode(in, mode);
		printf("\n");
	}
	while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r'))
		line.pop_back();
	return Narrow(line);
}

bool PasswordLogin(const vfun::Env& env, const std::string& savedId, vfun::SavedLogin& account, std::string& access)
{
	if (g_silent)
		return false;
	PromptConsole console;
	printf("Sign in with your VFUN account.\n");
	std::string id = savedId;
	for (int attempt = 0; attempt < 3; attempt++) {
		if (id.empty()) {
			printf("VFUN id: ");
			fflush(stdout);
			id = ReadLine(false);
		}
		else {
			printf("VFUN id: %s\n", id.c_str());
		}
		printf("Password: ");
		fflush(stdout);
		std::string password = ReadLine(true);
		if (id.empty() || password.empty())
			return false;

		std::string refresh;
		vfun::Result r = vfun::Login(env, id, password, access, refresh);
		SecureZeroMemory(password.data(), password.size());
		if (r.ok()) {
			account.refreshToken = refresh;
			account.userId = vfun::TokenUserId(refresh);
			if (account.userId.empty())
				account.userId = id;
			printf("Signed in as %s.\n", account.userId.c_str());
			return true;
		}
		if (r.code == 4073 || r.code == 4057) {
			Message("VFUN wants an extra verification step for this account (" + std::to_string(r.code) + ": " + r.msg +
				").\nSign in once through VFUN with auto-login ticked, then run SoulLauncher again.", MB_ICONWARNING);
			return false;
		}
		printf("Sign-in failed (%d): %s\n\n", r.code, r.msg.c_str());
		id.clear();
	}
	return false;
}

void OpenVfun()
{
	std::wstring dir = RegString(kLauncherKey, L"PATH");
	std::wstring file = RegString(kLauncherKey, L"FILENAME");
	if (!dir.empty() && dir.back() != L'\\')
		dir += L'\\';
	std::wstring exe = dir + (file.empty() ? L"VFUNLauncher.exe" : file);
	if (dir.empty() || (INT_PTR)ShellExecuteW(nullptr, nullptr, exe.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL) <= 32)
		Message("Could not start VFUN. Open it yourself to update SoulWorker.", MB_ICONWARNING);
}

struct GameInstall {
	enum Source { Custom, Steam, Vfun } source = Custom;
	std::wstring dir;
	std::wstring exe;
	steam::Install steam;
	bool missing = false;
	const char* name() const { return source == Steam ? "Steam" : source == Vfun ? "VFUN" : "custom folder"; }
};

std::wstring WithSlash(std::wstring p)
{
	for (auto& c : p)
		if (c == L'/')
			c = L'\\';
	if (!p.empty() && p.back() != L'\\')
		p += L'\\';
	return p;
}

bool ResolveInstall(const Options& opt, GameInstall& g, std::string& err)
{
	if (opt.useSteam && opt.useVfunInstall) {
		err = "--steam and --vfun-install can't be used together.";
		return false;
	}
	bool haveSteam = steam::FindInstall(steam::kSoulWorkerGB, g.steam);
	std::wstring vfunDir = WithSlash(RegString(kGameKey, L"PATH"));

	if (!opt.gameDir.empty()) {
		g.source = GameInstall::Custom;
		g.dir = WithSlash(opt.gameDir);
		if (haveSteam && _wcsicmp(g.dir.c_str(), g.steam.dir.c_str()) == 0)
			g.source = GameInstall::Steam;
		else if (!vfunDir.empty() && _wcsicmp(g.dir.c_str(), vfunDir.c_str()) == 0)
			g.source = GameInstall::Vfun;
	}
	else if (haveSteam && !opt.useVfunInstall) {
		g.source = GameInstall::Steam;
		g.dir = g.steam.dir;
	}
	else if (!vfunDir.empty() && !opt.useSteam) {
		g.source = GameInstall::Vfun;
		g.dir = vfunDir;
	}
	else {
		g.missing = true;
		err = opt.useSteam ? "SoulWorker isn't installed through Steam." : "No SoulWorker install found.";
		return false;
	}

	std::wstring exeName = g.source == GameInstall::Vfun ? RegString(kGameKey, L"FILENAME") : L"";
	g.exe = g.dir + (exeName.empty() ? L"SoulWorker.exe" : exeName);
	if (GetFileAttributesW(g.exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
		err = Narrow(g.exe) + " not found.";
		return false;
	}
	return true;
}

std::string Date(long long t)
{
	time_t tt = (time_t)t;
	tm lt;
	char buf[32] = "?";
	if (localtime_s(&lt, &tt) == 0)
		strftime(buf, sizeof(buf), "%Y-%m-%d", &lt);
	return buf;
}

// Empty when the install looks current (or can't be checked); otherwise why it's behind.
std::string OutdatedReason(const vfun::Env& env, const GameInstall& g, std::string& info)
{
	if (g.source == GameInstall::Vfun) {
		unsigned long installed = RegDword(kGameKey, L"VERSION", 0);
		unsigned long latest = installed ? vfun::LatestClientVersion(env) : 0;
		if (!latest) {
			info = "Version check skipped (patch server unreachable).";
			return {};
		}
		info = "VFUN install at patch " + std::to_string(installed) + ", latest " + std::to_string(latest) + ".";
		if (latest > installed)
			return "SoulWorker needs an update (installed " + std::to_string(installed) + ", latest " + std::to_string(latest) + ").";
		return {};
	}
	if (g.source == GameInstall::Steam) {
		if (!g.steam.upToDate())
			return "Steam has an update for SoulWorker that isn't installed yet.";
		unsigned long latest = vfun::LatestClientVersion(env);
		long long published = latest ? vfun::PatchPublishTime(env, latest) : 0;
		if (!published) {
			info = "Steam build " + g.steam.buildId + " (updated " + Date(g.steam.lastUpdated) + "); patch server unreachable.";
			return {};
		}
		info = "Steam build " + g.steam.buildId + " updated " + Date(g.steam.lastUpdated) + "; latest patch " +
			std::to_string(latest) + " published " + Date(published) + ".";
		// Steam records when it last updated the app; a newer patch on Valofe's CDN means Steam hasn't shipped it yet.
		if (published > g.steam.lastUpdated)
			return "Patch " + std::to_string(latest) + " came out on " + Date(published) + ", but Steam last updated SoulWorker on " +
				Date(g.steam.lastUpdated) + ", so the Steam copy is probably behind.";
		return {};
	}
	info = "Version check skipped for a custom folder.";
	return {};
}

// Returns false when the launch should stop.
bool ConfirmVersion(const std::string& reason, const GameInstall& g)
{
	if (g_silent) {
		Fail(reason + (g.source == GameInstall::Steam ? " Update it in Steam." : " Update it through VFUN."));
		return false;
	}
	if (g_console)
		fprintf(stderr, "%s\n", reason.c_str());
	if (g.source == GameInstall::Steam) {
		int choice = MessageBoxW(nullptr, Widen(reason + "\n\nYes: open SoulWorker in Steam to update\nNo: start anyway\nCancel: quit").c_str(),
			kTitle, MB_YESNOCANCEL | MB_ICONWARNING | MB_SETFOREGROUND);
		if (choice == IDYES)
			steam::OpenLibraryPage(steam::kSoulWorkerGB);
		return choice == IDNO;
	}
	if (MessageBoxW(nullptr, Widen(reason + "\n\nOpen VFUN to patch it now?").c_str(), kTitle,
			MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND) == IDYES)
		OpenVfun();
	return false;
}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
	InitOutput();

	Options opt;
	if (!ParseArgs(opt))
		return 2;

	if (opt.logout) {
		DeleteFileW(StorePath().c_str());
		Message("Saved login removed.", MB_ICONINFORMATION);
		return 0;
	}

	vfun::Env env;
	std::string err;
	if (!vfun::InitEnv(env, err))
		return Fail(err);

	GameInstall game;
	if (!ResolveInstall(opt, game, err)) {
		if (!game.missing || g_silent)
			return Fail(err);
		std::string how = steam::ClientInstalled() ? "Install it through Steam now? It's free, and Steam keeps it updated."
			: "Steam isn't installed either. Open SoulWorker's Steam store page? It's free, and Steam keeps it updated.";
		if (g_console)
			fprintf(stderr, "%s\n", err.c_str());
		if (MessageBoxW(nullptr, Widen(err + "\n\n" + how + "\nYou still play on your VFUN account; run SoulLauncher again once it's installed.").c_str(),
				kTitle, MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND) == IDYES)
			steam::OpenInstall(steam::kSoulWorkerGB);
		return 1;
	}

	// Checked before signing in so an outdated client never burns an auth code.
	std::string versionInfo, outdated;
	if (!opt.skipVersionCheck)
		outdated = OutdatedReason(env, game, versionInfo);

	if (opt.status) {
		vfun::SavedLogin saved;
		std::string login = LoadStore(saved) ? "saved (" + saved.userId + ")" : "none saved";
		auto vfunLogins = vfun::ReadVfunLogins(env);
		if (!vfunLogins.empty())
			login += "; VFUN auto-login: " + vfunLogins.front().userId;
		Message("Install : " + std::string(game.name()) + " - " + Narrow(game.dir) +
			"\nVersion : " + (outdated.empty() ? versionInfo : outdated) +
			"\nLogin   : " + login, MB_ICONINFORMATION);
		return 0;
	}

	if (!outdated.empty() && !ConfirmVersion(outdated, game))
		return 1;
	if (!versionInfo.empty())
		Print(versionInfo);

	vfun::SavedLogin account;
	std::string access;

	if (opt.login) {
		if (!PasswordLogin(env, opt.loginId, account, access))
			return Fail("Not signed in.");
	}
	else {
		bool have = !opt.vfun && LoadStore(account);
		if (!have) {
			for (const auto& l : vfun::ReadVfunLogins(env)) {
				if (!opt.vfunId.empty() && _stricmp(l.userId.c_str(), opt.vfunId.c_str()) != 0)
					continue;
				account = l;
				have = true;
				Print("Using VFUN's saved login for " + l.userId + ".");
				break;
			}
			if (opt.vfun && !have)
				return Fail("VFUN has no usable auto-login" + (opt.vfunId.empty() ? std::string() : " for " + opt.vfunId) +
					". Tick auto-login in VFUN once, or use --login.");
		}

		std::string rotated;
		vfun::Result r;
		if (have)
			r = vfun::Refresh(env, account.refreshToken, access, rotated);
		if (have && r.ok()) {
			if (!rotated.empty())
				account.refreshToken = rotated;
		}
		else {
			if (have)
				Print("Saved login expired (" + std::to_string(r.code) + ": " + r.msg + ").");
			if (!PasswordLogin(env, account.userId, account, access))
				return Fail("Not signed in.");
		}
	}

	if (account.userId.empty())
		account.userId = vfun::TokenUserId(account.refreshToken);
	if (!SaveStore(account))
		Print("Warning: could not save the login to " + Narrow(StorePath()));

	std::string authCode;
	vfun::Result r = vfun::MakeAuthCode(env, access, authCode);
	if (!r.ok() && r.code == 4010) {
		std::string rotated;
		if (vfun::Refresh(env, account.refreshToken, access, rotated).ok()) {
			if (!rotated.empty()) {
				account.refreshToken = rotated;
				SaveStore(account);
			}
			r = vfun::MakeAuthCode(env, access, authCode);
		}
	}
	if (!r.ok())
		return Fail("Could not get a game auth code (" + std::to_string(r.code) + "): " + r.msg);

	std::wstring params = vfun::LaunchTemplate(env);
	Replace(params, L"<PAUTHCODE>", Widen(authCode));
	Replace(params, L"<PLAN>", opt.lang.empty() ? vfun::LaunchLanguage(env) : opt.lang);
	Replace(params, L"<PCID>", Widen(account.userId));
	Replace(params, L"<SERVICE_CODE>", L"soulworker");
	Replace(params, L"<PFULLMODE>", L"0");
	Replace(params, L"<PRESOLUTION>", L"-1:-1");

	if (opt.dryRun) {
		Message("Account : " + account.userId + "\nInstall : " + game.name() + "\nVersion : " + env.version + "\nDevice  : " +
			env.deviceId + "\n\"" + Narrow(game.exe) + "\" " + Narrow(params), MB_ICONINFORMATION);
		return 0;
	}

	SHELLEXECUTEINFOW sei = { sizeof(sei) };
	sei.lpFile = game.exe.c_str();
	sei.lpParameters = params.c_str();
	sei.lpDirectory = game.dir.c_str();
	sei.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&sei))
		return Fail("Could not start the game (error " + std::to_string(GetLastError()) + ").");

	if (game.source == GameInstall::Vfun) {
		// VFUN stamps the launch time here after a successful start.
		unsigned long long now = std::chrono::duration_cast<std::chrono::duration<unsigned long long, std::ratio<1, 10000000>>>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		RegSetKeyValueW(HKEY_CURRENT_USER, kGameKey, L"FUNTIME", REG_QWORD, &now, sizeof(now));
	}

	Print("SoulWorker started from " + std::string(game.name()) + " as " + account.userId + ".");
	return 0;
}
