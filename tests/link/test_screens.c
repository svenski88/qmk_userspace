// Copyright 2026 svenski88
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fake_qmk.h"
#include "display.h"
#include "oled.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t now;
static bool master = true, left = true, rpc_ok = true, powered;
static uint8_t physical[10], mods, weak_mods, oneshot;
static bool caps;
static slave_callback_t receiver;
static unsigned attempts, drains, offs;
static uint8_t brightness;
static bool drained_before_off;
static link_screen_snapshot_t sent;
static uint8_t oled_frame[LINK_SCREEN_BYTES];
layer_state_t layer_state = 1, default_layer_state = 1;

bool is_keyboard_master(void) { return master; }
bool is_keyboard_left(void) { return left; }
uint32_t timer_read32(void) { return now; }
matrix_row_t matrix_get_row(uint8_t row) { assert(row < 10); return physical[row]; }
uint8_t get_mods(void) { return mods; }
uint8_t get_weak_mods(void) { return weak_mods; }
uint8_t get_oneshot_mods(void) { return oneshot; }
uint8_t get_highest_layer(layer_state_t state) {
    uint8_t layer = 0;
    while (state >>= 1) { ++layer; }
    return layer;
}
led_t host_keyboard_led_state(void) { return (led_t){caps}; }
void transaction_register_rpc(int8_t id, slave_callback_t callback) {
    assert(id == LINK_OLED_SYNC); receiver = callback;
}
bool transaction_rpc_send(int8_t id, uint8_t length, const void *data) {
    assert(id == LINK_OLED_SYNC && length == sizeof(sent));
    ++attempts; memcpy(&sent, data, length); return rpc_ok;
}
void oled_render_dirty(bool all) { assert(all); ++drains; drained_before_off = true; }
uint8_t oled_set_brightness(uint8_t level) { brightness = level; return level; }
bool oled_on(void) { powered = true; return true; }
bool oled_off(void) { assert(drained_before_off); drained_before_off = false; ++offs; powered = false; return true; }
void oled_write_raw(const char *data, uint16_t size) { assert(size == sizeof(oled_frame)); memcpy(oled_frame, data, size); }

static bool bit(const uint8_t *frame, int x, int y) { return frame[(y / 8) * 64 + x] & (1 << (y % 8)); }

static void update(link_screen_state_t *state, link_screen_snapshot_t *snapshot, uint8_t *keys, uint32_t time) {
    link_screen_update(state, keys, time, 60000, 0, false, 0, snapshot);
}

static void test_model(void) {
    link_screen_state_t state;
    link_screen_snapshot_t snapshot;
    uint8_t keys[10] = {0};
    link_screen_init(&state, 0);
    update(&state, &snapshot, keys, 59999); assert(snapshot.mode == LINK_DASHBOARD);
    update(&state, &snapshot, keys, 60000); assert(snapshot.mode == LINK_RAIN);
    keys[9] = 2;
    update(&state, &snapshot, keys, 60001); assert(snapshot.mode == LINK_DASHBOARD);
    update(&state, &snapshot, keys, 180001); assert(snapshot.mode == LINK_DASHBOARD); // Held SpaceFN never sleeps.
    keys[9] = 0;
    update(&state, &snapshot, keys, 180002); assert(snapshot.mode == LINK_DASHBOARD);
    update(&state, &snapshot, keys, 240002); assert(snapshot.mode == LINK_RAIN);
    link_screen_cycle(&state, snapshot.mode, 240002);
    keys[0] = 1;
    update(&state, &snapshot, keys, 240003); assert(snapshot.mode == LINK_OFF); // Typing cannot wake manual off.
    link_screen_cycle(&state, snapshot.mode, 240004);
    update(&state, &snapshot, keys, 240004); assert(snapshot.mode == LINK_DASHBOARD);
    link_screen_cycle(&state, snapshot.mode, 240005);
    update(&state, &snapshot, keys, 240005); assert(snapshot.mode == LINK_RAIN); // Manual rain persists while typing.

    link_screen_init(&state, 0); memset(keys, 0, sizeof(keys));
    keys[3] = 4; update(&state, &snapshot, keys, 10);
    keys[3] = 0; update(&state, &snapshot, keys, 11); assert(snapshot.keys[3] == 4);
    update(&state, &snapshot, keys, 109); assert(snapshot.keys[3] == 4);
    update(&state, &snapshot, keys, 110); assert(snapshot.keys[3] == 0);

    for (int caps_lock = 0; caps_lock < 2; ++caps_lock) {
        for (int shift = 0; shift < 2; ++shift) {
            snapshot.caps_lock = caps_lock; snapshot.mods = shift ? 0x02 : 0;
            assert(link_screen_caps_active(&snapshot) == (caps_lock != shift));
        }
    }
    link_screen_init(&state, UINT32_MAX - 50);
    update(&state, &snapshot, keys, 49); assert(snapshot.mode == LINK_DASHBOARD);
    update(&state, &snapshot, keys, 60000); assert(snapshot.mode == LINK_RAIN);
    snapshot.mode = 3; assert(!link_screen_snapshot_valid(&snapshot));
    snapshot.mode = 0; snapshot.layer = 32; assert(!link_screen_snapshot_valid(&snapshot));
    snapshot.layer = 2; snapshot.version = 255; assert(!link_screen_snapshot_valid(&snapshot));
}

static void test_rain_timeout(void) {
    link_screen_state_t state;
    link_screen_snapshot_t snapshot;
    uint8_t keys[10] = {0};
    link_screen_init(&state, 0);
    update(&state, &snapshot, keys, 60000); assert(snapshot.mode == LINK_RAIN);
    update(&state, &snapshot, keys, 659999); assert(snapshot.mode == LINK_RAIN);
    update(&state, &snapshot, keys, 660000); assert(snapshot.mode == LINK_OFF);
    link_screen_activity(&state, 660001); // Encoder rotation must not wake timeout-off.
    update(&state, &snapshot, keys, 660001); assert(snapshot.mode == LINK_OFF);
    keys[0] = 1;
    update(&state, &snapshot, keys, 660002); assert(snapshot.mode == LINK_DASHBOARD);

    // Manual rain's hard deadline is not extended by input or held keys.
    link_screen_cycle(&state, snapshot.mode, 660003);
    update(&state, &snapshot, keys, 660003); assert(snapshot.mode == LINK_RAIN);
    keys[1] = 2;
    update(&state, &snapshot, keys, 1260002); assert(snapshot.mode == LINK_RAIN);
    update(&state, &snapshot, keys, 1260003); assert(snapshot.mode == LINK_OFF);
    keys[1] = 0;
    update(&state, &snapshot, keys, 1260004); assert(snapshot.mode == LINK_OFF);
    keys[1] = 2;
    update(&state, &snapshot, keys, 1260005); assert(snapshot.mode == LINK_DASHBOARD);

    // Timeout arithmetic works across the 32-bit system timer wrap.
    link_screen_init(&state, UINT32_MAX - 100);
    memset(keys, 0, sizeof(keys));
    link_screen_cycle(&state, LINK_DASHBOARD, UINT32_MAX - 100);
    update(&state, &snapshot, keys, UINT32_MAX - 100);
    update(&state, &snapshot, keys, 599898); assert(snapshot.mode == LINK_RAIN);
    update(&state, &snapshot, keys, 599899); assert(snapshot.mode == LINK_OFF);
    link_screen_cycle(&state, snapshot.mode, 599900);
    update(&state, &snapshot, keys, 599900); assert(snapshot.mode == LINK_DASHBOARD);
}

static void test_render(void) {
    struct { uint8_t pre[16], frame[1024], post[16]; } guarded;
    uint8_t right_frame[1024], before[1024];
    memset(&guarded, 0xA5, sizeof(guarded));
    link_screen_snapshot_t snapshot = {.version = LINK_SCREEN_PROTOCOL, .layer = 2};
    snapshot.keys[1] = snapshot.keys[6] = 4;
    link_screen_render(&snapshot, true, guarded.frame);
    link_screen_render(&snapshot, false, right_frame);
    for (int y = 0; y < 38; ++y) {
        for (int x = 0; x < 64; ++x) { assert(bit(guarded.frame, x, y) == bit(right_frame, 63 - x, y)); }
    }
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 6; ++col) {
            int lx, ly, lh, rx, ry, rh;
            link_screen_key_position(true, row, col, &lx, &ly, &lh);
            link_screen_key_position(false, row, col, &rx, &ry, &rh);
            assert(lx == 64 - rx - 6 && ly == ry && lh == rh);
            assert(lx >= 0 && lx + 6 <= 64 && ly >= 0 && ly + lh < 38);
        }
    }
    for (int layer = 0; layer < 32; ++layer) {
        snapshot.layer = layer;
        link_screen_render(&snapshot, false, guarded.frame);
    }
    snapshot.mode = LINK_RAIN;
    for (int tick = 0; tick < 1024; ++tick) {
        snapshot.rain_frame = tick;
        link_screen_render(&snapshot, tick % 2, guarded.frame);
    }
    memcpy(before, guarded.frame, sizeof(before)); ++snapshot.rain_frame;
    link_screen_render(&snapshot, true, guarded.frame);
    assert(memcmp(before, guarded.frame, sizeof(before)) != 0);
    snapshot.mode = LINK_OFF; link_screen_render(&snapshot, true, guarded.frame);
    for (int i = 0; i < 1024; ++i) { assert(!guarded.frame[i]); }
    for (int i = 0; i < 16; ++i) { assert(guarded.pre[i] == 0xA5 && guarded.post[i] == 0xA5); }
}

static void test_adapter(void) {
    keyboard_post_init_user(); assert(receiver);
    now = 25; physical[9] = 2; layer_state = 1 | (1 << 2);
    weak_mods = 0x02; oneshot = 0x01; caps = true;
    housekeeping_task_user();
    assert(attempts == 1 && sent.layer == 2 && sent.mods == 3 && sent.keys[9] == 2);
    assert(!link_screen_caps_active(&sent));
    now = 26; physical[1] = 1; housekeeping_task_user(); assert(attempts == 1);
    now = 50; rpc_ok = false; housekeeping_task_user(); assert(attempts == 2);
    now = 51; housekeeping_task_user(); assert(attempts == 2);
    now = 75; rpc_ok = true; housekeeping_task_user(); assert(attempts == 3);

    // A right USB connection must still render the physical right dashboard.
    left = false; oled_task_user();
    uint8_t expected[1024]; link_screen_render(&sent, false, expected);
    assert(memcmp(oled_frame, expected, sizeof(expected)) == 0);
    master = false; receiver(sizeof(sent), &sent, 0, NULL);
    oled_task_user(); assert(memcmp(oled_frame, expected, sizeof(expected)) == 0);
    link_screen_snapshot_t invalid = sent; invalid.mode = 255;
    receiver(sizeof(invalid), &invalid, 0, NULL);
    receiver(1, &invalid, 0, NULL);
    oled_task_user(); assert(memcmp(oled_frame, expected, sizeof(expected)) == 0);

    master = true; now = 100;
    keyrecord_t button = {.event = {.key = {9, 5}, .pressed = true}};
    assert(!link_oled_process_record(0x1234, &button)); // Old VIA Play/Pause mapping is also intercepted.
    housekeeping_task_user(); assert(sent.mode == LINK_RAIN);
    oled_task_user(); assert(powered && brightness == 0);
    button.event.pressed = false; assert(!link_oled_process_record(0x1234, &button));
    now = 125; button.event.pressed = true; link_oled_process_record(LINK_DISPLAY, &button);
    housekeeping_task_user(); assert(sent.mode == LINK_OFF);
    oled_task_user(); assert(!powered && drains == 1 && offs == 1);
    physical[0] = 1; now = 150; housekeeping_task_user(); oled_task_user();
    assert(!powered && sent.mode == LINK_OFF);
    now = 175; link_oled_process_record(LINK_DISPLAY, &button); housekeeping_task_user();
    oled_task_user(); assert(powered && sent.mode == LINK_DASHBOARD && brightness == OLED_BRIGHTNESS);
    // The same dimming/restoration applies to the secondary physical panel.
    master = false;
    link_screen_snapshot_t remote = sent; remote.mode = LINK_RAIN;
    receiver(sizeof(remote), &remote, 0, NULL); oled_task_user();
    assert(powered && brightness == 0);
    remote.mode = LINK_OFF; receiver(sizeof(remote), &remote, 0, NULL);
    oled_task_user(); assert(!powered);
    remote.mode = LINK_DASHBOARD; receiver(sizeof(remote), &remote, 0, NULL);
    oled_task_user(); assert(powered && brightness == OLED_BRIGHTNESS);
    master = true;
    button.event.key.row = 0; button.event.key.col = 0;
    assert(link_oled_process_record(0x0004, &button));
    assert(encoder_update_user(0, true));
    assert(oled_init_user(OLED_ROTATION_90) == OLED_ROTATION_0);
}

static void write_preview(const char *directory) {
    char path[2048];
    uint8_t frame[1024];
    link_screen_snapshot_t snapshot = {.version = LINK_SCREEN_PROTOCOL, .mods = 0x03, .layer = 2};
    snapshot.keys[1] = 4; snapshot.keys[4] = 2; snapshot.keys[7] = 8; snapshot.keys[9] = 2;
    snprintf(path, sizeof(path), "%s/dashboard.raw", directory);
    FILE *file = fopen(path, "wb"); assert(file);
    for (int left = 1; left >= 0; --left) {
        link_screen_render(&snapshot, left, frame); assert(fwrite(frame, 1, sizeof(frame), file) == sizeof(frame));
    }
    fclose(file);
    snprintf(path, sizeof(path), "%s/rain.raw", directory);
    file = fopen(path, "wb"); assert(file);
    snapshot.mode = LINK_RAIN;
    for (int tick = 0; tick < 48; ++tick) {
        snapshot.rain_frame = tick;
        for (int left = 1; left >= 0; --left) {
            link_screen_render(&snapshot, left, frame); assert(fwrite(frame, 1, sizeof(frame), file) == sizeof(frame));
        }
    }
    fclose(file);
}

int main(int argc, char **argv) {
    test_model(); test_rain_timeout(); test_render(); test_adapter();
    if (argc == 2) { write_preview(argv[1]); }
    puts("PASS: mode cycle, idle/wake, held SpaceFN, XOR modifiers, mirrored keys, RPC sync/retry, off power, timer wrap, frame bounds");
    return 0;
}
