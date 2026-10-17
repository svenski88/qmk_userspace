// Copyright 2026 svenski88
// SPDX-License-Identifier: GPL-2.0-or-later

#include "display.h"
#include <string.h>

_Static_assert(sizeof(link_screen_snapshot_t) <= 32, "Display snapshot exceeds QMK's default RPC buffer");

void link_screen_init(link_screen_state_t *state, uint32_t now) {
    memset(state, 0, sizeof(*state));
    state->last_activity = now;
}

void link_screen_activity(link_screen_state_t *state, uint32_t now) {
    state->last_activity = now;
}

void link_screen_cycle(link_screen_state_t *state, uint8_t visible_mode, uint32_t now) {
    state->selected_mode = (visible_mode + 1) % 3;
    state->rain_active = false;
    state->timed_out = false;
    link_screen_activity(state, now);
}

void link_screen_update(link_screen_state_t *state, const uint8_t physical[LINK_SCREEN_ROWS],
                        uint32_t now, uint32_t idle_timeout, uint8_t mods, bool caps_lock,
                        uint8_t layer, link_screen_snapshot_t *snapshot) {
    bool held = false;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->version = LINK_SCREEN_PROTOCOL;
    snapshot->mods = mods;
    snapshot->caps_lock = caps_lock;
    snapshot->layer = layer;
    for (uint8_t row = 0; row < LINK_SCREEN_ROWS; ++row) {
        uint8_t down = physical[row] & 0x3F;
        uint8_t rising = down & ~state->physical[row];
        if (down != state->physical[row]) {
            link_screen_activity(state, now);
        }
        if (rising && state->timed_out) {
            state->timed_out = false;
        }
        held |= down != 0;
        state->physical[row] = down;
        snapshot->keys[row] = down;
        for (uint8_t col = 0; col < LINK_SCREEN_COLS; ++col) {
            uint8_t bit = 1 << col;
            if (rising & bit) {
                state->flashing[row] |= bit;
                state->pressed_at[row][col] = now;
            }
            if (state->flashing[row] & bit) {
                if ((uint32_t)(now - state->pressed_at[row][col]) < LINK_SCREEN_KEY_FLASH_MS) {
                    snapshot->keys[row] |= bit;
                } else {
                    state->flashing[row] &= ~bit;
                }
            }
        }
    }
    snapshot->mode = state->selected_mode;
    if (snapshot->mode == LINK_DASHBOARD && idle_timeout && !held &&
        (uint32_t)(now - state->last_activity) >= idle_timeout) {
        snapshot->mode = LINK_RAIN;
    }
    // Limit continuous rain, including manually selected rain. Activity does
    // not extend that limit. Only a fresh matrix press wakes timeout-off;
    // encoder rotation and key releases cannot turn the panels back on.
    if (state->timed_out) {
        snapshot->mode = LINK_OFF;
    }
    if (snapshot->mode == LINK_RAIN) {
        if (!state->rain_active) {
            state->rain_started = now;
        }
        if ((uint32_t)(now - state->rain_started) >= LINK_SCREEN_RAIN_MAX_MS) {
            state->timed_out = true;
            state->selected_mode = LINK_DASHBOARD;
            snapshot->mode = LINK_OFF;
        }
    }
    state->rain_active = snapshot->mode == LINK_RAIN;
    snapshot->rain_frame = snapshot->mode == LINK_RAIN ? (uint16_t)(now / 50) : 0;
}

bool link_screen_snapshot_valid(const link_screen_snapshot_t *snapshot) {
    if (snapshot->version != LINK_SCREEN_PROTOCOL || snapshot->mode > LINK_OFF ||
        snapshot->caps_lock > 1 || snapshot->layer > 31) {
        return false;
    }
    for (uint8_t row = 0; row < LINK_SCREEN_ROWS; ++row) {
        if (snapshot->keys[row] & ~0x3F) {
            return false;
        }
    }
    return true;
}

bool link_screen_caps_active(const link_screen_snapshot_t *snapshot) {
    return (snapshot->caps_lock != 0) != ((snapshot->mods & 0x22) != 0);
}

static void pixel(uint8_t *frame, int x, int y, bool on) {
    if (x < 0 || x >= LINK_SCREEN_WIDTH || y < 0 || y >= LINK_SCREEN_HEIGHT) {
        return;
    }
    uint16_t index = (y / 8) * LINK_SCREEN_WIDTH + x;
    uint8_t bit = 1 << (y % 8);
    if (on) {
        frame[index] |= bit;
    } else {
        frame[index] &= ~bit;
    }
}

static void rectangle(uint8_t *frame, int x, int y, int width, int height, bool fill, bool on) {
    for (int dy = 0; dy < height; ++dy) {
        for (int dx = 0; dx < width; ++dx) {
            if (fill || !dx || !dy || dx == width - 1 || dy == height - 1) {
                pixel(frame, x + dx, y + dy, on);
            }
        }
    }
}

// Compact 3x5 uppercase font. Rendering is independent of QMK's stock logo font.
static const uint8_t digits[10][5] = {
    {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
    {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
};
static const uint8_t letters[26][5] = {
    {2,5,7,5,5}, {6,5,6,5,6}, {3,4,4,4,3}, {6,5,5,5,6}, {7,4,6,4,7},
    {7,4,6,4,4}, {3,4,5,5,3}, {5,5,7,5,5}, {7,2,2,2,7}, {1,1,1,5,2},
    {5,5,6,5,5}, {4,4,4,4,7}, {5,7,7,5,5}, {5,7,7,7,5}, {2,5,5,5,2},
    {6,5,6,4,4}, {2,5,5,7,3}, {6,5,6,5,5}, {3,4,2,1,6}, {7,2,2,2,2},
    {5,5,5,5,7}, {5,5,5,5,2}, {5,5,7,7,5}, {5,5,2,5,5}, {5,5,2,2,2},
    {7,1,2,4,7},
};

static void text(uint8_t *frame, const char *label, int y, int scale, bool on) {
    int width = (int)strlen(label) * (4 * scale) - scale;
    int x = (LINK_SCREEN_WIDTH - width) / 2;
    for (; *label; ++label, x += 4 * scale) {
        const uint8_t *glyph = NULL;
        if (*label >= '0' && *label <= '9') {
            glyph = digits[*label - '0'];
        } else if (*label >= 'A' && *label <= 'Z') {
            glyph = letters[*label - 'A'];
        }
        if (!glyph) {
            continue;
        }
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 3; ++col) {
                if (glyph[row] & (1 << (2 - col))) {
                    rectangle(frame, x + col * scale, y + row * scale, scale, scale, true, on);
                }
            }
        }
    }
}

void link_screen_key_position(bool left, uint8_t row, uint8_t col, int *x, int *y, int *height) {
    static const uint8_t stagger[6] = {2, 0, 0, 1, 2, 3};
    *height = 4;
    if (row < 4) {
        *x = 43 - col * 8;
        *y = 2 + row * 6 + stagger[col];
    } else if (col == 5) {
        *x = 51;
        *y = 23;
        *height = 5;
    } else {
        *x = 51 - col * 8;
        *y = col ? 30 : 29;
        *height = col ? 4 : 6;
    }
    if (!left) {
        *x = LINK_SCREEN_WIDTH - *x - 6;
    }
}

static void keyboard(uint8_t *frame, const link_screen_snapshot_t *snapshot, bool left) {
    rectangle(frame, 0, 0, 64, 38, false, true);
    for (uint8_t row = 0; row < 5; ++row) {
        for (uint8_t col = 0; col < 6; ++col) {
            int x, y, height;
            link_screen_key_position(left, row, col, &x, &y, &height);
            bool down = snapshot->keys[row + (left ? 0 : 5)] & (1 << col);
            rectangle(frame, x, y, 6, height, down, true);
        }
    }
    rectangle(frame, 0, 41, 64, 1, true, true);
}

static void indicator(uint8_t *frame, const char *label, int y, bool active) {
    rectangle(frame, 2, y, 60, 24, active, true);
    text(frame, label, y + 7, 2, !active);
}

static void rain(uint8_t *frame, bool left, uint16_t tick) {
    for (uint32_t stream = 0; stream < 16; ++stream) {
        uint32_t seed = (stream + 1) * 1103515245u + (left ? 12345u : 98765u);
        int tail = 12 + (seed >> 16) % 25;
        int speed = 1 + (seed >> 23) % 3;
        int head = ((uint32_t)tick * speed + (seed >> 7)) % (128 + tail);
        int x = stream * 4 + ((seed >> 20) & 1);
        for (int offset = 0; offset < tail; ++offset) {
            // Monochrome dither thins the trail toward its tail.
            if (offset < 4 || ((seed + offset * 7) % (1 + offset / 4)) == 0) {
                pixel(frame, x, head - offset, true);
                if (offset < 2) {
                    pixel(frame, x + 1, head - offset, true);
                }
            }
        }
    }
}

void link_screen_render(const link_screen_snapshot_t *snapshot, bool left, uint8_t frame[LINK_SCREEN_BYTES]) {
    memset(frame, 0, LINK_SCREEN_BYTES);
    if (!link_screen_snapshot_valid(snapshot) || snapshot->mode == LINK_OFF) {
        return;
    }
    if (snapshot->mode == LINK_RAIN) {
        rain(frame, left, snapshot->rain_frame);
        return;
    }
    keyboard(frame, snapshot, left);
    if (left) {
        indicator(frame, "CTRL", 46, (snapshot->mods & 0x11) != 0);
        indicator(frame, "CAPS", 73, link_screen_caps_active(snapshot));
        indicator(frame, "ALT", 100, (snapshot->mods & 0x44) != 0);
    } else {
        char layer[3] = {0};
        if (snapshot->layer >= 10) {
            layer[0] = '0' + snapshot->layer / 10;
            layer[1] = '0' + snapshot->layer % 10;
        } else {
            layer[0] = '0' + snapshot->layer;
        }
        text(frame, "LAYER", 49, 2, true);
        text(frame, layer, 73, 7, true);
        text(frame, snapshot->layer ? "ACTIVE" : "BASE", 117, 1, true);
    }
}
