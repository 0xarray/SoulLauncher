<div align="center">

<img src=".github/logo.png" width="96" alt="SoulLauncher">

# [SoulLauncher](https://discord.gg/7ynGDqcnPJ)

**Play [SoulWorker](https://store.steampowered.com/app/1377580/Soulworker/) Global on your VFUN account — without the VFUN launcher.**

Signs in the way VFUN does, and runs the game from your Steam install so updates come through Steam's fast downloads.

![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D6)
![Language](https://img.shields.io/badge/C%2B%2B-17-00599C)
![Dependencies](https://img.shields.io/badge/dependencies-none-brightgreen)

[<img src="https://cdn.buymeacoffee.com/buttons/v2/default-blue.png" height="48" alt="Buy me a coffee">](https://www.buymeacoffee.com/rainyyy)

</div>

---

## What it does

It signs in the way VFUN does, gets a one-time game auth code from Valofe, and starts the client with the same arguments VFUN passes. The game files can come from the **Steam** release (app 1377580), which is the same client and patches much faster, or from a VFUN install.

## Usage

Put `SoulLauncher.exe` anywhere and run it. With no options it:

1. picks the Steam install of SoulWorker if there is one, otherwise the VFUN install (and offers to install it through Steam if neither exists);
2. checks the client is up to date against Valofe's patch server;
3. signs in with the saved login, else VFUN's auto-login, else asks for the VFUN id and password in a console;
4. starts the game.

| Option | |
|---|---|
| `--login [id]` | sign in with a VFUN id and password |
| `--vfun [id]` | reuse a login saved by the VFUN launcher's auto-login |
| `--logout` | forget the login saved by SoulLauncher |
| `--steam` | use the Steam install (default when there is one) |
| `--vfun-install` | use the VFUN install even if Steam has one |
| `--game <dir>` | use this SoulWorker folder |
| `--lang <name>` | game language: `English`, `Chinese` or `Taiwan` |
| `--skip-version-check` | start even if the client looks out of date |
| `--status` | show which install, version and login would be used, without signing in |
| `--dry-run` | sign in and show the launch command instead of starting the game |
| `--silent` | no message boxes or prompts |

## Notes

- `SoulLauncher.dat` (next to the exe) holds your refresh token, encrypted for your Windows user. **Don't share it**, and don't ship it with a release.
- Patching is left to Steam or VFUN. When Valofe's patch is out but Steam hasn't installed it yet, the launcher says so.
- Google, Facebook and Apple accounts have no VFUN password: sign in to VFUN once with auto-login ticked, then use `--vfun`.
- Unofficial. It talks to Valofe's login API the same way VFUN does; use at your own risk.

## Building

Visual Studio 2022+ (v143), `SoulLauncher.sln`, `Release|x64`. No external dependencies; the CRT is linked statically. `third_party/nlohmann/json.hpp` is nlohmann/json 3.10.5 (MIT).
