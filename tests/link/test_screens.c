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

static unsigned drop_count(const link_screen_snapshot_t *snapshot, unsigned side) {
    unsigned count = 0;
    for (unsigned i = 0; i < LINK_SCREEN_RAIN_DROPS; ++i) {
        count += snapshot->rain_drops[side][i] != 0;
    }
    return count;
}

static void test_brightness_and_reactive_rain(void) {
    link_screen_state_t state;
    link_screen_snapshot_t snapshot;
    uint8_t keys[10] = {0};
    link_screen_init(&state, 0);
    assert(state.normal_brightness == 255);
    link_screen_adjust_brightness(&state, true); assert(state.normal_brightness == 255);
    for (int i = 0; i < 32; ++i) { link_screen_adjust_brightness(&state, false); }
    assert(state.normal_brightness == 0);
    link_screen_adjust_brightness(&state, false); assert(state.normal_brightness == 0);
    for (int i = 0; i < 32; ++i) { link_screen_adjust_brightness(&state, true); }
    assert(state.normal_brightness == 255);
    state.normal_brightness = 160;
    update(&state, &snapshot, keys, 0); assert(snapshot.brightness == 160 && !snapshot.screensaver);
    link_screen_cycle(&state, LINK_DASHBOARD, 1);
    update(&state, &snapshot, keys, 1);
    assert(snapshot.mode == LINK_RAIN && snapshot.brightness == 160 && !snapshot.screensaver);
    keys[0] = 1;
    update(&state, &snapshot, keys, 2);
    assert(drop_count(&snapshot, 0) == 1 && drop_count(&snapshot, 1) == 0);
    uint16_t first = snapshot.rain_drops[0][0];
    update(&state, &snapshot, keys, 502); // Held key never creates more drops.
    assert(drop_count(&snapshot, 0) == 1 && snapshot.rain_drops[0][0] > first);
    keys[0] = 0; update(&state, &snapshot, keys, 503);
    assert(drop_count(&snapshot, 0) == 1); // Release does not create a drop.
    keys[0] = 1; update(&state, &snapshot, keys, 504);
    assert(drop_count(&snapshot, 0) == 2);
    keys[7] = 2; update(&state, &snapshot, keys, 505);
    assert(drop_count(&snapshot, 0) == 2 && drop_count(&snapshot, 1) == 1);
    update(&state, &snapshot, keys, 2255); // All trails have fully fallen offscreen.
    assert(drop_count(&snapshot, 0) == 0 && drop_count(&snapshot, 1) == 0);
    memset(keys, 0, sizeof(keys)); update(&state, &snapshot, keys, 2256);
    update(&state, &snapshot, keys, 62255); assert(!snapshot.screensaver && snapshot.brightness == 160);
    update(&state, &snapshot, keys, 62256);
    assert(snapshot.mode == LINK_RAIN && snapshot.screensaver && snapshot.brightness == 0);
    link_screen_adjust_brightness(&state, true); // Selected level cannot brighten an idle snapshot by itself.
    update(&state, &snapshot, keys, 62257); assert(snapshot.brightness == 0 && state.normal_brightness == 176);
    link_screen_activity(&state, 62258); // Turning the encoder restores active manual rain.
    update(&state, &snapshot, keys, 62258);
    assert(snapshot.mode == LINK_RAIN && !snapshot.screensaver && snapshot.brightness == 176);
    update(&state, &snapshot, keys, 600001); // Dimming/waking did not restart the hard deadline.
    assert(snapshot.mode == LINK_OFF);
    keys[0] = 1; update(&state, &snapshot, keys, 600002);
    assert(snapshot.mode == LINK_DASHBOARD && snapshot.brightness == 176);

    // Automatic screensaver always stays at minimum, then restores dashboard brightness.
    link_screen_init(&state, 0); state.normal_brightness = 80;
    memset(keys, 0, sizeof(keys)); update(&state, &snapshot, keys, 60000);
    assert(snapshot.mode == LINK_RAIN && snapshot.screensaver && snapshot.brightness == 0);
    keys[1] = 4; update(&state, &snapshot, keys, 60001);
    assert(snapshot.mode == LINK_DASHBOARD && snapshot.brightness == 80 && !drop_count(&snapshot, 0));

    // Simultaneous/burst input is bounded to eight live trails on each half.
    link_screen_cycle(&state, LINK_DASHBOARD, 60002);
    memset(keys, 0x3F, sizeof(keys)); update(&state, &snapshot, keys, 60002);
    assert(drop_count(&snapshot, 0) == LINK_SCREEN_RAIN_DROPS && drop_count(&snapshot, 1) == LINK_SCREEN_RAIN_DROPS);
    assert(link_screen_snapshot_valid(&snapshot));
    assert(sizeof(snapshot) <= RPC_M2S_BUFFER_SIZE);
    snapshot.rain_drops[1][0] = LINK_SCREEN_DROP_TICKS * 64 + 1;
    assert(!link_screen_snapshot_valid(&snapshot));
    snapshot.rain_drops[1][0] = 0; snapshot.screensaver = 2;
    assert(!link_screen_snapshot_valid(&snapshot));
    snapshot.screensaver = 1; snapshot.brightness = 1;
    assert(!link_screen_snapshot_valid(&snapshot));
    snapshot.brightness = 0; snapshot.mode = LINK_DASHBOARD;
    assert(!link_screen_snapshot_valid(&snapshot));

    // Drop age also works across the millisecond timer rollover.
    link_screen_init(&state, UINT32_MAX - 100);
    link_screen_cycle(&state, LINK_DASHBOARD, UINT32_MAX - 100);
    memset(keys, 0, sizeof(keys)); keys[5] = 1;
    update(&state, &snapshot, keys, UINT32_MAX - 99);
    assert(drop_count(&snapshot, 1) == 1);
    update(&state, &snapshot, keys, 0);
    assert(((snapshot.rain_drops[1][0] - 1) >> 6) == 2);
    update(&state, &snapshot, keys, 1650); assert(drop_count(&snapshot, 1) == 0);
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
            assert(lx == 63 - rx && ly == ry && lh == 1 && rh == 1);
            assert(lx >= 0 && lx <= 20 && ly >= 0 && ly < 19);
        }
    }
    // Every physical key adds exactly one unique pixel, with no idle key boxes
    // and no overlap with the silhouette or miniature OLED rectangle.
    memset(snapshot.keys, 0, sizeof(snapshot.keys));
    for (int side = 0; side < 2; ++side) {
        uint8_t idle[1024];
        link_screen_render(&snapshot, side == 0, idle);
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 6; ++col) {
                int x, y, height;
                link_screen_key_position(side == 0, row, col, &x, &y, &height);
                assert(!bit(idle, x, y));
                snapshot.keys[row + side * 5] = 1 << col;
                link_screen_render(&snapshot, side == 0, guarded.frame);
                int changed = 0;
                for (int i = 0; i < 1024; ++i) {
                    changed += __builtin_popcount((unsigned)(idle[i] ^ guarded.frame[i]));
                }
                assert(changed == 1 && bit(guarded.frame, x, y));
                snapshot.keys[row + side * 5] = 0;
                for (int other = row * 6 + col + 1; other < 30; ++other) {
                    int ox, oy, oh;
                    link_screen_key_position(side == 0, other / 6, other % 6, &ox, &oy, &oh);
                    assert(ox != x || oy != y);
                }
            }
        }
    }
    // Inactive indicators have no border; active corners are rounded.
    snapshot.mods = 0; snapshot.caps_lock = false;
    link_screen_render(&snapshot, true, guarded.frame);
    assert(!bit(guarded.frame, 2, 46) && !bit(guarded.frame, 3, 47));
    snapshot.mods = 1;
    link_screen_render(&snapshot, true, guarded.frame);
    assert(!bit(guarded.frame, 2, 46) && bit(guarded.frame, 5, 46) && bit(guarded.frame, 2, 49));
    for (int layer = 0; layer < 32; ++layer) {
        snapshot.layer = layer;
        link_screen_render(&snapshot, false, guarded.frame);
    }
    snapshot.mode = LINK_RAIN;
    for (int tick = 0; tick < 1024; ++tick) {
        snapshot.rain_frame = tick;
        link_screen_render(&snapshot, tick % 2, guarded.frame);
    }
    link_screen_render(&snapshot, true, before);
    link_screen_render(&snapshot, false, right_frame);
    int drop_x = 0;
    while (bit(before, drop_x, 40)) { ++drop_x; }
    assert(drop_x < 64);
    snapshot.rain_drops[0][0] = 1 + 10 * 64 + drop_x;
    link_screen_render(&snapshot, true, guarded.frame);
    assert(bit(guarded.frame, drop_x, 40) && memcmp(before, guarded.frame, 1024));
    link_screen_render(&snapshot, false, guarded.frame);
    assert(!memcmp(right_frame, guarded.frame, 1024)); // A left press adds no right-side trail.
    snapshot.rain_drops[0][0] = 0;
    memcpy(before, guarded.frame, sizeof(before)); ++snapshot.rain_frame;
    link_screen_render(&snapshot, true, guarded.frame);
    assert(memcmp(before, guarded.frame, sizeof(before)) != 0);
    snapshot.mode = LINK_OFF; link_screen_render(&snapshot, true, guarded.frame);
    for (int i = 0; i < 1024; ++i) { assert(!guarded.frame[i]); }
    for (int i = 0; i < 16; ++i) { assert(guarded.pre[i] == 0xA5 && guarded.post[i] == 0xA5); }
}

static void test_last_keys(void) {
    link_screen_state_t state;
    link_screen_snapshot_t snapshot;
    uint8_t keys[10] = {0};
    link_screen_init(&state, 0);
    update(&state, &snapshot, keys, 1); assert(snapshot.last_key_valid == 0);
    link_screen_keypress(&state, 0, 5, KC_ESC);
    link_screen_keypress(&state, 9, 1, KC_SPACE);
    update(&state, &snapshot, keys, 2);
    assert(snapshot.last_key_valid == 3 && snapshot.last_key[0] == KC_ESC && snapshot.last_key[1] == KC_SPACE);
    link_screen_keypress(&state, 0, 0, KC_NO);
    link_screen_keypress(&state, 9, 0, KC_TRNS);
    link_screen_keypress(&state, 255, 0, KC_A);
    link_screen_keypress(&state, 0, 6, KC_B);
    update(&state, &snapshot, keys, 3);
    assert(snapshot.last_key[0] == KC_ESC && snapshot.last_key[1] == KC_SPACE);
    link_screen_keypress(&state, 2, 4, KC_A);
    update(&state, &snapshot, keys, 4);
    assert(snapshot.last_key[0] == KC_A && snapshot.last_key[1] == KC_SPACE);
    update(&state, &snapshot, keys, 60000);
    assert(snapshot.mode == LINK_RAIN && snapshot.last_key[0] == KC_A && snapshot.last_key[1] == KC_SPACE);
    snapshot.version = 1; assert(!link_screen_snapshot_valid(&snapshot));
    snapshot.version = LINK_SCREEN_PROTOCOL; snapshot.last_key_valid = 4;
    assert(!link_screen_snapshot_valid(&snapshot));

    const struct { uint16_t code; const char *name; } cases[] = {
        {KC_A, "A"}, {KC_Z, "Z"}, {KC_0, "0"}, {KC_9, "9"}, {KC_ESC, "ESC"},
        {KC_SPACE, "SPACE"}, {KC_F12, "F12"}, {KC_F24, "F24"}, {KC_KP_0, "KP0"},
        {KC_RIGHT, "RIGHT"}, {KC_LSFT, "LSHFT"}, {KC_RCTL, "RCTRL"},
        {KC_MUTE, "MUTE"}, {KC_MPLY, "PLAY"}, {QK_BOOT, "BOOT"},
        {0x412C, "LT1"}, {0x422C, "LT2"}, {QK_MOMENTARY + 31, "MO31"},
        {0x2104, "CTRL"}, {0x2404, "ALT"}, {0x2304, "MT3"},
        {0x0204, "CHORD"}, {QK_MACRO + 127, "MACRO127"}, {0xFFFF, "KEY"},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        struct { char pre; char name[LINK_SCREEN_NAME_BYTES]; char post; } guarded = {.pre = 'X', .post = 'Y'};
        link_screen_key_name(cases[i].code, guarded.name);
        assert(!strcmp(guarded.name, cases[i].name) && guarded.pre == 'X' && guarded.post == 'Y');
    }
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
    assert(sent.last_key_valid == 2 && sent.last_key[1] == LINK_DISPLAY);
    oled_task_user(); assert(powered && brightness == OLED_BRIGHTNESS);
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
    link_screen_snapshot_t remote = sent; remote.mode = LINK_RAIN; remote.screensaver = 1; remote.brightness = 0;
    receiver(sizeof(remote), &remote, 0, NULL); oled_task_user();
    assert(powered && brightness == 0);
    remote.mode = LINK_OFF; remote.screensaver = 0; receiver(sizeof(remote), &remote, 0, NULL);
    oled_task_user(); assert(!powered);
    remote.mode = LINK_DASHBOARD; remote.brightness = OLED_BRIGHTNESS; receiver(sizeof(remote), &remote, 0, NULL);
    oled_task_user(); assert(powered && brightness == OLED_BRIGHTNESS);
    master = true;
    button.event.key.row = 0; button.event.key.col = 0;
    assert(link_oled_process_record(0x0004, &button));
    assert(encoder_update_user(0, true));
    assert(oled_init_user(OLED_ROTATION_90) == OLED_ROTATION_0);

    // Capture the resolved QMK action by physical half; releases and virtual
    // encoder events must never replace a last press. Layer-tap resolution
    // is supplied by QMK's tapping engine, represented here by tap.count.
    keyrecord_t press = {.event = {.key = {0, 5}, .pressed = true}};
    now = 200; assert(link_oled_process_record(KC_ESC, &press));
    housekeeping_task_user(); assert(sent.last_key[0] == KC_ESC);
    press.event.key.row = 6; press.event.key.col = 1;
    now = 225; assert(link_oled_process_record(KC_Y, &press));
    housekeeping_task_user(); assert(sent.last_key[0] == KC_ESC && sent.last_key[1] == KC_Y);
    press.event.pressed = false; now = 250;
    link_oled_process_record(KC_Z, &press); housekeeping_task_user(); assert(sent.last_key[1] == KC_Y);
    press.event.pressed = true; press.event.key.row = 9; press.event.key.col = 1; press.tap.count = 1;
    now = 275; link_oled_process_record(0x422C, &press); housekeeping_task_user();
    assert(sent.last_key[1] == KC_SPACE);
    press.tap.count = 0;
    now = 300; link_oled_process_record(0x422C, &press); housekeeping_task_user();
    assert(sent.last_key[1] == 0x422C);
    press.event.key.row = 4; press.event.key.col = 1; press.tap.count = 1;
    now = 325; link_oled_process_record(0x412C, &press); housekeeping_task_user();
    assert(sent.last_key[0] == KC_SPACE && sent.last_key[1] == 0x422C);
    press.tap.count = 0;
    now = 350; link_oled_process_record(0x412C, &press); housekeeping_task_user();
    assert(sent.last_key[0] == 0x412C);
    press.event.key.row = 2; press.tap.count = 1;
    now = 375; link_oled_process_record(0x2104, &press); housekeeping_task_user(); assert(sent.last_key[0] == KC_A);
    press.tap.count = 0;
    now = 400; link_oled_process_record(0x2104, &press); housekeeping_task_user(); assert(sent.last_key[0] == 0x2104);
    press.event.key.row = 255;
    now = 425; link_oled_process_record(KC_B, &press); housekeeping_task_user(); assert(sent.last_key[0] == 0x2104);
    // Slave receives both codes and renders its own, ignoring stale protocol 1.
    master = false; left = false;
    receiver(sizeof(sent), &sent, 0, NULL); oled_task_user();
    link_screen_render(&sent, false, expected); assert(!memcmp(expected, oled_frame, sizeof(expected)));
    remote = sent; remote.version = 1; remote.last_key[1] = KC_Z;
    receiver(sizeof(remote), &remote, 0, NULL); oled_task_user();
    assert(!memcmp(expected, oled_frame, sizeof(expected)));
    master = true; left = true;
    // Right rotation changes brightness without reaching the board's PgUp/PgDn
    // handler. Left volume is retained, and shared brightness reaches the slave.
    now = 450; assert(!encoder_update_user(1, false)); housekeeping_task_user();
    assert(sent.brightness == 239);
    oled_task_user(); assert(brightness == 239);
    master = false; receiver(sizeof(sent), &sent, 0, NULL); oled_task_user(); assert(brightness == 239);
    assert(!encoder_update_user(1, true)); // A non-master callback cannot change authoritative state.
    master = true; now = 475; housekeeping_task_user(); assert(sent.brightness == 239);
    now = 500; assert(encoder_update_user(0, true)); housekeeping_task_user(); assert(sent.brightness == 239);
    now = 525; assert(!encoder_update_user(1, true)); housekeeping_task_user(); assert(sent.brightness == 255);
    // Automatic rain remains dim; right rotation restores the selected dashboard level.
    memset(physical, 0, sizeof(physical)); now = 550; housekeeping_task_user();
    now = 60550; housekeeping_task_user(); oled_task_user();
    assert(sent.mode == LINK_RAIN && sent.screensaver && brightness == 0);
    now = 60575; assert(!encoder_update_user(1, false)); housekeeping_task_user(); oled_task_user();
    assert(sent.mode == LINK_DASHBOARD && !sent.screensaver && brightness == 239);
    // Dedicated rain keeps that level, adds a drop on the physical right, and
    // transmits the trail to the remote renderer without a second local event.
    button.event.key.row = 9; button.event.key.col = 5; button.event.pressed = true;
    now = 60600; link_oled_process_record(LINK_DISPLAY, &button); housekeeping_task_user();
    physical[6] = 4; now = 60625; housekeeping_task_user(); oled_task_user();
    assert(sent.mode == LINK_RAIN && sent.brightness == 239 && drop_count(&sent, 1) == 1);
    master = false; left = false; receiver(sizeof(sent), &sent, 0, NULL); oled_task_user();
    assert(brightness == 239);
    link_screen_render(&sent, false, expected); assert(!memcmp(expected, oled_frame, sizeof(expected)));
    remote = sent; remote.version = 2; remote.brightness = 123;
    receiver(sizeof(remote), &remote, 0, NULL); oled_task_user(); assert(brightness == 239);
    master = true; left = true;
}

static void write_preview(const char *directory) {
    char path[2048];
    uint8_t frame[1024];
    link_screen_snapshot_t snapshot = {.version = LINK_SCREEN_PROTOCOL, .mods = 0x03, .layer = 2,
                                       .last_key = {KC_ESC, KC_SPACE}, .last_key_valid = 3};
    memset(snapshot.keys, 0x3F, sizeof(snapshot.keys));
    snprintf(path, sizeof(path), "%s/dashboard.raw", directory);
    FILE *file = fopen(path, "wb"); assert(file);
    for (int left = 1; left >= 0; --left) {
        link_screen_render(&snapshot, left, frame); assert(fwrite(frame, 1, sizeof(frame), file) == sizeof(frame));
    }
    fclose(file);
    memset(snapshot.keys, 0, sizeof(snapshot.keys));
    snapshot.keys[1] = 4; snapshot.keys[4] = 2; snapshot.keys[7] = 8; snapshot.keys[9] = 2;
    snprintf(path, sizeof(path), "%s/dashboard-live.raw", directory);
    file = fopen(path, "wb"); assert(file);
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
    snprintf(path, sizeof(path), "%s/rain-reactive.raw", directory);
    file = fopen(path, "wb"); assert(file);
    link_screen_state_t state;
    link_screen_init(&state, 0);
    link_screen_cycle(&state, LINK_DASHBOARD, 0);
    uint8_t keys[10] = {0};
    for (int tick = 0; tick < 96; ++tick) {
        memset(keys, 0, sizeof(keys));
        if (tick % 5 == 0) { keys[(tick / 5) % 4] = 1 << ((tick / 5) % 6); }
        if (tick % 7 == 0) { keys[5 + (tick / 7) % 4] = 1 << ((tick / 7) % 6); }
        update(&state, &snapshot, keys, tick * 50);
        for (int left = 1; left >= 0; --left) {
            link_screen_render(&snapshot, left, frame); assert(fwrite(frame, 1, sizeof(frame), file) == sizeof(frame));
        }
    }
    fclose(file);
}

int main(int argc, char **argv) {
    test_model(); test_rain_timeout(); test_brightness_and_reactive_rain(); test_render(); test_last_keys(); test_adapter();
    if (argc == 2) { write_preview(argv[1]); }
    puts("PASS: brightness encoder/clamps/sync, active-vs-idle rain, reactive trails/burst/expiry/wrap, RPC v3, dashboard pixels/names/SpaceFN, rain hard deadline and wake rules");
    return 0;
}
