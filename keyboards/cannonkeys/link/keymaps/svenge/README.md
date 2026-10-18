# Link: mirrored keypress dashboards and matrix rain

Custom displays for the wired CannonKeys Link, with VIA remapping retained.
Use `feature/link-oled` in both the firmware and userspace repositories.

## Dashboard

Both displays use portrait 64x128 drawing. Each screen's top outside corner
contains a compact outline of its physical keyboard half, including a solid
2x6-pixel miniature screen. Finger columns, thumb cluster and encoder push are
mirrored between left and right. Only pressed keys appear as individual pixels;
unpressed positions remain empty. Short presses remain highlighted for at least
100 ms so quick taps are visible through refresh and split synchronization.

Beside each outline is the last key's readable name, with its hexadecimal QMK
keycode enlarged to 35x8 pixels above the underline. Each half retains its own
last press until another action is pressed on that half. Releases, unassigned
keys and virtual encoder rotation events do not replace it. These fields start
blank after reboot and survive mode changes during that session.

Names describe resolved QMK actions, not characters produced by the host OS.
For example, `ESC` is `0X0029` and `SPACE` is `0X002C`. A SpaceFN tap shows
`SPACE` / `0X002C`; a hold shows `LT1` / `0X412C` or `LT2` / `0X422C` once QMK
settles tap versus hold. Momentary layers show `MO1`, etc. Held modifiers retain
their keycode, modified-key chords show `CHORD`, and unknown actions show `KEY`
with their exact hex value. The reserved screen button shows `OLED` / `0X7E40`.
Normal key names use double-size text; longer names shrink to fit the inside
area without overlapping the keyboard silhouette.

The physical left screen uses the remaining area for large indicators:

- **CTRL:** either Control modifier is active.
- **CAPS:** host Caps Lock XOR either Shift, matching the WT75-C indicator.
- **ALT:** either Alt modifier is active.

Active panels have rounded fills and inverted text. Inactive indicators have
white labels with no box outline. Weak and one-shot modifiers are included.
These indicators always stay on the physical left.

The physical right screen shows **LAYER** and a large numeric active layer.
It reads QMK's effective layer state, including default layers. Your
`LT(1,KC_SPC)` and `LT(2,KC_SPC)` keys therefore show layers 1 and 2 when QMK
actually resolves them as holds. No guessed QWERTY/Colemak/Raise/Lower names are
displayed. If multiple layers are active, the highest active layer is shown.

See the [all-keys-lit dashboard preview](../../../../../previews/link/dashboard.png)
and [sample-keypress preview](../../../../../previews/link/dashboard-live.png),
generated from the same renderer used by the firmware. All-keys-lit is only a
preview fixture; on the device, pixels light only while pressed or briefly after.

## Right encoder push and idle behavior

Press the physical right encoder to cycle both screens together:

**Dashboard → Matrix rain → Off → Dashboard**

The button is electrical matrix **[9,5]**, the last slot of each VIA layer; it is
a normal key switch, separate from the rotation mappings. It is reserved for
screen control, so the former Play/Pause action is consumed even if VIA's current
EEPROM map still contains that action. Left encoder push retains its mapping,
and left encoder rotation retains volume control.

Right encoder rotation adjusts the **normal brightness of both screens**:
clockwise increases and counterclockwise decreases in steps of 16 on QMK's
0–255 contrast scale, clamped at both endpoints. It replaces the board's
Page Up/Down scrolling. Brightness applies to the dashboard and active dedicated
rain. The setting starts at `OLED_BRIGHTNESS` after reboot and is not stored in
EEPROM. Turning while Off updates the selected level without waking the panels.

The compiled map names this action `LINK_DISPLAY` (`QK_USER`, **0x7E40**). VIA
may show the raw value. It can also be assigned to another key using VIA's Any
keycode entry. Remapping the physical right encoder does not bypass its reserved
screen-control behavior in this edition.

- Dashboard automatically changes to rain after **60 seconds** without matrix
  activity or encoder rotation, matching QMK's usual OLED timeout interval.
- Holding a key, including either SpaceFN key, prevents automatic idle entry.
- A key press/release or encoder rotation returns automatic rain to the dashboard.
- Automatic screensaver rain always uses minimum brightness (**0**).
- Dedicated rain uses the selected normal brightness during activity. After
  **60 seconds idle**, it becomes a minimum-brightness screensaver while staying
  in rain mode. Key activity or encoder rotation restores the selected normal
  brightness and stays in dedicated rain. Holding a key prevents idle dimming.
- Both automatic and manual rain have a **10-minute continuous runtime limit**.
  At that deadline both OLED controllers power off. Typing during manual rain
  does not extend the limit. A fresh keypress wakes the dashboard; held keys,
  releases and encoder rotation do not wake this timeout-off state.
- Manually selected rain stays selected while typing until the next mode press
  or the 10-minute deadline.
- From automatic rain, the mode button advances directly to Off.
- Manual Off powers down both OLED controllers and stays off while typing.
  The next right-encoder press returns to Dashboard.
- Mode selection starts at Dashboard after a reboot; it is not written to EEPROM.

Rain uses falling monochrome pixel streams with dithered tails and different
patterns on the two physical halves. In dedicated rain, each physical keypress
adds an extra falling trail on that key's half, including modifier/layer keys and
otherwise unassigned switches. Releases, held keys and host repeats do not add
trails. Eight simultaneous trails per half are retained; rapid bursts replace
the oldest. Each trail leaves the screen after about 1.75 seconds. Fresh presses
after idle dimming brighten dedicated rain and also add trails.

The old automatic OLED power-off timeout is disabled; custom idle and continuous
runtime timers select dim rain and then power off. Dimming or waking dedicated
rain never resets the 10-minute continuous-rain deadline. See the
[base rain preview](../../../../../previews/link/rain.gif) and
[reactive rain preview](../../../../../previews/link/rain-reactive.gif). These are
short repeating animation samples; their bitmap previews do not simulate the
OLED controller's contrast setting.

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

The preview option additionally needs Pillow. The tests use the sibling
`qmk_firmware` checkout's real keycode definitions (override its path with
`QMK_HOME` if needed). Tests compile the actual display model, renderer, and
QMK adapter against a small host shim. Coverage includes
all Caps/Shift combinations, idle/wake and manual modes, held layer keys, short
keypresses, timer wrap, frame bounds, physical mirroring, highest-layer display,
weak/one-shot modifiers, right-button interception, split payload validation,
RPC retry throttling, USB/master independence, minimum rain brightness and
restoration on both halves, encoder brightness limits, dedicated-vs-idle rain,
per-half reactive trails and bounded bursts, the 10-minute deadline, wake rules, and OLED
power-down ordering, one unique pixel per key without idle markers, per-half
last-key persistence, resolved SpaceFN taps/holds, names and enlarged hex text,
and rejection of old screen-state protocol versions.

The output is **`cannonkeys_link_svenge.uf2`** in the userspace root. Flash the
same new UF2 to both halves when ready to test: the custom screen-state RPC needs
matching firmware on both. This edition uses screen-state protocol **3**; old
screen firmware cannot decode the new snapshot. No automated flashing is performed.
The master-to-secondary custom RPC buffer is 64 bytes to carry both halves'
active trails and effective brightness in one authoritative snapshot.

For the wired Link, CannonKeys' build guide identifies the outer/left USB port
on the left PCB as the computer connection. The other left port and the right
PCB port are the split interconnect. Do not assume the right interconnect port
can be used as a normal USB programming port. Establish the right PCB's actual
programming/recovery procedure before changing only one half's firmware.
`QK_BOOT` acts on the controller processing the keycode, not the remote half.

Firmware build, strict lint and host tests pass. Physical OLED orientation,
contrast, full split operation, and responsiveness
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
