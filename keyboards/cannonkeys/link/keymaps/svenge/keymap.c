// Copyright 2026 svenski88
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "oled.h"
#include "layout.inc"

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    return link_oled_process_record(keycode, record);
}
