# Design notes and DDC/CI war stories

This document explains *why* `bright.c` looks the way it does. Most of the
non-obvious code is a reaction to a measured failure, not a stylistic choice.
If you plan to change the hardware-write path, read this first.
Sections 2 and 3 are also available as a standalone article:
[DDC/CI pitfalls](DDC-CI-PITFALLS.md).

All measurements below were taken on one Windows 11 machine with a Ryzen 5
7600, an RX 7900 XTX and one 2560x1080 external monitor over DDC/CI. They are
reproducible on that setup; they are not a benchmark across hardware.

---

## 1. Shape of the program

```
                     one process, 5 threads
  +--------------------------------------------------------------+
  | hidden main window  (message pump, tray icon, timer)          |
  |                                                              |
  |   popup window  --- GDI-drawn sliders, WS_EX_NOACTIVATE       |
  |   OSD window    --- GDI-drawn percentage, top of screen       |
  |                                                              |
  |   latest_target  ---> [worker thread] ---> SetMonitorBrightness
  |       (volatile)        single writer        per physical monitor
  |                              |
  |                              +---> GetMonitorBrightness (verify)
  |                                                              |
  |   WH_MOUSE_LL hook (lifetime of process)                     |
  |   WH_KEYBOARD_LL + 3 hotkeys (only while popup is visible)   |
  +--------------------------------------------------------------+
```

- One `.c` file, ~1,316 lines, one generated `icon_data.h`.
- `dxva2.dll` is loaded at runtime with `LoadLibrary`/`GetProcAddress`, so the
  exe has no import-table dependency on the Monitor Configuration API. On a
  machine without a physical monitor the program still starts and reports
  "not supported" instead of failing to load.
- All UI is GDI: no common controls, no fonts to install, no resources beyond
  the icon bytes embedded in `icon_data.h`.
- The binary produced by TinyCC 0.9.27 win64 is **36,352 bytes**, with no
  installer, no config file, no service and no network code.

### Thread and object budget (measured)

| Metric | Idle / steady state |
| --- | --- |
| Threads | 5 |
| Modules loaded | 29 |
| Handles | 125 -> 128 over 40 open/close cycles of the popup |
| Private commit | 2,328 KB -> 2,332 KB over the same 40 cycles |
| Private working set | ~0.24 MB (rising briefly while the popup is mapped) |
| Working set | 0.5-1.0 MB |
| CPU while idle | 0% (no polling timer for brightness) |

The 40-cycle open/close test is the main evidence for "no leak": handle count
and private commit drift by a few units, not by tens per cycle.

---

## 2. Five decisions, and the failure that produced each one

### 2.1 Never write hardware on the UI thread

`SetMonitorBrightness` is a synchronous DDC/CI transaction. On the test
monitor one call could block for **up to 63 ms**. Dragging a slider with a
synchronous write produced visible stutter and a busy cursor.

Current behaviour: input handlers only store the newest target in a volatile
variable and invalidate the window. A single worker thread picks up the latest
value and performs the hardware write. Intermediate values are deliberately
discarded — there is no queue, because a stale DDC command is worse than a
skipped frame. UI-thread latency is now **under 1 ms**; the physical brightness
usually settles within ~100 ms of releasing the slider.

### 2.2 Throttle writes, with exponential backoff on rejection

Asynchrony creates a new problem: the worker can now issue DDC commands much
faster than a human can drag. Monitors do not have a command queue; sending
commands too densely makes them silently drop later ones, which looks exactly
like "the slider stopped working".

Rules in the worker:

- minimum **50 ms** between two hardware writes;
- on a rejected/failed write, double the interval up to **500 ms**;
- back off to 50 ms again once writes succeed.

### 2.3 Read the value back after writing

Some monitors return success from `SetMonitorBrightness` and then do nothing.
Others accept the value but clamp it. Within **2.5 s** of a write the worker
reads `GetMonitorBrightness`; a mismatch triggers one additional write, and a
persistent mismatch shows a red "no response" state in the popup.

The verification window is deliberately bounded. After it expires, the monitor's
real value wins. Without that cutoff, a user changing brightness on the OSD
would see the program force its old value back — a classic "brightness jumps
back by itself" bug that is actually caused by the tool, not the monitor.

### 2.4 Exactly one writer in the system

Two processes each holding a `HANDLE` to the same physical monitor overwrite
each other's DDC commands. So `BrightTray.exe set 40` does **not** open its own
DDC handle when an instance is already running: it finds the existing window
and posts the request to it, then exits. The same mechanism is used when the
user simply launches the exe a second time, which opens the popup instead of
creating a second tray icon.

Consequence for contributors: **every hardware write must go through the worker
thread of the resident instance.** Adding a second `SetMonitorBrightness` call
site reintroduces the bug.

### 2.5 `WS_EX_NOACTIVATE` and why it saved more memory than any code golf

The popup must not take keyboard focus. An early version used a normal window;
opening it caused Windows to inject IME/TSF DLLs into the process. Thread count
jumped from 5 to 12 and working set from ~1 MB to **3-6 MB** — a step change
with no relation to the few KB of code being optimised at the time.

`WS_EX_NOACTIVATE` prevents the focus transition entirely. Keyboard input is
handled by a temporary low-level keyboard hook plus three `RegisterHotKey`
bindings (Esc / Up / Down) that exist **only while the popup is visible** and
are released on close. This is also why a permanently installed `WH_MOUSE_LL`
hook is the only long-lived hook.

> Diagnosis note: the memory step was caused by focus-triggered DLL injection,
> not by GDI objects, the icon, or the slider drawing. If a future change makes
> the popup focusable again, expect the same symptom.

---

## 3. Two Win32 traps worth knowing before editing

### 3.1 `hPhysicalMonitor` can legitimately be `0`

`GetPhysicalMonitorsFromHMONITOR` returns a `PHYSICAL_MONITOR` whose handle is
an index-like value on some driver stacks; on the test machine the first (and
only) monitor's handle is exactly `0`. An early guard `if (!m->hpm) return
FALSE;` silently skipped every hardware write. The symptom was confusing: the
slider and percentage moved, the screen never changed, and the code looked
correct — leading to a wrong diagnosis of "the monitor's DDC channel is dead".

Validity is tracked with the program's own `ok` flag, not by testing the handle.

### 3.2 `NOTIFYICON_VERSION_4` reverses the message layout

With `NOTIFYICON_VERSION_4`, `wParam` carries coordinates and the low word of
`lParam` carries the event id; the older protocol has them the other way round.
Mixing the two conventions makes tray clicks appear to do nothing.

Additionally, v4 delivers both `WM_LBUTTONUP` and `NIN_SELECT` for a single
click. Without de-duplication the popup opened and immediately closed itself.
The fix is a short time-based guard around the open/close transition.

---

## 4. Message and input flow

| Input | Path | Effect |
| --- | --- | --- |
| Wheel over tray icon | `WH_MOUSE_LL` + cached tray icon rect | all monitors +/-5%, OSD flashes |
| Left click on icon | tray callback (de-duplicated) | open popup, read each monitor's real value |
| Wheel / Up / Down in popup | window message | all monitors together |
| Drag a single slider | GDI hit-test -> `latest_target` | per-monitor value |
| Esc / click outside | temporary hotkey / hook | close popup, release hooks and hotkeys |
| Right click | context menu | *Start with Windows* (HKCU Run), *Exit* |
| `set <n>` from another process | `FindWindow` + `PostMessage` | resident instance applies it |
| `test` | direct diagnostic path | enumerate, write, read back, restore, write log |

The tray icon rectangle is obtained from the shell when the icon is visible.
Windows 11 can hide the icon completely inside the `^` overflow area; in that
case the shell reports no position and wheel-over-icon is unavailable. The
popup and the `set` command still work. Use *Settings -> Personalization ->
Taskbar -> Other system tray icons* to pin it.

---

## 5. What the program deliberately does not do

| Not implemented | Reason |
| --- | --- |
| WMI backend for laptop internal panels | Would add COM/WMI initialization and a second code path. Most laptop panels are not controllable over DDC/CI, so this is the single biggest functional gap versus Twinkle Tray / Monitorian / lumos_ddc. |
| Gamma / software dimming | Reduces contrast and black level; defeats the point of hardware backlight control. |
| Auto brightness, ambient sensor, schedules | Needs a polling timer and a settings store, which conflicts with the 0% idle CPU / no-config-file goal. |
| Contrast, color temperature, input switching | Scope creep for a single-purpose tray tool. |
| Persisted brightness | The monitor's real current value is always the source of truth; external changes are picked up when the popup reopens. |
| More UI languages | English and Simplified Chinese only, chosen from `GetUserDefaultUILanguage()` with a `BRIGHTTRAY_LANG` override. A full translation framework would add files, a parser and complexity to a single-file program. |
| Per-Monitor DPI v2 | Currently System-DPI aware; mixed-DPI multi-monitor setups may render softly. |

---

## 6. Known technical debt

These are open items, not claims of correctness:

1. `RegisterHotKey` return values are not checked. If another application owns
   Esc/Up/Down globally, the keys silently do nothing.
2. Only System-DPI awareness is declared; no `SetThreadDpiAwarenessContext`
   per window.
3. Several Win32 structures and constants are declared by hand in `bright.c`
   because TCC's minimal headers omit `shellapi.h` and differ in layout. They
   are binary-compatible with the SDK on the tested toolchain but are a
   portability risk with other compilers.
4. No hardware-in-the-loop automation. CI runs the source/repository test
   suite, but DDC/CI behaviour still has to be checked manually with
   `BrightTray.exe test`, the 40-cycle open/close leak check and a
   rebuild-for-byte-identity check.
5. Crash diagnostics are a single-line dump; no minidump, no symbolication.
6. No version resource in the exe, so Windows Explorer shows no file version.
7. The `.def` files in `lib/` are generated from one machine's system DLLs;
   contributors on a different Windows build may need to regenerate them with
   `tools/mkdef.py`.
8. Only two UI locales, with no gettext-style translation pipeline. Adding a
   language means editing the `UI_TEXT` table in `bright.c` and rebuilding the
   (hash-pinned) binary.

Security-relevant design choices are in [SECURITY.md](../SECURITY.md); the
low-level hook and the unauthenticated local `PostMessage` channel are
documented there rather than hidden.

---

## 7. Build reproducibility

`bright.c` SHA-256:
`8ff57f3d53fc79605851ac5515abeaff77d24b4e64024a7503de6b5a33de46a3`

Rebuilding that exact source with TinyCC 0.9.27 win64 plus the `.def` files in
`lib/` yields a byte-for-byte identical 36,352-byte exe. There is no timestamp,
no random seed, and no absolute path embedded. CI publishes the SHA-256 of
every build so a user can verify a downloaded binary against a fresh build.

`icon_data.h` is generated by `gen_icon.py` (pure Python, supersampled sun
glyph, no Pillow) and is committed so that building needs only a C compiler.

---

## 8. Where this sits among other tools

| Project | Stack | Strengths relative to this one |
| --- | --- | --- |
| Twinkle Tray | Electron / JS, ~9.3k stars, MIT | DDC/CI + WMI, auto brightness, hotkeys, 20+ languages, store/winget distribution |
| Monitorian | C# / WPF, ~4.8k stars, MIT | DDC/CI + WMI, contrast, range calibration, ambient light sensor |
| lumos_ddc | pure C / Win32, ~200 KB | DDC/CI + WMI, schedule, idle dimming, Home Assistant, CLI, screen-reader support, CI |
| MonitorBrightnessControl | C / Win32 tray daemon, MIT | Similar minimalist footprint, different feature trade-offs |
| ddcutil | C / Linux | The reference implementation for DDC/CI quirks and monitor capability parsing |
| **BrightTray-Win32** | C / Win32, 36 KB | Smallest footprint, single-file, documented DDC write-reliability pipeline |

The defensible niche is not "pure C tray app" — several 2026-era projects
already do that. It is (a) the measured footprint and (b) the written record of
*why* DDC writes need throttling, read-back and a single writer. Both belong on
the first screen of the README.
