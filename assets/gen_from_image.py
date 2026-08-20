"""Convertit une photo detouree en creature ASCII animee.

Entree : une image et son manifeste de zones, produits par
tools/zone-editor.html. Sortie : les memes jeux de frames que
gen_creature.py, mais tires d'une vraie photo plutot que d'un dessin
parametrique.

Le principe : le manifeste dit OU sont les parties, ce script dit COMMENT
elles bougent. Un oeil s'ecrase vers son axe pour cligner, une bouche
s'etire, une oreille se cisaille. Rien n'est redessine — on deforme les
pixels d'origine.

Usage :
    py -3.12 assets/gen_from_image.py assets/creatures/fox.zones.json [--preview]
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


def load(manifest_path):
    with open(manifest_path, encoding="utf-8") as fh:
        man = json.load(fh)
    folder = os.path.dirname(os.path.abspath(manifest_path))
    img_path = os.path.join(folder, man["image"])
    if not os.path.exists(img_path):
        raise SystemExit("image introuvable : %s" % img_path)
    zones = {z["kind"]: z["points"] for z in man["zones"]}
    return Image.open(img_path).convert("L"), zones


def poly_mask(points, w, h, scale=1.0, offset=(0.0, 0.0)):
    """Masque flottant 0/1 a partir d'un polygone normalise."""
    im = Image.new("L", (w, h), 0)
    pts = [((p[0] + offset[0]) * w, (p[1] + offset[1]) * h) for p in points]
    if scale != 1.0:
        cx = sum(p[0] for p in pts) / len(pts)
        cy = sum(p[1] for p in pts) / len(pts)
        pts = [(cx + (x - cx) * scale, cy + (y - cy) * scale) for x, y in pts]
    ImageDraw.Draw(im).polygon(pts, fill=255)
    return np.asarray(im, dtype=np.float32) / 255.0


def bbox(points):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return min(xs), min(ys), max(xs), max(ys)


def squash_zone(img, points, factor, w, h):
    """Ecrase verticalement une zone vers son axe median.

    C'est ce qui ferme un oeil. Le vide libere est comble en etirant les
    lignes de bordure de la zone : approximatif, mais a 40x30 la conversion
    detruit de toute facon plus de detail que cette approximation n'en
    invente.
    """
    x0, y0, x1, y1 = bbox(points)
    px0, py0 = int(x0 * w), int(y0 * h)
    px1, py1 = int(math.ceil(x1 * w)), int(math.ceil(y1 * h))
    if px1 - px0 < 2 or py1 - py0 < 2:
        return img

    mask = poly_mask(points, w, h)
    band = img[py0:py1, px0:px1].copy()
    bh = band.shape[0]

    # Bordures haute et basse de la bande, pour remplir ce qu'on libere.
    top = band[0:1, :]
    bot = band[bh - 1:bh, :]

    new_h = max(1, int(round(bh * factor)))
    squeezed = np.asarray(
        Image.fromarray((band * 255).astype(np.uint8)).resize(
            (band.shape[1], new_h), Image.LANCZOS),
        dtype=np.float32) / 255.0

    pad_top = (bh - new_h) // 2
    pad_bot = bh - new_h - pad_top
    filled = np.concatenate([
        np.repeat(top, pad_top, axis=0) if pad_top > 0 else np.empty((0, band.shape[1]), np.float32),
        squeezed,
        np.repeat(bot, pad_bot, axis=0) if pad_bot > 0 else np.empty((0, band.shape[1]), np.float32),
    ], axis=0)

    out = img.copy()
    patch = out[py0:py1, px0:px1]
    m = mask[py0:py1, px0:px1]
    out[py0:py1, px0:px1] = patch * (1.0 - m) + filled * m
    return out


def shift_zone(img, points, dx, dy, w, h):
    """Translate une zone. Sert aux sourcils et au fremissement d'oreille."""
    mask = poly_mask(points, w, h)
    sx, sy = int(round(dx * w)), int(round(dy * h))
    moved = np.roll(np.roll(img, sy, axis=0), sx, axis=1)
    moved_mask = np.roll(np.roll(mask, sy, axis=0), sx, axis=1)
    out = img * (1.0 - moved_mask) + moved * moved_mask
    return out


def to_cells(img):
    """Image 960x960 -> grille 40x30 de luminances."""
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


def base_image(src, zones):
    """Photo recadree sur la silhouette, mise a l'echelle du panneau."""
    sil = zones.get("silhouette")
    if not sil:
        raise SystemExit("le manifeste n'a pas de silhouette")

    # Recadrer sur la boite de la silhouette, avec une marge, puis inscrire
    # dans le carre du panneau sans deformer.
    x0, y0, x1, y1 = bbox(sil)
    W, H = src.size
    side = max((x1 - x0) * W, (y1 - y0) * H) * 1.06
    cx, cy = (x0 + x1) / 2 * W, (y0 + y1) / 2 * H
    box = (int(cx - side / 2), int(cy - side / 2),
           int(cx + side / 2), int(cy + side / 2))
    crop = src.crop(box).resize((S_W, S_H), Image.LANCZOS)
    # Flouter AVANT de reduire. La fourrure a une texture qui survit au
    # moyennage par blocs et se convertit en bruit : elle mange les formes
    # au lieu de les decrire. Le rayon vaut environ une cellule.
    crop = crop.filter(ImageFilter.GaussianBlur(radius=CELL_W * 0.85))

    # Les zones sont exprimees sur l'image d'origine : on les ramene dans le
    # repere du recadrage, sinon tout serait decale.
    def remap(points):
        return [[(p[0] * W - box[0]) / side, (p[1] * H - box[1]) / side]
                for p in points]

    mapped = {k: remap(v) for k, v in zones.items()}
    arr = np.asarray(crop, dtype=np.float32) / 255.0

    # Aplatir l eclairage. Une photo porte un degrade d illumination — ici le
    # flanc droit en pleine lumiere, le gauche dans l ombre — et la
    # conversion lit ce degrade plutot que l animal. On divise par une
    # version tres floue de l image, ce qui retire l eclairage et garde le
    # contraste local, c est-a-dire la forme.
    flat = np.asarray(crop.filter(ImageFilter.GaussianBlur(radius=S_W * 0.16)),
                      dtype=np.float32) / 255.0
    arr = arr / np.maximum(flat, 0.06)
    arr = arr / max(float(arr.max()), 1e-6)

    # Hors silhouette : fond. Sur un panneau noir, ce qui n'est pas la
    # creature doit disparaitre, pas devenir gris.
    mask = poly_mask(mapped["silhouette"], S_W, S_H)
    arr = arr * mask

    # Un peu de contraste : la conversion ecrase tout vers le milieu de la
    # rampe si on lui donne une photo telle quelle.
    ink = arr[mask > 0.5]
    if ink.size:
        lo, hi = np.percentile(ink, 10), np.percentile(ink, 92)
        if hi - lo > 0.02:
            arr = np.clip((arr - lo) / (hi - lo), 0.0, 1.0) * mask

    # --- accentuation par zone -------------------------------------------
    # A 40x30 le contraste propre d une photo ne suffit pas : les yeux se
    # noient dans la fourrure. Mais le manifeste dit OU ils sont, donc on
    # peut les appuyer. C est exactement ce que fait un caricaturiste : il
    # ne recopie pas, il force ce qui identifie.
    EMPHASIS = {
        "eye_left": -0.55, "eye_right": -0.55,   # creuser : un oeil est sombre
        "nose": -0.65,
        "mouth": -0.30,
        "brow_left": -0.25, "brow_right": -0.25,
    }
    for kind, amount in EMPHASIS.items():
        if kind not in mapped:
            continue
        zm = poly_mask(mapped[kind], S_W, S_H)
        # Adoucir le bord, sinon la zone apparait comme un rectangle colle.
        zm = np.asarray(Image.fromarray((zm * 255).astype(np.uint8)).filter(
            ImageFilter.GaussianBlur(radius=CELL_W * 0.5)), dtype=np.float32) / 255.0
        arr = np.clip(arr + amount * zm, 0.0, 1.0) * mask

    return arr, mapped


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        raise SystemExit(__doc__)
    src, zones = load(args[0])
    base, mapped = base_image(src, zones)

    print("image  : %s" % src.size[0] + "x%d" % src.size[1])
    print("zones  : %s" % ", ".join(sorted(mapped)))
    print()
    print("=== repos ===")
    for r in to_chars(to_cells(base)):
        print("  |" + r + "|")

    if "eye_left" in mapped and "eye_right" in mapped:
        shut = squash_zone(base, mapped["eye_left"], 0.12, S_W, S_H)
        shut = squash_zone(shut, mapped["eye_right"], 0.12, S_W, S_H)
        print()
        print("=== yeux fermes ===")
        for r in to_chars(to_cells(shut)):
            print("  |" + r + "|")
