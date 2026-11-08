# Changelog

All notable changes to this project are documented here. This project follows [Semantic Versioning](https://semver.org/) loosely; before v1.0 the minor version may change with behaviour.

## Unreleased - popup label removal (2026-11-08)

### Changed

- **Removed the "brightness" label from the tray popup.** With a single
  monitor the whole top label row is gone: the percentage moves up and the
  popup shrinks from 215 px to 198 px tall. With multiple monitors the row
  still shows each monitor's name so the columns remain distinguishable.
- The red "no response" warning is preserved. Without a label row it now
  replaces the percentage line, drawn in the smaller label font so the
  Chinese text fits the 76 px column.
- `w_label` and its `ToW("亮度", ...)` initialisation were deleted.
  `g_nameh` (set in `OpenPopup`) now drives `TRACK_TOP`, `TRACK_BOT`,
  `POPUP_H` and the slider hit-test geometry, so the shorter popup still
  responds in the right place.

### Reproducibility

`bright.c` SHA-256 `53a20233cc7503d231f11f278a31d3aa09adde3ab2468fd1bdac91ddfeeef917`
(1,330 lines). Rebuilding with TinyCC 0.9.27 win64 and the import
definitions in `lib/` yields a 35,328-byte `BrightTray.exe` with SHA-256
`9c4ef1c65b0fae0112a00ce4c7903ade3f2535491bd70edc77ee56a7ab7e8082`.
The golden hash, CI source pin, both READMEs, `docs/DESIGN.md`,
`SECURITY.md` and `docs/assets/popup.svg` were updated together.

### Publishing readiness

- `publish.cmd` now pre-checks branch, commits, a clean worktree and the test
  suite, retries the reachability probe and the push, tolerates an existing
  `origin` (so re-running is safe), and sets libcurl low-speed timeouts so a
  stalled mirror cannot hang the upload forever.
- `docs/PUBLISHING.md` documents PAT creation, the `gh-proxy.com` rewrite
  (including its token-exposure trade-off), push troubleshooting, and a REST
  API fallback for when the git transport is unusable.
- An API uploader lives outside this repository at
  `../upload-helper/upload_via_api.py` and is intentionally not published.

## 2026-11-07 - repository hardening (historical)

**This batch changed no line of `bright.c`.** The hashes in the
reproducibility section at the bottom of this file describe the current
source, not this historical entry.

### Added

- `tests/run_tests.py` (29 standard-library checks): source design invariants,
  repository hygiene and privacy, documentation links, workflow pinning, and
  reproducibility bookkeeping. `tests/lint_source.py` for source lint.
- `tests/expected-binary.sha256`: golden hash that CI enforces before publish.
- `.github/workflows/codeql.yml`: C/C++ static analysis in `build-mode: none`
  (the Win32-only source cannot be compiled on a Linux runner).
- `docs/assets/architecture.svg` and `docs/assets/popup.svg`: vector
  architecture diagram and UI mockup, referenced from both READMEs.
- `publish.cmd`: interactive helper for the first push only. It never runs
  automatically and asks for confirmation before adding a remote.
- `tests/README.md`, CI status badges, and a release-verification section in
  `SECURITY.md`.
- `.github/dependabot.yml`: grouped monthly updates for the SHA-pinned actions,
  so pinning does not mean freezing.

### Changed

- **Fixed a reproducibility hazard**: `.gitattributes` declared
  `*.c eol=crlf` while every file in the tree is LF. A Windows checkout would
  have rewritten `bright.c` and broken the source-hash evidence chain. All
  text files now stay LF; CI hashes `bright.c` immediately after checkout.
- All third-party Actions are pinned to full commit SHAs; the TinyCC 0.9.27
  archive is pinned by SHA-256 and verified on every mirror before extraction.
- Tagged releases carry a GitHub build provenance attestation, and the build
  job now runs the test suite before compiling.
- The name `BrightTray-Win32` is final (zero GitHub name conflicts on
  2026-11-07). All placeholder account URLs were removed from active issue
  templates; `docs/PUBLISHING.md` now contains the optional copy-paste snippet.
- READMEs document the CI pipeline, the tests, and the two diagrams.

### Still open (require code changes)

- WMI backend for laptop internal panels, i18n/English UI, Per-Monitor DPI v2,
  accessibility, checking the return value of `RegisterHotKey`, and automated
  hardware-in-the-loop tests. These are tracked in the design notes, not
  silently hidden.

## v0.1.0 - 2026-11-07

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

### Known limitations

- No WMI backend, so most laptop internal panels are not supported.
- No auto brightness, scheduling, contrast, color temperature, or software dimming.
- Chinese UI strings only; no i18n yet.
- System-DPI awareness only (mixed-DPI multi-monitor setups may render softly).
- A low-level mouse hook stays installed for the lifetime of the process; see SECURITY.md.

### Notes

This release intentionally contains no binary in the source tree. Download `BrightTray.exe` from the Releases page, where CI attaches it together with `BrightTray.exe.sha256`.
