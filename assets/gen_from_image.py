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
# Rampe mesuree dans glcdfont.h, la police que LovyanGFX compile :
# chaque caractere est classe par le nombre de pixels qu'il allume
# reellement sur le panneau (le : y est plus leger que le ., le *
# plus dense que le $). Guillemet double et antislash exclus — ils
# casseraient les chaines C emises.
RAMP = " :.;-~/^_'`,!|<>(){}j+rvxi=fl1cIJL7otunakwzs%XVCeyq&hKmSPUbdgAO2$8HNWGQZERD*0M#@B"

N_IDLE, N_EXPR = 40, 16

# Valeurs par defaut du rendu. L editeur de zones expose les memes noms, et
# les embarque dans le manifeste : regler dans le navigateur suffit.
DEFAULT_RENDER = {
    "floor": 0.10, "amp": 0.80, "levels": 9,
    "eyeRx": 0.62, "eyeRy": 1.25,
    "contrast": 1.00, "bright": 0.00, "blur": 0.75, "edges": 0.50,
}


def load(manifest_path):
    with open(manifest_path, encoding="utf-8") as fh:
        man = json.load(fh)
    folder = os.path.dirname(os.path.abspath(manifest_path))
    img_path = os.path.join(folder, man["image"])
    if not os.path.exists(img_path):
        raise SystemExit("image introuvable : %s" % img_path)
    zones = {z["kind"]: z["points"] for z in man["zones"]}
    # Les reglages viennent de l editeur quand il en a mis : ce qu on voit
    # dans l apercu est alors exactement ce qui est produit ici.
    render = dict(DEFAULT_RENDER)
    render.update(man.get("render", {}))
    return Image.open(img_path).convert("L"), zones, render


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


def to_chars(cells, gamma=0.80, lift=0.06, span=0.86, strokes=None):
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
            # Un trait prime sur le ton : c'est lui qui porte la forme.
            if strokes is not None and (r, c) in strokes:
                out.append(strokes[(r, c)])
                continue
            v = float(np.clip((cells[r, c] - lift) / span, 0.0, 1.0))
            out.append(RAMP[min(max(int(round(v ** gamma * (n - 1))), 0), n - 1)])
        rows.append("".join(out).rstrip().ljust(COLS))
    return rows


def stroke_grid(img, body, mapped, strength):
    """Traits au niveau cellule : contour de silhouette + aretes internes.

    Chaque cellule traversee par une arete recoit le caractere oriente
    comme elle — c'est le vocabulaire du dessin au trait, celui des petits
    formats d'ASCII art qui se lisent. Les boites des yeux, du nez et de
    la bouche sont exclues : ces zones sont dessinees, pas tracees.
    """
    cells = to_cells(img)
    bcells = to_cells(body)

    excl = []
    for k in ("eye_left", "eye_right", "nose", "mouth"):
        if k in mapped:
            excl.append(bbox(mapped[k]))

    def excluded(cx, cy):
        for x0, y0, x1, y1 in excl:
            if x0 - 0.02 <= cx <= x1 + 0.02 and y0 - 0.02 <= cy <= y1 + 0.02:
                return True
        return False

    gy, gx = np.gradient(cells)
    mag = np.hypot(gx, gy)
    if mag.max() > 0:
        mag = mag / mag.max()
    bgy, bgx = np.gradient(bcells)
    bmag = np.hypot(bgx, bgy)

    thr = 0.60 - 0.40 * float(strength)
    out = {}
    for r in range(ROWS):
        for c in range(COLS):
            x = (c + 0.5) / COLS - 0.5
            y = (r + 0.5) / ROWS - 0.5
            if x * x + y * y > 0.25:
                continue
            cx, cy = (c + 0.5) / COLS, (r + 0.5) / ROWS
            is_rim = bmag[r, c] > 0.28 and bcells[r, c] > 0.15
            is_edge = (bcells[r, c] > 0.5 and mag[r, c] > thr
                       and not excluded(cx, cy))
            if not (is_rim or is_edge):
                continue
            ax = bgx[r, c] if is_rim else gx[r, c]
            ay = bgy[r, c] if is_rim else gy[r, c]
            # Le gradient est perpendiculaire a l'arete : on tourne de 90
            # degres pour obtenir la direction du trait lui-meme.
            edge = (math.degrees(math.atan2(ay, ax)) + 90.0) % 180.0
            if edge < 22.5 or edge >= 157.5:
                ch = "-"
            elif edge < 67.5:
                ch = "/"
            elif edge < 112.5:
                ch = "|"
            else:
                ch = chr(92)          # antislash, echappe a l'emission
            out[(r, c)] = ch
    return out


# Ce qu'on appuie, et de combien. Negatif = on creuse.
EMPHASIS = {
    "eye_left": -0.55, "eye_right": -0.55,
    "nose": -0.65,
    "mouth": -0.30,
    "brow_left": -0.25, "brow_right": -0.25,
}


def centroid(points):
    return (sum(p[0] for p in points) / len(points),
            sum(p[1] for p in points) / len(points))


def ellipse_mask(cx, cy, rx, ry, w, h):
    im = Image.new("L", (w, h), 0)
    ImageDraw.Draw(im).ellipse([(cx - rx) * w, (cy - ry) * h,
                               (cx + rx) * w, (cy + ry) * h], fill=255)
    return np.asarray(im, dtype=np.float32) / 255.0


def base_image(src, zones, render=None):
    """Compose la creature : photo posterisee, traits redessines.

    Le manifeste n'est pas isotrope — l'editeur normalise x sur la largeur
    et y sur la hauteur — donc tout passe par les pixels avant d'atteindre
    le panneau, sinon une tete carree ressort etiree.
    """
    R = dict(DEFAULT_RENDER)
    if render:
        R.update(render)

    sil = zones.get("silhouette")
    if not sil:
        raise SystemExit("le manifeste n'a pas de silhouette")

    W, H = src.size

    def to_px(points):
        return [(p[0] * W, p[1] * H) for p in points]

    sil_px = to_px(sil)
    xs = [p[0] for p in sil_px]
    ys = [p[1] for p in sil_px]
    bw, bh = max(xs) - min(xs), max(ys) - min(ys)
    if bw <= 1 or bh <= 1:
        raise SystemExit("silhouette degeneree")

    # Inscrire par la diagonale : le panneau est rond, une forme calee sur
    # sa largeur laisserait deux bandes vides en haut et en bas.
    FILL = 0.98
    field = math.hypot(bw, bh) / FILL
    ccx, ccy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    box = (ccx - field / 2, ccy - field / 2, ccx + field / 2, ccy + field / 2)

    def place(points):
        return [[(p[0] * W - box[0]) / field, (p[1] * H - box[1]) / field]
                for p in points]

    mapped = {kind: place(pts) for kind, pts in zones.items()}
    body = poly_mask(mapped["silhouette"], S_W, S_H)

    # --- la photo, posterisee ---------------------------------------------
    crop = src.crop((int(box[0]), int(box[1]), int(box[2]), int(box[3])))
    crop = crop.resize((S_W, S_H), Image.LANCZOS)

    photo = np.asarray(crop.filter(ImageFilter.GaussianBlur(radius=CELL_W * R["blur"])),
                       dtype=np.float32) / 255.0
    # Aplatir l'eclairage, sinon la conversion lit le degrade d'illumination
    # plutot que l'animal.
    flat = np.asarray(crop.filter(ImageFilter.GaussianBlur(radius=S_W * 0.16)),
                      dtype=np.float32) / 255.0
    photo = photo / np.maximum(flat, 0.06)
    photo = photo / max(float(photo.max()), 1e-6)

    ink = photo[body > 0.5]
    if ink.size:
        lo, hi = np.percentile(ink, 8), np.percentile(ink, 94)
        if hi - lo > 0.02:
            photo = np.clip((photo - lo) / (hi - lo), 0.0, 1.0)

    # Quatre tons suffisent a decrire une tete ; les variations plus fines
    # ne survivaient pas a la conversion et n'ajoutaient que du bruit.
    photo = np.clip((photo - 0.5) * R["contrast"] + 0.5 + R["bright"], 0.0, 1.0)

    # Quantiles a population egale plutot que bandes de largeur egale : la
    # fourrure ecrase l'histogramme, et des bandes egales laissaient presque
    # tous les pixels dans une ou deux d'entre elles — un rendu monochrome.
    # Chaque ton recoit ici le meme nombre de pixels, donc chaque caractere
    # de la rampe est reellement utilise.
    L = max(2, int(round(R["levels"])))
    sample = photo[body > 0.5]
    if sample.size:
        cuts = np.quantile(sample, np.linspace(0.0, 1.0, L + 1)[1:-1])
        photo = np.digitize(photo, cuts).astype(np.float32) / (L - 1)
    else:
        photo = np.round(photo * (L - 1)) / (L - 1)

    # Pose basse et resserree : la plupart des cellules doivent rester
    # creuses, sinon le panneau bave en une masse blanche.
    img = (R["floor"] + R["amp"] * photo) * body

    # Liseré sombre au bord, pour que la silhouette morde sur le fond.
    inner = poly_mask([[0.5 + (p[0] - 0.5) * 0.94, 0.5 + (p[1] - 0.5) * 0.94]
                       for p in mapped["silhouette"]], S_W, S_H)
    rim = np.clip(body - inner, 0.0, 1.0)
    img = img * (1.0 - rim)

    # --- museau clair ------------------------------------------------------
    snout = []
    for k_s in ("nose", "mouth"):
        if k_s in mapped:
            snout += mapped[k_s]
    if snout:
        ax0, ay0, ax1, ay1 = bbox(snout)
        m = ellipse_mask((ax0 + ax1) / 2, (ay0 + ay1) / 2,
                         (ax1 - ax0) * 0.90, (ay1 - ay0) * 0.80, S_W, S_H) * body
        img = img * (1.0 - m * 0.85) + 0.60 * m * 0.85

    # --- yeux --------------------------------------------------------------
    # Agrandis par rapport au trace : de grands yeux ronds font la creature
    # mignonne, et a 40x30 un oeil fidele ne ferait qu'une cellule et demie.
    drawn_eyes = {}
    for k_eye in ("eye_left", "eye_right"):
        if k_eye not in mapped:
            continue
        ex0, ey0, ex1, ey1 = bbox(mapped[k_eye])
        ecx, ecy = (ex0 + ex1) / 2, (ey0 + ey1) / 2
        rx = max((ex1 - ex0) * R["eyeRx"], 0.070)
        ry = max((ey1 - ey0) * R["eyeRy"], 0.070)

        white = ellipse_mask(ecx, ecy, rx, ry, S_W, S_H)
        img = img * (1.0 - white) + 1.0 * white
        pup = ellipse_mask(ecx, ecy + ry * 0.08, rx * 0.50, ry * 0.62, S_W, S_H)
        img = img * (1.0 - pup) + 0.04 * pup
        sh = ellipse_mask(ecx - rx * 0.34, ecy - ry * 0.34,
                          rx * 0.18, ry * 0.20, S_W, S_H)
        img = img * (1.0 - sh) + 1.0 * sh

        # Les deformations d'animation portent sur l'oeil DESSINE, plus gros
        # que le trace d'origine.
        drawn_eyes[k_eye] = [[ecx + rx * 1.15 * math.cos(a),
                              ecy + ry * 1.25 * math.sin(a)]
                             for a in [i * math.pi / 8 for i in range(16)]]

    # --- nez ---------------------------------------------------------------
    if "nose" in mapped:
        nx0, ny0, nx1, ny1 = bbox(mapped["nose"])
        m = ellipse_mask((nx0 + nx1) / 2, (ny0 + ny1) / 2,
                         (nx1 - nx0) * 0.34, (ny1 - ny0) * 0.28, S_W, S_H)
        img = img * (1.0 - m) + 0.03 * m

    img = np.clip(img, 0.0, 1.0) * body
    mapped.update(drawn_eyes)
    strokes = stroke_grid(img, body, mapped, R["edges"])
    return img, mapped, strokes


# --- jeux de frames -------------------------------------------------------
def ease(u):
    return math.sin(u * math.pi)


def draw_lid(img, points, w, h):
    """Trace une paupiere fermee, en arc.

    Ecraser une zone d oeil la fait disparaitre : le vide se comble avec la
    fourrure des bords et il ne reste rien. Un oeil ferme doit se voir, donc
    on pose un arc sombre a sa place.
    """
    x0, y0, x1, y1 = bbox(points)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    rx, ry = (x1 - x0) * 0.44, (y1 - y0) * 0.30
    lower = ellipse_mask(cx, cy, rx, ry, w, h)
    upper = ellipse_mask(cx, cy - ry * 0.62, rx, ry, w, h)
    arc = np.clip(lower - upper, 0.0, 1.0)
    return img * (1.0 - arc) + 0.05 * arc


def close_eye(img, mapped, key, factor, w, h):
    """Ecrase l oeil, et pose une paupiere des qu il est presque clos."""
    if key not in mapped:
        return img
    img = squash_zone(img, mapped[key], max(factor, 0.05), w, h)
    if factor < 0.30:
        img = draw_lid(img, mapped[key], w, h)
    return img


def both_eyes(img, mapped, factor):
    for k in ("eye_left", "eye_right"):
        img = close_eye(img, mapped, k, factor, S_W, S_H)
    return img


def build_sets(base, mapped, strokes):
    sets = {}

    idle = []
    for f in range(N_IDLE):
        o = 1.0
        # Deux clignements par boucle, inegalement espaces pour que le cycle
        # ne s'entende pas.
        for start in (9, 27):
            if start <= f < start + 3:
                o = min(o, 1.0 - ease((f - start) / 3.0) * 0.92)
        idle.append(to_chars(to_cells(base if o > 0.99 else both_eyes(base, mapped, o)), strokes=strokes))
    sets["ASCII"] = idle

    sets["ASCII_SLEEP"] = [to_chars(to_cells(both_eyes(base, mapped, 0.10)), strokes=strokes)] * 4

    wink = []
    for f in range(N_EXPR):
        u = f / float(N_EXPR - 1)
        img = base
        img = close_eye(img, mapped, "eye_right", 1.0 - 0.92 * ease(u), S_W, S_H)
        wink.append(to_chars(to_cells(img), strokes=strokes))
    sets["WINK"] = wink

    sur = []
    for f in range(N_EXPR):
        u = f / float(N_EXPR - 1)
        img = both_eyes(base, mapped, 1.0 + 0.50 * ease(u))
        if "mouth" in mapped:
            img = squash_zone(img, mapped["mouth"], 1.0 + 0.60 * ease(u), S_W, S_H)
        sur.append(to_chars(to_cells(img), strokes=strokes))
    sets["SURPRISED"] = sur

    hap = []
    for f in range(N_EXPR):
        u = f / float(N_EXPR - 1)
        hap.append(to_chars(to_cells(both_eyes(base, mapped, 1.0 - 0.55 * ease(u))), strokes=strokes))
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
        ang.append(to_chars(to_cells(img), strokes=strokes))
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
                # L'antislash des traits diagonaux doit etre double dans un
                # litteral C, sinon il avale le caractere suivant.
                lines.append('    "%s",' % r.replace(chr(92), chr(92) * 2))
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

    src, zones, render = load(args[0])
    base, mapped, strokes = base_image(src, zones, render)
    name = os.path.basename(args[0]).split(".")[0] + "photo"

    print("image : %dx%d" % src.size)
    print("zones : %s" % ", ".join(sorted(mapped)))
    print()
    print("=== repos ===")
    for r in to_chars(to_cells(base), strokes=strokes):
        print("  |" + r + "|")

    sets = build_sets(base, mapped, strokes)
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "assets")
    stem = emit(name, sets, out)

    print()
    for k, v in sets.items():
        print("  %-14s %2d frames" % (k, len(v)))
    print("  -> src/assets/%s.cpp" % stem)
