# GD Leaderboard List

A polished in-game Geometry Dash leaderboard companion for Geode, credited to **Aluxs / Aluxs-arch**.

![Leaderboard List logo](logo.png)

## Features

- A restrained midnight/copper cover from `resources/leaderboard-bg.png`, a custom Extreme Demon emblem with optional soft glow, consistent flat controls, and a mobile-fitted popup.
- One **ALL** index merges the public level lists from AREDL, Global Demonlist Classic, and Pointercrate by Geometry Dash in-game ID.
- Source tabs for **ALL / AREDL / CLASSIC / POINTER** plus source marks on each matching level.
- **6 detailed levels per page**, dynamic page count, and previous/next navigation. **INFO** shows each source rank, **ID** copies the GD ID, and **OPEN** asks Geometry Dash for the level by its ID and opens its native level page directly (no copy/paste or list-site redirect). If GD servers are unavailable, an error is shown.
- Refresh/error states and English/Bahasa Indonesia UI. The floating shortcut has four smaller size presets (very small, small, medium, large; small by default), an optional caption (off by default), and drag-to-move positioning saved across sessions; it can also be hidden. Drag on the icon to move it, tap it to open the list. Turn off thumbnails or video previews to save bandwidth; turn on Reduce Motion to disable the subtle emblem glow and button pulse.

## Live data

- **AREDL** — `https://api.aredl.net/api/aredl/levels`
- **Global Demonlist Classic** — `https://api.demonlist.org/level/classic/list`
- **Pointercrate** — `https://pointercrate.com/api/v2/demons/listed/` (retrieved in 100-item API pages)

All lists load asynchronously and merge by the Geometry Dash level ID. Each row has a bundled illustrated thumbnail; if a list record includes a valid YouTube video link, the currently visible rows can show that video’s cover instead. Video covers are **not guaranteed to be in-game screenshots**. Only validated YouTube IDs are requested, previews are capped and cached per popup, and there is a setting to disable external thumbnail requests. Records without a usable in-game ID are skipped so the copy action never mistakes a list-internal ID for a GD ID. Internet access is required; if one API is down, the other source tabs remain usable.

## Build and release

The `Build Geode release` GitHub Actions workflow builds **Windows 64-bit, macOS, iOS, Android 32-bit, and Android 64-bit** binaries for **Geometry Dash 2.2081** using Geode SDK 5.11.2. It verifies the combined `.geode` archive and uploads it as the **Leaderboard-List-v2.0.0** artifact (retained for 30 days). Open a successful [workflow run](https://github.com/Aluxs-arch/GD-Leaderboard-list/actions/workflows/geode-release.yml) and download that artifact to obtain the `.geode` file. Pushing a `v*` tag also publishes a GitHub release with the package attached; only tag a tested commit intended for release. The mod version is **v2.0.0** (different from the Geometry Dash version).

For a local build, install Geode SDK and CLI, then run:

```sh
cmake -B build -DGEODE_SDK="$GEODE_SDK" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

The Geode SDK is not installed in this workspace, so the local native build cannot be run here. Use the CI workflow to produce and validate the `.geode` binary package.

## Credits

**Aluxs** · **Aluxs-arch** · [GitHub repository](https://github.com/Aluxs-arch/GD-Leaderboard-list)

List data and marks belong to their respective owners: AREDL, Global Demonlist, and Pointercrate.
