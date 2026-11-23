#include "controls.h"

enum {
    HOME_STEPS = 20, HOME_DELTA = 120, AIM_STEPS = 5, DRAG_STEPS = 10,
    MOVE_MS = 12, DOWN_MS = 32, DRAG_MS = 15,
    // Portrait starting offsets in relative mouse counts, inward from
    // a clamped corner. Keep the button up until the pointer is inside the app.
    AIM_VERTICAL_X = 145, AIM_VERTICAL_Y = 210, AIM_HORIZONTAL_X = 95, AIM_HORIZONTAL_Y = 285,
    DRAG_VERTICAL = 380, DRAG_HORIZONTAL = 190,
};

page_swipe_direction_t page_swipe_for(page_profile_t profile, bool invert, page_action_t action)
{
    if (action != PAGE_NEXT && action != PAGE_PREV) {
        return SWIPE_NONE;
    }
    bool next = (action == PAGE_NEXT) != invert;
    if (profile == PROFILE_IPHONE_VERTICAL) {
        return next ? SWIPE_UP : SWIPE_DOWN;
    }
    if (profile == PROFILE_IPHONE_HORIZONTAL) {
        return next ? SWIPE_LEFT : SWIPE_RIGHT;
    }
    return SWIPE_NONE;
}

bool page_swipe_frame(page_swipe_direction_t direction, unsigned step, uint8_t *bytes, uint8_t *delay_ms)
{
    if (direction <= SWIPE_NONE || direction > SWIPE_RIGHT) {
        return false;
    }

    bool vertical = direction == SWIPE_UP || direction == SWIPE_DOWN;
    int x_sign = direction == SWIPE_LEFT ? -1 : 1;
    int y_sign = direction == SWIPE_UP ? -1 : 1;
    int x = 0, y = 0;
    uint8_t buttons = 0;

    if (step < HOME_STEPS) {
        x = -x_sign * HOME_DELTA;
        y = -y_sign * HOME_DELTA;
        *delay_ms = MOVE_MS;
    } else if (step < HOME_STEPS + AIM_STEPS) {
        x = x_sign * (vertical ? AIM_VERTICAL_X : AIM_HORIZONTAL_X) / AIM_STEPS;
        y = y_sign * (vertical ? AIM_VERTICAL_Y : AIM_HORIZONTAL_Y) / AIM_STEPS;
        *delay_ms = MOVE_MS;
    } else if (step == HOME_STEPS + AIM_STEPS) {
        buttons = 1;
        *delay_ms = DOWN_MS;
    } else if (step <= HOME_STEPS + AIM_STEPS + DRAG_STEPS) {
        buttons = 1;
        x = vertical ? 0 : x_sign * DRAG_HORIZONTAL / DRAG_STEPS;
        y = vertical ? y_sign * DRAG_VERTICAL / DRAG_STEPS : 0;
        *delay_ms = DRAG_MS;
    } else {
        // The BLE transport sends a zero report to release the button.
        return false;
    }

    bytes[0] = buttons;
    bytes[1] = (uint8_t)x;
    bytes[2] = (uint8_t)y;
    bytes[3] = 0;
    return true;
}
