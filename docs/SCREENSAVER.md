# Pocket Nexus custom screen saver

Pocket Nexus v0.3 includes a manual and automatic screen saver.

- Open app **14 SAVER** and press the front blue **A** key to start it immediately.
- With no button interaction for 60 seconds, Pocket Nexus starts the screen saver automatically.
- Press either key to exit.
- IR receive and secure Wi-Fi setup suppress the automatic screen saver while they are active.

## Default mode

If no custom image is compiled in, the screen saver shows a minimal clock/date/battery layout.

## Custom image mode

The firmware optionally includes `screensaver_image.h`. When present, it must define:

```cpp
#pragma once
#include <stdint.h>

#define PN_SCREENSAVER_WIDTH 135
#define PN_SCREENSAVER_HEIGHT 240

static const uint16_t PN_SCREENSAVER_RGB565[135 * 240] PROGMEM = {
  // RGB565 pixels, row-major, portrait orientation
};
```

The image is displayed full-screen at native 135×240 resolution. The firmware uses a 16-bit M5Canvas so photographs and illustrations have better color fidelity than the previous 8-bit canvas.

For this project the preferred workflow is: send the desired image in ChatGPT, crop/resize it to 135×240, convert it to RGB565, commit the generated header to the repository, let GitHub Actions build, then reinstall from the same browser installer. No local VS Code or image-conversion environment is required.
