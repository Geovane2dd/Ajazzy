# Ajazzy

**Still in active development.** The protocol and both interfaces work against real hardware, but only one exact model has been tested (see [Supported devices](#supported-devices)), and it hasn't had outside users yet — expect rough edges, and please open an issue if you hit one.

A reverse-engineered, open-source Linux driver for AJAZZ gaming mice — no Windows VM, no Wine, just talking to the hardware directly over `hidraw`.

AJAZZ never published a Linux driver or a protocol spec. This project exists because the official Windows app (`AJAZZ Driver (X)`) was reverse engineered from scratch by capturing real USB traffic with Wireshark/USBPcap while operating every control in the original app. See [`docs/reverse-engineering.md`](docs/reverse-engineering.md) for the full writeup of the wire protocol.

It was built and tested against an **AJ179 V2 MAX** (dongle `249A:5C2F`, dev_id `N625`). Every other AJAZZ mouse in `devices/` uses the same USB interface and command structure according to the original driver's own device table, so it should work — but "should" isn't "confirmed." Full model-by-model breakdown in [`SUPPORTED_DEVICES.md`](SUPPORTED_DEVICES.md).

## Installing

Once this repo is on GitHub, every push to `main` builds and publishes a release automatically (see `.github/workflows/release.yml`) — a `.deb`, an `.rpm`, and two standalone tarballs (CLI-only, GUI-only), each with the install command spelled out in that release's notes. Until then, see [Building](#building) below.

## What works

| Feature | Status |
|---|---|
| Device detection (name, VID:PID, firmware id) | Confirmed |
| Battery level | Confirmed (see note below) |
| DPI — read/write all 6 stages, switch active stage | Confirmed |
| Report rate (125/250/500/1000 Hz) | Confirmed |
| RGB — rainbow-cycle effect, brightness, speed, off | Confirmed |
| RGB — constant/solid-colour effect, brightness | Confirmed |
| Lift-off distance | Confirmed |
| Sensor/power — sleep timer, idle light-off | Confirmed |
| "Wake mouse on motion" toggle | Confirmed to change *something* — see note below |
| Button remap — media/browser keys, keyboard keys | Confirmed |
| Button remap — rapid-fire/auto-click | Confirmed |
| Macros — up to 3 key/button press+release actions per button, with delay | Confirmed |
| Reset buttons to factory default (all, or one at a time) | Confirmed |
| Multiple RGB effect *names* beyond "off"/"rainbow"/"constant" | **Not distinguished yet** — see below |

A few notes on the rougher edges:

- **Battery** isn't a calibrated percentage. The original driver shows a 4-bar icon, not a number, so this reads the same raw sensor byte and buckets it into 4 tiers. Only the top tier is anchored to a real data point; the rest are an evenly-spaced guess.
- **Macros** are stored on the device, not played back in software — up to 3 press/release actions per button, a limit of the single-packet write (longer macros need a multi-packet write that isn't implemented yet). See [`docs/reverse-engineering.md`](docs/reverse-engineering.md#macros) for how the wire format was figured out.
- **Report rate** and **DPI stage** are independent commands. `rate get` / the GUI read back the real current rate.
- **Sensor & power** (`ajazzyctl power get/sleep/light-idle-off`, `liftoff set`) all live in one 32-byte report — changing one preserves the others. Lift-off distance might double as a "wake mouse on motion" toggle; a capture of that control landed on the same byte and it's not clear yet whether that's really the same setting.
- **RGB** supports off/rainbow/constant, both set and read back (`rgb rainbow`/`rgb constant`/`rgb off`/`rgb get`). Other effect names from the original driver aren't distinguished on the wire — they may just not be real, separate commands.

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
./ajazzyctl rgb rainbow 4 2             # brightness 0-4, speed 0-4
./ajazzyctl rgb constant 3              # solid colour, brightness 0-4
./ajazzyctl rgb off
./ajazzyctl liftoff set 2               # 2mm lift-off
./ajazzyctl power get                   # sleep timer, lift-off, idle light-off, all in one report
./ajazzyctl power sleep 600              # auto-sleep after 10 minutes idle
./ajazzyctl power light-idle-off 1       # turn the light off once idle
./ajazzyctl keys list-functions         # what you can assign with 'keys set'
./ajazzyctl keys set 5 volume-up        # button 5 -> volume up
./ajazzyctl keys set-key 5 c ctrl       # button 5 -> Ctrl+C
./ajazzyctl keys set-fire 5 5 1         # button 5 -> rapid-fire
./ajazzyctl keys reset-slot 5           # back to default
./ajazzyctl keys reset                  # reset every button
./ajazzyctl macro set 5 10 a b          # button 5 -> press a, press b, 10ms delay between each step
./ajazzyctl macro set 4 10 lclick       # button 4 -> replay a left click
```

`ajazzyctl watch` dumps raw incoming reports and `ajazzyctl raw <64 hex chars>` sends an arbitrary 32-byte report — both are there for protocol research, not everyday use.

## Using the GUI

`ajazzy-gui` (or `make install` it and launch "Ajazzy" from your app launcher). Every page reads live from the mouse when you open it, so there's no "load" button to remember. Picks up the Portuguese translation automatically on a `pt_BR` system locale, English otherwise.

It's got its own look — a fixed accent colour instead of whatever your system theme happens to be, a battery bar on the Home page, DPI stage buttons coloured to match the mouse's actual per-stage LED colours, a live preview on the Lighting page. Every setting the CLI has is in the GUI too, going through the same `protocol/hid.c` functions underneath. The Home page's Compatibility section shows what's actually been tested on your exact model — green means confirmed, yellow means "should work, nobody's checked."

Closing the window doesn't quit the app — it minimizes to a tray icon showing the battery level, with a menu to reopen the window, refresh, or quit for real. Needs a tray host to actually show up; on GNOME that means the [AppIndicator extension](https://extensions.gnome.org/extension/615/appindicator-support/).

The button page has a small generic mouse diagram — it's not a drawing of your specific model, just a rough layout to click through the 5 configurable button slots quickly.

## Architecture

```
Ajazzy/
├── cli/                  ajazzyctl -- the command-line tool
├── gui/
│   ├── main.c                the GTK4 + libadwaita front-end, ajazzy-gui
│   └── tray.c / tray.h       system tray icon (StatusNotifierItem + dbusmenu, no library)
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

Short version: the installer's `config.xml` (pulled out with `innoextract`, no need to run it) gave the VID/PID table for device detection. Everything about the actual protocol — the checksum, every command family, the button/key table layout — came from capturing real USB traffic with Wireshark and USBPcap on a Windows VM while operating every control in the original app (DPI, RGB, report rate, button remapping) and diffing the resulting packets. The full walkthrough is in [`docs/reverse-engineering.md`](docs/reverse-engineering.md).

## License

GPL-3.0 — see [`LICENSE`](LICENSE).
