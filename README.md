# Ajazzy

**Still in active development.** The protocol and both interfaces work against real hardware, but only one exact model has been tested (see [Supported devices](#supported-devices)), and it hasn't had outside users yet — expect rough edges, and please open an issue if you hit one.

A reverse-engineered, open-source Linux driver for AJAZZ gaming mice — no Windows VM, no Wine, just talking to the hardware directly over `hidraw`.

AJAZZ never published a Linux driver or a protocol spec. This project exists because the official Windows app (`AJAZZ Driver (X)`) was reverse engineered from scratch by capturing real USB traffic with Wireshark/USBPcap while operating every control in the original app. See [`docs/reverse-engineering.md`](docs/reverse-engineering.md) for the full writeup of the wire protocol.

It was built and tested against an **AJ179 V2 MAX** (dongle `249A:5C2F`, dev_id `N625`). Every other AJAZZ mouse in `devices/` uses the same USB interface and command structure according to the original driver's own device table, so it should work — but "should" isn't "confirmed." Full model-by-model breakdown in [`SUPPORTED_DEVICES.md`](SUPPORTED_DEVICES.md).

## Installing

Every push to `main` builds and publishes a [release](https://github.com/Geovane2dd/Ajazzy/releases) automatically (see `.github/workflows/release.yml`) — a `.deb`, an `.rpm`, and two standalone tarballs (CLI-only, GUI-only), each with the install command spelled out in that release's notes. To build from source instead, see [Building](#building) below.

## What works

| Feature | Status |
|---|---|
| Device detection (name, VID:PID, firmware id), USB and 2.4G | Confirmed |
| Battery level, charging state | Confirmed |
| DPI — 6 stages, values, indicator colours, active stage, stages used by the DPI button | Confirmed |
| Report rate (125/250/500/1000 Hz) | Confirmed |
| Lighting — off, steady colour (any RGB), breathing (up to 7 colours) | Confirmed. Flowing, neon and colour wave exist in the firmware protocol but the official driver disables them for every model it ships a config for (and they did nothing on an AJ179 V2 MAX), so Ajazzy only offers them where a model's table enables them |
| Lighting — colour cycle and custom fade | Software effects (run while Ajazzy is open) |
| Sensor — lift-off (1/2 mm), angle snapping, ripple control, motion sync | Confirmed (round-trips) |
| Power — sleep timer, wake on movement, light off when idle, button debounce | Confirmed (round-trips) |
| Buttons — mouse functions, media, system shortcuts, any keyboard shortcut, rapid-fire, disable | Confirmed for remaps and rapid-fire; entries marked *beta* in the GUI come from the official driver but haven't been pressed on hardware |
| Macros — record, edit, library, import/export, repeat modes (once / while held / until click / N times), up to 101 steps | Written and read back; playback of long macros depends on the mouse's memory |
| Profiles — whole-mouse snapshots incl. macros, import/export, auto-apply on connect | Local to this PC |
| Battery saver — one switch for 250 Hz + short sleep + lights off + idle light-off, restores afterwards, optional auto-on when low | Local logic over confirmed commands |
| Factory reset | Reconstructed from the official driver |

A few notes:

- **Battery** is a real 0-100% value (see [`docs/reverse-engineering.md`](docs/reverse-engineering.md#the-battery-query)). Ajazzy only queries the mouse over the air every 30 s (2 min when hidden in the tray) and checks plugged/unplugged with plain filesystem calls, because every query wakes the mouse's radio and costs battery.
- **A mouse that just woke up** answers queries with empty replies for a moment. The transport retries those instead of showing zeros, and the GUI shows a banner while the mouse is asleep.
- **Macros** are stored on the device, per button. The button-table entry carries the button index and the repeat mode — an earlier version of this project hard-coded the index of the button it was captured on.
- **Lift-off** and **wake on movement** used to be mixed up in this project (they were assumed to share one byte). They are separate commands; both are handled correctly now.

## Supported devices

Every model listed in the original driver's `config.xml` lives in its own file under [`devices/`](devices/) — one file per mouse, matching its VID/PID/dev_id combinations. Only `devices/aj179v2max.h` has its capabilities marked `confirmed`; everything else is listed for detection purposes but untested. Both the CLI and GUI will tell you plainly when you're on unconfirmed ground, they just won't stop you from trying.

**Compatible** (per the original driver's own device table) is not the same as **confirmed working** (someone actually tested it with this project). See [`SUPPORTED_DEVICES.md`](SUPPORTED_DEVICES.md) for the full model-by-model list, tested or not.

If you've got a different AJAZZ mouse and it works (or doesn't), please open a PR — see [Adding a new mouse](#adding-a-new-mouse-model) below.

## Building

Needs a C11 compiler and, for the GUI, GTK4 + libadwaita development headers (`gtk4-devel`/`libgtk-4-dev`, `libadwaita-devel`/`libadwaita-1-dev`). `gettext` (`msgfmt`/`xgettext`) is used for the Portuguese translation but is optional — the GUI falls back to English without it.

```sh
make            # builds ajazzyctl (CLI), ajazzy-gui (GUI) if GTK4/libadwaita are found, and the pt_BR translation
sudo make install   # installs to /usr/local by default; set PREFIX= to change it
```

Want a `.deb`/`.rpm`/tarball yourself instead of waiting for CI? The same scripts the release workflow uses are right there in `packaging/`:

```sh
make
packaging/build-deb.sh 1.2.3        # needs dpkg-deb
packaging/build-rpm.sh 1.2.3        # needs rpmbuild, and a git checkout (it archives HEAD)
packaging/build-tarballs.sh 1.2.3   # no extra tools needed
```

### Permissions

The mouse's configuration interface shows up as a `/dev/hidraw*` device owned by root. Install the udev rule so your user can talk to it without `sudo`:

```sh
sudo cp udev/71-ajazzy.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Then unplug and replug the mouse/dongle.

## Using the CLI

```sh
./ajazzyctl info                       # what's connected, and what's confirmed for it
./ajazzyctl battery
./ajazzyctl dpi list
./ajazzyctl dpi set 1 1600              # stage 1 -> 1600 DPI
./ajazzyctl dpi active 3                # switch to stage 3
./ajazzyctl rate set 1000               # 1000 Hz polling
./ajazzyctl rgb steady ff0000 4         # solid colour (RRGGBB), brightness 0-4
./ajazzyctl rgb breathing 4 2 ff0000 0000ff   # breathing, brightness, speed, up to 7 colours
./ajazzyctl rgb flow 4 2                # also: neon, wave
./ajazzyctl rgb off
./ajazzyctl sensor get                  # lift-off, angle snapping, ripple, motion sync
./ajazzyctl sensor liftoff 2            # 2mm lift-off
./ajazzyctl sensor motionsync 1
./ajazzyctl power get                   # sleep timer, wake on movement, idle light-off, debounce
./ajazzyctl power sleep 600             # auto-sleep after 10 minutes idle
./ajazzyctl power wake 1                # wake the mouse on movement
./ajazzyctl power light-idle-off 1      # turn the light off once idle
./ajazzyctl power debounce 8            # button debounce in ms
./ajazzyctl keys list-functions         # what you can assign with 'keys set'
./ajazzyctl keys set 5 volume-up        # button 5 -> volume up
./ajazzyctl keys set-key 5 c ctrl       # button 5 -> Ctrl+C
./ajazzyctl keys set-fire 5 5 1         # button 5 -> rapid-fire
./ajazzyctl keys reset-slot 5           # back to default
./ajazzyctl keys reset                  # reset every button
./ajazzyctl macro set 5 10 a b          # button 5 -> press a, press b, 10ms delay between each step
```

`ajazzyctl watch` dumps raw incoming reports and `ajazzyctl raw <64 hex chars>` sends an arbitrary 32-byte report — both are there for protocol research, not everyday use.

## Using the GUI

`ajazzy-gui` (or `make install` it and launch "Ajazzy" from your app launcher). Picks up the Portuguese translation automatically on a `pt_BR` system locale, English otherwise; light and dark themes follow the system or can be forced in Settings.

**Everything applies the moment you change it** — there are no Apply buttons to forget. Sliders and number fields are debounced so a drag is one write, and every value on screen is re-read from the mouse, so what you see is what the mouse really has.

| Page | What it does |
|---|---|
| **Overview** | Battery ring, the numbers that matter (DPI, report rate, lighting, battery saver), one-click profiles, what each button does, device details |
| **Buttons** | Click a button on the product photo of your model, then pick a function; record any keyboard shortcut; put a macro or rapid-fire on it; reset one or all |
| **Performance** | Battery saver, DPI stages (slider, colours, how many the DPI button cycles), report rate, sensor tuning, sleep/wake/debounce |
| **Lighting** | Animated effect gallery (six run inside the mouse, two are clearly labelled software effects), per-effect colours and speed, presets |
| **Macros** | Library with live recording (keys and clicks with your real timing), step editor, repeat modes, import/export |
| **Profiles** | Save/apply/import/export whole-mouse setups, incl. macros |
| **Settings** | Theme, start with the computer, tray behaviour, low-battery warning, diagnostics |

Shortcuts: `Ctrl+1…7` switch page, `F5` re-reads the mouse, `Ctrl+Q` quits. Closing the window keeps Ajazzy in the tray (configurable) with the battery level on the icon — on GNOME that needs the [AppIndicator extension](https://extensions.gnome.org/extension/615/appindicator-support/).

### Easy on the mouse's battery

Ajazzy is built not to be the thing that drains your mouse: presence is checked locally, battery is read rarely, nothing polls the mouse while it sleeps, and the effects that would keep its radio busy (the two software effects) are labelled as such. The **Battery saver** switch does the rest of the work for you.

The pictures in `gui/images/` come from the official driver package (see `gui/images/README.md` for licensing and how to add a model).

Developer aids: `AJAZZY_SNAPSHOT_DIR=<dir> ajazzy-gui` renders every page to a PNG and quits; `AJAZZY_NO_DEVICE=1` pretends no mouse is attached so the empty states can be reviewed.

## Architecture

```
Ajazzy/
├── cli/                  ajazzyctl -- the command-line tool
├── gui/
│   ├── main.c                app shell: window, navigation, connection manager
│   ├── ui.c / ui.h           shared components (page, card, row, chip, segmented control)
│   ├── style.c               the visual language (one CSS sheet)
│   ├── page_*.c              one file per page
│   └── tray.c / tray.h       system tray icon (StatusNotifierItem + dbusmenu, no library)
├── tests/                 wire-format tests (`make test`), no hardware needed
├── protocol/
│   ├── hid.c / hid.h         wire format: report struct, checksum, command builders
│   └── transport.c / .h      hidraw I/O, device discovery
├── devices/
│   ├── device.h               shared struct + capability flags
│   ├── aj179v2max.h            the confirmed reference model
│   ├── aj159.h, aj179.h, ...   the rest of the AJAZZ lineup, unconfirmed
│   └── registry.c / .h         glues every devices/*.h file into one lookup table
├── docs/
│   └── reverse-engineering.md  how the protocol was figured out, byte by byte
├── packaging/             scripts release.yml uses to build .deb/.rpm/tarballs
├── .github/workflows/     CI (build check) and the release automation
├── po/                    gettext translations (pt_BR)
└── udev/                  the permissions rule mentioned above
```

The protocol is the same across the whole AJAZZ lineup (same vendor HID interface, same command families, per the original driver's own device table), so `devices/*.h` is purely metadata — VID/PID, the model name, and which capabilities have actually been confirmed on real hardware. There's no per-model protocol code to write.

### Adding a new mouse model

1. Get the model's VID/PID/dev_id combinations. The most reliable source is the original Windows driver's `config.xml`, which you can pull out of the installer without running it:
   ```sh
   innoextract -e "AJAZZ Driver (X)-x.x.x.x.exe"
   cat app/config.xml
   ```
2. Copy `devices/aj159.h` as a template, rename it to your model, and fill in the entries from `config.xml`. Leave `confirmed` at `0` until you've tested.
3. Add `#include "yourmodel.h"` and a line in the `groups[]` table in `devices/registry.c`.
4. Build, plug in your mouse, and try each feature with `ajazzyctl`. Whatever actually works, flip on the matching `AJAZZ_CAP_*` bit(s) for that model in your `devices/yourmodel.h`.
5. Open a PR. If something *doesn't* match this protocol, that's useful information too — describe what you saw (ideally with a USBPcap capture) so it can be looked into.

## How this was reverse engineered

Short version: the installer's `config.xml` (pulled out with `innoextract`, no need to run it) gave the VID/PID table for device detection. Everything about the actual protocol — the checksum, every command family, the button/key table layout — came from capturing real USB traffic with Wireshark and USBPcap on a Windows VM while operating every control in the original app (DPI, RGB, report rate, button remapping) and diffing the resulting packets — and, for what a capture can't show (every lighting mode, the sensor commands, the button-function codes, macro repeat modes), from decompiling the official driver with Ghidra and then confirming each command against the real mouse by writing it and reading it back. The full walkthrough is in [`docs/reverse-engineering.md`](docs/reverse-engineering.md).

## License

GPL-3.0 — see [`LICENSE`](LICENSE).
