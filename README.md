# GD Leaderboard List

A compact, native-looking in-game Geometry Dash leaderboard companion for Geode, credited to **Aluxs / Aluxs-arch**.

![Leaderboard List logo](logo.png)

## What's new in v2.3.0

- **Rebuilt panel, nothing spills out.** Every element is placed from a single layout contract: the panel height is computed from the row count, action buttons are measured before placement, and the level-name column gets exactly the space that remains. Tabs, status line, rows and pagination all stay inside the border on every screen size.
- **Smaller by default.** The panel is now 400×276 logical points (was 440×370) and the default *Compact* scale renders it at roughly **300×207** — denser rows, native GD buttons, no wasted space.
- **Clean, professional art.** The background is a proper nine-slice texture with a GD-style bevel and *no baked-in arrows, rules or ornaments* to collide with live UI. Rows use native `ButtonSprite` actions, native page arrows, and a neutral placeholder tile instead of fake generated "thumbnails".
- **A full settings page** organized into collapsible sections — Appearance, List content, Behavior, Menu shortcut and About — with conditional (`enable-if`) controls. See the table below.

## Features

- One **ALL** index merges the public level lists from AREDL, Global Demonlist Classic, and Pointercrate by Geometry Dash in-game ID, with source tabs **ALL / AREDL / CLASSIC / POINTER** and per-row **A·C·P** source marks.
- Paginated rows (3–10 per page, 6 by default) with rank, optional thumbnail, name, optional in-game ID, and three actions: **INFO** (ranks on every list), **ID** (copies the GD ID) and **OPEN** (asks Geometry Dash for the level and opens its native level page directly; optional confirmation dialog).
- Live spinner and status line, partial-failure tolerance (if one API is down the others still work), and a shared in-memory cache so reopening the panel is instant. **REFRESH** always fetches fresh data.
- English / Bahasa Indonesia UI. GD-blue or midnight theme, configurable accent color, optional top-3 medal colors.
- A draggable main-menu shortcut (tap to open, drag to move, position saved across sessions) with size presets, opacity, optional caption, live-applied settings and a one-tap position reset.

## Settings

| Section | Setting | Default | Notes |
| --- | --- | --- | --- |
| — | Language / Bahasa | English | English or Bahasa Indonesia |
| Appearance | Panel theme | GD blue | GD blue or midnight; reopen to apply |
| Appearance | Panel size | Compact | Compact ≈ 75%, Normal ≈ 88%, Large 100% |
| Appearance | Accent color | cyan | Tab marker and row rails |
| Appearance | Compact rows | on | Shorter rows; off = bigger touch targets |
| Appearance | Striped rows | on | Alternating row shading |
| Appearance | Highlight top 3 | on | Gold / silver / bronze ranks |
| Appearance | Header emblem | off | Small demon mark in the header |
| Appearance | Footer credit | off | Author + version line |
| Appearance | Dim the game | on | Darkens only the game behind the panel |
| Appearance | Dim strength | 150 | 40–220, requires dimming |
| Appearance | Reduce motion | off | Disables the shortcut pulse |
| List content | Levels per page | 6 | 3–10; panel height adapts |
| List content | Default tab | ALL | Used when *Remember last tab* is off |
| List content | Remember last tab | on | Reopen on the last selected tab |
| List content | Reverse order | off | Lowest ranks first |
| List content | Rank numbers | on | Rank column |
| List content | Thumbnails | on | Preview tile per level |
| List content | Video covers | on | YouTube cover when a list provides a valid link; requires thumbnails |
| List content | Source marks | on | A·C·P marks |
| List content | Level ID under name | off | In-game ID below each name |
| Behavior | Confirm before opening | off | Ask before opening a level |
| Behavior | Wrap pages | off | Last page wraps to first |
| Behavior | Cache duration | 5 min | 0–60; 0 always refetches |
| Behavior | Request timeout | 30 s | 10–90 per API |
| Menu shortcut | Show shortcut | on | Main-menu button |
| Menu shortcut | Shortcut size | Small | Very small / Small / Medium / Large |
| Menu shortcut | Shortcut caption | off | LIST / DAFTAR label |
| Menu shortcut | Shortcut opacity | 100% | 30–100% |
| Menu shortcut | Reset position | button | Returns the button to its default spot |

Settings that affect the panel layout apply when the panel is reopened; shortcut settings apply live while the main menu is on screen.

## Live data

- **AREDL** — `https://api.aredl.net/api/aredl/levels`
- **Global Demonlist Classic** — `https://api.demonlist.org/level/classic/list`
- **Pointercrate** — `https://pointercrate.com/api/v2/demons/listed/` (retrieved in 100-item API pages)

All lists load asynchronously and merge by the Geometry Dash level ID. Records without a usable in-game ID are skipped so the copy action never mistakes a list-internal ID for a GD ID. Only validated YouTube IDs are requested for video covers, previews are capped and cached per popup, and external thumbnail requests can be disabled. Video covers are **not guaranteed to be in-game screenshots**; the placeholder is an honest icon, never fake art. Internet access is required.

## Build and release

The `Build Geode release` GitHub Actions workflow builds **Windows 64-bit, macOS, iOS, Android 32-bit, and Android 64-bit** binaries for **Geometry Dash 2.2081** using Geode SDK 5.11.2. It verifies the combined `.geode` archive and uploads it as the **Leaderboard-List-v2.3.0** artifact (retained for 30 days). Open a successful [workflow run](https://github.com/Aluxs-arch/GD-Leaderboard-list/actions/workflows/geode-release.yml) and download that artifact to obtain the `.geode` file. Pushing a `v*` tag also publishes a GitHub release with the package attached; only tag a tested commit intended for release. The mod version is **v2.3.0** (different from the Geometry Dash version).

For a local build, install Geode SDK and CLI, then run:

```sh
cmake -B build -DGEODE_SDK="$GEODE_SDK" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

The Geode SDK is not installed in this workspace, so the local native build cannot be run here. Use the CI workflow to produce and validate the `.geode` binary package.

Panel textures and the shortcut badge are generated by `scripts/make_assets.py` (requires `pip install pillow`); run it from the repository root after changing the art direction.

## Ringkasan (Bahasa Indonesia)

Panel ditulis ulang agar **tidak ada elemen yang keluar dari bingkai**: tinggi panel dihitung dari jumlah baris, tombol diukur sebelum ditempatkan, dan nama level mendapat sisa ruang yang ada. Ukuran bawaan lebih kecil (±300×207 poin), gaya mengikuti tombol asli GD, dan halaman pengaturan kini punya banyak opsi: tema, warna aksen, jumlah baris, urutan, thumbnail, konfirmasi buka level, cache, batas waktu, opacity pintasan, dan tombol atur ulang posisi.

## Credits

**Aluxs** · **Aluxs-arch** · [GitHub repository](https://github.com/Aluxs-arch/GD-Leaderboard-list)

List data and marks belong to their respective owners: AREDL, Global Demonlist, and Pointercrate.
