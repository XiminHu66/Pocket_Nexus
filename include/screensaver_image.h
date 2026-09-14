#pragma once
#include <Arduino.h>

#define PN_SCREENSAVER_JPEG 1
constexpr int PN_SCREENSAVER_WIDTH = 135;
constexpr int PN_SCREENSAVER_HEIGHT = 240;

static const uint8_t PN_SCREENSAVER_JPG[] PROGMEM = {
#include "screensaver_part0.inc"
,
#include "screensaver_part1.inc"
,
#include "screensaver_part2.inc"
,
#include "screensaver_part3.inc"
};

constexpr size_t PN_SCREENSAVER_JPG_SIZE = sizeof(PN_SCREENSAVER_JPG);
