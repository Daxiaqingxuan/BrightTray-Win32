# Changelog

All notable changes to this project are documented here. This project follows [Semantic Versioning](https://semver.org/) loosely; before v1.0 the minor version may change with behaviour.

## v0.1.1 - 2026-11-08

### Added

- **English UI with automatic language selection.** All user-facing strings now live in one `UI_TEXT` table in `bright.c`. Windows set to Simplified Chinese (zh-CN) keeps the original Chinese wording; every other locale gets English. `BRIGHTTRAY_LANG=en|zh` overrides the detection, which is how the English screenshots were captured on a Chinese Windows. No config file, no extra DLL, no new thread.
- Real screenshots of the tray popup and the OSD in `docs/assets/`. The vector diagrams remain alongside them as labelled schematics.
- [DDC/CI pitfalls](docs/DDC-CI-PITFALLS.md): a standalone write-up of the five write-path decisions and the two Win32 traps, with the measurements and code locations behind each one.
- [Verified monitors](docs/VERIFIED-MONITORS.md) plus a matching issue form, so hardware compatibility reports land in one table instead of scattered threads.
- Repository description, homepage and topics for search/discovery on GitHub.

### Changed

- **Removed the "brightness" label from the tray popup.** With one monitor the whole top label row is gone: the percentage moves up and the popup shrinks from 215 px to 198 px tall. With multiple monitors the row still names each display so the columns remain distinguishable.
- The red "no response" warning is preserved. Without a label row it now replaces the percentage line, drawn in the smaller label font.
- `w_label` and its initialisation were deleted. `g_nameh` (set in `OpenPopup`) drives `TRACK_TOP`, `TRACK_BOT`, `POPUP_H` and the slider hit-test geometry, so the shorter popup still responds in the right place.
- Removed stale pre-publication notes from both READMEs, the internal README-fix log and the name-search anecdote from the publishing checklist.
- Both SVG diagrams now use English labels, matching the default UI.
- `v0.1.0` release date corrected to 2026-11-08.

### Reproducibility

`bright.c` SHA-256 `8ff57f3d53fc79605851ac5515abeaff77d24b4e64024a7503de6b5a33de46a3`
(1,372 lines). Rebuilding with TinyCC 0.9.27 win64 and the import
definitions in `lib/` yields a 36,352-byte `BrightTray.exe` with SHA-256
`4fbe696dd4c316290ae31448ef4e06b29772a3e788bb52a1a7a41aec37106b06`.
The golden hash, CI source pin and size guard, both READMEs,
`docs/DESIGN.md`, `SECURITY.md` and `docs/PUBLISHING.md` were updated together.

## v0.1.0 - 2026-11-08

First public/open-source-ready snapshot. This release packages the already-working local build without changing a single line of the compiled source.

Source identity: `bright.c` SHA-256 `53a20233cc7503d231f11f278a31d3aa09adde3ab2468fd1bdac91ddfeeef917`. Rebuilding with TinyCC 0.9.27 win64 and the import definitions in `lib/` produces a byte-for-byte identical 35,328-byte `BrightTray.exe`.

### Included

- Hardware brightness control over DDC/CI via the Windows Monitor Configuration API, for up to 8 physical monitors.
- Tray popup with one GDI-drawn slider per monitor, per-monitor read-back of the real current value, and an OSD percentage overlay.
- Scroll over the tray icon, scroll inside the popup, and Up/Down keys to move all monitors by 5%.
- `WS_EX_NOACTIVATE` popup plus a temporary keyboard hook and hotkeys, so the process never takes focus and never triggers IME/TSF DLL injection.
- Asynchronous single-writer DDC pipeline: latest-value-wins queue, 50 ms throttling with exponential backoff to 500 ms, read-back verification with retry, and a visible "no response" state.
- CLI: `set <percent>`, `test` (self-check with log), `shot` (debug popup).
- Optional *Start with Windows* entry under HKCU Run.
- Deterministic one-command build (`build.cmd`), CI build with SHA-256 publication, MIT license, and full project documentation.

### Known limitations at the time

- No WMI backend, so most laptop internal panels are not supported.
- No auto brightness, scheduling, contrast, color temperature, or software dimming.
- Chinese UI strings only; no i18n yet (addressed in v0.1.1).
- System-DPI awareness only (mixed-DPI multi-monitor setups may render softly).
- A low-level mouse hook stays installed for the lifetime of the process; see SECURITY.md.

### Notes

This release intentionally contains no binary in the source tree. Download `BrightTray.exe` from the Releases page, where CI attaches it together with `BrightTray.exe.sha256`.
