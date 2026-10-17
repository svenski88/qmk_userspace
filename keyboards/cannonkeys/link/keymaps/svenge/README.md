# CannonKeys Link OLED development

This VIA-enabled starting keymap is for the wired RP2040 CannonKeys Link. It
copies the stock three-layer layout and leaves the original screen drawing,
rotations, split transport and encoder behavior in place. It does not import a
personal Link VIA layout yet; save that layout before choosing to flash/reset.

Use `feature/link-oled` in both `qmk_firmware` and `qmk_userspace`. This firmware
branch starts from clean official QMK `develop`, independently of the WT75-C
lighting changes. QMK's existing external-userspace configuration applies here.

```sh
qmk compile -kb cannonkeys/link -km svenge
qmk lint --strict -kb cannonkeys/link -km svenge
```

The output is `cannonkeys_link_svenge.uf2` in the userspace root. This is a build
baseline for future screen work; no custom screen design is enabled yet and no
automatic flashing is performed.

## Where screen changes go

- `oled.c`: personal initialization and rendering callbacks.
- `keymap.c`: initial stock layout, which VIA may replace in EEPROM.
- `rules.mk`: VIA support and the personal OLED source file.
- Firmware `keyboards/cannonkeys/link/link.c`: stock screen and encoder behavior.

The OLED driver already calls `oled_init_user()` after the keyboard's rotation
selection. The USB-connected/master half defaults to rotation 0; the secondary
half defaults to rotation 90. A personal rotation can be selected in `oled.c`.
The framebuffer is configured as 64x128 before rotation.

`oled_task_user()` currently returns true, so the board draws the existing status
panel on the master half and the QMK logo on the secondary half. To replace those,
draw the custom content in this callback, use `is_keyboard_master()` to distinguish
the halves, and return false to suppress the board's drawing. Use
`is_keyboard_left()` instead when content should follow the physical half even
if the USB connection moves. Drawing dimensions must account for rotation.

The Link configuration already synchronizes layer and matrix state across the
split. Any additional information needed on the secondary display should be
checked for split synchronization before use. WPM rendering requires enabling
the WPM feature; it is not enabled in this baseline.

## Branch-specific builds

This branch's `qmk.json` selects only the Link build target. The WT75-C files stay
in the repository, but to build that board use userspace `main` together with
firmware `feature/wt75-c-led-matrix`, as documented in the root README.

Before designing the screens, decide what each half should show, desired display
orientation, which layer names apply to the user's layout, and whether any data
must come from the host computer rather than the keyboard itself.
