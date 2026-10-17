// Copyright 2026 svenski88
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define QK_USER 0x7E40
#define LINK_OLED_SYNC 12
#define LINK_OLED_IDLE_TIMEOUT 60000UL
#define MATRIX_ROWS 10
#define MATRIX_COLS 6
typedef uint32_t layer_state_t;
typedef uint8_t matrix_row_t;
typedef enum { OLED_ROTATION_0, OLED_ROTATION_90 } oled_rotation_t;
typedef struct { struct { struct { uint8_t row, col; } key; bool pressed; } event; } keyrecord_t;
typedef struct { bool caps_lock; } led_t;
typedef void (*slave_callback_t)(uint8_t, const void *, uint8_t, void *);

extern layer_state_t layer_state, default_layer_state;
bool is_keyboard_master(void);
bool is_keyboard_left(void);
uint32_t timer_read32(void);
matrix_row_t matrix_get_row(uint8_t row);
uint8_t get_mods(void);
uint8_t get_weak_mods(void);
uint8_t get_oneshot_mods(void);
uint8_t get_highest_layer(layer_state_t state);
led_t host_keyboard_led_state(void);
void transaction_register_rpc(int8_t id, slave_callback_t callback);
bool transaction_rpc_send(int8_t id, uint8_t length, const void *data);
void oled_render_dirty(bool all);
bool oled_on(void);
bool oled_off(void);
void oled_write_raw(const char *data, uint16_t size);

void keyboard_post_init_user(void);
void housekeeping_task_user(void);
bool encoder_update_user(uint8_t index, bool clockwise);
oled_rotation_t oled_init_user(oled_rotation_t rotation);
bool oled_task_user(void);
