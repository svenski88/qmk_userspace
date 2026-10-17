// Copyright 2026 svenski88
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LINK_SCREEN_WIDTH 64
#define LINK_SCREEN_HEIGHT 128
#define LINK_SCREEN_BYTES (LINK_SCREEN_WIDTH * LINK_SCREEN_HEIGHT / 8)
#define LINK_SCREEN_ROWS 10
#define LINK_SCREEN_COLS 6
#define LINK_SCREEN_KEY_FLASH_MS 100
#define LINK_SCREEN_PROTOCOL 1

enum link_screen_mode { LINK_DASHBOARD, LINK_RAIN, LINK_OFF };

typedef struct {
    uint8_t version;
    uint8_t mode;
    uint8_t mods;
    uint8_t caps_lock;
    uint8_t layer;
    uint8_t keys[LINK_SCREEN_ROWS];
    uint16_t rain_frame;
} link_screen_snapshot_t;

typedef struct {
    uint8_t selected_mode;
    uint8_t physical[LINK_SCREEN_ROWS];
    uint8_t flashing[LINK_SCREEN_ROWS];
    uint32_t pressed_at[LINK_SCREEN_ROWS][LINK_SCREEN_COLS];
    uint32_t last_activity;
} link_screen_state_t;

void link_screen_init(link_screen_state_t *state, uint32_t now);
void link_screen_activity(link_screen_state_t *state, uint32_t now);
void link_screen_cycle(link_screen_state_t *state, uint8_t visible_mode, uint32_t now);
void link_screen_update(link_screen_state_t *state, const uint8_t physical[LINK_SCREEN_ROWS],
                        uint32_t now, uint32_t idle_timeout, uint8_t mods, bool caps_lock,
                        uint8_t layer, link_screen_snapshot_t *snapshot);
bool link_screen_snapshot_valid(const link_screen_snapshot_t *snapshot);
bool link_screen_caps_active(const link_screen_snapshot_t *snapshot);
void link_screen_render(const link_screen_snapshot_t *snapshot, bool left, uint8_t frame[LINK_SCREEN_BYTES]);
void link_screen_key_position(bool left, uint8_t row, uint8_t col, int *x, int *y, int *height);
