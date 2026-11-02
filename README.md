# GD Leaderboard List

A polished in-game Geometry Dash leaderboard companion for Geode, credited to **Aluxs / Aluxs-arch**.

![Leaderboard List logo](logo.png)

## Features

- Original navy-and-gold logo, quiet angular panel background, and a matching floating icon with a gentle pulse.
- One **ALL** index merges the public level lists from AREDL, Global Demonlist Classic, and Pointercrate by Geometry Dash in-game ID.
- Source tabs for **ALL / AREDL / CLASSIC / POINTER** plus source marks on each matching level.
- **10 levels per page**, dynamic page count, and previous/next navigation. **INFO** shows each source rank, **ID** copies the GD ID, and **OPEN** visits that level on a list site.
- Refresh/error states, English and Bahasa Indonesia UI, and a setting to hide the floating shortcut.

## Live data

- **AREDL** — `https://api.aredl.net/api/aredl/levels`
- **Global Demonlist Classic** — `https://api.demonlist.org/level/classic/list`
- **Pointercrate** — `https://pointercrate.com/api/v2/demons/listed/` (retrieved in 100-item API pages)

All lists load asynchronously and merge by the Geometry Dash level ID. Records without a usable in-game ID are skipped so the copy action never mistakes a list-internal ID for a GD ID. Internet access is required; if one API is down, the other source tabs remain usable.

## Build and release

The `Build Geode release` GitHub Actions workflow builds Windows, macOS, Android 32-bit, and Android 64-bit binaries, combines them into one `.geode` package, and uploads the package as an artifact. Pushing a `v*` tag also publishes a GitHub release with that combined package attached. The requested first release uses **v1.0.0**.

For a local build, install Geode SDK and CLI, then run:

```sh
cmake -B build -DGEODE_SDK="$GEODE_SDK" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

The Geode SDK is not installed in this workspace, so the local native build cannot be run here. Use the CI workflow to produce and validate the `.geode` binary package.

## Credits

**Aluxs** · **Aluxs-arch** · [GitHub repository](https://github.com/Aluxs-arch/GD-Leaderboard-list)

List data and marks belong to their respective owners: AREDL, Global Demonlist, and Pointercrate.
