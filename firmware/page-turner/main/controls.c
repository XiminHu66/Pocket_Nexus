#include "controls.h"

enum { DEBOUNCE_MS = 25, MODE_HOLD_MS = 600, DEVICE_HOLD_MS = 3000, VIDEO_WHEEL_STEPS = 3 };

page_action_t page_buttons_poll(page_buttons_t *buttons, uint8_t raw, uint32_t now)
{
    if (buttons->raw != raw) {
        buttons->raw = raw;
        buttons->changed_at = now;
    }
    if (buttons->stable != raw && now - buttons->changed_at >= DEBOUNCE_MS) {
        buttons->stable = raw;
        if (raw == KEYS_BOTH) {
            buttons->chord_at = now;
        }
        if (raw != 0) {
            // Consume both buttons until both are released, even if released one at a time.
            buttons->gesture |= raw;
        } else {
            uint8_t gesture = buttons->gesture;
            bool open_modes = buttons->mode_pending && !buttons->devices_fired;
            buttons->gesture = 0;
            buttons->mode_pending = false;
            buttons->devices_fired = false;
            if (gesture == KEY_FRONT) {
                return PAGE_NEXT;
            }
            if (gesture == KEY_SIDE) {
                return PAGE_PREV;
            }
            if (open_modes) {
                return PAGE_MODE_GESTURE;
            }
        }
    }
    if (buttons->stable == KEYS_BOTH && raw == KEYS_BOTH && !buttons->devices_fired) {
        uint32_t held = now - buttons->chord_at;
        if (held >= DEVICE_HOLD_MS) {
            buttons->devices_fired = true;
            return PAGE_DEVICE_GESTURE;
        }
        if (held >= MODE_HOLD_MS) {
            buttons->mode_pending = true;
        }
    }
    return PAGE_NONE;
}

page_report_t page_report_for(page_profile_t profile, bool invert, page_action_t action)
{
    if ((action != PAGE_NEXT && action != PAGE_PREV) || (unsigned)profile >= PROFILE_COUNT) {
        return (page_report_t){0};
    }
    bool next = (action == PAGE_NEXT) != invert;
    switch (profile) {
    case PROFILE_BOOX:
        return (page_report_t){ REPORT_CONSUMER, REPORT_CONSUMER_SIZE, { next ? 2 : 1 } };
    case PROFILE_VIDEO_WHEEL: {
        int wheel = next ? -VIDEO_WHEEL_STEPS : VIDEO_WHEEL_STEPS;
        return (page_report_t){ REPORT_MOUSE, REPORT_MOUSE_SIZE, { 0, 0, 0, (uint8_t)wheel } };
    }
    default: {
        static const uint8_t keyboard[][2] = {
            [PROFILE_PPT] = { 0x4e, 0x4b },          // Page Down, Page Up
            [PROFILE_MANGA] = { 0x4f, 0x50 },        // Right, Left
            [PROFILE_VIDEO_KEYS] = { 0x51, 0x52 },   // Down, Up
            [PROFILE_MANGA_SCROLL] = { 0x4e, 0x4b }, // Viewport down, up
        };
        return (page_report_t){ REPORT_KEYBOARD, REPORT_KEYBOARD_SIZE,
                                { 0, 0, keyboard[profile][next ? 0 : 1] } };
    }
    }
}

const char *page_profile_name(page_profile_t profile)
{
    static const char *const names[] = {
        [PROFILE_BOOX] = "BOOX Volume",
        [PROFILE_PPT] = "PPT",
        [PROFILE_MANGA] = "Manga Pages",
        [PROFILE_VIDEO_KEYS] = "Video PC Keys",
        [PROFILE_VIDEO_WHEEL] = "Video Phone Wheel",
        [PROFILE_MANGA_SCROLL] = "Manga Scroll",
    };
    return (unsigned)profile < PROFILE_COUNT ? names[profile] : "?";
}
