# Link: mirrored keypress dashboards and matrix rain

Custom displays for the wired CannonKeys Link, with VIA remapping retained.
Use `feature/link-oled` in both the firmware and userspace repositories.

## Dashboard

Both displays use portrait 64x128 drawing, independent of which half has USB.
At the top, each screen shows only its own physical half: finger columns,
thumb cluster and encoder push, mirrored between left and right. Pressed keys
fill in. Short presses remain highlighted for at least 100 ms so quick taps are
visible through screen refresh and split synchronization.

The physical left screen uses the remaining area for large indicators:

- **CTRL:** either Control modifier is active.
- **CAPS:** host Caps Lock XOR either Shift, matching the WT75-C indicator.
- **ALT:** either Alt modifier is active.

Active panels are filled with inverted text. Weak and one-shot modifiers are
included. These indicators stay on the physical left, even if USB is on the right.

The physical right screen shows **LAYER** and a large numeric active layer.
It reads QMK's effective layer state, including default layers. Your
`LT(1,KC_SPC)` and `LT(2,KC_SPC)` keys therefore show layers 1 and 2 when QMK
actually resolves them as holds. No guessed QWERTY/Colemak/Raise/Lower names are
displayed. If multiple layers are active, the highest active layer is shown.

See the [dashboard preview](../../../../../previews/link/dashboard.png), generated
from the same renderer used by the firmware.

## Right encoder push and idle behavior

Press the physical right encoder to cycle both screens together:

**Dashboard → Matrix rain → Off → Dashboard**

The button is electrical matrix **[9,5]**, the last slot of each VIA layer; it is
a normal key switch, separate from the rotation mappings. It is reserved for
screen control, so the former Play/Pause action is consumed even if VIA's current
EEPROM map still contains that action. Left encoder push and both encoders'
rotation actions remain as configured by the board/keymap.

The compiled map names this action `LINK_DISPLAY` (`QK_USER`, **0x7E40**). VIA
may show the raw value. It can also be assigned to another key using VIA's Any
keycode entry. Remapping the physical right encoder does not bypass its reserved
screen-control behavior in this edition.

- Dashboard automatically changes to rain after **60 seconds** without matrix
  activity or encoder rotation, matching QMK's usual OLED timeout interval.
- Holding a key, including either SpaceFN key, prevents automatic idle entry.
- A key press/release or encoder rotation returns automatic rain to the dashboard.
- Manually selected rain stays selected while typing until the next mode press.
- From automatic rain, the mode button advances directly to Off.
- Manual Off powers down both OLED controllers and stays off while typing.
  The next right-encoder press returns to Dashboard.
- Mode selection starts at Dashboard after a reboot; it is not written to EEPROM.

Rain uses falling monochrome pixel streams with dithered tails and different
patterns on the two physical halves. The old automatic OLED power-off timeout is
disabled; the custom idle timer chooses rain instead. See the
[animated preview](../../../../../previews/link/rain.gif).

## Saved layout and VIA

`link.layout.json` is your original six-layer VIA export and is preserved
unchanged. All 360 matrix entries are imported into `layout.inc`, with one
documented change: right encoder push [9,5] becomes `LINK_DISPLAY` on every layer.
The SpaceFN layer-tap definitions and all other keys are retained.

After updating the saved export, regenerate/check the compiled defaults from the
userspace root:

```sh
python keyboards/cannonkeys/link/keymaps/svenge/import_layout.py
python keyboards/cannonkeys/link/keymaps/svenge/import_layout.py --check
```

VIA's existing stored keymap takes precedence over compiled defaults. No EEPROM
reset is required just to use the new screens or the reserved right-encoder
button. If you deliberately want to load the saved layout as fresh defaults,
save current VIA changes before initializing EEPROM. Nonempty VIA macros or
encoder maps are rejected by the importer rather than silently discarded.

## Build and test

With the current QMK environment and external userspace configured:

```sh
qmk compile -kb cannonkeys/link -km svenge
qmk lint --strict -kb cannonkeys/link -km svenge
python tests/link/run_tests.py
python tests/link/run_tests.py --preview
```

The preview option additionally needs Pillow. Tests compile the actual display
model, renderer, and QMK adapter against a small host shim. Coverage includes
all Caps/Shift combinations, idle/wake and manual modes, held layer keys, short
keypresses, timer wrap, frame bounds, physical mirroring, highest-layer display,
weak/one-shot modifiers, right-button interception, split payload validation,
RPC retry throttling, USB/master independence, and OLED power-down ordering.

The output is **`cannonkeys_link_svenge.uf2`** in the userspace root. Flash the
same new UF2 to both halves when ready to test: the custom screen-state RPC needs
matching firmware on both. No automated flashing is performed.

Firmware build, strict lint and host tests pass. Physical OLED orientation,
contrast, full split operation with USB connected to either side, and responsiveness
must still be verified on the keyboard. Both displays currently use rotation 0;
adjust `oled_init_user()` if a panel is physically mounted in another orientation.

## Source files

- `display.c` / `display.h`: hardware-independent mode/idle model and pixel renderer.
- `oled.c`: QMK hooks, physical input sampling, modifier/layer capture and a
  validated split RPC snapshot. Attempts are capped at 40 per second, with a
  250 ms heartbeat and throttled retries.
- `oled.h`: the custom display keycode and key-processing entry point.
- `config.h`: screen refresh, idle threshold and custom split transaction ID.
- `layout.inc` / `import_layout.py`: compiled defaults from the saved VIA matrix.
- `tests/link/`: host regressions and preview generation, at the userspace root.

To work on the WT75-C instead, use userspace `main` and firmware
`feature/wt75-c-led-matrix`. This branch's `qmk.json` selects only the Link target.
