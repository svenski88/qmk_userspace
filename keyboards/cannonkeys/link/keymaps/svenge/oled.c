// Copyright 2026 svenski88
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#ifdef OLED_ENABLE

oled_rotation_t oled_init_user(oled_rotation_t rotation) {
    // Preserve the board's default rotation on both halves for the baseline.
    return rotation;
}

bool oled_task_user(void) {
    // Add custom drawing here. Return false once it replaces the stock display;
    // true lets oled_task_kb() draw its status panel and QMK logo as before.
    return true;
}

#endif
