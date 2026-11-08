# DDC/CI is a slow, lossy, half-duplex channel

*Seven things that broke while writing a 36 KB brightness tray in pure Win32 C*

The Windows Monitor Configuration API makes brightness look trivial:

```c
SetMonitorBrightness(hPhysicalMonitor, value);   /* one call, right? */
```

It is not one call. It is a synchronous transaction on the monitor's I²C bus,
through a driver stack you do not control, to a device that may silently ignore
you, clamp your value, or answer half a second later. The API reports success
for several different kinds of failure, and two processes holding a handle to
the same panel will happily fight each other.

This article collects five design decisions and two Win32 traps from
[BrightTray-Win32](../README.md). Each one came from a reproducible failure,
not from a style preference. The full design notes, including the memory
measurements, are in [DESIGN.md](DESIGN.md).

**Scope of the numbers below.** All timings and memory figures were measured on
one Windows 11 machine (Ryzen 5 7600, RX 7900 XTX) with a single 2560x1080
external monitor over DDC/CI. They are reproducible on that setup. They are
not a benchmark across panels, GPUs or driver versions — which is exactly why
the [verified monitors table](VERIFIED-MONITORS.md) exists.

---

## The mental model

A monitor is not a register you write. Treat it as a slow, half-duplex,
best-effort serial device with no command queue:

- a write can block the calling thread for tens of milliseconds;
- a write can return `TRUE` and still do nothing;
- two writes issued too close together cause the later one to be dropped;
- the value you read back is the only truth, and it may have been changed by
  the person sitting in front of the monitor.

Everything below follows from those four properties.

---

## 1. One synchronous write cost 63 ms of UI-thread latency

**Symptom.** Dragging the slider stuttered and the cursor turned into a busy
spinner. `SetMonitorBrightness` blocked for **up to 63 ms** on the test panel.

**What changed.** Input handlers no longer touch hardware. They store the newest
target in a volatile slot (`g_target[]`) and invalidate the window. One worker
thread (`BrightWorker`) wakes on an event, takes the latest value with
`InterlockedExchange`, and performs the actual write. UI-thread latency dropped
to **under 1 ms**; the physical panel normally settles within about 100 ms of
releasing the slider.

**The deliberate loss.** Intermediate values are thrown away. There is no
queue. A stale DDC command is worse than a skipped frame — replaying every
mouse-move sample would make the panel lag further behind the pointer with
every frame, then keep writing long after the user let go.

---

## 2. Going asynchronous immediately made it worse

**Symptom.** Once the UI stopped blocking, the worker could issue commands far
faster than a human can drag. The slider appeared to freeze at some value: the
monitor had silently dropped the later commands.

**What changed.** The worker enforces a minimum **50 ms** gap between two
hardware writes. On a rejected write the interval doubles, up to **500 ms**,
and relaxes back to 50 ms after a success. The backoff also gives a monitor
that has stopped answering time to recover instead of being hammered.

**Trade-off.** Fast drags feel smooth on screen but the panel follows in
steps. That is honest: the bottleneck is the panel, not the code.

---

## 3. A success return value is not confirmation

**Symptom.** Some panels accept `SetMonitorBrightness`, return success and do
nothing. Others clamp the value (a panel whose minimum is not zero will never
reach 0%). Neither case is visible from the return code.

**What changed.** Within a bounded **2.5 s** window after a write the worker
reads `GetMonitorBrightness` (the relevant bookkeeping is `g_written[]`,
`g_wrTick[]` and `g_vfTick[]`). A mismatch triggers one extra write; a
persistent mismatch turns the popup's percentage into a red "no response"
state, so the user is told the hardware did not obey instead of being shown a
lie.

**Why the window is bounded.** After it expires, the monitor's real value wins.
Without that cutoff, someone changing brightness on the physical OSD would see
the tool force its stale value back a moment later. "Brightness jumps back by
itself" is often a bug in the tool, not in the monitor.

---

## 4. Two writers make the brightness jump back

**Symptom.** A second process opening its own DDC handle to the same panel
overwrites the first process's commands. Whichever writes last wins, so the
visible value depends on timing.

**What changed.** There is exactly one writer: the worker thread of the
resident instance. `BrightTray.exe set 40` does **not** load dxva2 or open a
handle when an instance is already running — it finds that instance's hidden
window and posts a `WM_SETPCT` message, then exits. Launching the exe a second
time opens the popup rather than creating a second tray icon, using the same
handoff.

**Consequence for contributors.** Every hardware write must go through that
worker. Adding a second `SetMonitorBrightness` call site anywhere else
reintroduces the bug.

---

## 5. A focus-stealing popup cost 7 threads and 3–6 MB

**Symptom.** An early popup used a normal focusable window. Opening it made
Windows inject IME/TSF DLLs into the process: thread count jumped **5 → 12**
and working set from about 1 MB to **3–6 MB**, a step change with no relation
to the few KB of code being optimised at the time. It did not come back down
after closing the window.

**What changed.** `WS_EX_NOACTIVATE` prevents the focus transition entirely.
Keyboard input is handled by a temporary low-level keyboard hook plus three
`RegisterHotKey` bindings (Esc / Up / Down) that exist **only while the popup
is visible** and are released on close. The only long-lived hook is
`WH_MOUSE_LL`, needed for scroll-over-tray-icon; see [SECURITY.md](../SECURITY.md)
for what it does and does not observe.

**Takeaway.** On Windows, "the window took focus" is not merely a UX detail —
it changes which DLLs get loaded into your process. If a future change makes
the popup focusable again, expect the same memory step.

---

## Trap 1: `hPhysicalMonitor == 0` is a valid handle

`GetPhysicalMonitorsFromHMONITOR` returns an index-like value on some driver
stacks. On the test machine the first (and only) monitor's handle is exactly
`0`. An early guard — `if (!m->hpm) return FALSE;` — silently skipped every
hardware write.

The symptom was actively misleading: the slider moved, the percentage changed,
the code looked correct, and the screen stayed dark. It is easy to conclude
"this monitor's DDC channel is dead" and give up. Validity is now tracked with
the program's own `ok` flag, never by testing the handle. The comment is still
in `WriteHw()` in `bright.c` so nobody re-adds the guard.

---

## Trap 2: `NOTIFYICON_VERSION_4` reverses the message layout

With `NOTIFYICON_VERSION_4`, `wParam` carries coordinates and the low word of
`lParam` carries the event id. The older protocol has them the other way round.
Mixing the two conventions makes tray clicks appear to do nothing at all.

There is a second, nastier wrinkle: v4 delivers **both** `WM_LBUTTONUP` *and*
`NIN_SELECT` for a single click. Without de-duplication the popup opens and
immediately closes itself, which looks like a broken hit-test. The fix is a
short time-based guard around the open/close transition.

---

## What this still does not solve

- Laptop internal panels usually expose WMI brightness, not DDC/CI. There is
  deliberately no WMI/COM backend here; this is the single biggest functional
  gap versus larger tools.
- HDR exclusive mode, virtual displays, remote desktop sessions and some KVMs
  or docks block the DDC/CI channel entirely.
- Only System-DPI awareness is declared; mixed-scaling multi-monitor setups can
  render softly.
- Verification is bounded and best-effort. No amount of read-back makes a
  lossy channel reliable — it only makes failures visible.

---

## Reproducing and checking

The whole program is one `bright.c` translated with TinyCC 0.9.27 win64.
`build.cmd` produces a byte-for-byte identical exe; CI hashes the source right
after checkout, rebuilds it on a clean Windows runner and compares against the
committed golden hash in [tests/expected-binary.sha256](../tests/expected-binary.sha256).
`BrightTray.exe test` enumerates monitors, writes and reads back brightness,
restores the original value and writes a log — that log is the most useful
thing to attach to a compatibility report.

If your panel behaves differently, please add it to the
[verified monitors table](VERIFIED-MONITORS.md). One machine's DDC/CI quirks
are an anecdote; a table of panels is data.
