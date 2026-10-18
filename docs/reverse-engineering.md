# Reverse engineering the AJAZZ protocol

A record of the wire protocol, for anyone extending this project or double-checking a claim. Written against the AJAZZ Driver (X) v1.0.7.3 Windows installer and an AJ179 V2 MAX (dongle `249A:5C2F`, dev_id `N625`).

## Method: capture real traffic, then diff it

Pass the dongle through to a Windows VM, run the original AJAZZ driver, and capture with Wireshark + USBPcap while operating every control in the UI — DPI changes, RGB effects, button remaps, report rate — one action at a time with a few seconds of gap so packets can be matched to actions by timestamp.

The installer's `app/config.xml` (extracted with `innoextract`, no need to run the installer) lists every supported mouse model with its VID/PID/mode/dev_id combinations — that's where `devices/*.h` comes from. Everything about the wire protocol below comes from captured USB packets, not from the driver binary.

Filtering `usb.bus_id==<n> && usb.device_address==<addr>` down to the mouse's USB address, most traffic is standard descriptor requests (enumeration, ignore) or high-frequency movement reports on endpoint `0x81` (ignore). The interesting traffic:

- **endpoint `0x05` (interrupt OUT)** — commands sent to the mouse
- **endpoint `0x84` (interrupt IN)** — the mouse's responses/acks

Both carry 32-byte reports.

## Wire format

```
byte 0      family    which subsystem this command is for
byte 1      0x00      always zero
byte 2      mode      0x01 for a SET command, 0x00 for a GET query
byte 3      opcode    sub-command within the family
byte 4-30   payload   27 bytes, meaning depends on family/opcode
byte 31     checksum  sum(byte4..byte30) & 0xFF
```

The checksum holds on every captured packet, including all-zero-payload ones (checksum `0x00`).

### GET queries

Sent as `[queryId, 0x00, 0x00, 0x00, 0, 0, ...]`. The device replies with the same `queryId` in byte 0, `mode=0x01`, the opcode byte set to how many payload bytes are actually meaningful (same convention family `0x08`/macro uses on the SET side), and the data in the payload.

| Query ID | Contents |
|---|---|
| `0x10` | Device info: 4 ASCII bytes = dev_id (e.g. `"N625"`), then version bytes |
| `0x11` | Button/key mapping table (see below) |
| `0x12` | Report rate — see "Report rate and lighting readback" below |
| `0x13` | DPI value table, active stage included (see below) |
| `0x14` | Per-DPI-stage indicator colour table. Reply is `opcode/3` raw RGB triples back to back, no header. A live reply decoded to exactly 6 colours — red, green, blue, cyan, yellow, magenta — one per DPI stage, matching the mouse's actual behaviour (LED colour follows the active stage). Matches the firmware's own debug strings `cmd get dpi color` / `set dpi color`. The SET side for this table has never been seen on the wire, but see the family `0x04` section below for a decompiled reconstruction. |
| `0x15` | RGB lighting effect state — see "Report rate and lighting readback" below |
| `0x16` | Sensor performance (pairs with SET family `0x06`) |
| `0x17` | Power/buttons (pairs with SET family `0x07`) |
| `0x20` | Battery status — see the battery section below |

### SET commands

| Family | Opcode | Meaning |
|---|---|---|
| `0x02` | `0x01` | Report rate. `payload[0]` = polling interval in milliseconds (8/4/2/1 for 125/250/500/1000 Hz). Does not touch DPI stage. |
| `0x03` | `0x25` | Full DPI table rewrite, active stage included (see below) — this is also how active DPI stage is switched, there's no separate "just switch stage" command |
| `0x04` | `0x12` | Per-DPI-stage indicator LED colour, all 6 stages: 18 bytes = 6×`{R,G,B}` triples in stage order — see below |
| `0x05` | `0x02`/`0x03`/`0x05`/`0x18` | Lighting. `payload[0]` is the **mode** (0 off, 1 flow, 2 breathing, 3 steady, 4 neon, 5 wave), `payload[1] = (brightness << 4) \| speed`; steady adds RGB at `payload[2..4]`, breathing adds a count (7) and 7×RGB. See "Lighting modes" below — the earlier reading of these as constant bytes was wrong |
| `0x06` | `0x05` | Sensor performance: `[lift-off 1\|2 mm, angle snapping, ripple control, motion sync]` |
| `0x07` | `0x04` | Power/buttons: `[sleep (×10 s), wake on movement, light off when idle, button debounce ms]` — **not** lift-off; see below |
| `0x08` | length | Macro content (see below) |
| `0x09` | `0x0f` | Button/key mapping table (see below) |

## Corrections from decompiling the official driver (1.0.1.4)

The Windows driver's `AJAZZ Driver (X).exe` was decompiled headlessly with Ghidra (all ~5,500 functions dumped, call graph walked up from the one `WriteFile` wrapper that sends reports). That recovered the exact byte layout of every setter and showed that several early readings of captures were off. Each item below was then **confirmed on a real AJ179 V2 MAX by writing it and reading it back** (`ajazzyctl`, `make test` locks the bytes).

Report buffers in the driver carry a leading report-id byte; everything here is in the 32-byte report layout used on the wire.

### Lighting modes (family `0x05`)

`payload = [mode, (brightness << 4) | speed, ...]`

| mode | opcode | extra payload | official name |
|---|---|---|---|
| 0 | `0x02` | — | off (reads back with opcode `0x01`) |
| 1 | `0x03` | — | Flowing light (流光) |
| 2 | `0x18` | `[7, 7×RGB]` | Breathing (呼吸) — cycles through the colours with a breathing fade |
| 3 | `0x05` | `[R, G, B]` | Constant / steady (常亮) |
| 4 | `0x02` | — | Neon (霓虹) |
| 5 | `0x02` | — | Colourful wave (七彩波浪) |

So the old "rainbow" command is breathing with the default 7-colour palette, and "constant" has always carried a colour (`03 42 ff 00 00` is red) — the earlier `0xff` was the red channel, not a marker. Reading query `0x15` back returns the same layout (`payload[0]` = mode). Which modes a given model exposes is listed per device in the installer's `device/mouse_*.xml` (`<light_N enable=…>`): the AJ139 PRO, AJ159, AJ159 MC and AJ179 files all enable only breathing (2), steady (3) and off — flowing, neon and wave have `enable="0"`. The firmware stores modes 1/4/5 and reads them back, but on a real AJ179 V2 MAX they produce no visible effect, so Ajazzy hides them (`ajazz_lookup_light_modes()`).

### Sensor and power

- Family `0x07` opcode `0x04` (query `0x17`): `[sleep, move_wakeup, move_closelight, button_respondtime]`. Byte 1 is **wake on movement**, byte 2 **light off when idle**, byte 3 the **button debounce in ms** (factory 8) — not the constant `0x08` it was taken for. Byte 1 was previously called "lift-off", which is why that control seemed to do nothing physical: lift-off lives elsewhere.
- Family `0x06` opcode `0x05` (query `0x16`): `[lift_off 1|2 mm, angle_snap, ripple_control, motion_sync]` (flag names from the driver's own checkboxes; the installer's string table calls them Angle Snap, Ripple Control and Motion Sync).

### DPI table low nibble

`payload[0] = (dpi_index << 4) | dpi_count` — the low nibble is the driver's "DPI count" setting, not a constant. A freshly reset V2 MAX reports 5 even though six stage values are stored. It round-trips (written 6, 3, 5 and read back each time); whether it really limits how many stages the DPI button cycles through needs a physical check.

### Button table encodings

Slot bytes `[marker, a, b]` (nine slots; slots 0-5 are the five buttons and the DPI button). From the driver's button-table writer:

| bytes | function |
|---|---|
| `10 01/04/02/08/10 00` | left / middle / right / back / forward |
| `10 00 f5` / `10 00 f6` | scroll left / right (unverified) |
| `20 01 00` / `20 ff 00` | scroll up / down (unverified) |
| `30 32 03` | double click (unverified); `30 interval count` is rapid-fire |
| `40 01/02/03 00` | DPI cycle / up / down (only cycle is the factory default; up/down unverified) |
| `60 00 00` | button disabled |
| `70 mods key` | keyboard shortcut (HID usage); modifier-only is `70 mod 00` |
| `80 lo hi` | consumer-control usage: play/pause `cd`, stop `b7`, prev `b6`, next `b5`, vol+ `e9`, vol− `ea`, mute `e2`, brightness `6f`/`70`, calculator `92 01`, my computer `94 01`, mail `8a 01`, browser home `23 02` |
| `70 08 07` / `70 08 2b` | Win+D / Win+Tab |
| `70 01 06` / `19` / `1b` | Ctrl+C / V / X |
| `90 idx\|flags count` | macro, see below |

### Macros

- The content stream starts with the **button index** (`[key_index, step…]`), not a constant `0x03` — the early captures were all button 4 (index 3).
- The button-table entry is `[0x90, key_index | flags, count]` with flags `0x10` repeat while held, `0x40` repeat until any click, `0x20` repeat `count` times; none = play once. The old constant `23 01` is "button 4, repeat 1 time". Verified by writing `90 24 01` and `90 14 00` to button 5 and reading them back.
- Modifier keys are their own step kinds: `0x20` down / `0xa0` up, value = HID modifier bit. Two keys (`[` / `]` style GUI keys, VK `0x5b/0x5d`) take the same path.
- A step with no recorded delay is written with 1 ms.

### Queries on a sleeping or just-woken mouse

The dongle answers the identity query (`0x10`) from its own cache even when the mouse is asleep; every other query then goes unanswered. Right after the mouse wakes it answers with the right family but opcode `0` and an all-zero payload, which looks like real data (0 DPI, 0 s sleep timer). `ajazz_query()` therefore retries empty replies a few times. The original driver also sends a one-byte "prepare" write (family `0x0d` before `0x12`, family `0x0e` before `0x15`) with a 100 ms wait; the retry makes that unnecessary on the V2 MAX but it is the likely reason for the "stale read after a SET" behaviour documented below.

## The DPI table

Family `0x03` opcode `0x25` (and the matching `0x13` GET query) use this layout for the 27-byte payload:

```
payload[0]      flags: high nibble = active stage (0-based), low nibble = 5
                (constant in every capture seen so far)
payload[1..24]  6 stages x 4 bytes: (X_lo, X_hi, Y_lo, Y_hi), each a 16-bit LE value.
                Actual DPI = value * 100. X and Y are always identical (no
                separate X/Y sensitivity was ever configured), so `protocol/hid.c`
                writes the same value to both.
payload[25..26] unused, always zero
```

Confirmed with a capture of switching active stage from 1 to 5: five family `0x03` writes with `payload[0]` = `0x05, 0x15, 0x25, 0x35, 0x45` in order, high nibble tracking the stage. None of those writes touched family `0x02`.

Round-tripped on real hardware: write a value, read it back, matches — across DPI, report rate, and lift-off. **Superseded:** the next paragraphs describe how the lift-off/wake-on-motion mix-up looked at the time. The corrected layout is in "Corrections from decompiling the official driver" above. The mouse genuinely accepts and stores the byte -- it's just not a control the official app exposes for this model, so treat it as an extra capability this project surfaces rather than a like-for-like port of an official feature.

Re-validated against real hardware in this session via `ajazzyctl`: `rate set 500` → `rate get` reads back `500`, `rate set 1000` → `1000`; `liftoff set 1` and `liftoff set 2` are both correctly reflected by a subsequent `power get`. Report rate and lift-off both round-trip cleanly at the protocol level -- no bug in either.

That protocol-level round-trip does **not** mean lift-off has a real physical effect, though. Tested directly on hardware: set level 1, slowly lift the mouse off the desk while watching the on-screen cursor, note the height where it stops tracking; set level 2, repeat. No visible difference between the two -- the cursor stops tracking at the same height either way. Combined with the official app never exposing this control for this model (see above), the working theory is that the byte is genuinely stored by the firmware but isn't wired up to the sensor's actual cutoff height on the AJ179 V2 MAX -- or it's wired to something else entirely (the "wake mouse on motion" possibility already noted in `protocol/hid.h`) rather than lift-off distance. The GUI/CLI still expose the control since the mouse does accept and persist the value, but shouldn't claim it visibly changes tracking behaviour on this model.

## Factory reset (family `0x0f`)

The official driver's "Restore Factory Settings" button, `DeviceMainDlg::SetMouseRestoreFactoryData`, builds (for `this->m_device_type < 0xc9`, the AJ179 V2 MAX's branch) a single 32-byte report by hand:

```
byte 0   0x0f     family
byte 1   0x00     always zero
byte 2   0x01     mode = SET
byte 3   0x01     opcode = 1 (payload byte count)
byte 4   0xff     payload[0]
byte 5..30        always zero
```

This command has never turned up in a capture (the same caveat as the DPI colour SET above); it's reconstructed purely from decompiling the function. The individual per-section "reset to default" buttons (`SetMouseResetDPI`/`SetMouseResetLight`/`SetMouseResetReport`) are local-only -- they just reload values from the driver's own SQLite config and reapply them through the per-section SET commands already documented above (family `0x03` for DPI, etc.). The single-command factory reset is the one genuinely new wire command among them.

## Setting the per-DPI-stage indicator colour (family `0x04`)

Unlike everything else in this document, this side of the command has never turned up in a USB capture. It comes from decompiling `AJAZZ Driver (X).exe` with Ghidra: the installer ships full PDB debug symbols for both the main EXE and `MUI.dll`, so function and variable names came straight from Microsoft's own symbol format rather than blind disassembly. It's confirmed against real hardware, though: `dpicolor set` changes the colour, `dpicolor list` reads the new value back, and the mouse's actual indicator LED changes too.

**Unreliable over the 2.4G dongle interface specifically.** With the mouse's charge cable plugged in and its 2.4G dongle still paired, both a `USB`-mode and a `2.4G`-mode hidraw node exist at once. A SET sent over the dongle node gets acked (`ajazz_send_and_wait` returns success) but doesn't stick -- reading query `0x14` back afterward, even a second later, comes back all-zero every time. The same SET/GET round-trips correctly and immediately over the `USB` node. That's why `ajazz_open()` prefers a `"USB"` candidate over a `"2.4G"` one when both are present (see `protocol/transport.c`). The tradeoff is a battery reading that runs a bit high while charging over USB, but that's already surfaced via `dev.charging`/the "(charging)" label rather than silently wrong -- general command reliability matters more.

`DeviceMainDlg::SetMouseDPIColorData` is the function behind the DPI colour picker UI. For `this->m_device_type < 0xc9` (which should cover the AJ179 V2 MAX, since every other command it uses matches this driver's `DriverComm::SetDeviceData` interrupt-report path rather than the `SetFeatureReport` path the `else` branch takes for other device families), it builds a 32-byte report by hand:

```
byte 0   0x04     family
byte 1   0x00     always zero
byte 2   0x01     mode = SET
byte 3   0x12     opcode = 18 (payload byte count: 6 stages * 3)
byte 4.. 18 bytes: 6 back-to-back {R, G, B} triples, one per DPI stage,
         pulled from the driver's per-profile SQLite config
         (columns dpi_group0_rgb .. dpi_group5_rgb, decoded from a
         packed Windows COLORREF integer: R = value & 0xFF,
         G = (value >> 8) & 0xFF, B = (value >> 16) & 0xFF)
```

This is exactly the mirror image of the `0x14` GET reply's shape. Implemented as `AJAZZ_FAM_DPI_COLOR` / `ajazz_build_set_dpi_colors()`, exposed as `ajazzyctl dpicolor set <1-6> <RRGGBB>`.

## The button/key mapping table

Family `0x09` opcode `0x0f` (SET) / query `0x11` (GET). The 27-byte payload is nine 3-byte slots, `[marker, byte1, byte2]`:

| Marker | Meaning |
|---|---|
| `0x10` | Native/default function. `byte1` = `1 << slot_index` (a per-button bit flag), `byte2=0x00` |
| `0x80` | Consumer control remap. `byte1 \| (byte2 << 8)` is a 16-bit LE standard USB HID Consumer Page usage code — e.g. `0x00E9` Volume Increment, `0x00EA` Volume Decrement, `0x00E2` Mute, `0x0223` AC Home |
| `0x70` | Keyboard key remap. `byte1` = modifier bitmask (standard USB HID keyboard report modifiers: bit0 LCtrl, bit1 LShift, ...), `byte2` = USB HID Keyboard Page usage code (e.g. `0x04` = 'A') |
| `0x30` | Rapid-fire/auto-click. `byte1` = interval (confirmed range 5-255), `byte2` = a repeat count (0-255; exact edge-case semantics not independently verified) |
| `0x90` | Macro trigger, see "Macros" below |

Only the first five slots (button 1-5) ever change; slots 6+ stay at a constant `[0x40, 0x01, 0x00]`, so `AJAZZ_KEY_SLOTS` is 5 and that trailing data is preserved byte-for-byte by read-modify-write rather than interpreted.

**Verifying a reset command:** the Windows driver's own UI reads from its local SQLite database, not the device, so it can show a stale mapping after a reset issued from outside that app even though the mouse itself really did reset. Check physical button behaviour, not what another piece of software displays.

## DPI stage and report rate

Independent commands. Report rate (family `0x02`/opcode `0x01`, `payload[0]` = interval in ms) — confirmed with a capture of setting 125→250→500→1000 Hz in order, nothing else touched. DPI stage (family `0x03`/opcode `0x25`, full table rewrite) — confirmed with a capture of switching stage low-to-high, report rate untouched, every frame family `0x03`.

Manually constructing a family `0x02` write with a small `payload[0]` value also moves the device's active DPI stage as a side effect: setting report rate to 500 Hz (`payload[0]=2`) changes query `0x12`'s reply from 1 to 2. `ajazz_build_set_dpi_table()` avoids this entirely by using family `0x03` for DPI stage, matching what the real driver does.

## Macros

Macros are stored on the device, not played back by the Windows app in software.

**Source of the semantic model:** the original driver's local SQLite database (`%LOCALAPPDATA%`-style config folder, `mouse_<model>_data.db`). Its `t_macrorecord` table stores each macro as an ordered sequence of typed events — `type` 2/3 = keyboard key down/up, 4/5 = mouse button down/up, 1 = delay, each with a `value` (ASCII code for keys, a per-button bit flag for mouse buttons). `t_key_macro_data` links a button to a `macro_id`, though that mapping is internal to the driver's bookkeeping, not reflected byte-for-byte on the wire (see below).

**Captures used to confirm the wire format:** a macro with a single mouse button, a single keyboard key, two keyboard keys in sequence, a delay-only macro, and a capture of resetting a button to factory default then assigning it a macro for the first time.

### Macro content — family `0x08`

The opcode byte here is the number of payload bytes actually used, not a fixed sub-command id. `payload[0]` is a constant `0x03`, followed by up to six 4-byte steps: `[kind, delay_lo, delay_hi, value]`.

| kind | meaning |
|---|---|
| `0x10` | mouse button pressed |
| `0x90` | mouse button released (`0x10 \| 0x80`) |
| `0x30` | keyboard key pressed |
| `0xb0` | keyboard key released (`0x30 \| 0x80`) |

`delay_lo`/`delay_hi` together are a **16-bit little-endian** pause in milliseconds after that step (0-65535ms), not a 1-byte delay plus a constant byte -- every capture so far happened to use delays under 256ms (`10`/`20`), so the high byte always read `0x00` and looked constant until decompiling the official driver's `DeviceMainDlg::GetMouseMacroData` (see below) showed it filling both bytes from a 16-bit value. `value` is the same per-button bit flag as the key table (`1`=button1, `2`=button2, `4`=button3, ...) for mouse steps, or a USB HID Keyboard Page usage code for key steps.

Every macro follows a down → delay → up → delay shape per action. A delay-only macro produces just the constant `0x03` byte and nothing else — there's no way to represent a standalone delay with no action attached.

Capacity: 27-byte payload = 1 constant byte + up to 6 four-byte steps = 3 press+release actions per macro in one report. Longer macros use a chunked/continuation transfer — see the next section.

### Macro content, chunked (macros over 3 actions)

Reconstructed by decompiling `DeviceMainDlg::SetMouseKeyMacroData(int, int)` (the 2-argument overload, distinct from the no-argument one that rewrites the whole key table) and the `DeviceMainDlg::GetMouseMacroData` helper it calls to serialize a macro's steps into bytes. Not independently confirmed against a wire capture past 2 chunks, but this is straight from the vendor's own compiled logic rather than extrapolation.

The full content is one byte stream: `[0x03, step0, step1, ...]` (steps in the 4-byte shape above). That stream is split into `AJAZZ_PAYLOAD_LEN`-byte (27) pieces **without** aligning to step boundaries — a step's bytes can straddle a chunk boundary, which is why an earlier capture showed "the tail of one packet's payload reappearing at the start of the next". Each chunk is sent as its own family `0x08` report:

```
mode    (chunk_index << 4) | total_chunks   -- both counted so a 2-chunk
                                                transfer sends mode 0x02 then 0x12
opcode  this chunk's byte count (27 for every chunk but the last)
payload this chunk's slice of the byte stream
```

Both nibbles cap at 15, so the wire format tops out at 15 chunks (`AJAZZ_MACRO_MAX_CHUNKS`) — `AJAZZ_MACRO_MAX_CHUNKED_STEPS` in `protocol/hid.h` is derived from that. Whether the mouse's onboard macro memory actually holds a macro that long is untested; a device-side limit below the wire-format ceiling would show up as the mouse rejecting or truncating playback, not as a bug in the write itself.

### Assigning a macro to a button

A second write to the key table marks a slot's marker as `0x90` with a constant trailing `[0x23, 0x01]`. That pair never varied across six different macros in six captures — including the reset→assign capture, where the only thing that changed was the marker going from `0x10` to `0x90`. The device plays back whichever macro content was most recently written for that button, rather than looking one up by id. The macro-content write happens before the key-table write that flips the button into macro mode.

## The battery query

Query `0x20` is unreliable: captures of the official driver's own traffic show it coming back either stuck on a stale value or with opcode `0x00` (zero meaningful payload bytes, i.e. no data). Don't use it.

The real, live battery level lives at `AJAZZ_BATTERY_PAYLOAD_OFFSET` (`payload[9]`, wire byte 13) of the `0x10` device info reply, which this driver already queries anyway. That byte tracks the mouse's own unsolicited status push in lockstep -- a `c0 <flag> <raw> 00 ...` report the mouse sends unprompted over the interrupt-in endpoint on dongle reconnect and on charge-cable plug/unplug. `<raw>` at byte 2 of that push and `payload[9]` of the following `0x10` reply always match.

`<raw>` is a plain **0-100 percentage**, not some device-specific coded scale. That's confirmed by decompiling the official Windows driver itself with Ghidra, using the debug PDBs shipped next to `AJAZZ Driver (X).exe` and `MUI.dll`. The main EXE's `DeviceMainDlg::GetMouseStatusData` forwards the raw byte from the `0xc0` push straight into `DUI::BatteryCtrl::SetBatteryInfo(raw, charge_flag)` with zero conversion. The actual bar count is decided later, in `DUI::BatteryCtrl::DrawControl` (`MUI.dll`, offset `0x1f9b0`), which compares `this->m_battery_value` against `0x19`/`0x32`/`0x4b` (25/50/75) in even quarters:

```
raw >= 75           -> 4/4 bars
50 <= raw < 75       -> 3/4 bars
25 <= raw < 50       -> 2/4 bars
raw < 25              -> 1/4 bar (drawn in red -- there's no distinct "empty" tier)
```

`ajazz_battery_label()` and the GUI's `battery_tier()` implement exactly this. `charge_flag` (the byte right after `raw` in the `0xc0` push) separately drives a small plug/charging icon overlay in `DrawControl` -- it doesn't change which quarter is lit.

## Report rate and lighting readback

**`0x12`** is report rate. Setting 125, 500, then 1000 Hz (with a ~1 second pause before each read) gets back `payload[0]` = `0x08`, `0x02`, `0x01` respectively, the same interval-in-ms encoding the SET command uses. Reading immediately after the SET's ack can catch the pre-update value, so a short delay matters. No dedicated "get active DPI stage" query exists -- read that from `AJAZZ_QUERY_DPI_TABLE`'s payload (`ajazz_dpi_table_active_stage()`).

**`0x15`** is the RGB lighting effect state. Cycling off → rainbow → constant → off and reading after each change gives a stable opcode per effect: `0x01` for off, `0x05` for constant, `0x18` for rainbow, matching `AJAZZ_LED_OFF`/`CONSTANT`/`RAINBOW`. Constant colour's brightness reads back correctly at all 5 levels, and rainbow's brightness and speed do too. Rainbow's payload mirrors the SET command's shape. Off's reply opcode (`0x01`) doesn't match the "off" SET command's own opcode (`0x02`) the way constant and rainbow's do -- no explanation for that, it's simply what the device sends.

**Timing:** this query can report the previous effect for a couple of seconds after switching to a different effect *type* (off→rainbow, rainbow→constant). A 1 second pause is enough for a parameter change within the same effect (e.g. constant colour's brightness), but an effect-type switch can need up to ~3 seconds. `0x12` shows the same pattern, so this may be a general "give the firmware a moment after any SET" rule rather than something lighting-specific. Neither `cmd_rgb_get()`/`refresh_lighting()` nor `cmd_rate_get()`/`refresh_config()` retry or wait for this — reading right after applying a change in the GUI can show stale data briefly.

## What's still unknown

- **Flowing / neon / colour wave.** Exact wire format, round-trips, but no visible effect on an AJ179 V2 MAX; presumably meant for other (RGB-strip) models.
- **Whether the DPI count nibble limits the DPI button's cycle**, and the buttons marked *beta* in the GUI (scroll left/right/up/down, double click, DPI up/down, system shortcuts): encodings come from the driver, not from pressing the buttons.
- **The firmware's own macro limit.** The wire format allows 101 steps (15 chunks); a smaller on-device limit would show as truncated playback.
- **Models other than the AJ179 V2 MAX.** The protocol is shared; each model's `device/mouse_*.xml` in the installer lists which lighting modes and features are enabled.
