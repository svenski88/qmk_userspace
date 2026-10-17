// Copyright 2026 svenski88
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "oled.h"
#include "display.h"
#include "transactions.h"
#include <string.h>

static link_screen_state_t state;
static link_screen_snapshot_t snapshot;
static link_screen_snapshot_t last_sent;
static uint32_t last_sync_attempt;
static uint32_t last_sync_success;
static uint8_t frame[LINK_SCREEN_BYTES];

static void receive_screen(uint8_t length, const void *data, uint8_t out_length, void *out_data) {
    (void)out_length;
    (void)out_data;
    if (length != sizeof(link_screen_snapshot_t)) {
        return;
    }
    link_screen_snapshot_t received;
    memcpy(&received, data, sizeof(received));
    if (link_screen_snapshot_valid(&received)) {
        snapshot = received;
    }
}

void keyboard_post_init_user(void) {
    link_screen_init(&state, timer_read32());
    snapshot.version = LINK_SCREEN_PROTOCOL;
    transaction_register_rpc(LINK_OLED_SYNC, receive_screen);
}

bool link_oled_process_record(uint16_t keycode, keyrecord_t *record) {
    // The encoder push is a normal matrix switch, not encoder rotation. Reserve
    // the physical right push even if VIA EEPROM still maps it to Play/Pause.
    bool right_encoder = record->event.key.row == 9 && record->event.key.col == 5;
    if (keycode == LINK_DISPLAY || right_encoder) {
        if (record->event.pressed && is_keyboard_master()) {
            link_screen_cycle(&state, snapshot.mode, timer_read32());
        }
        return false;
    }
    return true;
}

bool encoder_update_user(uint8_t index, bool clockwise) {
    (void)index;
    (void)clockwise;
    if (is_keyboard_master()) {
        link_screen_activity(&state, timer_read32());
    }
    return true; // Preserve the keyboard's volume and page-up/down rotation actions.
}

void housekeeping_task_user(void) {
    if (!is_keyboard_master()) {
        return;
    }
    uint8_t physical[LINK_SCREEN_ROWS];
    for (uint8_t row = 0; row < LINK_SCREEN_ROWS; ++row) {
        physical[row] = matrix_get_row(row);
    }
    uint32_t now = timer_read32();
    uint8_t mods = get_mods() | get_weak_mods() | get_oneshot_mods();
    uint8_t layer = get_highest_layer(layer_state | default_layer_state);
    link_screen_update(&state, physical, now, LINK_OLED_IDLE_TIMEOUT, mods,
                       host_keyboard_led_state().caps_lock, layer, &snapshot);

    // At most 40 RPC attempts/second, with a heartbeat for reconnection. A failed
    // attempt is throttled too so a disconnected half cannot flood the scan loop.
    if ((uint32_t)(now - last_sync_attempt) < 25) {
        return;
    }
    if (memcmp(&snapshot, &last_sent, sizeof(snapshot)) == 0 &&
        (uint32_t)(now - last_sync_success) < 250) {
        return;
    }
    last_sync_attempt = now;
    if (transaction_rpc_send(LINK_OLED_SYNC, sizeof(snapshot), &snapshot)) {
        last_sent = snapshot;
        last_sync_success = now;
    }
}

oled_rotation_t oled_init_user(oled_rotation_t rotation) {
    (void)rotation;
    // Portrait on both physical halves, independently of which USB port is used.
    return OLED_ROTATION_0;
}

bool oled_task_user(void) {
    if (snapshot.mode == LINK_OFF) {
        // QMK wakes OLEDs while flushing dirty blocks. Drain pending drawing
        // before powering down so a leftover dashboard update cannot wake it.
        oled_render_dirty(true);
        oled_off();
        return false;
    }
    oled_set_brightness(snapshot.mode == LINK_RAIN ? 0 : OLED_BRIGHTNESS);
    oled_on();
    link_screen_render(&snapshot, is_keyboard_left(), frame);
    oled_write_raw((const char *)frame, sizeof(frame));
    return false; // Fully replace the stock status panel and logo.
}
