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
import sys

import numpy as np
from PIL import Image, ImageDraw

# --- panel geometry --------------------------------------------------------
COLS, ROWS = 40, 30          # 240px / 6px, 240px / 8px with the built-in font
CELL_W, CELL_H = 24, 32      # supersampling factor per cell
S_W, S_H = COLS * CELL_W, ROWS * CELL_H   # 960 x 960

# Density ramp, darkest to brightest. No double-quote and no backslash: these
# go straight into C string literals.
# Rampe mesuree dans glcdfont.h, la police que LovyanGFX compile :
# chaque caractere est classe par le nombre de pixels qu'il allume
# reellement sur le panneau (le : y est plus leger que le ., le *
# plus dense que le $). Guillemet double et antislash exclus — ils
# casseraient les chaines C emises.
RAMP = " :.;-~/^_'`,!|<>(){}j+rvxi=fl1cIJL7otunakwzs%XVCeyq&hKmSPUbdgAO2$8HNWGQZERD*0M#@B"

# Deux fois plus de frames qu'avant, jouees deux fois plus vite : meme duree
# de boucle, mais deux fois moins de saut entre deux images. Le bus SPI est
# deja sature a ~11 ms par plein ecran, donc la fluidite ne peut plus venir
# de la cadence — seulement de la finesse de l'animation elle-meme.
N_FRAMES = 80                # ~4.4 s de boucle a 55 ms
N_SLEEP = 24

# --- profils de creature ---------------------------------------------------
# Tout le dessin etait deja en coordonnees normalisees : les formes sortent
# donc dans cette table, et une espece de plus n'est qu'une ligne.
#
# Ce qui distingue les especes a 40x30, par ordre de lisibilite : la hauteur
# des oreilles d'abord, la forme du museau ensuite. Les nuances de pelage,
# elles, ne survivent pas a la conversion.
PROFILES = {
    "cat": {
        "prefix": "CAT",
        "export": "CAT",
        "out": "cat_ascii",
        "head_rx": 0.355, "head_ry": 0.315, "head_cy": 0.585,
        "ear_base": 0.58, "ear_tip": 0.95, "ear_h": 0.250,
        "ear_w_in": 0.085, "ear_w_out": 0.105, "ear_round": 0.0,
        "cheek_dx": 0.78, "cheek_dy": 0.26, "cheek_rx": 0.090, "cheek_ry": 0.110,
        "muzzle_rx": 0.150, "muzzle_ry": 0.098, "muzzle_dy": 0.44,
        "snout": 0.0, "taper": 0.0, "taper_from": 0.0, "ear_dark": 0.0, "muzzle_val": 0.68,
        "bib": 0.0,
        "eye_dx": 0.45, "eye_dy": -0.16, "eye_scale": 1.0, "whisker": 1.0,
    },
    "fox": {
        "prefix": "FOX",
        "export": "FOX",
        "out": "fox_ascii",
        # Tete plus etroite et plus haute, portee un peu plus bas pour
        # laisser respirer des oreilles nettement plus grandes.
        # Crane nettement plus etroit que celui du chat : c est la largeur
        # qui trahissait le felin, pas les oreilles.
        "head_rx": 0.298, "head_ry": 0.288, "head_cy": 0.575,
        # Oreilles hautes, pointues, et surtout ecartees : posees sur un
        # crane etroit, elles doivent deborder pour rester des oreilles de
        # renard et non de chaton.
        # La pointe doit monter, pas s ecarter : un ear_tip superieur a
        # ear_base fait partir les oreilles sur les cotes, et on lit une
        # souris. Base large et pointe presque a l aplomb.
        "ear_base": 0.86, "ear_tip": 0.99, "ear_h": 0.330,
        "ear_w_in": 0.108, "ear_w_out": 0.124, "ear_round": 0.044,
        # Fraise de joues plus marquee, portee plus bas.
        # Fraise portee haut et resserree : trop large en bas, la silhouette
        # s ecrase et le museau disparait — on tombe sur un lynx.
        "cheek_dx": 0.88, "cheek_dy": 0.10, "cheek_rx": 0.076, "cheek_ry": 0.098,
        # Museau etroit, prolonge par un long triangle : le nez du renard.
        "muzzle_rx": 0.080, "muzzle_ry": 0.066, "muzzle_dy": 0.74,
        "snout": 0.150, "taper": 0.66, "taper_from": 0.10,
        # Laisse a zero : une marque sombre sur fond sombre n est pas une
        # marque, c est un trou dans la silhouette.
        "ear_dark": 0.0,
        "muzzle_val": 0.88,
        # Joues et gorge blanches : sur fond noir, seules les marques CLAIRES
        # se lisent comme des marques. C est le signal d espece le plus fort
        # dont on dispose ici.
        "bib": 0.93,
        # Oeil plus petit et moustaches courtes : cales sur un crane de
        # chat, ils touchaient les bords et debordaient dans le vide.
        "eye_dx": 0.50, "eye_dy": -0.20, "eye_scale": 0.76, "whisker": 0.55,
    },
}

P = PROFILES["cat"]


def set_profile(name):
    global P
    if name not in PROFILES:
        raise SystemExit("profil inconnu : %s (connus : %s)"
                         % (name, ", ".join(sorted(PROFILES))))
    P = PROFILES[name]

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
                 mouth_open=0.0, brow=0.0):
    """t in [0,1) drives breathing. Returns a 40x30 luminance array.

    eye_open is either one value for both eyes, or a (left, right) pair — the
    pair is what makes a wink possible. Values above 1.0 widen the eye past
    its resting size, which is what reads as surprise.

    mouth_open in [0,1] opens the muzzle into a round O. At 0 the usual
    closed cat smile is drawn.

    brow in [0,1] lowers an angled brow towards the nose. A narrowed eye
    alone reads as sleepy or pleased at this resolution; the slanted bar
    above it is what makes anger unambiguous.
    """
    if not isinstance(eye_open, (tuple, list)):
        eye_open = (eye_open, eye_open)
    Y, X = np.mgrid[0:S_H, 0:S_W].astype(np.float32)
    X /= S_W
    Y /= S_H

    breath = math.sin(t * 2.0 * math.pi)

    head_cx = 0.5
    head_cy = P["head_cy"] + breath * 0.006
    head_rx = P["head_rx"] + breath * 0.005
    head_ry = P["head_ry"] + breath * 0.009

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
        bx = head_cx + side * head_rx * P["ear_base"]
        ax = head_cx + side * head_rx * P["ear_tip"] + tw
        ay = ear_y - P["ear_h"] - abs(tw) * 0.5
        outer = mask_polygon([
            (bx - side * P["ear_w_in"], ear_y + 0.035),
            (bx + side * P["ear_w_out"], ear_y - 0.020),
            (ax, ay),
        ])
        img = over(img, outer, fur * 0.86)
        if P["ear_round"] > 0.001:
            # Un triangle pur donne une oreille en lame. L ellipse au sommet
            # l emousse et lui donne du corps.
            img = over(img, mask_ellipse(ax, ay + P["ear_round"] * 0.55,
                                         P["ear_round"], P["ear_round"] * 0.85),
                       fur * 0.86)
        inner = mask_polygon([
            (bx - side * P["ear_w_in"] * 0.47, ear_y + 0.012),
            (bx + side * P["ear_w_out"] * 0.67, ear_y - 0.018),
            (ax * 0.30 + bx * 0.70, ay * 0.32 + ear_y * 0.68),
        ])
        img = over(img, inner, 0.72)

        if P["ear_dark"] > 0.001:
            # Garde pour memoire, mais laisse a zero : une marque sombre sur
            # un fond sombre ne se lit pas comme une marque, elle efface la
            # silhouette. Seules les marques CLAIRES portent ici.
            k = P["ear_dark"]
            mx = ax + (bx - ax) * k
            my = ay + (ear_y - ay) * k
            img = over(img, mask_polygon([
                (mx - side * P["ear_w_in"] * 0.42, my),
                (mx + side * P["ear_w_out"] * 0.42, my - 0.020),
                (ax, ay),
            ]), 0.06)

    # --- head -------------------------------------------------------------
    head = mask_ellipse(head_cx, head_cy, head_rx, head_ry)
    cheek_l = mask_ellipse(head_cx - head_rx * P["cheek_dx"],
                           head_cy + head_ry * P["cheek_dy"],
                           P["cheek_rx"], P["cheek_ry"])
    cheek_r = mask_ellipse(head_cx + head_rx * P["cheek_dx"],
                           head_cy + head_ry * P["cheek_dy"],
                           P["cheek_rx"], P["cheek_ry"])
    body = np.clip(head + cheek_l + cheek_r, 0.0, 1.0)

    if P["taper"] > 0.001:
        # Une tete de canide n est pas un ovale : c est un coin, large aux
        # oreilles et resserre vers le nez. On rabote donc les flancs d une
        # largeur qui decroit avec la hauteur.
        # L effilement ne mord qu au-dessous du niveau donne : au-dessus la
        # tete garde toute sa largeur. Rabote des le front, on obtient un
        # crane etroit au lieu d un coin.
        y0 = head_cy + head_ry * P["taper_from"]
        y1 = head_cy + head_ry
        u = np.clip((Y - y0) / max(y1 - y0, 1e-6), 0.0, 1.0)
        halfw = head_rx * (1.0 - P["taper"] * u * u)
        body = body * (np.abs(X - head_cx) <= halfw)

    if P["snout"] > 0.001:
        # Museau prolonge en triangle : c'est le second signal d'espece
        # apres les oreilles.
        tip = head_cy + head_ry * P["muzzle_dy"] + P["snout"]
        body = np.clip(body + mask_polygon([
            (head_cx - P["muzzle_rx"] * 1.05, head_cy + head_ry * 0.30),
            (head_cx + P["muzzle_rx"] * 1.05, head_cy + head_ry * 0.30),
            (head_cx, tip),
        ]), 0.0, 1.0)
    img = over(img, body, fur)

    # Rim shading: darken the silhouette edge so the head detaches from the
    # background instead of bleeding into it.
    inner_body = mask_ellipse(head_cx, head_cy, head_rx * 0.93, head_ry * 0.93)
    rim = np.clip(body - inner_body, 0.0, 1.0)
    img = over(img, rim * 0.80, 0.13)

    # --- muzzle -----------------------------------------------------------
    muzzle = mask_ellipse(head_cx, head_cy + head_ry * P["muzzle_dy"],
                          P["muzzle_rx"], P["muzzle_ry"])
    # Le museau se detache par un liseré sombre autant que par sa clarte :
    # sans lui, une tache claire sur un pelage clair se noie.
    outer_m = mask_ellipse(head_cx, head_cy + head_ry * P["muzzle_dy"],
                           P["muzzle_rx"] * 1.30, P["muzzle_ry"] * 1.34)
    img = over(img, np.clip(outer_m - muzzle, 0.0, 1.0) * 0.90, 0.22)
    img = over(img, muzzle * 0.97, P["muzzle_val"])

    # --- joues et gorge claires -------------------------------------------
    if P["bib"] > 0.001:
        mz_y = head_cy + head_ry * P["muzzle_dy"]
        for side in (-1, 1):
            img = over(img, mask_ellipse(head_cx + side * P["muzzle_rx"] * 1.85,
                                         mz_y - P["muzzle_ry"] * 0.45,
                                         P["muzzle_rx"] * 0.80,
                                         P["muzzle_ry"] * 1.15) * 0.92,
                       P["bib"])
        # Gorge : prolonge le blanc sous le museau jusqu au menton.
        img = over(img, mask_ellipse(head_cx, mz_y + P["muzzle_ry"] * 1.25,
                                     P["muzzle_rx"] * 1.05,
                                     P["muzzle_ry"] * 0.85) * 0.90,
                   P["bib"])

    # --- eyes -------------------------------------------------------------
    eye_y = head_cy + head_ry * P["eye_dy"]
    eye_dx = head_rx * P["eye_dx"]
    for side in (-1, 1):
        ecx = head_cx + side * eye_dx
        eo = eye_open[0] if side < 0 else eye_open[1]
        if eo < 0.06:
            lid = mask_line(ecx - 0.075, eye_y + 0.004,
                            ecx + 0.075, eye_y + 0.004, 0.011)
            img = over(img, lid, 0.16)
            continue

        ery = 0.100 * eo * P["eye_scale"]
        # L'oeil s'elargit aussi un peu quand il s'ouvre au-dela du repos,
        # sinon un oeil surpris n'est qu'une fente plus haute.
        erx = 0.112 * P["eye_scale"] * (1.0 + 0.22 * max(0.0, eo - 1.0))

        # Liseré sombre autour du blanc, dessine AVANT lui pour qu il n en
        # reste qu un anneau.
        #
        # Un blanc a 1.0 sur une fourrure a 0,5 devrait suffire, et pourtant
        # l oeil se fondait : a 40x30 une cellule moyenne 24x32 pixels, donc
        # le bord du blanc et la fourrure atterrissent dans la MEME cellule et
        # s annulent. Un anneau sombre place entre les deux garantit un ecart
        # franc quel que soit le decoupage — c est du contraste local, la
        # seule chose qui survive a la reduction.
        ring = mask_ellipse(ecx, eye_y, erx + EYE_RING, ery + EYE_RING)
        img = over(img, ring, EYE_RING_VAL)

        sclera = mask_ellipse(ecx, eye_y, erx, ery)
        img = over(img, sclera, 1.0)

        # Vertical slit, wide enough to read as a shape rather than as one
        # stray dark cell.
        pcx = ecx + gaze_x
        pcy = eye_y + gaze_y * 0.55
        pupil = mask_ellipse(pcx, pcy, 0.042 * P["eye_scale"], ery * 0.86)
        img = over(img, pupil, 0.14)

    # --- sourcils ---------------------------------------------------------
    if brow > 0.01:
        for side in (-1, 1):
            ecx = head_cx + side * eye_dx
            # De l'exterieur vers l'interieur, en descendant : c'est
            # l'inclinaison vers le nez qui fait la colere. Le bout interieur
            # vient presque toucher la paupiere.
            ox = ecx + side * 0.105
            ix = ecx - side * 0.098
            oy = eye_y - 0.185
            # Le bout interieur descend DANS l oeil : c est l occlusion qui
            # porte le signal a cette resolution, pas la barre elle-meme.
            iy = oy + 0.145 * brow
            img = over(img, mask_line(ox, oy, ix, iy, 0.026), 0.02)

    # --- nose and mouth ---------------------------------------------------
    nose_y = head_cy + head_ry * (P["muzzle_dy"] - 0.12) + P["snout"] * 0.55
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
        for i, (ang, ln0) in enumerate(((-0.32, 0.27), (-0.02, 0.31), (0.26, 0.27))):
            ln = ln0 * P["whisker"]
            a = ang + sway * side * (0.6 + 0.2 * i)
            x2 = bx + side * math.cos(a) * ln
            y2 = by + math.sin(a) * ln
            mx = (bx + x2) * 0.5
            my = (by + y2) * 0.5 + 0.014
            img = over(img, mask_line(bx, by, mx, my, 0.010), 0.94)
            img = over(img, mask_line(mx, my, x2, y2, 0.009), 0.90)

    # --- downsample to the character grid ---------------------------------
    cells = img.reshape(ROWS, CELL_H, COLS, CELL_W).mean(axis=(1, 3))
    # Le masque du corps suit la meme reduction : la posterisation ne doit
    # regarder que la creature, jamais le fond, sinon le noir autour ecrase
    # la distribution et toutes les nuances se tassent dans une seule bande.
    bcells = body.reshape(ROWS, CELL_H, COLS, CELL_W).mean(axis=(1, 3))
    return cells, bcells


# --- rendu tonal et traits -------------------------------------------------
# Reglages trouves par balayage, en maximisant le nombre de caracteres
# DISTINCTS par image — c est ce qui fait la richesse a 40x30 — SOUS
# CONTRAINTE de ne pas perdre d encre.
#
#   ancien (0.06 / 0.86 / 0.80) : 77,0 distincts, 496 cellules visibles
#   retenu (0.03 / 0.62 / 1.00) : 78,9 distincts, 510 cellules visibles
#
# La contrainte n est pas decorative. RAMP[0] est une ESPACE : un ton pousse
# a l index zero ne s assombrit pas, il DISPARAIT. Un balayage qui maximise
# les caracteres distincts sans regarder l encre choisit donc joyeusement un
# gamma qui efface la moitie basse de la creature — menton, museau et
# moustaches — sans que le critere s en apercoive, puisque les espaces ne
# comptent pas comme un caractere. Essaye et verifie.
#
# Une piste a ete essayee et abandonnee : etirement par percentiles puis
# posterisation par quantiles, comme pour les creatures photo. Deux raisons
# de ne pas la reprendre. D abord elle ECRETE aux deux bouts, et les cellules
# saturees se collapsent sur un meme caractere — 70 a 72 distincts seulement,
# et des marques fines perdues autour des yeux. Ensuite elle repond a un
# probleme qui n existe pas ici : elle sert a dompter l histogramme d une
# photo, ecrase par la fourrure. Ce chat est dessine avec un modele
# d eclairage controle, sa distribution est deja etalee.
LIFT = 0.03
SPAN = 0.62
GAMMA = 1.00

# Masque flou inverse, applique sur la GRILLE CELLULE et non sur l image
# pleine resolution : c est apres la reduction que les traits du visage se
# noient, chaque cellule moyennant 24x32 pixels. Nez, yeux, oreilles et
# bouche ressortent de ce que leur cellule s ecarte de ses voisines, et c est
# exactement ce que cette operation amplifie.
#
# Mesure sur le chat : contraste local moyen de 0,172 a 0,236. Le prix est
# une baisse des caracteres distincts, de 79 a 76 — un echange assume, parce
# qu un ecart de deux caracteres sur 81 ne se voit pas, alors qu un tiers de
# contraste local en plus se voit tout de suite.
SHARPEN = 1.4

# Liseré sombre autour du blanc de l oeil. Epaisseur en unites normalisees du
# panneau, et valeur de gris — 0 est le plus sombre.
#
# Une cellule fait 1/40 de large, soit 0,025 : un anneau plus fin que ca ne
# remplit jamais une cellule entiere et se dilue dans la moyenne. Plus epais,
# il mange le blanc. Regle juste en dessous, pour marquer sans devorer.
EYE_RING = 0.018
EYE_RING_VAL = 0.05

# Traits de contour. A ZERO PAR DEFAUT, et ce n est pas un detail : le
# contour remplace une trentaine de cellules aux tons varies par quatre
# caracteres seulement, ce qui coute environ sept caracteres distincts par
# image — de 80 a 73. Il dessine une silhouette nette, mais au prix exact de
# ce qu on cherche ici. Monter au-dessus de zero le reactive.
# Contours de silhouette. Reactives : ils coutent des caracteres distincts,
# mais ce sont eux qui detachent la creature du fond.
EDGES = 0.18

# Seuil d encrage, sur la luminance BRUTE : une cellule vide le reste quoi
# que fasse la courbe tonale. C est ce qui garde les oreilles et les
# moustaches, dessinees hors du masque de la tete.
INK = 0.02


def unsharp(cells, amount, radius=1):
    """Renforce l ecart de chaque cellule avec son voisinage."""
    if amount <= 0.0:
        return cells
    p = np.pad(cells, radius, mode="edge")
    k = 2 * radius + 1
    blur = np.zeros_like(cells)
    for dy in range(k):
        for dx in range(k):
            blur += p[dy:dy + cells.shape[0], dx:dx + cells.shape[1]]
    blur /= float(k * k)
    out = np.clip(cells + amount * (cells - blur), 0.0, 1.0)
    # Le renforcement ne doit pas encrer le fond : une cellule vide le reste.
    return out * (cells > INK)


def stroke_grid(cells, bcells):
    """Traits orientes par le gradient, au niveau cellule.

    C est le vocabulaire du dessin au trait : une cellule traversee par une
    arete recoit le caractere penche comme elle. A cette resolution, une
    arete tracee se lit ou un degrade se perd.
    """
    # A zero, aucun trait — pas meme le contour. La version precedente
    # laissait le liseré de silhouette passer quoi qu il arrive, parce que sa
    # condition ne regardait pas EDGES : la desactivation n en etait pas une.
    if EDGES <= 0.0:
        return {}

    gy, gx = np.gradient(cells)
    mag = np.hypot(gx, gy)
    if mag.max() > 0:
        mag = mag / mag.max()
    bgy, bgx = np.gradient(bcells)
    bmag = np.hypot(bgx, bgy)

    thr = 0.86 - 0.40 * EDGES
    out = {}
    for r in range(ROWS):
        for c in range(COLS):
            x = (c + 0.5) / COLS - 0.5
            y = (r + 0.5) / ROWS - 0.5
            if x * x + y * y > 0.25:
                continue
            is_rim = bmag[r, c] > 0.42 and bcells[r, c] > 0.25
            is_edge = bcells[r, c] > 0.5 and mag[r, c] > thr
            if not (is_rim or is_edge):
                continue
            ax = bgx[r, c] if is_rim else gx[r, c]
            ay = bgy[r, c] if is_rim else gy[r, c]
            # +90 degres : le trait suit l arete, pas la pente.
            ang = (math.degrees(math.atan2(ay, ax)) + 90.0) % 180.0
            if ang < 22.5 or ang >= 157.5:
                out[(r, c)] = "-"
            elif ang < 67.5:
                out[(r, c)] = "/"
            elif ang < 112.5:
                out[(r, c)] = "|"
            else:
                out[(r, c)] = chr(92)
    return out


def to_chars(cells, bcells=None):
    """Luminance grid -> list of 40-char rows, blanked outside the circle."""
    sharp = unsharp(cells, SHARPEN)
    strokes = stroke_grid(sharp, bcells) if bcells is not None else {}
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
            # Un trait prime sur le ton : c est lui qui porte la forme.
            if (r, c) in strokes:
                out.append(strokes[(r, c)])
                continue
            if cells[r, c] <= INK:
                out.append(" ")
                continue
            v = float(np.clip((sharp[r, c] - LIFT) / SPAN, 0.0, 1.0))
            idx = int(round(v ** GAMMA * (n - 1)))
            # Plancher a 1, et c est structurel : RAMP[0] est une ESPACE, donc
            # un ton pousse a zero ne s assombrit pas, il DISPARAIT. Sans ce
            # plancher, assombrir — par le gamma ou par le masque flou —
            # efface des morceaux entiers de la creature. C est arrive : le
            # menton, le museau et les moustaches s etaient volatilises.
            out.append(RAMP[min(max(idx, 1), n - 1)])
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
        frames.append(render_frame(t, eye, tw, gx, gy))
    return frames


def build_sleep():
    frames = []
    for f in range(N_SLEEP):
        t = f / float(N_SLEEP)
        frames.append(render_frame(t, 0.0, 0.0, 0.0, 0.0, sleepy=True))
    return frames


# --- expressions -----------------------------------------------------------
# Boucles courtes, jouees UNE fois pendant une animation nommee au lieu de
# tourner en rond. C'est ce qui permet au chat de faire lui-meme la mimique,
# au lieu d'etre remplace a l'ecran par un visage generique.
N_EXPR = 24


def ease(u):
    """0 au debut, 1 au milieu, 0 a la fin, doux aux extremites."""
    return math.sin(u * math.pi)


def build_wink():
    frames = []
    for f in range(N_EXPR):
        u = f / float(N_EXPR - 1)
        # Seul l'oeil droit se ferme : c'est tout l'interet du parametre par
        # oeil, un clin d'oeil symetrique n'est qu'un clignement.
        frames.append((render_frame(
            u * 0.5, (1.0, 1.0 - ease(u)), 0.0, 0.014, 0.004)))
    return frames


def build_surprised():
    frames = []
    for f in range(N_EXPR):
        u = f / float(N_EXPR - 1)
        e = ease(u)
        frames.append((render_frame(
            u * 0.5, 1.0 + 0.55 * e, 0.045 * e, 0.0, -0.012, mouth_open=e)))
    return frames


def build_happy():
    frames = []
    for f in range(N_EXPR):
        u = f / float(N_EXPR - 1)
        # Yeux plisses de contentement, oreilles qui remuent.
        frames.append((render_frame(
            u, 1.0 - 0.62 * ease(u),
            math.sin(u * math.pi * 2.0) * 0.050,
            math.sin(u * math.pi * 2.0) * 0.020, 0.008)))
    return frames


def build_angry():
    frames = []
    for f in range(N_EXPR):
        u = f / float(N_EXPR - 1)
        # Le sourcil tombe vite, tient, puis se releve : la colere doit se
        # lire tout de suite et durer, pas monter doucement.
        if u < 0.18:
            b = u / 0.18
        elif u > 0.82:
            b = (1.0 - u) / 0.18
        else:
            b = 1.0
        # Vibration courte : la tete tremble au lieu de rester figee.
        shake = math.sin(u * math.pi * 9.0) * 0.011 * b
        frames.append((render_frame(
            u, 1.0 - 0.38 * b, shake * 0.6, shake, 0.008, brow=b)))
    return frames


# --- emit C ----------------------------------------------------------------
# Chaque entree : (prefixe des tableaux de lignes, nom exporte, frames).
def emit(sets):
    os.makedirs(OUT_DIR, exist_ok=True)

    h = ['#pragma once', '#include "../ui/asciiart.h"', '',
         '// Generated by assets/gen_creature.py - do not hand-edit.',
         '// Dense 40x30 ASCII animation, sized for the built-in',
         '// 6x8 font on a 240x240 panel (one character per cell).',
         '//',
         '// The expression sets are short and meant to be played once, in step',
         '// with a named animation, not looped like the idle frames.',
         '', 'namespace assets {', '']
    for _, name, _f in sets:
        h.append('extern const asciiart::Frame %s[];' % name)
        h.append('extern const int %s_COUNT;' % name)
    h += ['', '}', '']
    with open(os.path.join(OUT_DIR, P["out"] + ".h"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(h))

    lines = ['#include "' + P["out"] + '.h"', '',
             '// Generated by assets/gen_creature.py - do not hand-edit.', '',
             'namespace {', '']
    for prefix, _name, frames in sets:
        for i, rows in enumerate(frames):
            lines.append('const char* const %s_%02d[] = {' % (prefix, i))
            for r in rows:
                # Antislash et guillemet doivent etre echappes. Les traits de
                # contour introduisent des antislashs dans l art, et un
                # antislash suivi d un U ou d un x forme une sequence
                # d echappement C invalide qui casse la compilation. Le defaut
                # etait invisible tant que les contours restaient desactives :
                # aucun antislash n apparaissait dans l art.
                esc = r.replace(chr(92), chr(92) * 2).replace('"', chr(92) + '"')
                lines.append('    "%s",' % esc)
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
    with open(os.path.join(OUT_DIR, P["out"] + ".cpp"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))


if __name__ == "__main__":
    # Un argument nomme le profil a generer. Sans argument, tous les profils
    # connus : sinon une retouche du dessin ne mettrait a jour qu'une espece,
    # et les autres divergeraient sans qu'on s'en apercoive.
    wanted = sys.argv[1:] or sorted(PROFILES)

    for name in wanted:
        set_profile(name)
        pfx = P["export"]
        # Les constructeurs rendent des grilles de luminance et le masque
        # associe ; la conversion en caracteres vient apres, les traits ayant
        # besoin des deux.
        raw = [
            (P["prefix"], pfx + "_ASCII", build_awake()),
            ("NAP", pfx + "_ASCII_SLEEP", build_sleep()),
            ("WNK", pfx + "_WINK", build_wink()),
            ("SUR", pfx + "_SURPRISED", build_surprised()),
            ("HAP", pfx + "_HAPPY", build_happy()),
            ("ANG", pfx + "_ANGRY", build_angry()),
        ]
        sets = [(pre, ename, [to_chars(cells, bcells) for cells, bcells in frames])
                for pre, ename, frames in raw]
        emit(sets)

        print("=== profil %s ===" % name)
        for _, ename, frames in sets:
            print("  %-20s %2d frames" % (ename, len(frames)))
        print("  -> src/assets/%s.cpp" % P["out"])
        print()
        print("  apercu au repos :")
        for r in sets[0][2][0]:
            print("    |" + r + "|")
        print()

    print("grille : %dx%d, rampe de %d niveaux" % (COLS, ROWS, len(RAMP)))
