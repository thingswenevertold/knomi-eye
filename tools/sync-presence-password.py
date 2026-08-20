"""Recopie ADMIN_PASSWORD de include/secrets.h vers tools/presence.local.json.

Pourquoi cet outil existe : le mot de passe du dashboard vit a trois endroits
— secrets.h, le firmware reellement flashe, et ce fichier de configuration.
Changer secrets.h ne suffit pas ; il faut reflasher, et penser a realigner ce
fichier. L'oubli se manifeste par un 401 sur les taches de presence, ce qui
ressemble a une carte injoignable plutot qu'a un mot de passe perime.

A lancer apres chaque envoi qui modifie ADMIN_PASSWORD :

    py -3.12 tools/sync-presence-password.py

La valeur est deplacee d'un fichier local a l'autre — tous deux ignores par
git — et n'est jamais affichee.
"""
import io
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SECRETS = os.path.join(ROOT, "include", "secrets.h")
PRESENCE = os.path.join(ROOT, "tools", "presence.local.json")


def main():
    if not os.path.exists(SECRETS):
        print("Absent : %s" % SECRETS)
        return 1

    src = io.open(SECRETS, encoding="utf-8", errors="replace").read()
    m = re.search(r'#define\s+ADMIN_PASSWORD\s+"((?:[^"\\]|\\.)*)"', src)
    if not m:
        print("ADMIN_PASSWORD introuvable dans include/secrets.h")
        return 1
    admin = m.group(1)

    if os.path.exists(PRESENCE):
        cfg = json.load(io.open(PRESENCE, encoding="utf-8"))
    else:
        # Premiere execution : on pose une configuration complete plutot que
        # d'obliger a la creer a la main.
        cfg = {"host": "zaza.local", "user": "admin"}

    before = cfg.get("password", "")
    if before == admin:
        print("Deja aligne (%d caracteres). Rien a faire." % len(admin))
        return 0

    cfg["password"] = admin
    io.open(PRESENCE, "w", encoding="utf-8").write(json.dumps(cfg, indent=2) + "\n")
    print("presence.local.json realigne : %d -> %d caracteres."
          % (len(before), len(admin)))
    print("Rappel : la carte ne prend la nouvelle valeur qu'apres un envoi.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
