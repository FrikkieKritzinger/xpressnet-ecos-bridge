"""Illustrations of the OmniConnect OLED pages for the user documentation.

The firmware draws these on a 128x64 two-colour SSD1306 (top 16 px yellow, the rest blue)
with the Adafruit GFX default 5x7 font. This script reproduces that layout - same
coordinates, same text, same icons - from sample data, so the manual can show every page
without a camera. They are ILLUSTRATIONS, not photographs of the real screen; replace them
with photos if you prefer (same file names).

  python tools/render_oled_pages.py [output-dir]

Needs Pillow and the Adafruit GFX font (glcdfont.c), found in .pio/libdeps after a build.
"""
import os
import re
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "docs", "user", "images")

W, H = 128, 64
SPLIT = 16
YELLOW = (255, 214, 0)
BLUE = (70, 170, 255)
OFF = (6, 8, 14)
SCALE = 5


def load_font():
    for env in ("wemos", "debug", "wemos_dev"):
        path = os.path.join(ROOT, ".pio", "libdeps", env, "Adafruit GFX Library", "glcdfont.c")
        if os.path.exists(path):
            text = open(path, encoding="utf-8", errors="ignore").read()
            body = text[text.index("{"):text.rindex("}")]
            values = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{2}", body)]
            return values
    sys.exit("glcdfont.c not found - build the firmware once so PlatformIO downloads the library")


FONT = load_font()


class Screen:
    def __init__(self):
        self.px = [[0] * W for _ in range(H)]

    def pixel(self, x, y, on=1):
        if 0 <= x < W and 0 <= y < H:
            self.px[y][x] = on

    def text(self, x, y, s, invert=False):
        for ci, ch in enumerate(s):
            c = ord(ch)
            for col in range(5):
                line = FONT[c * 5 + col]
                for row in range(8):
                    if line & (1 << row):
                        self.pixel(x + ci * 6 + col, y + row, 0 if invert else 1)

    def hline(self, x0, x1, y):
        for x in range(x0, x1):
            self.pixel(x, y)

    def line(self, x0, y0, x1, y1):
        dx, dy = abs(x1 - x0), abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx - dy
        while True:
            self.pixel(x0, y0)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 > -dy:
                err -= dy
                x0 += sx
            if e2 < dx:
                err += dx
                y0 += sy

    def rect_fill(self, x, y, w, h, on=1):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.pixel(xx, yy, on)

    def circle(self, cx, cy, r, fill=False):
        for y in range(-r, r + 1):
            for x in range(-r, r + 1):
                d = x * x + y * y
                if fill and d <= r * r + r // 2:
                    self.pixel(cx + x, cy + y)
                elif not fill and r * r - r <= d <= r * r + r:
                    self.pixel(cx + x, cy + y)

    def triangle_fill(self, x0, y0, x1, y1, x2, y2):
        for y in range(min(y0, y1, y2), max(y0, y1, y2) + 1):
            for x in range(min(x0, x1, x2), max(x0, x1, x2) + 1):
                def s(ax, ay, bx, by, px, py):
                    return (px - bx) * (ay - by) - (ax - bx) * (py - by)
                d1, d2, d3 = s(x, y, x0, y0, x1, y1), s(x, y, x1, y1, x2, y2), s(x, y, x2, y2, x0, y0)
                neg = d1 < 0 or d2 < 0 or d3 < 0
                pos = d1 > 0 or d2 > 0 or d3 > 0
                if not (neg and pos):
                    self.pixel(x, y)

    def bitmap(self, x, y, w, h, rows):
        for yy in range(h):
            for xx in range(w):
                if rows[yy][xx]:
                    self.pixel(x + xx, y + yy)

    def image(self, out_path):
        scale = SCALE
        margin = 14
        im = Image.new("RGB", (W * scale + 2 * margin, H * scale + 2 * margin), (28, 28, 32))
        d = ImageDraw.Draw(im)
        d.rectangle([margin - 6, margin - 6, margin + W * scale + 5, margin + H * scale + 5],
                    fill=OFF, outline=(90, 90, 96), width=2)
        for y in range(H):
            for x in range(W):
                if self.px[y][x]:
                    col = YELLOW if y < SPLIT else BLUE
                    x0, y0 = margin + x * scale, margin + y * scale
                    d.rectangle([x0, y0, x0 + scale - 2, y0 + scale - 2], fill=col)
        im.save(out_path)


# ---- the firmware's own drawing helpers --------------------------------------------------
def wifi(s, bars):
    heights = [3, 5, 7, 9]
    bx = 115
    for i in range(4):
        if i < bars:
            s.rect_fill(bx, 0 + (9 - heights[i]), 2, heights[i])
        bx += 3


def connection(s, state):
    x, y = 102, 0
    if state == "connected":
        s.circle(x + 3, y + 4, 3, fill=True)
    elif state == "disconnected":
        s.line(x, y + 1, x + 6, y + 7)
        s.line(x + 6, y + 1, x, y + 7)
    elif state == "connecting":
        s.circle(x + 3, y + 4, 3)
    else:
        s.triangle_fill(x, y + 7, x + 6, y + 7, x + 3, y + 1)


def dots(s, current):
    start = (W - 5 * 8) // 2
    for i in range(6):
        s.circle(start + i * 8, H - 4, 2, fill=(i == current))


def page(title, lines, current, status=None, status_line=None, conn=None, bars=4):
    s = Screen()
    s.text(0, 0, title)
    if status_line:
        s.text(0, 8, status_line)
    wifi(s, bars)
    if conn:
        connection(s, conn)
    s.hline(0, W, SPLIT)
    for i, text in enumerate(lines):
        s.text(0, 17 + 8 * i, text)
    dots(s, current)
    return s


def boot_logo():
    header = open(os.path.join(ROOT, "xpressnet_ecos_bridge", "display", "boot_logo.h"), encoding="utf-8").read()
    body = header[header.index("boot_logo_bitmap[]"):]
    body = body[body.index("{"):body.index("};")]
    data = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{2}", body)]
    w = int(re.search(r"BOOT_LOGO_WIDTH\s+(\d+)", header).group(1))
    h = int(re.search(r"BOOT_LOGO_HEIGHT\s+(\d+)", header).group(1))
    per_row = (w + 7) // 8
    rows = [[(data[y * per_row + x // 8] >> (7 - x % 8)) & 1 for x in range(w)] for y in range(h)]
    s = Screen()
    title = "OmniConnect"
    s.text((W - len(title) * 6) // 2, 4, title)
    s.bitmap((W - w) // 2, SPLIT + 4, w, h, rows)
    return s


def popup(base, message):
    base.rect_fill(8, 20, 112, 24, 1)
    base.text(12, 25, message, invert=True)
    return base


def main():
    os.makedirs(OUT, exist_ok=True)
    pages = {
        "oled_boot": boot_logo(),
        "oled_1_main": page("OmniConnect", ["Locos: 2", "Last: Loco 5452", "Spd: 54 Dir: Fwd",
                                            "Fn: 0,1", "Acc 2: Str"], 0),
        "oled_2_device": page("Device Status", ["IP: 192.168.0.51", "Up: 02:14:37", "Heap: 13/80KB (16%)",
                                                "CPU: 160MHz", "FW: v1.5.5"], 1),
        "oled_3_xpressnet": page("XPRESSNET", ["Active: 2 locos", "Last Msg: 3s", "Commands: 128",
                                               "Echo Prev: 14"], 2, conn="connected"),
        "oled_4_ecos": page("ECOS LAN", ["IP: 192.168.0.50", "Heartbeat: OK", "Subscribed: 2 locos",
                                         "Latency: 45ms"], 3, conn="connected"),
        "oled_5_z21": page("Z21 LAN", ["Clients: 1", "Last Msg: 2s", "From: 192.168.0.115"], 4, conn="connected"),
        "oled_6_loconet": page("LocoNet", ["Frames: 1042", "Last Msg: 1s", "Slots: 1/20", "S3 speed 64",
                                           "S3 Fwd F0,1"], 5, conn="connected"),
        "oled_x_disconnected": page("ECOS LAN", ["IP: 192.168.0.50", "Heartbeat: NO", "Subscribed: 0 locos",
                                                  "Latency: N/A"], 3, conn="disconnected", bars=2),
        "oled_x_lowmemory": page("Device Status", ["IP: 192.168.0.51", "Up: 00:03:12", "Heap: 8/80KB (10%)",
                                                   "CPU: 160MHz", "FW: v1.5.5", "!WARNING: Low memory"], 1),
    }
    for name, screen in pages.items():
        screen.image(os.path.join(OUT, name + ".png"))
        print("wrote", name)


if __name__ == "__main__":
    main()
