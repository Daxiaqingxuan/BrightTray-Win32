# Security policy

## Supported versions

| Version | Supported |
| --- | --- |
| v0.1.x | yes |
| older local snapshots | no |

## Reporting a vulnerability

Please use GitHub's **Security -> Report a vulnerability** (private advisory) for anything with a security impact. For ordinary bugs, monitor compatibility problems, or DDC/CI quirks, open a normal issue using the bug template instead. Do not post exploit details in a public issue before a fix is available.

Please include: Windows version, monitor model and connection type, steps to reproduce, and the output of `BrightTray.exe test` (it contains no personal data beyond display names and brightness values).

## Verifying a release

CI refuses to publish a binary unless it matches the committed golden hash in `tests/expected-binary.sha256` (36,352 bytes, SHA-256 `4fbe696dd4c316290ae31448ef4e06b29772a3e788bb52a1a7a41aec37106b06`). Tagged releases additionally carry a build provenance attestation:

```
gh attestation verify BrightTray.exe --owner <your-account>
```

The compiler archive itself is hash-pinned in the workflow, so a substituted toolchain cannot silently alter the output. You can also rebuild locally with TinyCC 0.9.27 win64 and compare byte-for-byte. An unsigned 36 KB binary that installs a low-level hook can trigger heuristic antivirus alerts; the source and the deterministic recipe exist precisely so the binary can be checked independently.

## What this program does and does not do

| Property | Status |
| --- | --- |
| Network access / telemetry / update checker | None |
| Administrator rights / services / drivers | None required; runs as a normal user |
| Persistence | Only an optional `HKCU\...\Run` value when *Start with Windows* is enabled |
| Files written | `BrightTray_test.log` (test mode only), `BrightTray_crash.txt` (crash only) |
| Code signing | Not Authenticode-signed. Releases publish a SHA-256 and a GitHub build provenance attestation; verify both before running |
| Administrator-level system changes | None |

## Hooks and input observation

Two low-level hooks are used, and their scope is deliberately narrow:

- `WH_MOUSE_LL` is installed for the whole process lifetime. It is needed for *scroll over the tray icon* and *click outside to close*. It inspects wheel and button events and calls `CallNextHookEx` for everything else; ordinary clicks are not swallowed. To determine the icon rectangle it periodically calls `Shell_NotifyIconGetRect` (cached for 1.2 s). It does not log, store, or transmit input.
- `WH_KEYBOARD_LL` and three global hotkeys (`Esc`, `Up`, `Down`) exist **only while the popup is visible** and are released immediately when it closes. They are needed because a `WS_EX_NOACTIVATE` window cannot receive normal keyboard focus.

A low-level hook plus an unsigned tiny binary can trigger heuristic antivirus alerts. Releases should be checked against VirusTotal; the exact source and the deterministic build recipe are provided so the binary can be rebuilt and compared. A switch to disable the permanent mouse hook (at the cost of scroll-over-icon) is a welcome contribution.

## Local IPC

The `set` command locates the running instance with `FindWindowW` and delivers a `WM_SETPCT` message via `PostMessage`. There is no sender authentication: any process in the same user session could send such a message and change brightness. This is a local convenience channel, not a privileged endpoint, and the worst outcome is a brightness change. It should not be exposed across sessions or elevated boundaries.

## Hardware writes

DDC/CI writes go to the monitor's backlight VCP code through `dxva2.dll`. Commands are throttled (50 ms minimum, exponential backoff to 500 ms on rejection) because sending them too quickly can make a monitor stop answering until its power is cycled. The program never sends arbitrary VCP codes and never writes monitor firmware.
