# Supported devices

Every model below is listed in the original AJAZZ Windows driver's `config.xml`, and every one of them talks to the same vendor HID interface with the same command families — that's the driver's own device table saying so, not a guess. The `confirmed` column is about something narrower: has anyone actually run this project's code against that exact mouse and checked the features work.

If you own one of the untested models, please try it and open a PR — see [Adding a new mouse model](README.md#adding-a-new-mouse-model) in the README. If it works, flip on the matching capability bits in your `devices/<model>.h`. If something *doesn't* match, that's useful too — describe what you saw, ideally with a USBPcap capture.

| Model | dev_id(s) | Modes | Status | Source file |
|---|---|---|---|---|
| **AJ179 V2 MAX** | `N625` | USB, 2.4G | **Confirmed** — DPI, report rate, RGB, sensor/power, buttons, battery, macros | `devices/aj179v2max.h` |
| AJ139 PRO | `M129`, `M139` | USB, 2.4G | Untested | `devices/aj139pro.h` |
| AJ139 V2 PRO | `M139` | USB, 2.4G | Untested | `devices/aj139v2pro.h` |
| AJ159 | `M620`, `P630` | USB, 2.4G | Untested | `devices/aj159.h` |
| AJ159 MC | `M630`, `M620` | USB, 2.4G | Untested | `devices/aj159mc.h` |
| AJ159P MC | `N620` | USB, 2.4G | Untested | `devices/aj159pmc.h` |
| AJ179 | `M179`, `P179`, `M184` | USB, 2.4G | Untested | `devices/aj179.h` |
| AJ179P MC | `179P` | USB, 2.4G | Untested | `devices/aj179pmc.h` |
| AJ179 V2 | `N624` | USB, 2.4G | Untested | `devices/aj179v2.h` |
| T500 | `T500` | USB, 2.4G | Untested | `devices/t500.h` |

"Untested" doesn't mean "doesn't work" — it means nobody's checked yet. The protocol (checksum, command families, wire format) is identical across the whole lineup as far as the original driver is concerned, so these should work out of the box. `ajazzyctl info` and the GUI's Home page will tell you plainly which capabilities are confirmed for whatever's plugged in.

## VID/PID reference

All models share the vendor HID interface `MI_02`. USB mode and 2.4G dongle mode use different PIDs for the same model:

| Mode | VID | PID |
|---|---|---|
| USB | `248A` | `5C2E`, `5D2E`, `5E2E`, or `5B2E` (older models) |
| 2.4G dongle | `248A` or `249A` | `5C2F` or `5B2F` (older models) |

Several models share the exact same VID/PID pair — the mouse's `dev_id` (read from the device itself over the wire, query `0x10`) is what actually distinguishes them. See `devices/registry.c` for the full lookup table.
