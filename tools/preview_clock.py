# Turns raw frames from preview_clock into a PNG contact sheet and a GIF (LED-dot look).
import os, numpy as np
from PIL import Image, ImageDraw

tmp, out = os.environ["TMP"], os.environ["OUT"]
CELL = 8
yy, xx = np.mgrid[0:CELL, 0:CELL]
d = np.hypot(xx + 0.5 - CELL / 2, yy + 0.5 - CELL / 2)
MASK = np.clip((3.7 - d) / 1.2, 0, 1)[:, :, None]  # round LED with soft edge

def frames(name):
    a = np.fromfile(os.path.join(tmp, name + ".bin"), dtype=np.uint8)
    return a.reshape(-1, 64, 64, 3)

def dots(frame):
    big = np.kron(frame, np.ones((CELL, CELL, 1))) * np.tile(MASK, (64, 64, 1))
    return Image.fromarray(big.astype(np.uint8))

def sheet(items, cols, label_h=14):
    n = len(items)
    rows = (n + cols - 1) // cols
    cw = 64 * CELL
    img = Image.new("RGB", (cols * (cw + 8) + 8, rows * (cw + label_h + 8) + 8), (28, 28, 32))
    dr = ImageDraw.Draw(img)
    for i, (label, fr) in enumerate(items):
        x, y = 8 + (i % cols) * (cw + 8), 8 + (i // cols) * (cw + label_h + 8)
        img.paste(dots(fr), (x, y + label_h))
        dr.text((x, y), label, fill=(210, 210, 215))
    return img

day = frames("day")
items = [("12:45 PM  t=0.5s", day[15]), ("t=2.0s", day[60]), ("t=4.5s (new minute: looks down)", day[135]),
         ("9:07 AM", frames("morning")[40]), ("2:10 AM (sleepy lids)", frames("night")[100]), ("no time yet", frames("notime")[30])]
for k, name in enumerate(["clear 36", "partly 28", "cloudy 24", "fog 12", "rain 19", "snow -2", "storm 22"]):
    items.append((name, frames("w%d" % k)[60]))
items.append(("clear night 18", frames("n0")[60]))
items.append(("temperature phase (6.5 s)", frames("w0")[195]))
items.append(("temperature -2", frames("w5")[195]))
items.append(("partly night 18", frames("n1")[60]))
sheet(items, 3).save(os.path.join(out, "sheet.png"))

gif = [dots(f).convert("P", palette=Image.ADAPTIVE, colors=128) for f in day[::2]]
gif[0].save(os.path.join(out, "day.gif"), save_all=True, append_images=gif[1:], duration=66, loop=0)

names = ["neutral", "happy", "surprised", "smug", "disgust", "scared", "asleep", "heart eyes", "wink", "curious"]
sheet([(n, frames("m%d" % i)[75]) for i, n in enumerate(names)], 5).save(os.path.join(out, "moods.png"))
mg = [dots(f).convert("P", palette=Image.ADAPTIVE, colors=128) for f in frames("moods")[::2]]
mg[0].save(os.path.join(out, "moods.gif"), save_all=True, append_images=mg[1:], duration=66, loop=0)
