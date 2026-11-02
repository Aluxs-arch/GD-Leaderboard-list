# GD Leaderboard List

A clean in-game Geometry Dash leaderboard companion mod for **Geode**, credited to **Aluxs / Aluxs-arch**.

## Features

- Floating **EXTREME** shortcut on the main menu; hide it from the mod settings.
- In-game AREDL and Pointercrate data loading through their public JSON endpoints.
- Rank/name rows with five-per-page navigation, one-tap **ID** copy, and **OPEN** level/list links.
- Refresh button, connection/error states, and a dedicated Classic list shortcut.
- English and Bahasa Indonesia UI language setting.
- Dark navy/gold presentation with compact, readable list cards.

## Lists and data notes

- **AREDL** — fetched from `api.aredl.net`; level links use the AREDL list page.
- **Pointercrate** — fetched from the public v2 listed-demons endpoint.
- **Classic Demonlist** — available via a direct official-page shortcut. Its site/API data is not integrated into the in-game live feed in this build, so the mod is explicit about that instead of showing fabricated results.

The mod uses the list services' public data endpoints; internet access is required to load live rows. If a service changes its API/schema or is temporarily unavailable, use **REFRESH** later. Copy is intended to copy the Geometry Dash level ID when the source supplies one.

## Build

Install the Geode SDK for your platform, then configure and build with CMake:

```sh
cmake -B build -DGEODE_SDK="$GEODE_SDK" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

The Geode SDK packages the mod from `mod.json`. The build has not been run in this workspace because no Geode SDK is installed here.

## Credits

**Aluxs** · **Aluxs-arch** · [GitHub repository](https://github.com/Aluxs-arch/GD-Leaderboard-list)

List data and marks belong to their respective owners: AREDL, demonlist.org, and Pointercrate.
