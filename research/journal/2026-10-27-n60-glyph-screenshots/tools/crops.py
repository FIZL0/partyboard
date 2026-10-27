# crops.py <out.png> <x0> <y0> <x1> <y1> <scale> img...: the same region of each image, stacked, labelled
import sys
from PIL import Image, ImageDraw
out = sys.argv[1]
x0, y0, x1, y1 = map(int, sys.argv[2:6])
sc = float(sys.argv[6])
paths = sys.argv[7:]
w, h = int((x1 - x0) * sc), int((y1 - y0) * sc)
sheet = Image.new("RGB", (w, len(paths) * (h + 16)), "white")
d = ImageDraw.Draw(sheet)
for i, p in enumerate(paths):
    im = Image.open(p).convert("RGB").crop((x0, y0, x1, y1)).resize((w, h), Image.NEAREST)
    sheet.paste(im, (0, i * (h + 16) + 16))
    d.text((2, i * (h + 16) + 2), "/".join(p.split("/")[-3:]), fill="black")
sheet.save(out)
