## What changed

<!-- One or two sentences. Link the issue if there is one. -->

## Why

<!-- Especially for DDC/CI behaviour: which monitor/driver made this necessary? -->

## Impact on footprint

- [ ] No new DLL, runtime, thread, config file, or network access
- [ ] Rebuilt with TinyCC 0.9.27 win64 and recorded the new binary size
- [ ] `BrightTray.exe test` passes on at least one physical DDC/CI monitor
- [ ] No change to the single-writer rule (all hardware writes still go through the worker thread)

## Verification

| Item | Result |
| --- | --- |
| Build command | `build.cmd` |
| Binary size (bytes) | |
| SHA-256 | |
| Windows version | |
| Monitor model / connection | |
| Tray scroll / popup slider / `set` / `test` all exercised? | |

## Checklist

- [ ] Kept the source in `bright.c` (no new build system or project file)
- [ ] Updated `docs/DESIGN.md` if a design decision changed
- [ ] Updated `CHANGELOG.md` under an Unreleased section
- [ ] No binaries, personal paths, user names, or machine-specific data added
- [ ] Read [CONTRIBUTING.md](../CONTRIBUTING.md)

By submitting this PR you agree that your contribution is licensed under the MIT License.
