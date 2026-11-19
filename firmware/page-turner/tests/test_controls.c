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

static void test_reports(void)
{
    const uint8_t next_keys[] = { 2, 0x4e, 0x4f };
    const uint8_t prev_keys[] = { 1, 0x4b, 0x50 };
    for (page_profile_t profile = PROFILE_VOLUME; profile < PROFILE_COUNT; ++profile) {
        for (unsigned inverted = 0; inverted <= 1; ++inverted) {
            for (page_action_t action = PAGE_NEXT; action <= PAGE_PREV; ++action) {
                bool volume = profile == PROFILE_VOLUME;
                unsigned key_index = volume ? 0 : 2;
                page_report_t report = page_report_for(profile, inverted, action);
                assert(report.id == (volume ? REPORT_CONSUMER : REPORT_KEYBOARD));
                assert(report.size == (volume ? 1 : 8));
                const uint8_t *keys = ((action == PAGE_NEXT) != inverted) ? next_keys : prev_keys;
                for (unsigned i = 0; i < sizeof(report.bytes); ++i) {
                    assert(report.bytes[i] == (i == key_index ? keys[profile] : 0));
                }
            }
        }
    }
    assert(page_report_for(PROFILE_VOLUME, false, PAGE_NONE).id == 0);
    assert(page_report_for(PROFILE_VOLUME, false, PAGE_PAIR_GESTURE).id == 0);
    assert(page_report_for(PROFILE_COUNT, false, PAGE_NEXT).id == 0);
}

int main(void)
{
    test_button_taps();
    test_hold_and_clock_wrap();
    test_pairing_chord();
    test_reports();
    puts("PASS: 200 simulated taps, debounce, hold, pairing chord, timer wrap, and all HID mappings");
    return 0;
}
