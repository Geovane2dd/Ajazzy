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
| `0x14` | Per-DPI-stage indicator colour table. Reply is `opcode/3` raw RGB triples back to back, no header. A live reply decoded to exactly 6 colours — red, green, blue, cyan, yellow, magenta — one per DPI stage, matching the mouse's actual behaviour (LED colour follows the active stage). Matches the firmware's own debug strings `cmd get dpi color` / `set dpi color`. The SET side for this table hasn't been captured yet. |
| `0x15` | RGB lighting effect state — see "Report rate and lighting readback" below |
| `0x16` | Unidentified |
| `0x17` | Sensor/lift-off settings |
| `0x20` | Battery status — see the battery section below |

### SET commands

| Family | Opcode | Meaning |
|---|---|---|
| `0x02` | `0x01` | Report rate. `payload[0]` = polling interval in milliseconds (8/4/2/1 for 125/250/500/1000 Hz). Does not touch DPI stage. |
| `0x03` | `0x25` | Full DPI table rewrite, active stage included (see below) — this is also how active DPI stage is switched, there's no separate "just switch stage" command |
| `0x05` | `0x02` | All-zero payload — turns lighting off |
| `0x05` | `0x05` | Constant light: `payload[0]=0x03` (constant), `payload[1] = (brightness << 4) \| 0x02` (brightness 0-4), `payload[2]=0xff` (constant) |
| `0x05` | `0x18` | RGB rainbow effect: `payload[0]=0x02`, `payload[1] = (brightness << 4) \| speed` (both 0-4), `payload[2]=0x07` (7 colours follow), then 7×3 bytes of RGB (fixed palette: red/green/blue/cyan/yellow/magenta/white) |
| `0x07` | `0x04` | Sensor: `payload[0]=0x06` (constant), `payload[1]`=lift-off level, `payload[2]`=turn light off when idle, `payload[3]=0x08` (constant) |
| `0x08` | length | Macro content (see below) |
| `0x09` | `0x0f` | Button/key mapping table (see below) |

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

Round-tripped on real hardware: write a value, read it back, matches — across DPI, report rate, and lift-off.

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

Manually constructing a family `0x02` write with a small `payload[0]` value does move the device's active DPI stage as a side effect — confirmed live: setting report rate to 500 Hz (`payload[0]=2`) changed query `0x12`'s reply from 1 to 2. `ajazz_build_set_dpi_table()` avoids this entirely by using family `0x03` for DPI stage, matching what the real driver does.

## Macros

Macros are stored on the device, not played back by the Windows app in software.

**Source of the semantic model:** the original driver's local SQLite database (`%LOCALAPPDATA%`-style config folder, `mouse_<model>_data.db`). Its `t_macrorecord` table stores each macro as an ordered sequence of typed events — `type` 2/3 = keyboard key down/up, 4/5 = mouse button down/up, 1 = delay, each with a `value` (ASCII code for keys, a per-button bit flag for mouse buttons). `t_key_macro_data` links a button to a `macro_id`, though that mapping is internal to the driver's bookkeeping, not reflected byte-for-byte on the wire (see below).

**Captures used to confirm the wire format:** a macro with a single mouse button, a single keyboard key, two keyboard keys in sequence, a delay-only macro, and a capture of resetting a button to factory default then assigning it a macro for the first time.

### Macro content — family `0x08`

The opcode byte here is the number of payload bytes actually used, not a fixed sub-command id. `payload[0]` is a constant `0x03`, followed by up to six 4-byte steps: `[kind, delay_ms, 0x00, value]`.

| kind | meaning |
|---|---|
| `0x10` | mouse button pressed |
| `0x90` | mouse button released (`0x10 \| 0x80`) |
| `0x30` | keyboard key pressed |
| `0xb0` | keyboard key released (`0x30 \| 0x80`) |

`delay_ms` is the pause in milliseconds after that step (confirmed with both `10` and `20` in different captures). `value` is the same per-button bit flag as the key table (`1`=button1, `2`=button2, `4`=button3, ...) for mouse steps, or a USB HID Keyboard Page usage code for key steps.

Every macro follows a down → delay → up → delay shape per action. A delay-only macro produces just the constant `0x03` byte and nothing else — there's no way to represent a standalone delay with no action attached.

Capacity: 27-byte payload = 1 constant byte + up to 6 four-byte steps = 3 press+release actions per macro in one report. One capture of a longer macro showed a different header shape (`mode` `0x02` then `0x12`, with the tail of one packet's payload reappearing at the start of the next) suggesting a chunked/continuation transfer, but two data points aren't enough to pin the addressing scheme down. `AJAZZ_MACRO_MAX_STEPS` reflects the single-packet limit.

### Assigning a macro to a button

A second write to the key table marks a slot's marker as `0x90` with a constant trailing `[0x23, 0x01]`. That pair never varied across six different macros in six captures — including the reset→assign capture, where the only thing that changed was the marker going from `0x10` to `0x90`. The device plays back whichever macro content was most recently written for that button, rather than looking one up by id. The macro-content write happens before the key-table write that flips the button into macro mode.

## The battery query

Query `0x20` returns `[level, mode, 0, 0, ...]`. Not a calibrated percentage — the Windows UI shows a 4-bar icon, not a number. `ajazz_battery_label()` buckets the raw byte into four tiers; only the top one (`>=25` = 4/4, confirmed against raw `0x21`/33 while fully charged) is anchored to a real data point, the other three boundaries are an evenly-spaced guess. Least-confident part of the protocol.

## Report rate and lighting readback

**`0x12`** is report rate. Confirmed by setting 125, 500, then 1000 Hz (with a ~1 second pause before each read) and getting back `payload[0]` = `0x08`, `0x02`, `0x01` respectively — the same interval-in-ms encoding the SET command uses. Reading immediately after the SET's ack can catch the pre-update value; a short delay matters. No dedicated "get active DPI stage" query exists — read that from `AJAZZ_QUERY_DPI_TABLE`'s payload (`ajazz_dpi_table_active_stage()`).

**`0x15`** is the RGB lighting effect state. Confirmed by cycling off → rainbow → constant → off and reading after each change: the reply's opcode byte is `0x01` for off, `0x05` for constant, `0x18` for rainbow, matching `AJAZZ_LED_OFF`/`CONSTANT`/`RAINBOW`. Constant colour's brightness was set and read back at all 5 levels and matched every time; rainbow's brightness and speed the same way. Rainbow's payload mirrors the SET command's shape. Off's reply opcode (`0x01`) doesn't match the "off" SET command's own opcode (`0x02`) the way constant and rainbow's do — no explanation, that's just what the device sends.

**Timing:** this query can report the previous effect for a couple of seconds after switching to a different effect *type* (off→rainbow, rainbow→constant). A 1 second pause is enough for a parameter change within the same effect (e.g. constant colour's brightness), but an effect-type switch can need up to ~3 seconds. `0x12` shows the same pattern, so this may be a general "give the firmware a moment after any SET" rule rather than something lighting-specific. Neither `cmd_rgb_get()`/`refresh_lighting()` nor `cmd_rate_get()`/`refresh_config()` retry or wait for this — reading right after applying a change in the GUI can show stale data briefly.

## What's still unknown

- **Macros longer than 3 actions.** See the chunked-transfer hint under "Macros" above — not enough data to implement.
- **RGB effect selection between "breathing" and "rainbow".** Both produce the identical `0x05`/`0x18` command shape, only brightness/speed differ. Either there's a real "effect id" byte not yet isolated, or the two effect names don't correspond to distinct wire commands.
- **The "reacts to movement" lighting toggle.** A capture of this reused the same family `0x07`/opcode `0x04` sensor command as lift-off distance, landing on the same byte (`payload[1]`) lift-off level uses. Not implemented since writing it might corrupt the lift-off setting — needs a capture that isolates this control while lift-off is held constant.
- **The per-DPI-stage indicator colour SET command.** Query `0x14`'s reply format is understood, but the write side hasn't been captured.
- **Query `0x16`.** Not pinned down. Possibly "cmd get Sensor advanced par" going by the firmware's debug-string order — unchecked.
- **Sensor command's `payload[2]`.** `protocol/hid.c` accepts a byte for this slot; nothing else is known to go there.
