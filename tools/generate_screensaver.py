from __future__ import annotations

import io
import re
from pathlib import Path
from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "include"
parts = sorted(INCLUDE.glob("screensaver_part*.inc"))
if not parts:
    raise SystemExit("No screensaver_part*.inc files found")

raw_text = "\n".join(p.read_text(encoding="utf-8") for p in parts)
raw = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", raw_text))
if not raw.startswith(b"\xff\xd8"):
    raise SystemExit("Embedded screensaver source is not a JPEG")

img = Image.open(io.BytesIO(raw)).convert("RGB")
img = ImageOps.fit(img, (135, 240), method=Image.Resampling.LANCZOS, centering=(0.5, 0.5))

pixels = []
for r, g, b in img.getdata():
    pixels.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

out = INCLUDE / "screensaver_rgb565.h"
with out.open("w", encoding="utf-8") as f:
    f.write("#pragma once\n#include <Arduino.h>\n\n")
    f.write("#define PN_SCREENSAVER_RGB565 1\n")
    f.write("constexpr int PN_SCREENSAVER_WIDTH = 135;\n")
    f.write("constexpr int PN_SCREENSAVER_HEIGHT = 240;\n")
    f.write("static const uint16_t PN_SCREENSAVER_PIXELS[] PROGMEM = {\n")
    for i in range(0, len(pixels), 16):
        row = pixels[i:i+16]
        f.write("  " + ", ".join(f"0x{v:04X}" for v in row))
        f.write(",\n" if i + 16 < len(pixels) else "\n")
    f.write("};\n")
    f.write("static_assert(sizeof(PN_SCREENSAVER_PIXELS) / sizeof(PN_SCREENSAVER_PIXELS[0]) == 135 * 240, \"screensaver size mismatch\");\n")

print(f"Generated {out} from {len(raw)} JPEG bytes")
