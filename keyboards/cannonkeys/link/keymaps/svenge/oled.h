// Copyright 2026 svenski88
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include QMK_KEYBOARD_H

enum link_custom_keycodes { LINK_DISPLAY = QK_USER };

bool link_oled_process_record(uint16_t keycode, keyrecord_t *record);
