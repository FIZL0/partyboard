# contact.py <out.png> <cols> <thumb_w> img...: labelled thumbnails in a grid
import sys
from PIL import Image, ImageDraw
out, cols, tw = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
paths = sys.argv[4:]
ims = [Image.open(p).convert("RGB") for p in paths]
th = int(tw * ims[0].height / ims[0].width)
rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", (cols * tw, rows * (th + 18)), "white")
d = ImageDraw.Draw(sheet)
for i, (p, im) in enumerate(zip(paths, ims)):
    x, y = (i % cols) * tw, (i // cols) * (th + 18)
    sheet.paste(im.resize((tw, th)), (x, y + 18))
    d.text((x + 2, y + 2), "/".join(p.split("/")[-3:]), fill="black")
sheet.save(out)
