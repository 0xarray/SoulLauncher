#include "vfun.h"
#include "http.h"
#include "util.h"

#include <windows.h>
#include <bcrypt.h>
#include <comdef.h>
#include <intrin.h>
#include <wbemidl.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace vfun {

namespace {

const wchar_t* kLauncherKey = L"SOFTWARE\\Valofe\\Vlauncher";
const char* kInfoKey = "@#$%&*()-=asdwswdVFUNLauncher20260219@#$%&*()";
const wchar_t* kFallbackTemplate =
	L"fromVLauncher::VALOFE:<PAUTHCODE>:gbl:43.159.147.134:10000:gbl:43.130.7.93:10000:chn:49.233.245.180:10000:<PLAN>:<PCID>";

std::vector<unsigned char> Sha256(const void* data, size_t len)
{
	std::vector<unsigned char> out(32);
	BCRYPT_ALG_HANDLE alg = nullptr;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
		return {};
	BCryptHash(alg, nullptr, 0, (PUCHAR)data, (ULONG)len, out.data(), (ULONG)out.size());
	BCryptCloseAlgorithmProvider(alg, 0);
	return out;
}

bool AesCbcDecrypt(const unsigned char key[32], const unsigned char iv[16], std::vector<unsigned char> data, bool padded, std::vector<unsigned char>& out)
{
	BCRYPT_ALG_HANDLE alg = nullptr;
	BCRYPT_KEY_HANDLE hkey = nullptr;
	bool ok = false;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) >= 0 &&
		BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0) >= 0 &&
		BCryptGenerateSymmetricKey(alg, &hkey, nullptr, 0, (PUCHAR)key, 32, 0) >= 0) {
		unsigned char ivCopy[16];
		memcpy(ivCopy, iv, 16);
		ULONG len = 0;
		out.resize(data.size());
		ok = BCryptDecrypt(hkey, data.data(), (ULONG)data.size(), nullptr, ivCopy, 16, out.data(), (ULONG)out.size(), &len,
			padded ? BCRYPT_BLOCK_PADDING : 0) >= 0;
		out.resize(ok ? len : 0);
	}
	if (hkey) BCryptDestroyKey(hkey);
	if (alg) BCryptCloseAlgorithmProvider(alg, 0);
	return ok;
}

// Info\*.bin: AES-256-CBC, key = SHA256(kInfoKey), IV = hash[16..31] reversed.
bool ReadInfoJson(const Env& env, const wchar_t* name, json& out)
{
	if (env.vfunDir.empty())
		return false;
	std::vector<unsigned char> enc, plain;
	if (!ReadFileBytes(env.vfunDir + L"Info\\" + name, enc) || enc.empty() || enc.size() % 16)
		return false;
	auto h = Sha256(kInfoKey, strlen(kInfoKey));
	if (h.size() != 32)
		return false;
	unsigned char iv[16];
	std::reverse_copy(h.begin() + 16, h.end(), iv);
	if (!AesCbcDecrypt(h.data(), iv, enc, true, plain))
		return false;
	size_t skip = plain.size() >= 3 && plain[0] == 0xEF && plain[1] == 0xBB && plain[2] == 0xBF ? 3 : 0;
	out = json::parse(plain.begin() + skip, plain.end(), nullptr, false);
	return !out.is_discarded();
}

std::wstring AppUrl(const Env& env, int id, const wchar_t* fallback)
{
	static json info;
	static bool loaded = ReadInfoJson(env, L"AppInfo.bin", info);
	if (loaded) {
		auto it = info.find(std::to_string(id));
		if (it != info.end() && it->contains("GL") && (*it)["GL"].is_string() && !(*it)["GL"].get<std::string>().empty())
			return Widen((*it)["GL"].get<std::string>());
	}
	return fallback;
}

std::string QueryDeviceId()
{
	std::string id;
	HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	IWbemLocator* locator = nullptr;
	IWbemServices* services = nullptr;
	IEnumWbemClassObject* rows = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (void**)&locator)) &&
		SUCCEEDED(locator->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services)) &&
		SUCCEEDED(CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
			RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE)) &&
		SUCCEEDED(services->ExecQuery(_bstr_t(L"WQL"), _bstr_t(L"SELECT UUID FROM Win32_ComputerSystemProduct"),
			WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &rows))) {
		IWbemClassObject* row = nullptr;
		ULONG n = 0;
		if (rows->Next(WBEM_INFINITE, 1, &row, &n) == S_OK && n) {
			VARIANT v;
			VariantInit(&v);
			if (SUCCEEDED(row->Get(L"UUID", 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR)
				id = Narrow(v.bstrVal);
			VariantClear(&v);
			row->Release();
		}
	}
	if (rows) rows->Release();
	if (services) services->Release();
	if (locator) locator->Release();
	if (SUCCEEDED(init))
		CoUninitialize();
	return id;
}

// VFUN sends "<__DATE__ as yymmdd>.<registry VERSION>"; the link timestamp gives the same day.
std::string LauncherVersion(const Env& env)
{
	unsigned long build = RegDword(kLauncherKey, L"VERSION", 0);
	std::vector<unsigned char> pe;
	if (build && !env.vfunDir.empty() && ReadFileBytes(env.vfunDir + L"x64\\VLauncher.exe", pe) && pe.size() > 0x200) {
		auto nt = *(const DWORD*)&pe[0x3C];
		if (nt + 12 < pe.size() && *(const DWORD*)&pe[nt] == IMAGE_NT_SIGNATURE) {
			time_t stamp = (time_t)*(const DWORD*)&pe[nt + 8] + 9 * 3600; // built in KST
			tm t;
			gmtime_s(&t, &stamp);
			char buf[32];
			snprintf(buf, sizeof(buf), "%02d%02d%02d.%lu", t.tm_year % 100, t.tm_mon + 1, t.tm_mday, build);
			return buf;
		}
	}
	return "260923.261";
}

HttpHeaders BaseHeaders(const Env& env, const char* contentType)
{
	return {
		{ "Referer", Narrow(AppUrl(env, 10001, L"https://vfun.valofe.com/")) },
		{ "Content-Type", contentType },
		{ "Version", env.version },
	};
}

Result Post(const std::wstring& url, const HttpHeaders& headers, const std::string& body, HttpResponse& resp, json& data)
{
	Result r;
	std::string err;
	if (!HttpPost(url, headers, body, resp, err)) {
		r.msg = err;
		return r;
	}
	json j = json::parse(resp.body, nullptr, false);
	if (j.is_discarded() || !j.is_object()) {
		r.msg = "HTTP " + std::to_string(resp.status) + ": " + resp.body.substr(0, 200);
		return r;
	}
	if (j.contains("result") && j["result"].is_number_integer())
		r.code = j["result"].get<int>();
	if (j.contains("msg") && j["msg"].is_string())
		r.msg = j["msg"].get<std::string>();
	if (j.contains("data"))
		data = j["data"];
	return r;
}

// Saved refresh tokens: AES-256-CBC, key = {hash(machine string), 0...}, IV = 0x09 * 16, zero padded to 1024.
unsigned int MachineHash(const std::string& s)
{
	unsigned int v = 0;
	int bit = (int)s.size() - 1;
	for (char c : s) {
		v += (unsigned int)((int)(signed char)c * (bit < 32 ? (1u << bit) : 0u));
		bit = bit > 0 ? bit - 1 : 0;
	}
	return v;
}

std::string MachineString(const Env& env)
{
	int regs[4];
	__cpuid(regs, 1);
	DWORD serial = 0;
	GetVolumeInformationA("C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
	char buf[128];
	snprintf(buf, sizeof(buf), "%s_%lu_%08X%08X", env.deviceId.c_str(), serial, (unsigned)regs[3], (unsigned)regs[0]);
	return buf;
}

std::wstring ReadVfunString(const std::vector<unsigned char>& d, size_t& p, bool& ok)
{
	if (p + 4 > d.size()) { ok = false; return {}; }
	unsigned n = *(const unsigned*)&d[p];
	p += 4;
	if (p + 2ull * n > d.size()) { ok = false; return {}; }
	std::wstring s(n, L'\0');
	for (unsigned i = 0; i < n; i++)
		s[i] = (wchar_t)(*(const unsigned short*)&d[p + 2 * i] ^ 0x0F);
	p += 2ull * n;
	return s;
}

}

bool InitEnv(Env& env, std::string& err)
{
	env.vfunDir = RegString(kLauncherKey, L"PATH");
	if (!env.vfunDir.empty() && env.vfunDir.back() != L'\\')
		env.vfunDir += L'\\';
	env.deviceId = QueryDeviceId();
	if (env.deviceId.empty()) {
		err = "could not read the machine UUID (Win32_ComputerSystemProduct)";
		return false;
	}
	env.version = LauncherVersion(env);
	return true;
}

Result Login(const Env& env, const std::string& id, const std::string& password, std::string& access, std::string& refresh)
{
	auto headers = BaseHeaders(env, "application/json; charset=UTF-8;");
	headers.push_back({ "Device", "launcher" });
	headers.push_back({ "Device-Id", env.deviceId });
	json body = { { "service_code", "vfun" }, { "input_user_id", id }, { "input_user_password", password } };

	HttpResponse resp;
	json data;
	Result r = Post(AppUrl(env, 10023, L"https://external-api.valofe.com/api/vfun/login"), headers, body.dump(), resp, data);
	if (r.ok()) {
		access = CookieValue(resp, "L-Access-Token");
		refresh = CookieValue(resp, "L-Refresh-Token");
		if (access.empty() || refresh.empty()) {
			r.code = -1;
			r.msg = "login succeeded but no tokens came back";
		}
	}
	return r;
}

Result Refresh(const Env& env, const std::string& refresh, std::string& access, std::string& newRefresh)
{
	auto headers = BaseHeaders(env, "application/json; charset=UTF-8");
	headers.push_back({ "Cookie", "L-Refresh-Token=" + refresh });
	headers.push_back({ "Device", "launcher" });
	headers.push_back({ "Device-Id", env.deviceId });

	HttpResponse resp;
	json data;
	Result r = Post(AppUrl(env, 10026, L"https://external-api.valofe.com/api/vfun/refresh_token"), headers, "", resp, data);
	if (r.ok()) {
		access = CookieValue(resp, "L-Access-Token");
		newRefresh = CookieValue(resp, "L-Refresh-Token");
		if (access.empty()) {
			r.code = -1;
			r.msg = "token refresh returned no access token";
		}
	}
	return r;
}

Result MakeAuthCode(const Env& env, const std::string& access, std::string& authCode)
{
	auto headers = BaseHeaders(env, "application/json; charset=UTF-8");
	headers.push_back({ "Cookie", "L-Access-Token=" + access + ";Device=launcher;Device-Id=" + env.deviceId + ";" });

	HttpResponse resp;
	json data;
	Result r = Post(AppUrl(env, 10027, L"https://external-api.valofe.com/api/vfun/make_auth_code"), headers, "", resp, data);
	if (r.ok()) {
		if (data.is_object() && data.contains("auth_code") && data["auth_code"].is_string())
			authCode = data["auth_code"].get<std::string>();
		if (authCode.empty()) {
			r.code = -1;
			r.msg = "make_auth_code returned no code";
		}
	}
	return r;
}

std::vector<SavedLogin> ReadVfunLogins(const Env& env)
{
	std::vector<SavedLogin> logins;
	std::vector<unsigned char> d;
	if (env.vfunDir.empty() || !ReadFileBytes(env.vfunDir + L"UserData\\AutoLoginUser.dat", d) || d.size() < 10)
		return logins;

	unsigned int k = MachineHash(MachineString(env));
	unsigned char key[32] = {}, iv[16];
	memcpy(key, &k, 4);
	memset(iv, 0x09, sizeof(iv));

	size_t p = 4 + 1 + 1;
	unsigned count = *(const unsigned*)&d[p];
	p += 4;
	for (unsigned i = 0; i < count && p < d.size(); i++) {
		bool ok = true;
		bool global = d[p++] == 1;
		std::wstring id = ReadVfunString(d, p, ok);
		ReadVfunString(d, p, ok);
		ReadVfunString(d, p, ok);
		if (!ok || p + 12 > d.size())
			break;
		unsigned long long used = *(const unsigned long long*)&d[p];
		unsigned len = *(const unsigned*)&d[p + 8];
		p += 12;
		if (p + len > d.size())
			break;
		std::vector<unsigned char> blob(d.begin() + p, d.begin() + p + len), plain;
		p += len;
		if (!global || len == 0 || len % 16 || !AesCbcDecrypt(key, iv, blob, false, plain))
			continue;
		std::string token((const char*)plain.data(), strnlen((const char*)plain.data(), plain.size()));
		if (TokenUserId(token).empty())
			continue; // key mismatch (different machine or VFUN changed the scheme)
		logins.push_back({ Narrow(id), token, used });
	}
	std::sort(logins.begin(), logins.end(), [](const SavedLogin& a, const SavedLogin& b) { return a.lastUsed > b.lastUsed; });
	return logins;
}

std::wstring GameDataString(const Env& env, const char* field, const wchar_t* fallback)
{
	json games;
	if (ReadInfoJson(env, L"GameData.bin", games)) {
		auto gl = games.find("GL");
		if (gl != games.end() && gl->contains("soulworker")) {
			const json& g = (*gl)["soulworker"];
			if (g.contains(field) && g[field].is_string() && !g[field].get<std::string>().empty())
				return Widen(g[field].get<std::string>());
		}
	}
	return fallback;
}

std::wstring LaunchTemplate(const Env& env)
{
	return GameDataString(env, "LaunchParams", kFallbackTemplate);
}

std::wstring PatchInfoUrl(const Env& env)
{
	return GameDataString(env, "PatchCdnFile", L"https://sw-npprotect-cdn.qijisoft.com/live/global/patch/patchVersionInfo.txt");
}

long long PatchPublishTime(const Env& env, unsigned long version)
{
	std::wstring base = PatchInfoUrl(env);
	base.resize(base.find_last_of(L'/') + 1);
	wchar_t chunk[64];
	swprintf_s(chunk, L"%lu/%lu_000001.lzma", version, version);
	HttpResponse resp;
	std::string err;
	if (!HttpHead(base + chunk, resp, err) || resp.status != 200)
		return 0;
	return resp.lastModified;
}

unsigned long LatestClientVersion(const Env& env)
{
	std::wstring url = PatchInfoUrl(env);
	HttpResponse resp;
	std::string err;
	if (!HttpGet(url, resp, err) || resp.status != 200)
		return 0;

	unsigned long latest = 0;
	bool inUserOpen = false;
	size_t pos = 0;
	while (pos < resp.body.size()) {
		size_t end = resp.body.find('\n', pos);
		std::string line = resp.body.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
		pos = end == std::string::npos ? resp.body.size() : end + 1;
		while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
			line.pop_back();
		if (!line.empty() && line[0] == '[')
			inUserOpen = line == "[useropen]";
		else if (inUserOpen && !line.empty())
			latest = std::max(latest, strtoul(line.c_str(), nullptr, 10));
	}
	return latest;
}

std::wstring LaunchLanguage(const Env& env)
{
	std::vector<std::string> langs = { "English", "Chinese", "Taiwan" };
	int index = 0;
	json options;
	if (ReadInfoJson(env, L"GameOption.bin", options) && options.contains("games") && options["games"].is_array()) {
		for (const auto& g : options["games"]) {
			if (g.value("code", "") != "soulworker" || g.value("region", "") != "GL" || !g.contains("langs"))
				continue;
			langs.clear();
			for (const auto& l : g["langs"])
				langs.push_back(l.value("code", ""));
			if (g.contains("defaults"))
				index = g["defaults"].value("selectedLangIdx", 0);
		}
	}
	std::vector<unsigned char> saved;
	if (!env.vfunDir.empty() && ReadFileBytes(env.vfunDir + L"UserData\\GameOption\\GL\\soulworker.dat", saved) && saved.size() >= 4)
		index = *(const int*)saved.data();
	if (index < 0 || index >= (int)langs.size())
		index = 0;
	return langs.empty() ? L"English" : Widen(langs[index]);
}

std::string TokenUserId(const std::string& jwt)
{
	size_t a = jwt.find('.');
	size_t b = a == std::string::npos ? a : jwt.find('.', a + 1);
	if (b == std::string::npos)
		return {};
	json payload = json::parse(Base64UrlDecode(jwt.substr(a + 1, b - a - 1)), nullptr, false);
	if (payload.is_discarded() || !payload.contains("user_id") || !payload["user_id"].is_string())
		return {};
	return payload["user_id"].get<std::string>();
}

}
