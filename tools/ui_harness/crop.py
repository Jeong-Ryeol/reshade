# Crops harness screenshots to the overlay window so they are cheap to look at.
#   python -I crop.py <run dir> <out dir> [name ...]     (no names = every "* Overlay.png")
import glob, os, re, sys
from PIL import Image

run, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
ini = open(os.path.join(run, "ReShade.ini"), encoding="utf-8", errors="replace").read()
m = re.search(r"\[###Viewport\],Pos=(\d+),,(\d+),Size=(\d+),,(\d+)", ini)
x, y, w, h = (int(v) for v in m.groups()) if m else (0, 0, 10 ** 6, 10 ** 6)
names = sys.argv[3:] or [os.path.basename(p)[:-len(" Overlay.png")] for p in glob.glob(os.path.join(run, "shots", "* Overlay.png"))]
for n in names:
    im = Image.open(os.path.join(run, "shots", n + " Overlay.png")).convert("RGB")
    pad = 14
    box = (max(0, x - pad), max(0, y - pad), min(im.width, x + w + pad), min(im.height, y + h + pad))
    im.crop(box).save(os.path.join(out, n + ".png"))
    print(n, box)
