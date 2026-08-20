"""Convertit une photo detouree en creature ASCII animee.

Entree : une image et son manifeste de zones, produits par
tools/zone-editor.html. Sortie : les memes jeux de frames que
gen_creature.py, mais tires d'une vraie photo plutot que d'un dessin
parametrique.

Le principe : le manifeste dit OU sont les parties, ce script dit COMMENT
elles bougent. Un oeil s'ecrase vers son axe pour cligner, une bouche
s'etire, un sourcil descend. Rien n'est redessine — on deforme les pixels
d'origine, donc ca marche sur n'importe quelle image sans rien savoir de
son contenu.

Deux traitements se sont averes indispensables, et ils ne sont pas
evidents :

  - Aplatir l'eclairage. Une photo porte un degrade d'illumination, et la
    conversion lit ce degrade plutot que l'animal. Avant correction, seule
    l'oreille du cote eclaire existait.
  - Accentuer les zones. A 40x30 le contraste propre d'une photo noie les
    yeux dans la fourrure. Le manifeste dit ou ils sont, donc on les creuse.
    C'est ce que fait un caricaturiste : il ne recopie pas, il force ce qui
    identifie.

Usage :
    py -3.12 assets/gen_from_image.py assets/creatures/fox.zones.json
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

# --- geometrie du panneau, identique a gen_creature.py --------------------
COLS, ROWS = 40, 30
CELL_W, CELL_H = 24, 32
S_W, S_H = COLS * CELL_W, ROWS * CELL_H   # 960 x 960
RAMP = " .`',:;!~+=*xo#%8@"

N_IDLE, N_EXPR = 40, 16


def load(manifest_path):
    with open(manifest_path, encoding="utf-8") as fh:
        man = json.load(fh)
    folder = os.path.dirname(os.path.abspath(manifest_path))
    img_path = os.path.join(folder, man["image"])
    if not os.path.exists(img_path):
        raise SystemExit("image introuvable : %s" % img_path)
    zones = {z["kind"]: z["points"] for z in man["zones"]}
    return Image.open(img_path).convert("L"), zones


def poly_mask(points, w, h):
    im = Image.new("L", (w, h), 0)
    ImageDraw.Draw(im).polygon([(p[0] * w, p[1] * h) for p in points], fill=255)
    return np.asarray(im, dtype=np.float32) / 255.0


def bbox(points):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return min(xs), min(ys), max(xs), max(ys)


def squash_zone(img, points, factor, w, h):
    """Ecrase (ou dilate) une zone verticalement autour de son axe median.

    C'est ce qui ferme un oeil, et au-dela de 1.0 ce qui l'ecarquille. Le
    vide libere est comble en etirant les lignes de bordure : approximatif,
    mais a 40x30 la conversion detruit de toute facon plus de detail que
    cette approximation n'en invente.
    """
    x0, y0, x1, y1 = bbox(points)
    px0, py0 = max(0, int(x0 * w)), max(0, int(y0 * h))
    px1 = min(w, int(math.ceil(x1 * w)))
    py1 = min(h, int(math.ceil(y1 * h)))
    if px1 - px0 < 2 or py1 - py0 < 2:
        return img

    mask = poly_mask(points, w, h)
    band = img[py0:py1, px0:px1]
    bh, bw = band.shape

    new_h = max(1, min(bh, int(round(bh * factor))))
    squeezed = np.asarray(
        Image.fromarray((band * 255).astype(np.uint8)).resize((bw, new_h), Image.LANCZOS),
        dtype=np.float32) / 255.0

    pad_top = (bh - new_h) // 2
    pad_bot = bh - new_h - pad_top
    parts = []
    if pad_top > 0:
        parts.append(np.repeat(band[0:1, :], pad_top, axis=0))
    parts.append(squeezed)
    if pad_bot > 0:
        parts.append(np.repeat(band[bh - 1:bh, :], pad_bot, axis=0))
    filled = np.concatenate(parts, axis=0)

    out = img.copy()
    m = mask[py0:py1, px0:px1]
    out[py0:py1, px0:px1] = band * (1.0 - m) + filled * m
    return out


def shift_zone(img, points, dx, dy, w, h):
    """Translate une zone. Sert aux sourcils et au fremissement d'oreille."""
    mask = poly_mask(points, w, h)
    sx, sy = int(round(dx * w)), int(round(dy * h))
    moved = np.roll(np.roll(img, sy, axis=0), sx, axis=1)
    moved_mask = np.roll(np.roll(mask, sy, axis=0), sx, axis=1)
    return img * (1.0 - moved_mask) + moved * moved_mask


def to_cells(img):
    return img.reshape(ROWS, CELL_H, COLS, CELL_W).mean(axis=(1, 3))


def to_chars(cells, gamma=0.80, lift=0.06, span=0.86):
    rows = []
    n = len(RAMP)
    for r in range(ROWS):
        out = []
        for c in range(COLS):
            x = (c + 0.5) / COLS - 0.5
            y = (r + 0.5) / ROWS - 0.5
            if x * x + y * y > 0.25:          # hors du panneau rond
                out.append(" ")
                continue
            v = float(np.clip((cells[r, c] - lift) / span, 0.0, 1.0))
            out.append(RAMP[min(max(int(round(v ** gamma * (n - 1))), 0), n - 1)])
        rows.append("".join(out).rstrip().ljust(COLS))
    return rows


# Ce qu'on appuie, et de combien. Negatif = on creuse.
EMPHASIS = {
    "eye_left": -0.55, "eye_right": -0.55,
    "nose": -0.65,
    "mouth": -0.30,
    "brow_left": -0.25, "brow_right": -0.25,
}


def base_image(src, zones):
    sil = zones.get("silhouette")
    if not sil:
        raise SystemExit("le manifeste n'a pas de silhouette")

    x0, y0, x1, y1 = bbox(sil)
    W, H = src.size
    side = max((x1 - x0) * W, (y1 - y0) * H) * 1.06
    cx, cy = (x0 + x1) / 2 * W, (y0 + y1) / 2 * H
    box = (int(cx - side / 2), int(cy - side / 2),
           int(cx + side / 2), int(cy + side / 2))
    crop = src.crop(box).resize((S_W, S_H), Image.LANCZOS)

    # Flouter AVANT de reduire : la fourrure a une texture qui survit au
    # moyennage par blocs et se convertit en bruit.
    blurred = crop.filter(ImageFilter.GaussianBlur(radius=CELL_W * 0.85))

    def remap(points):
        return [[(p[0] * W - box[0]) / side, (p[1] * H - box[1]) / side]
                for p in points]

    mapped = {k: remap(v) for k, v in zones.items()}
    arr = np.asarray(blurred, dtype=np.float32) / 255.0

    # Aplatir l'eclairage en divisant par une version tres floue : retire le
    # degrade d'illumination, garde le contraste local, c'est-a-dire la forme.
    flat = np.asarray(crop.filter(ImageFilter.GaussianBlur(radius=S_W * 0.16)),
                      dtype=np.float32) / 255.0
    arr = arr / np.maximum(flat, 0.06)
    arr = arr / max(float(arr.max()), 1e-6)

    mask = poly_mask(mapped["silhouette"], S_W, S_H)
    arr = arr * mask

    ink = arr[mask > 0.5]
    if ink.size:
        lo, hi = np.percentile(ink, 10), np.percentile(ink, 92)
        if hi - lo > 0.02:
            arr = np.clip((arr - lo) / (hi - lo), 0.0, 1.0) * mask

    for kind, amount in EMPHASIS.items():
        if kind not in mapped:
            continue
        zm = poly_mask(mapped[kind], S_W, S_H)
        zm = np.asarray(Image.fromarray((zm * 255).astype(np.uint8)).filter(
            ImageFilter.GaussianBlur(radius=CELL_W * 0.5)), dtype=np.float32) / 255.0
        arr = np.clip(arr + amount * zm, 0.0, 1.0) * mask

    return arr, mapped


# --- jeux de frames -------------------------------------------------------
def ease(u):
    return math.sin(u * math.pi)


def both_eyes(img, mapped, factor):
    for k in ("eye_left", "eye_right"):
        if k in mapped:
            img = squash_zone(img, mapped[k], factor, S_W, S_H)
    return img


def build_sets(base, mapped):
    sets = {}

    idle = []
    for f in range(N_IDLE):
        o = 1.0
        # Deux clignements par boucle, inegalement espaces pour que le cycle
        # ne s'entende pas.
        for start in (9, 27):
            if start <= f < start + 3:
                o = min(o, 1.0 - ease((f - start) / 3.0) * 0.92)
        idle.append(to_chars(to_cells(base if o > 0.99 else both_eyes(base, mapped, o))))
    sets["ASCII"] = idle

    sets["ASCII_SLEEP"] = [to_chars(to_cells(both_eyes(base, mapped, 0.10)))] * 4

    wink = []
    for f in range(N_EXPR):
        u = f / float(N_EXPR - 1)
        img = base
        if "eye_right" in mapped:
            img = squash_zone(img, mapped["eye_right"], 1.0 - 0.90 * ease(u), S_W, S_H)
        wink.append(to_chars(to_cells(img)))
    sets["WINK"] = wink

    sur = []
    for f in range(N_EXPR):
        u = f / float(N_EXPR - 1)
        img = both_eyes(base, mapped, 1.0 + 0.50 * ease(u))
        if "mouth" in mapped:
            img = squash_zone(img, mapped["mouth"], 1.0 + 0.60 * ease(u), S_W, S_H)
        sur.append(to_chars(to_cells(img)))
    sets["SURPRISED"] = sur

    hap = []
    for f in range(N_EXPR):
        u = f / float(N_EXPR - 1)
        hap.append(to_chars(to_cells(both_eyes(base, mapped, 1.0 - 0.55 * ease(u)))))
    sets["HAPPY"] = hap

    ang = []
    for f in range(N_EXPR):
        u = f / float(N_EXPR - 1)
        e = ease(u)
        img = base
        for k in ("brow_left", "brow_right"):
            if k in mapped:
                img = shift_zone(img, mapped[k], 0.0, 0.050 * e, S_W, S_H)
        img = both_eyes(img, mapped, 1.0 - 0.30 * e)
        ang.append(to_chars(to_cells(img)))
    sets["ANGRY"] = ang

    return sets


def emit(name, sets, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    up = name.upper()
    stem = name.lower() + "_ascii"

    with open(os.path.join(out_dir, stem + ".h"), "w", encoding="utf-8") as fh:
        fh.write('#pragma once\n#include "../ui/asciiart.h"\n\n')
        fh.write("// Generated by assets/gen_from_image.py - do not hand-edit.\n")
        fh.write("// Tire d'une photo detouree, pas d'un dessin parametrique.\n\n")
        fh.write("namespace assets {\n\n")
        for key in sets:
            fh.write("extern const asciiart::Frame %s_%s[];\n" % (up, key))
            fh.write("extern const int %s_%s_COUNT;\n" % (up, key))
        fh.write("\n}\n")

    lines = ['#include "%s.h"' % stem, "",
             "// Generated by assets/gen_from_image.py - do not hand-edit.", "",
             "namespace {", ""]
    for key, frames in sets.items():
        for i, rows in enumerate(frames):
            lines.append("const char* const %s_%s_%02d[] = {" % (up, key, i))
            for r in rows:
                lines.append('    "%s",' % r)
            lines.append("};")
        lines.append("")
    lines.append("}")
    lines.append("")
    lines.append("namespace assets {")
    lines.append("")
    for key, frames in sets.items():
        lines.append("const asciiart::Frame %s_%s[] = {" % (up, key))
        for i in range(len(frames)):
            lines.append("    { %s_%s_%02d, %d }," % (up, key, i, ROWS))
        lines.append("};")
        lines.append("const int %s_%s_COUNT = %d;" % (up, key, len(frames)))
        lines.append("")
    lines.append("}")
    lines.append("")

    with open(os.path.join(out_dir, stem + ".cpp"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))
    return stem


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        raise SystemExit(__doc__)

    src, zones = load(args[0])
    base, mapped = base_image(src, zones)
    name = os.path.basename(args[0]).split(".")[0] + "photo"

    print("image : %dx%d" % src.size)
    print("zones : %s" % ", ".join(sorted(mapped)))
    print()
    print("=== repos ===")
    for r in to_chars(to_cells(base)):
        print("  |" + r + "|")

    sets = build_sets(base, mapped)
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "assets")
    stem = emit(name, sets, out)

    print()
    for k, v in sets.items():
        print("  %-14s %2d frames" % (k, len(v)))
    print("  -> src/assets/%s.cpp" % stem)
