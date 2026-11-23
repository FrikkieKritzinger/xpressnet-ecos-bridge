"""Crops the title-page logo and trims blank space from the web-page screenshots in docs/user/images."""
import os

from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG = os.path.join(ROOT, "docs", "user", "images")

logo = Image.open(os.path.join(ROOT, "logo.png")).convert("RGB")
logo.crop((190, 330, 840, 710)).save(os.path.join(IMG, "logo_title.png"))

for name in os.listdir(IMG):
    if not name.startswith("web_"):
        continue
    path = os.path.join(IMG, name)
    im = Image.open(path).convert("RGB")
    bg = Image.new("RGB", im.size, (255, 255, 255))
    box = ImageChops.difference(im, bg).getbbox()
    if box:
        im = im.crop((0, 0, im.width, min(im.height, box[3] + 14)))
        # thin grey frame so the page stands out on a white document
        framed = Image.new("RGB", (im.width + 2, im.height + 2), (160, 160, 160))
        framed.paste(im, (1, 1))
        framed.save(path)
    print("trimmed", name)
