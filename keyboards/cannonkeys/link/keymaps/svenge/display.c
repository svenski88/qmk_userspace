// Copyright 2026 svenski88
// SPDX-License-Identifier: GPL-2.0-or-later

#include "display.h"
#include "keycodes.h"
#include <string.h>

_Static_assert(sizeof(link_screen_snapshot_t) <= 32, "Display snapshot exceeds QMK's default RPC buffer");

void link_screen_init(link_screen_state_t *state, uint32_t now) {
    memset(state, 0, sizeof(*state));
    state->last_activity = now;
}

void link_screen_activity(link_screen_state_t *state, uint32_t now) {
    state->last_activity = now;
}

void link_screen_keypress(link_screen_state_t *state, uint8_t row, uint8_t col, uint16_t keycode) {
    // Virtual encoder events have out-of-matrix coordinates. Ignore unassigned
    // keys too: no keyboard action was selected for KC_NO / KC_TRNS.
    if (row >= LINK_SCREEN_ROWS || col >= LINK_SCREEN_COLS || keycode <= KC_TRNS) {
        return;
    }
    uint8_t side = row / 5;
    state->last_key[side] = keycode;
    state->last_key_valid |= 1 << side;
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
    memcpy(snapshot->last_key, state->last_key, sizeof(snapshot->last_key));
    snapshot->last_key_valid = state->last_key_valid;
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
        snapshot->caps_lock > 1 || snapshot->layer > 31 || snapshot->last_key_valid > 3) {
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

static const uint8_t *glyph_for(char c) {
    static const uint8_t minus[5] = {0, 0, 7, 0, 0};
    if (c >= '0' && c <= '9') {
        return digits[c - '0'];
    }
    if (c >= 'A' && c <= 'Z') {
        return letters[c - 'A'];
    }
    return minus; // Only used for an unknown name's separator.
}

static void text_at(uint8_t *frame, const char *label, int x, int y, int scale, bool on) {
    for (; *label; ++label, x += 4 * scale) {
        const uint8_t *glyph = glyph_for(*label);
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 3; ++col) {
                if (glyph[row] & (1 << (2 - col))) {
                    rectangle(frame, x + col * scale, y + row * scale, scale, scale, true, on);
                }
            }
        }
    }
}

static void text(uint8_t *frame, const char *label, int y, int scale, bool on) {
    int width = (int)strlen(label) * (4 * scale) - scale;
    text_at(frame, label, (LINK_SCREEN_WIDTH - width) / 2, y, scale, on);
}

static void numbered_name(char name[LINK_SCREEN_NAME_BYTES], const char *prefix, uint8_t number) {
    // Prefixes below use at most five characters and numbers at most three.
    size_t n = strlen(prefix);
    memcpy(name, prefix, n);
    if (number >= 100) {
        name[n++] = '0' + number / 100;
    }
    if (number >= 10) {
        name[n++] = '0' + (number / 10) % 10;
    }
    name[n++] = '0' + number % 10;
    name[n] = '\0';
}

void link_screen_key_name(uint16_t keycode, char name[LINK_SCREEN_NAME_BYTES]) {
    if (keycode >= KC_A && keycode <= KC_Z) {
        name[0] = 'A' + keycode - KC_A;
        name[1] = '\0';
        return;
    }
    if (keycode >= KC_1 && keycode <= KC_0) {
        name[0] = keycode == KC_0 ? '0' : '1' + keycode - KC_1;
        name[1] = '\0';
        return;
    }
    if ((keycode >= KC_F1 && keycode <= KC_F12) || (keycode >= KC_F13 && keycode <= KC_F24)) {
        numbered_name(name, "F", keycode <= KC_F12 ? keycode - KC_F1 + 1 : keycode - KC_F13 + 13);
        return;
    }
    if (keycode >= KC_KP_1 && keycode <= KC_KP_0) {
        numbered_name(name, "KP", keycode == KC_KP_0 ? 0 : keycode - KC_KP_1 + 1);
        return;
    }
    if (IS_QK_LAYER_TAP(keycode)) {
        numbered_name(name, "LT", (keycode >> 8) & 15);
        return;
    }
    if (IS_QK_MOD_TAP(keycode)) {
        uint8_t mods = (keycode >> 8) & 15;
        // One held modifier has a readable name; combinations remain MTn.
        const char *label = mods == 1 ? "CTRL" : mods == 2 ? "SHIFT" : mods == 4 ? "ALT" : mods == 8 ? "GUI" : NULL;
        if (label) {
            strcpy(name, label);
        } else {
            numbered_name(name, "MT", (keycode >> 8) & 31);
        }
        return;
    }
    const struct { uint16_t first; const char *prefix; } layers[] = {
        {QK_TO, "TO"}, {QK_MOMENTARY, "MO"}, {QK_DEF_LAYER, "DF"},
        {QK_TOGGLE_LAYER, "TG"}, {QK_ONE_SHOT_LAYER, "OSL"},
        {QK_LAYER_TAP_TOGGLE, "TT"}, {QK_PERSISTENT_DEF_LAYER, "PDF"},
    };
    for (uint8_t i = 0; i < sizeof(layers) / sizeof(layers[0]); ++i) {
        if (keycode >= layers[i].first && keycode < layers[i].first + 32) {
            numbered_name(name, layers[i].prefix, keycode & 31);
            return;
        }
    }
    if (IS_QK_MODS(keycode)) {
        // The hex preserves the complete modified keycode; do not incorrectly
        // label a chord as an unmodified character.
        strcpy(name, "CHORD");
        return;
    }
    static const struct { uint16_t keycode; const char *name; } names[] = {
        {KC_ENTER, "ENTER"}, {KC_ESCAPE, "ESC"}, {KC_BACKSPACE, "BSPC"},
        {KC_TAB, "TAB"}, {KC_SPACE, "SPACE"}, {KC_MINUS, "MINUS"},
        {KC_EQUAL, "EQUAL"}, {KC_LEFT_BRACKET, "LBRC"}, {KC_RIGHT_BRACKET, "RBRC"},
        {KC_BACKSLASH, "BSLS"}, {KC_NONUS_HASH, "NUHS"}, {KC_SEMICOLON, "SCLN"},
        {KC_QUOTE, "QUOTE"}, {KC_GRAVE, "GRAVE"}, {KC_COMMA, "COMMA"},
        {KC_DOT, "DOT"}, {KC_SLASH, "SLASH"}, {KC_CAPS_LOCK, "CAPS"},
        {KC_PRINT_SCREEN, "PSCR"}, {KC_SCROLL_LOCK, "SCRL"}, {KC_PAUSE, "PAUSE"},
        {KC_INSERT, "INS"}, {KC_HOME, "HOME"}, {KC_PAGE_UP, "PGUP"},
        {KC_DELETE, "DEL"}, {KC_END, "END"}, {KC_PAGE_DOWN, "PGDN"},
        {KC_RIGHT, "RIGHT"}, {KC_LEFT, "LEFT"}, {KC_DOWN, "DOWN"}, {KC_UP, "UP"},
        {KC_NUM_LOCK, "NUM"}, {KC_KP_SLASH, "KPSLS"}, {KC_KP_ASTERISK, "KPAST"},
        {KC_KP_MINUS, "KPMNS"}, {KC_KP_PLUS, "KPPLS"}, {KC_KP_ENTER, "KPENT"},
        {KC_KP_DOT, "KPDOT"}, {KC_KP_EQUAL, "KPEQL"}, {KC_NONUS_BACKSLASH, "NUBS"},
        {KC_APPLICATION, "APP"}, {KC_LEFT_CTRL, "LCTRL"}, {KC_RIGHT_CTRL, "RCTRL"},
        {KC_LEFT_SHIFT, "LSHFT"}, {KC_RIGHT_SHIFT, "RSHFT"},
        {KC_LEFT_ALT, "LALT"}, {KC_RIGHT_ALT, "RALT"},
        {KC_LEFT_GUI, "LGUI"}, {KC_RIGHT_GUI, "RGUI"},
        {KC_AUDIO_MUTE, "MUTE"}, {KC_AUDIO_VOL_UP, "VOLUP"}, {KC_AUDIO_VOL_DOWN, "VOLDN"},
        {KC_MEDIA_PLAY_PAUSE, "PLAY"}, {KC_MEDIA_NEXT_TRACK, "NEXT"},
        {KC_MEDIA_PREV_TRACK, "PREV"}, {KC_MEDIA_STOP, "STOP"},
        {QK_BOOTLOADER, "BOOT"}, {QK_REBOOT, "REBOOT"}, {QK_USER, "OLED"},
    };
    for (uint8_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (names[i].keycode == keycode) {
            strcpy(name, names[i].name);
            return;
        }
    }
    if (IS_QK_MACRO(keycode)) {
        numbered_name(name, "MACRO", keycode - QK_MACRO);
        return;
    }
    strcpy(name, "KEY"); // Unknown actions still retain their exact hex value.
}

void link_screen_key_position(bool left, uint8_t row, uint8_t col, int *x, int *y, int *height) {
    static const uint8_t stagger[6] = {1, 0, 0, 1, 2, 3};
    static const uint8_t thumbs[6][2] = {{20,23}, {18,21}, {15,20}, {13,20}, {11,21}, {19,21}};
    *height = 1;
    if (row < 4) {
        *x = 17 - col * 2;
        *y = 12 + row * 2 + stagger[col];
    } else {
        *x = thumbs[col][0];
        *y = thumbs[col][1];
    }
    *x -= 3;
    *y -= 8;
    if (!left) {
        *x = LINK_SCREEN_WIDTH - *x - 1;
    }
}

static void keyboard(uint8_t *frame, const link_screen_snapshot_t *snapshot, bool left) {
    // Approved one-pixel silhouette, stored by scanline to preserve the exact
    // preview raster. Bit x in row y is pixel (x,y) of the physical left shape.
    static const uint32_t outline[19] = {
        0x0, 0xFFFF8, 0x100006, 0x100002, 0x100002, 0x100002,
        0x100002, 0x100002, 0x100002, 0x100002, 0x100002, 0x100002,
        0x100002, 0x100002, 0x100002, 0x1007FE, 0x101800, 0x86000, 0x78000,
    };
    for (int y = 0; y < 19; ++y) {
        for (int x = 0; x <= 20; ++x) {
            if (outline[y] & (1UL << x)) {
                pixel(frame, left ? x : 63 - x, y, true);
            }
        }
    }
    rectangle(frame, left ? 16 : 46, 5, 2, 6, true, true);
    for (uint8_t row = 0; row < 5; ++row) {
        for (uint8_t col = 0; col < 6; ++col) {
            if (snapshot->keys[row + (left ? 0 : 5)] & (1 << col)) {
                int x, y, height;
                link_screen_key_position(left, row, col, &x, &y, &height);
                pixel(frame, x, y, true);
            }
        }
    }
    rectangle(frame, 0, 41, 64, 1, true, true);
}

static void last_key(uint8_t *frame, const link_screen_snapshot_t *snapshot, bool left) {
    uint8_t side = left ? 0 : 1;
    if (!(snapshot->last_key_valid & (1 << side))) {
        return;
    }
    uint16_t code = snapshot->last_key[side];
    char name[LINK_SCREEN_NAME_BYTES];
    link_screen_key_name(code, name);
    int scale = strlen(name) <= 5 ? 2 : 1;
    int width = (int)strlen(name) * 4 * scale - scale;
    text_at(frame, name, (left ? 23 : 0) + (41 - width) / 2, 6, scale, true);

    static const char hex[] = "0123456789ABCDEF";
    char label[7] = {'0', 'X', hex[(code >> 12) & 15], hex[(code >> 8) & 15],
                     hex[(code >> 4) & 15], hex[code & 15], '\0'};
    // Nearest-neighbor 1.5x scale: the original 23x5 line becomes 35x8.
    // Sample pixel centers to match the approved PNG at native OLED resolution.
    for (int y = 0; y < 8; ++y) {
        int sy = ((2 * y + 1) * 5) / 16;
        for (int x = 0; x < 35; ++x) {
            int sx = ((2 * x + 1) * 23) / 70;
            if (sx % 4 < 3 && (glyph_for(label[sx / 4])[sy] & (1 << (2 - sx % 4)))) {
                pixel(frame, 14 + x, 31 + y, true);
            }
        }
    }
}

static void indicator(uint8_t *frame, const char *label, int y, bool active) {
    if (active) {
        // Radius-four fill, matching the approved monochrome pixel corners.
        static const uint8_t inset[4] = {3, 1, 1, 0};
        for (int row = 0; row < 24; ++row) {
            int edge = row < 4 ? row : 23 - row;
            int margin = edge < 4 ? inset[edge] : 0;
            rectangle(frame, 2 + margin, y + row, 60 - 2 * margin, 1, true, true);
        }
    }
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
    last_key(frame, snapshot, left);
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
