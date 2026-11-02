# GD Leaderboard List

A polished in-game Geometry Dash leaderboard companion for Geode, credited to **Aluxs / Aluxs-arch**.

![Leaderboard List logo](logo.png)

## Features

- A custom navy-and-gold brand mark, matching in-game panel, and a gently pulsing logo shortcut on the main menu.
- A single **ALL** index that merges the public level lists from AREDL, Global Demonlist Classic, and Pointercrate. The same GD level is merged by its Geometry Dash in-game ID and shows which sources list it.
- Source tabs for **ALL / AREDL / CLASSIC / POINTER** to narrow the unified index without leaving the panel.
- **10 levels per page**, dynamic page count, and previous/next controls (for example, `1 / 450`). The in-game level page includes rank, name, source marks, ID copy, details, and an official list link.
- One-tap ID copying, internal level details, refresh/error states, and English/Bahasa Indonesia interface.
- Mod settings for language and visibility of the floating shortcut.

## Live data

- **AREDL** — `https://api.aredl.net/api/aredl/levels`
- **Global Demonlist Classic** — `https://api.demonlist.org/level/classic/list`
- **Pointercrate** — `https://pointercrate.com/api/v2/demons/listed/`

All three lists load asynchronously and are merged by the Geometry Dash level ID. Records without a usable in-game level ID are omitted so the copy button never mistakes a list-internal ID for a GD ID. Internet access is required to refresh the data. If one source is unavailable, the other source tabs and their levels remain usable.

## Build

Install the Geode SDK and CLI for your platform, then configure and build with CMake:

```sh
cmake -B build -DGEODE_SDK="$GEODE_SDK" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

The SDK/CLI packages the PNG sprite declared in `mod.json`. This workspace does not have the Geode SDK installed, so the native mod build has not been run here.

## Credits

**Aluxs** · **Aluxs-arch** · [GitHub repository](https://github.com/Aluxs-arch/GD-Leaderboard-list)

List data and marks belong to their respective owners: AREDL, Global Demonlist, and Pointercrate.
