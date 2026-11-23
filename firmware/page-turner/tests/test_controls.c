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

static void test_pairing_chord(void)
{
    page_buttons_t buttons = {0};
    assert(page_buttons_poll(&buttons, KEY_FRONT, 100) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEY_FRONT, 125) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEYS_BOTH, 200) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEYS_BOTH, 225) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEYS_BOTH, 5224) == PAGE_NONE);
    assert(page_buttons_poll(&buttons, KEYS_BOTH, 5225) == PAGE_PAIR_GESTURE);
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
    assert(page_report_for(PROFILE_BOOX, false, PAGE_PAIR_GESTURE).id == 0);
    assert(page_report_for(PROFILE_COUNT, false, PAGE_NEXT).id == 0);
}

int main(void)
{
    test_button_taps();
    test_hold_and_clock_wrap();
    test_pairing_chord();
    test_mode_chord();
    test_reports();
    puts("PASS: 200 simulated taps, debounce, hold, mode / pairing chords, timer wrap, and all HID mappings");
    return 0;
}
