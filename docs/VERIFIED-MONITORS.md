# Verified monitors

DDC/CI is panel-specific. A monitor can accept writes, silently ignore them,
clamp values, or drop commands that arrive too quickly. This table records
**real results on real hardware**, so you can check your panel before
downloading anything.

The program never phones home, so compatibility data can only come from users.
**Please contribute**: use the *Verified monitor* issue form, or open a regular
issue with the fields below. Attaching the output of `BrightTray.exe test` is
the single most useful thing you can do — it enumerates the monitors, writes and
reads back brightness, restores the original value, and writes
`BrightTray_test.log` next to the exe. Do not paste personal paths or user
names; the log does not contain any.

Fields: monitor model / EDID ID, resolution, GPU, connection (DP / HDMI / DVI /
USB-C / dock), Windows version, BrightTray version, and what actually happened.

## Confirmed working

| Monitor (EDID vendor / model) | Resolution | GPU | Connection | Windows | BrightTray | Result |
| --- | --- | --- | --- | --- | --- | --- |
| LHC / P30A2M (`DISPLAY\LHCFFFF`) | 2560x1080 | AMD Radeon RX 7900 XTX | digital (EDID); cable not confirmed separately | Windows 11 | v0.1.1 | Working |

Notes on the first row: this is the development machine, and all measurements
in [DESIGN.md](DESIGN.md) and [DDC-CI-PITFALLS.md](DDC-CI-PITFALLS.md) come
from this panel. `GetPhysicalMonitorsFromHMONITOR` returns a handle of `0` for
it, which is valid — see pitfall 1 in the article. Read-back matches the
written value; scroll on the tray icon, the popup slider and `set` all take
effect. The vendor string is the three-letter EDID ID, not necessarily a retail
brand name.

## Reported problems

| Monitor | Symptom | Details / issue |
| --- | --- | --- |
| _none reported yet_ | | |

If your panel ignores writes, that is still a useful row: it tells the next
person what to expect. Use the [bug report form](../../issues/new/choose)
for a failure and the *Verified monitor* form for a success.

## What "working" means here

- `GetMonitorBrightness` returns a plausible current value at startup.
- `SetMonitorBrightness` changes the **physical backlight**, and reading it
  back within the 2.5 s verification window matches the requested value.
- Scroll on the tray icon, dragging the slider, and `BrightTray.exe set <n>`
  all take effect.
- `BrightTray.exe test` restores the original brightness afterwards.

## Known blind spots

These are not "unverified monitors" — they are paths this program does not
implement at all:

- Laptop internal panels (usually WMI brightness; no WMI backend here).
- HDR exclusive mode on some panels, virtual displays, remote desktop sessions.
- KVM switches and docks that block the DDC/CI channel.

See [README.md](../README.md) for the full limitations list.
