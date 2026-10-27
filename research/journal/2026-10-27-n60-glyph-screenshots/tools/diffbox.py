# diffbox.py <a.png> <b.png> name:x0,y0,x1,y1 ...: bounding box of pixels that differ between two frames, per box
import sys
from PIL import Image, ImageChops
a = Image.open(sys.argv[1]).convert("RGB"); b = Image.open(sys.argv[2]).convert("RGB")
d = ImageChops.difference(a, b).convert("L").point(lambda v: 255 if v > 24 else 0)
print("whole frame:", d.getbbox())
for spec in sys.argv[3:]:
    name, box = spec.split(":")
    x0, y0, x1, y1 = map(int, box.split(","))
    bb = d.crop((x0, y0, x1, y1)).getbbox()
    print(f"  {name}:", None if bb is None else f"{bb[2]-bb[0]}x{bb[3]-bb[1]} at ({x0+bb[0]},{y0+bb[1]})")
