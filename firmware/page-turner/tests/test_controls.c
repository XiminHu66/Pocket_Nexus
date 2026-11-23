#include "controls.h"

#include <assert.h>
#include <stdio.h>

static void test_button_taps(void)
{
    page_buttons_t buttons = {0};
    uint32_t now = 500;
    for (unsigned count = 0; count < 200; ++count) {
        uint8_t key = count < 100 ? KEY_FRONT : KEY_SIDE;
        assert(page_buttons_poll(&buttons, key, now) == PAGE_NONE);
        assert(page_buttons_poll(&buttons, 0, now + 2) == PAGE_NONE);
        assert(page_buttons_poll(&buttons, key, now + 4) == PAGE_NONE);
        assert(page_buttons_poll(&buttons, key, now + 29) == PAGE_NONE);
        assert(page_buttons_poll(&buttons, 0, now + 43) == PAGE_NONE);
        assert(page_buttons_poll(&buttons, key, now + 45) == PAGE_NONE);
        assert(page_buttons_poll(&buttons, 0, now + 48) == PAGE_NONE);
        assert(page_buttons_poll(&buttons, 0, now + 72) == PAGE_NONE);
        assert(page_buttons_poll(&buttons, 0, now + 73) ==
               (key == KEY_FRONT ? PAGE_NEXT : PAGE_PREV));
        assert(page_buttons_poll(&buttons, 0, now + 78) == PAGE_NONE);
        now += 85;
    }
}

static void test_hold_and_clock_wrap(void)
{
    page_buttons_t buttons = {0};
    uint32_t start = UINT32_MAX - 10;
    assert(page_buttons_poll(&buttons, KEY_SIDE, start) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEY_SIDE, start + 25) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEY_SIDE, start + 10000) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, 0, start + 11000) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, 0, start + 11025) == PAGE_PREV);
    assert(page_buttons_poll(&buttons, 0, start + 11050) == PAGE_NONE);
}

static void test_device_chord(void)
{
    page_buttons_t buttons = {0};
    assert(page_buttons_poll(&buttons, KEY_FRONT, 100) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEY_FRONT, 125) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEYS_BOTH, 200) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEYS_BOTH, 225) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEYS_BOTH, 3224) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEYS_BOTH, 3225) == PAGE_DEVICE_GESTURE);
    assert(page_buttons_poll(&buttons, KEYS_BOTH, 7000) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEY_SIDE, 7100) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEY_SIDE, 7125) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, 0, 7200) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, 0, 7225) == PAGE_NONE);

    assert(page_buttons_poll(&buttons, KEYS_BOTH, 8000) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEYS_BOTH, 8025) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, 0, 8125) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, 0, 8150) == PAGE_NONE);
}

static void test_mode_chord(void)
{
    page_buttons_t buttons = {0};
    assert(page_buttons_poll(&buttons, KEYS_BOTH, 100) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEYS_BOTH, 125) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEYS_BOTH, 724) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEYS_BOTH, 725) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEY_FRONT, 900) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEY_FRONT, 925) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, 0, 950) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, 0, 975) == PAGE_MODE_GESTURE);
    assert(page_buttons_poll(&buttons, 0, 1000) == PAGE_NONE);

    assert(page_buttons_poll(&buttons, KEY_SIDE, 1050) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEY_SIDE, 1075) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, 0, 1100) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, 0, 1125) == PAGE_PREV);
}

static void test_reports(void)
{
    static const struct {
        uint8_t id, size, key_index, next, prev;
    } expected[PROFILE_COUNT] = {
        [PROFILE_BOOX] = { REPORT_CONSUMER, 1, 0, 2, 1 },
        [PROFILE_PPT] = { REPORT_KEYBOARD, 8, 2, 0x4e, 0x4b },
        [PROFILE_MANGA] = { REPORT_KEYBOARD, 8, 2, 0x4f, 0x50 },
        [PROFILE_VIDEO_KEYS] = { REPORT_KEYBOARD, 8, 2, 0x51, 0x52 },
        [PROFILE_VIDEO_WHEEL] = { REPORT_MOUSE, 4, 3, 0xfd, 3 },
        [PROFILE_MANGA_SCROLL] = { REPORT_KEYBOARD, 8, 2, 0x4e, 0x4b },
    };
    for (page_profile_t profile = PROFILE_BOOX; profile < PROFILE_COUNT; ++profile) {
        for (unsigned inverted = 0; inverted <= 1; ++inverted) {
            for (page_action_t action = PAGE_NEXT; action <= PAGE_PREV; ++action) {
                page_report_t report = page_report_for(profile, inverted, action);
                assert(report.id == expected[profile].id);
                assert(report.size == expected[profile].size);
                uint8_t key = ((action == PAGE_NEXT) != inverted) ?
                              expected[profile].next : expected[profile].prev;
                for (unsigned i = 0; i < sizeof(report.bytes); ++i) {
                    assert(report.bytes[i] == (i == expected[profile].key_index ? key : 0));
                }
            }
        }
    }
    assert(PROFILE_BOOX == 0 && PROFILE_PPT == 1 && PROFILE_MANGA == 2);
    assert(page_report_for(PROFILE_BOOX, false, PAGE_NONE).id == 0);
    assert(page_report_for(PROFILE_PPT, false, PAGE_MODE_GESTURE).id == 0);
    assert(page_report_for(PROFILE_BOOX, false, PAGE_DEVICE_GESTURE).id == 0);
    assert(page_report_for(PROFILE_COUNT, false, PAGE_NEXT).id == 0);
}

static void test_iphone_swipe_frames(void)
{
    assert(page_swipe_for(PROFILE_IPHONE_VERTICAL, false, PAGE_NEXT) == SWIPE_UP);
    assert(page_swipe_for(PROFILE_IPHONE_VERTICAL, false, PAGE_PREV) == SWIPE_DOWN);
    assert(page_swipe_for(PROFILE_IPHONE_HORIZONTAL, false, PAGE_NEXT) == SWIPE_LEFT);
    assert(page_swipe_for(PROFILE_IPHONE_HORIZONTAL, false, PAGE_PREV) == SWIPE_RIGHT);
    assert(page_swipe_for(PROFILE_IPHONE_VERTICAL, true, PAGE_NEXT) == SWIPE_DOWN);
    assert(page_swipe_for(PROFILE_IPHONE_HORIZONTAL, true, PAGE_PREV) == SWIPE_LEFT);
    assert(page_swipe_for(PROFILE_PPT, false, PAGE_NEXT) == SWIPE_NONE);
    assert(page_swipe_for(PROFILE_IPHONE_VERTICAL, false, PAGE_DEVICE_GESTURE) == SWIPE_NONE);

    for (page_swipe_direction_t dir = SWIPE_UP; dir <= SWIPE_RIGHT; ++dir) {
        uint8_t frame[REPORT_MOUSE_SIZE];
        uint8_t delay;
        unsigned step = 0, elapsed = 0, pressed = 0;
        int x = 0, y = 0, x_at_down = 0, y_at_down = 0;
        int drag_x = 0, drag_y = 0;
        // Model a small iPhone screen and start the pointer away from the chosen corner.
        int cursor_x = 250, cursor_y = 420;
        while (page_swipe_frame(dir, step, frame, &delay)) {
            assert(++step < 50);
            assert(frame[0] <= 1 && frame[3] == 0);
            assert(delay >= 10 && delay <= 40);
            int dx = (int8_t)frame[1];
            int dy = (int8_t)frame[2];
            cursor_x += dx;
            cursor_y += dy;
            if (cursor_x < 0) cursor_x = 0;
            if (cursor_y < 0) cursor_y = 0;
            if (cursor_x > 375) cursor_x = 375;
            if (cursor_y > 667) cursor_y = 667;
            elapsed += delay;
            if (frame[0]) {
                if (pressed++ == 0) {
                    assert(dx == 0 && dy == 0 && delay >= 25);
                    x_at_down = cursor_x;
                    y_at_down = cursor_y;
                }
                drag_x += dx;
                drag_y += dy;
                x = cursor_x;
                y = cursor_y;
            } else {
                assert(pressed == 0);
            }
        }
        assert(step == 36 && pressed == 11 && elapsed < 550);
        assert(x_at_down > 70 && x_at_down < 305 && y_at_down > 150 && y_at_down < 520);
        switch (dir) {
        case SWIPE_UP: assert(drag_x == 0 && drag_y < -300 && y < y_at_down); break;
        case SWIPE_DOWN: assert(drag_x == 0 && drag_y > 300 && y > y_at_down); break;
        case SWIPE_LEFT: assert(drag_y == 0 && drag_x < -150 && x < x_at_down); break;
        case SWIPE_RIGHT: assert(drag_y == 0 && drag_x > 150 && x > x_at_down); break;
        default: assert(false);
        }
    }
}

int main(void)
{
    test_button_taps();
    test_hold_and_clock_wrap();
    test_device_chord();
    test_mode_chord();
    test_reports();
    test_iphone_swipe_frames();
    puts("PASS: 200 simulated taps, debounce, hold, mode / device chords, timer wrap, HID mappings and iPhone swipe frames");
    return 0;
}
