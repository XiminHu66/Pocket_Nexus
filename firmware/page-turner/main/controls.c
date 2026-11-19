#include "controls.h"

enum { DEBOUNCE_MS = 25, CHORD_MS = 5000 };

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
            // A chord consumes both keys until both are released.
            buttons->gesture |= raw;
        } else {
            uint8_t gesture = buttons->gesture;
            buttons->gesture = 0;
            buttons->chord_fired = false;
            if (gesture == KEY_FRONT) {
                return PAGE_NEXT;
            }
            if (gesture == KEY_SIDE) {
                return PAGE_PREV;
            }
        }
    }
    if (buttons->stable == KEYS_BOTH && raw == KEYS_BOTH && !buttons->chord_fired &&
        now - buttons->chord_at >= CHORD_MS) {
        buttons->chord_fired = true;
        return PAGE_PAIR_GESTURE;
    }
    return PAGE_NONE;
}

page_report_t page_report_for(page_profile_t profile, bool invert, page_action_t action)
{
    if ((action != PAGE_NEXT && action != PAGE_PREV) || (unsigned)profile >= PROFILE_COUNT) {
        return (page_report_t){0};
    }
    bool next = (action == PAGE_NEXT) != invert;
    if (profile == PROFILE_VOLUME) {
        return (page_report_t){ .id = REPORT_CONSUMER, .size = 1, .bytes = { next ? 2 : 1 } };
    }
    uint8_t key = profile == PROFILE_PAGE ? (next ? 0x4e : 0x4b) : (next ? 0x4f : 0x50);
    return (page_report_t){ .id = REPORT_KEYBOARD, .size = 8, .bytes = { 0, 0, key } };
}

const char *page_profile_name(page_profile_t profile)
{
    static const char *const names[] = { "VOLUME", "PAGE", "ARROW" };
    return (unsigned)profile < PROFILE_COUNT ? names[profile] : "?";
}
