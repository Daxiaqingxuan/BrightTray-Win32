# Contributing

Thanks for helping out. Bug reports from unusual monitor / GPU / dock combinations are the most valuable contribution: DDC/CI behaviour differs a lot between panels.

## Ground rules

This project has one defining constraint: **a tray utility should stay under 1 MB of RAM and ship as a single small exe**. Please discuss before proposing:

- a runtime (.NET, Electron, Python, a JS engine), an installer, or a background service;
- a new permanent thread, polling loop, or timer;
- additional Win32 common controls or UI frameworks;
- telemetry, networking, or an update checker.

Features that are welcome: more robust monitor handling, a WMI backend for laptop panels (behind a clean abstraction), i18n, accessibility, a hook-free fallback mode, and tests.

## Development setup

1. Windows with TinyCC 0.9.27 win64. Set `TCC` to the folder containing `tcc.exe`, or place `tcc.exe` on `PATH`.
2. Copy `lib/*.def` into TCC's `lib/` directory. TCC's bundled import libraries miss several APIs used here. CI does this automatically; `build.cmd` deliberately does not modify your local compiler.
3. Run `build.cmd`. Output: `build/BrightTray.exe` plus its SHA-256. The script never overwrites a running executable.
4. Run `python tests/run_tests.py` and `python tests/lint_source.py`. Both are standard-library-only, cross-platform, and never touch hardware. See [tests/README.md](tests/README.md) for what they protect.
5. Python 3 (no third-party packages) is only needed if you change the tray icon: `python gen_icon.py` regenerates `icon_data.h`.

Source style: plain C as understood by TCC, tab-free 4-space indentation, UTF-8 source with Chinese UI strings converted at runtime via `MultiByteToWideChar`. `bright.c` intentionally stays a single translation unit; if you split it into modules, keep the zero-dependency, single-exe property.

## Before opening a pull request

Hardware behaviour cannot be verified by CI (GitHub runners have no real monitors), so please run this checklist locally and paste the results in the PR:

- [ ] `BrightTray.exe test` succeeds and restores the original brightness; attach the log.
- [ ] `BrightTray.exe set 40` and `set 75` reach exactly those values.
- [ ] Dragging a slider feels immediate; no busy cursor; UI thread stays responsive.
- [ ] Scroll over the tray icon, scroll inside the popup, Up/Down keys, Esc, and click-outside-to-close all work.
- [ ] The popup does **not** take focus: the previously active window stays active, and the process stays at 5 threads / ~1 MB after opening the popup.
- [ ] Open/close the popup 30+ times and scroll 20+ times: handle count and private bytes stay flat (no leaks).
- [ ] Rebuild produces a byte-for-byte identical exe (compare SHA-256).
- [ ] No new binary files are added to the source tree (see `.gitignore`).
- [ ] `python tests/run_tests.py` passes with 0 failures.

For changes to the DDC path, note the exact monitor model, cable type, and whether DDC/CI was enabled in the OSD.

## Issues

Use the provided templates. Please do not paste the full `BrightTray_crash.txt` without redacting local paths if it contains any, and never include unrelated system information from your machine.

By submitting a contribution you agree that it is licensed under the [MIT license](LICENSE).
