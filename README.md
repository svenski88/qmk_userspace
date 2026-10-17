# Svenge's WT75-C / 7V firmware

This external QMK userspace ports the 2021 personal layout and lighting to the
optional LED Matrix support on `feature/wt75-c-led-matrix`. The board support is
kept in its own commit so a future QMK PR can exclude this directory.

## Active branch: Link OLED development

This `feature/link-oled` branch stages custom screens for the wired CannonKeys
Link. See the [Link development guide](keyboards/cannonkeys/link/keymaps/svenge/README.md).
Select the matching `feature/link-oled` branch in the firmware repository, then:

```sh
qmk compile -kb cannonkeys/link -km svenge
```

The screen callbacks currently preserve the stock status panel and logo. This
branch's build targets select Link only; the WT75-C project below remains on
userspace `main` and firmware `feature/wt75-c-led-matrix`.

## Build

This is a standalone [QMK external userspace](https://docs.qmk.fm/newbs_external_userspace)
repository. Until optional WT75-C LED Matrix support is merged into official QMK,
build against the patched firmware fork:

```sh
git clone --branch feature/wt75-c-led-matrix https://github.com/svenski88/qmk_firmware.git
git clone https://github.com/svenski88/qmk_userspace.git
```

Configure your current QMK CLI with absolute paths to these two checkouts:

```sh
qmk config user.qmk_home=/absolute/path/to/qmk_firmware
qmk config user.overlay_dir=/absolute/path/to/qmk_userspace
qmk compile -kb wilba_tech/wt75_c -km svenge
```

On this Windows setup the paths are `C:/UTIL/Code_Projects/qmk_firmware` and
`C:/UTIL/Code_Projects/qmk_userspace`. Run these commands in a configured QMK shell.
The fork's `master` mirrors official QMK and does not include the pending board
support; keep the firmware checkout on `feature/wt75-c-led-matrix` for this build.

The board support originated in firmware commit `493d943631`; the personal
userspace came from migration commit `85c895100a`. The source files retain their
original copyright and GPL-2.0-or-later notices; see [LICENSE](LICENSE).

The build enables VIA, LED Matrix and LTO. The output is
`wilba_tech_wt75_c_svenge.hex` in this repository. Generated firmware is ignored by
Git. The original firmware is archived in the fork's
[`legacy/wt75-c-custom`](https://github.com/svenski88/qmk_firmware/tree/legacy/wt75-c-custom)
branch and [legacy release](https://github.com/svenski88/qmk_firmware/releases/tag/legacy-wt75-c-known-working).

## Controls

The saved `wilba_tech_wt75_c.layout.json` VIA export supplies all four default
layers, including your navigation/media keys and Fn+R bootloader shortcut.
Its legacy lighting names are translated to current LED Matrix keycodes:

| Shortcut | Action |
| --- | --- |
| Fn+[ / Fn+] | Previous / next effect |
| Fn+B / Fn+G | Brightness down / up |
| Fn+, / Fn+. | Speed down / up |

The saved layout replaces the temporary Fn+F-key controls from the initial port.
To change the compiled defaults after saving another VIA export:

```sh
python import_layout.py
python import_layout.py --check
```

Then rebuild the firmware. `layout.inc` is generated from all 384 matrix entries.
The importer verifies the board ID and dimensions and refuses to silently drop
nonempty macros or encoder mappings. The original export is preserved unchanged.
Legacy `RESET` becomes `QK_BOOT`; `EF_*`, `BR_*`, and `ES_*` become the matching
`LM_*` effect, brightness and speed controls.

VIA stores a separate copy of the keymap in EEPROM. Flashing alone can retain
that stored map instead of these new defaults. Initialize EEPROM deliberately
to load the new defaults, after saving any current VIA changes. The original
export still contains old lighting keycode names; use the compiled defaults for
the migrated lighting controls rather than importing that export unchanged.

Solid is the initial mode. The next eight modes are the migrated breathing,
radial wave, horizontal wave, key fade, row splash, cross splash, modifier-only,
and fading raindrops effects. The selector has no unimplemented tenth mode.
Speed uses four coarse bands of QMK's 0-255 range; standard speed keys change the
range in steps of 16, so four presses move between coarse bands. Custom modes use
the original 50 ms tick duration and up to 18 random hits per raindrop update.

The F1-F4 Ctrl, F5-F8 caps/shift XOR, and F9-F12 Alt indicators are retained,
along with layer priority 3 > 2 > 1, activity blinking on channel 55 and the idle
display after 50 seconds on channel 40. Weak and one-shot modifiers are now
included. These personal indicators override the board's caps/scroll display.

## Migration differences to review

- All rendering and hit tracking now run in the main loop. There is no custom
  timer ISR, direct register access, or interrupt-shared activity counter.
- Absent matrix positions are skipped before indexing the hit-age array.
- All five legacy spacebar channels react together. Unpopulated channels stay
  off. Physical channel population and the legacy radial tuning still need
  checking on the 7V; the old code itself marked some channels as uncertain.
- Wave wrapping now uses a consistent 256-position cycle. Scheduling comes from
  QMK rather than an exact hardware interrupt, so visual timing needs comparison.
- Turning lighting off now turns all indicators off too. While lighting is on,
  personal indicators retain their original full brightness, independent of the
  effect brightness. Activity blink visibility still depends on the base effect.
- Lighting persistence uses standard QMK LED Matrix EEPROM storage. The unused
  indicator fields and old 31-byte VIA reservation are gone. This does not migrate
  old EEPROM or VIA maps. Export the current VIA layout before flashing and
  deliberately initialize EEPROM for the new firmware.
- VIA remapping is compiled in. The old online WT75-C definition's custom lighting
  menu is not guaranteed compatible with the new LED Matrix channel or custom
  mode list. Use the Fn controls for lighting initially; a matching VIA v3
  definition and real-device verification are follow-up work.

## Validation

The legacy default, optional LED Matrix default, and personal VIA build compile
with current QMK CLI 1.2.0 and AVR GCC 8.4.0. Host regression tests exercise the
real lighting implementation and QMK-generated mapping with a small API shim:

```sh
python tests/run_tests.py
```

The test runner needs `qmk` and a host GCC on `PATH` (or `QMK_BIN` and `CC`). It
also discovers the sibling firmware checkout's isolated Windows tools when present. It covers all
15 absent matrix slots, invalid key coordinates, shared backspace, five-channel
spacebar tracking, all effect/speed combinations, brightness, indicator overrides,
weak/one-shot modifiers, idle behavior, and 32-bit timer wraparound.

No firmware has been flashed as part of this migration. Before using the new
firmware, review the mappings, save VIA data, and compare every LED/effect,
suspend/wake, EEPROM persistence, and typing responsiveness on the keyboard.
