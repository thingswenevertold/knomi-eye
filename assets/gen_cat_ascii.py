"""Generate dense animated ASCII art of a cat face for the KNOMI panel.

Why a generator instead of hand-typed art: at the panel's real character
resolution the picture is 40x30 = 1200 cells per frame, and there are dozens
of frames. Shading that by hand is not realistic, and hand-typed art cannot
produce smooth gradients anyway.

Pipeline:
  1. Draw the cat with PIL at 960x960 in greyscale (supersampled 24x32 per
     final cell, which is what antialiases the edges).
  2. Composite a simple lighting model over it so the form reads as volume
     rather than as a flat silhouette. Flat fills convert to featureless
     blobs of a single character.
  3. Block-average down to 40x30 luminance.
  4. Map luminance through a density ramp to characters.
  5. Emit src/assets/cat_ascii.cpp / .h as asciiart::Frame data.

Run:  python assets/gen_cat_ascii.py
Needs: pip install pillow numpy
"""

import math
import os

import numpy as np
from PIL import Image, ImageDraw

# --- panel geometry --------------------------------------------------------
COLS, ROWS = 40, 30          # 240px / 6px, 240px / 8px with the built-in font
CELL_W, CELL_H = 24, 32      # supersampling factor per cell
S_W, S_H = COLS * CELL_W, ROWS * CELL_H   # 960 x 960

# Density ramp, darkest to brightest. No double-quote and no backslash: these
# go straight into C string literals.
RAMP = " .`',:;!~+=*xo#%8@"

N_FRAMES = 40                # ~4.4 s loop at 110 ms
N_SLEEP = 16

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "src", "assets")


# --- helpers ---------------------------------------------------------------
def blank():
    return Image.new("L", (S_W, S_H), 0)


def as_arr(img):
    return np.asarray(img, dtype=np.float32) / 255.0


def px(x, y):
    """Normalized [0,1] coordinates to supersampled pixels."""
    return (x * S_W, y * S_H)


def mask_ellipse(cx, cy, rx, ry):
    im = blank()
    d = ImageDraw.Draw(im)
    x0, y0 = px(cx - rx, cy - ry)
    x1, y1 = px(cx + rx, cy + ry)
    d.ellipse([x0, y0, x1, y1], fill=255)
    return as_arr(im)


def mask_polygon(points):
    im = blank()
    d = ImageDraw.Draw(im)
    d.polygon([px(x, y) for x, y in points], fill=255)
    return as_arr(im)


def mask_line(x1, y1, x2, y2, width):
    im = blank()
    d = ImageDraw.Draw(im)
    d.line([px(x1, y1), px(x2, y2)], fill=255, width=int(width * S_W))
    return as_arr(im)


def over(base, m, value):
    """Alpha-composite a constant or an array `value` through mask `m`."""
    return base * (1.0 - m) + value * m


# --- the cat ---------------------------------------------------------------
def render_frame(t, eye_open, twitch, gaze_x, gaze_y, sleepy=False,
                 mouth_open=0.0):
    """t in [0,1) drives breathing. Returns a 40x30 luminance array.

    eye_open is either one value for both eyes, or a (left, right) pair — the
    pair is what makes a wink possible. Values above 1.0 widen the eye past
    its resting size, which is what reads as surprise.

    mouth_open in [0,1] opens the muzzle into a round O. At 0 the usual
    closed cat smile is drawn.
    """
    if not isinstance(eye_open, (tuple, list)):
        eye_open = (eye_open, eye_open)
    Y, X = np.mgrid[0:S_H, 0:S_W].astype(np.float32)
    X /= S_W
    Y /= S_H

    breath = math.sin(t * 2.0 * math.pi)

    head_cx = 0.5
    head_cy = 0.585 + breath * 0.006
    head_rx = 0.355 + breath * 0.005
    head_ry = 0.315 + breath * 0.009

    img = np.zeros((S_H, S_W), dtype=np.float32)

    # Lighting: a soft key from the upper left. This is what turns the flat
    # head fill into something with a readable gradient.
    lx, ly = 0.34, 0.34
    dist = np.sqrt((X - lx) ** 2 + (Y - ly) ** 2)
    key = np.clip(1.0 - dist / 0.80, 0.0, 1.0) ** 1.05
    fur = 0.34 + 0.34 * key           # coat luminance field

    # --- ears (drawn first so the head overlaps their base) ---------------
    ear_y = head_cy - head_ry * 0.62
    for side in (-1, 1):
        tw = twitch if ((side < 0) == (twitch < 0)) else 0.0
        tw = abs(tw) * side
        bx = head_cx + side * head_rx * 0.58
        ax = head_cx + side * head_rx * 0.95 + tw
        ay = ear_y - 0.250 - abs(tw) * 0.5
        outer = mask_polygon([
            (bx - side * 0.085, ear_y + 0.035),
            (bx + side * 0.105, ear_y - 0.020),
            (ax, ay),
        ])
        img = over(img, outer, fur * 0.86)
        inner = mask_polygon([
            (bx - side * 0.040, ear_y + 0.012),
            (bx + side * 0.070, ear_y - 0.018),
            (ax * 0.30 + bx * 0.70, ay * 0.32 + ear_y * 0.68),
        ])
        img = over(img, inner, 0.72)

    # --- head -------------------------------------------------------------
    head = mask_ellipse(head_cx, head_cy, head_rx, head_ry)
    cheek_l = mask_ellipse(head_cx - head_rx * 0.78, head_cy + head_ry * 0.26,
                           0.090, 0.110)
    cheek_r = mask_ellipse(head_cx + head_rx * 0.78, head_cy + head_ry * 0.26,
                           0.090, 0.110)
    body = np.clip(head + cheek_l + cheek_r, 0.0, 1.0)
    img = over(img, body, fur)

    # Rim shading: darken the silhouette edge so the head detaches from the
    # background instead of bleeding into it.
    inner_body = mask_ellipse(head_cx, head_cy, head_rx * 0.93, head_ry * 0.93)
    rim = np.clip(body - inner_body, 0.0, 1.0)
    img = over(img, rim * 0.80, 0.13)

    # --- muzzle -----------------------------------------------------------
    muzzle = mask_ellipse(head_cx, head_cy + head_ry * 0.44, 0.150, 0.098)
    img = over(img, muzzle * 0.95, 0.68)

    # --- eyes -------------------------------------------------------------
    eye_y = head_cy - head_ry * 0.16
    eye_dx = head_rx * 0.45
    for side in (-1, 1):
        ecx = head_cx + side * eye_dx
        eo = eye_open[0] if side < 0 else eye_open[1]
        if eo < 0.06:
            lid = mask_line(ecx - 0.075, eye_y + 0.004,
                            ecx + 0.075, eye_y + 0.004, 0.011)
            img = over(img, lid, 0.16)
            continue

        ery = 0.100 * eo
        # L'oeil s'elargit aussi un peu quand il s'ouvre au-dela du repos,
        # sinon un oeil surpris n'est qu'une fente plus haute.
        erx = 0.112 * (1.0 + 0.22 * max(0.0, eo - 1.0))
        sclera = mask_ellipse(ecx, eye_y, erx, ery)
        img = over(img, sclera, 1.0)

        # Vertical slit, wide enough to read as a shape rather than as one
        # stray dark cell.
        pcx = ecx + gaze_x
        pcy = eye_y + gaze_y * 0.55
        pupil = mask_ellipse(pcx, pcy, 0.042, ery * 0.86)
        img = over(img, pupil, 0.14)

    # --- nose and mouth ---------------------------------------------------
    nose_y = head_cy + head_ry * 0.32
    nose = mask_polygon([(head_cx - 0.050, nose_y - 0.026),
                         (head_cx + 0.050, nose_y - 0.026),
                         (head_cx, nose_y + 0.036)])
    img = over(img, nose, 0.04)

    mouth_y = nose_y + 0.034
    img = over(img, mask_line(head_cx, nose_y + 0.028,
                              head_cx, mouth_y, 0.014), 0.04)

    if mouth_open > 0.01:
        # Museau ouvert : un O plein et sombre, qui se lit tout de suite
        # comme de la surprise a cette resolution.
        img = over(img, mask_ellipse(head_cx, mouth_y + 0.030 * mouth_open,
                                     0.038 * mouth_open, 0.045 * mouth_open),
                   0.04)

    for side in (-1, 1):
        cx = head_cx + side * 0.046
        pts = []
        for i in range(9):
            a = math.pi * (i / 8.0)
            pts.append((cx - math.cos(a) * 0.046, mouth_y + math.sin(a) * 0.036))
        if mouth_open > 0.5:
            continue
        for i in range(len(pts) - 1):
            img = over(img, mask_line(pts[i][0], pts[i][1],
                                      pts[i + 1][0], pts[i + 1][1], 0.014), 0.04)

    # --- whiskers ---------------------------------------------------------
    sway = math.sin(t * 2.0 * math.pi + 1.0) * 0.035
    if not sleepy:
        sway *= 1.0
    else:
        sway *= 0.3
    for side in (-1, 1):
        bx = head_cx + side * 0.080
        by = nose_y + 0.008
        for i, (ang, ln) in enumerate(((-0.32, 0.27), (-0.02, 0.31), (0.26, 0.27))):
            a = ang + sway * side * (0.6 + 0.2 * i)
            x2 = bx + side * math.cos(a) * ln
            y2 = by + math.sin(a) * ln
            mx = (bx + x2) * 0.5
            my = (by + y2) * 0.5 + 0.014
            img = over(img, mask_line(bx, by, mx, my, 0.010), 0.94)
            img = over(img, mask_line(mx, my, x2, y2, 0.009), 0.90)

    # --- downsample to the character grid ---------------------------------
    cells = img.reshape(ROWS, CELL_H, COLS, CELL_W).mean(axis=(1, 3))
    return cells


def to_chars(cells):
    """Luminance grid -> list of 40-char rows, blanked outside the circle."""
    rows = []
    n = len(RAMP)
    for r in range(ROWS):
        out = []
        for c in range(COLS):
            # Cell centre in normalized panel coordinates.
            x = (c + 0.5) / COLS - 0.5
            y = (r + 0.5) / ROWS - 0.5
            if x * x + y * y > 0.25:      # outside the round panel
                out.append(" ")
                continue
            v = float(np.clip((cells[r, c] - 0.06) / 0.86, 0.0, 1.0))
            idx = int(round(v ** 0.80 * (n - 1)))
            out.append(RAMP[min(max(idx, 0), n - 1)])
        rows.append("".join(out).rstrip().ljust(COLS))
    return rows


# --- animation script ------------------------------------------------------
def blink_curve(f, start, length):
    """1.0 open, dipping to 0.0 across `length` frames from `start`."""
    if f < start or f >= start + length:
        return 1.0
    u = (f - start) / float(length)
    return abs(u * 2.0 - 1.0)


def build_awake():
    frames = []
    for f in range(N_FRAMES):
        t = f / float(N_FRAMES)
        # Two blinks per loop, deliberately unevenly spaced so the cycle is
        # not obviously periodic.
        eye = min(blink_curve(f, 9, 4), blink_curve(f, 28, 4))
        # One ear flick, on the left ear, away from the blinks.
        tw = 0.0
        if 18 <= f < 23:
            tw = -math.sin((f - 18) / 5.0 * math.pi) * 0.055
        # Gaze: a slow drift with a quick look to the right mid-loop.
        if f < 14:
            gx, gy = -0.012, 0.004
        elif f < 20:
            gx, gy = 0.026, -0.006
        elif f < 32:
            gx, gy = 0.008, 0.010
        else:
            gx, gy = -0.010, 0.0
        frames.append(to_chars(render_frame(t, eye, tw, gx, gy)))
    return frames


def build_sleep():
    frames = []
    for f in range(N_SLEEP):
        t = f / float(N_SLEEP)
        frames.append(to_chars(render_frame(t, 0.0, 0.0, 0.0, 0.0, sleepy=True)))
    return frames


# --- expressions -----------------------------------------------------------
# Boucles courtes, jouees UNE fois pendant une animation nommee au lieu de
# tourner en rond. C'est ce qui permet au chat de faire lui-meme la mimique,
# au lieu d'etre remplace a l'ecran par un visage generique.
N_EXPR = 12


def ease(u):
    """0 au debut, 1 au milieu, 0 a la fin, doux aux extremites."""
    return math.sin(u * math.pi)


def build_wink():
    frames = []
    for f in range(N_EXPR):
        u = f / float(N_EXPR - 1)
        # Seul l'oeil droit se ferme : c'est tout l'interet du parametre par
        # oeil, un clin d'oeil symetrique n'est qu'un clignement.
        frames.append(to_chars(render_frame(
            u * 0.5, (1.0, 1.0 - ease(u)), 0.0, 0.014, 0.004)))
    return frames


def build_surprised():
    frames = []
    for f in range(N_EXPR):
        u = f / float(N_EXPR - 1)
        e = ease(u)
        frames.append(to_chars(render_frame(
            u * 0.5, 1.0 + 0.55 * e, 0.045 * e, 0.0, -0.012, mouth_open=e)))
    return frames


def build_happy():
    frames = []
    for f in range(N_EXPR):
        u = f / float(N_EXPR - 1)
        # Yeux plisses de contentement, oreilles qui remuent.
        frames.append(to_chars(render_frame(
            u, 1.0 - 0.62 * ease(u),
            math.sin(u * math.pi * 2.0) * 0.050,
            math.sin(u * math.pi * 2.0) * 0.020, 0.008)))
    return frames


# --- emit C ----------------------------------------------------------------
# Chaque entree : (prefixe des tableaux de lignes, nom exporte, frames).
def emit(sets):
    os.makedirs(OUT_DIR, exist_ok=True)

    h = ['#pragma once', '#include "../ui/asciiart.h"', '',
         '// Generated by assets/gen_cat_ascii.py - do not hand-edit.',
         '// Dense 40x30 ASCII animation of a cat face, sized for the built-in',
         '// 6x8 font on a 240x240 panel (one character per cell).',
         '//',
         '// The expression sets are short and meant to be played once, in step',
         '// with a named animation, not looped like the idle frames.',
         '', 'namespace assets {', '']
    for _, name, _f in sets:
        h.append('extern const asciiart::Frame %s[];' % name)
        h.append('extern const int %s_COUNT;' % name)
    h += ['', '}', '']
    with open(os.path.join(OUT_DIR, "cat_ascii.h"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(h))

    lines = ['#include "cat_ascii.h"', '',
             '// Generated by assets/gen_cat_ascii.py - do not hand-edit.', '',
             'namespace {', '']
    for prefix, _name, frames in sets:
        for i, rows in enumerate(frames):
            lines.append('const char* const %s_%02d[] = {' % (prefix, i))
            for r in rows:
                lines.append('    "%s",' % r)
            lines.append('};')
        lines.append('')
    lines += ['}', '', 'namespace assets {', '']
    for prefix, name, frames in sets:
        lines.append('const asciiart::Frame %s[] = {' % name)
        for i in range(len(frames)):
            lines.append('    { %s_%02d, %d },' % (prefix, i, ROWS))
        lines.append('};')
        lines.append('const int %s_COUNT = %d;' % (name, len(frames)))
        lines.append('')
    lines += ['}', '']
    with open(os.path.join(OUT_DIR, "cat_ascii.cpp"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))


if __name__ == "__main__":
    sets = [
        ("CAT", "CAT_ASCII", build_awake()),
        ("NAP", "CAT_ASCII_SLEEP", build_sleep()),
        ("WNK", "CAT_WINK", build_wink()),
        ("SUR", "CAT_SURPRISED", build_surprised()),
        ("HAP", "CAT_HAPPY", build_happy()),
    ]
    emit(sets)
    for _, name, frames in sets:
        print("%-16s %2d frames" % (name, len(frames)))
    print("grille : %dx%d, rampe de %d niveaux" % (COLS, ROWS, len(RAMP)))
    print()
    for label, idx, which in (("clin d'oeil, mi-parcours", 6, 2),
                              ("surprise, mi-parcours", 6, 3)):
        print("apercu " + label + " :")
        for r in sets[which][2][idx]:
            print("  |" + r + "|")
        print()
