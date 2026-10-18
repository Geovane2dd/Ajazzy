# Mouse pictures

Product photos of the supported mice, taken from the official AJAZZ Windows
driver package (`skins/theme1/device/`). They are only used to show which mouse
is being configured; they remain the property of their owner (AJAZZ) and are
not covered by this project's GPL licence. Replace or remove them if that is a
problem for your distribution.

| file | used for |
|---|---|
| `mouse_aj179.png` | AJ179, AJ179P MC, AJ179 V2, AJ179 V2 MAX (closest available picture for the V2 models) |
| `mouse_aj139pro.png` | AJ139 PRO, AJ139 V2 PRO |
| `mouse_aj159.png` | AJ159, AJ159 MC, AJ159P MC |
| `mouse_generic.png` | anything else |

To add a picture for a model: drop a PNG here, extend `ajazz_lookup_image()` in
`devices/registry.c`, and add the button positions to `IMAGES[]` in
`gui/page_buttons.c`.
