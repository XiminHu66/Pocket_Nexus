#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The first three IDs are the stored v0.1 volume, page, and arrow profiles.
typedef enum {
    PROFILE_BOOX = 0, PROFILE_PPT = 1, PROFILE_MANGA = 2,
    PROFILE_VIDEO_KEYS = 3, PROFILE_VIDEO_WHEEL = 4, PROFILE_MANGA_SCROLL = 5,
    PROFILE_IPHONE_VERTICAL = 6, PROFILE_IPHONE_HORIZONTAL = 7, PROFILE_IPHONE_SCROLL = 8,
    PROFILE_COUNT
} page_profile_t;

typedef enum {
    PAGE_NONE, PAGE_NEXT, PAGE_PREV, PAGE_MODE_GESTURE, PAGE_DEVICE_GESTURE, PAGE_MODE_MENU_GESTURE,
} page_action_t;
typedef enum { KEY_FRONT = 1, KEY_SIDE = 2, KEYS_BOTH = 3 } page_keys_t;

typedef enum {
    SWIPE_NONE, SWIPE_UP, SWIPE_DOWN, SWIPE_LEFT, SWIPE_RIGHT, SWIPE_SCROLL_UP, SWIPE_SCROLL_DOWN,
} page_swipe_direction_t;

enum { REPORT_KEYBOARD = 1, REPORT_CONSUMER = 2, REPORT_MOUSE = 3 };
enum { REPORT_KEYBOARD_SIZE = 8, REPORT_CONSUMER_SIZE = 1, REPORT_MOUSE_SIZE = 4 };

typedef struct {
    uint8_t raw;
    uint8_t stable;
    uint8_t gesture;
    uint32_t changed_at;
    uint32_t chord_at;
    page_action_t pending_chord;
} page_buttons_t;

typedef struct {
    uint8_t id;
    uint8_t size;
    uint8_t bytes[REPORT_KEYBOARD_SIZE];
} page_report_t;

page_action_t page_buttons_poll(page_buttons_t *buttons, uint8_t raw, uint32_t now);
page_report_t page_report_for(page_profile_t profile, bool invert, page_action_t action);
page_swipe_direction_t page_swipe_for(page_profile_t profile, bool invert, page_action_t action);
bool page_swipe_frame(page_swipe_direction_t direction, unsigned step, uint8_t *bytes, uint8_t *delay_ms);
page_profile_t page_iphone_next_profile(page_profile_t profile);
const char *page_profile_name(page_profile_t profile);

#ifdef __cplusplus
}
#endif
