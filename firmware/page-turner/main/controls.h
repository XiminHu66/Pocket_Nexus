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
    PROFILE_COUNT
} page_profile_t;

typedef enum { PAGE_NONE, PAGE_NEXT, PAGE_PREV, PAGE_MODE_GESTURE, PAGE_DEVICE_GESTURE } page_action_t;
typedef enum { KEY_FRONT = 1, KEY_SIDE = 2, KEYS_BOTH = 3 } page_keys_t;

enum { REPORT_KEYBOARD = 1, REPORT_CONSUMER = 2, REPORT_MOUSE = 3 };
enum { REPORT_KEYBOARD_SIZE = 8, REPORT_CONSUMER_SIZE = 1, REPORT_MOUSE_SIZE = 4 };

typedef struct {
    uint8_t raw;
    uint8_t stable;
    uint8_t gesture;
    uint32_t changed_at;
    uint32_t chord_at;
    bool mode_pending;
    bool devices_fired;
} page_buttons_t;

typedef struct {
    uint8_t id;
    uint8_t size;
    uint8_t bytes[REPORT_KEYBOARD_SIZE];
} page_report_t;

page_action_t page_buttons_poll(page_buttons_t *buttons, uint8_t raw, uint32_t now);
page_report_t page_report_for(page_profile_t profile, bool invert, page_action_t action);
const char *page_profile_name(page_profile_t profile);

#ifdef __cplusplus
}
#endif
