"""Trouve le port serie de la carte, et dit si elle execute vraiment le firmware.

Deux problemes recurrents sur ce projet, un par mode :

  (sans option)  Le numero de COM change d'une machine a l'autre et meme d'un
                 replug a l'autre. Un port fige en dur dans platformio.ini
                 casse le flash en silence, alors on ne fige plus rien et on
                 verifie ici avant d'envoyer.

  --check        La carte demarre parfois en ROM download mode au lieu de
                 lancer le firmware : ecran noir, absente du reseau, aucun
                 BLE, mais la LED rouge est allumee et elle a l'air vivante.
                 Voir PANDA_KNOMI_FINDINGS.md section 2. Le test ci-dessous
                 est celui de la doc, et il ne touche pas aux lignes DTR/RTS,
                 donc il ne peut pas faire basculer la carte lui-meme.
"""
import argparse
import subprocess
import sys

from serial.tools import list_ports

# Ponts USB-serie qu'on croise sur ces cartes. La KNOMI V1 utilise un CH340.
PONTS = {
    (0x1A86, 0x7523): "CH340",
    (0x1A86, 0x55D4): "CH9102",
    (0x10C4, 0xEA60): "CP210x",
    (0x0403, 0x6001): "FT232",
}
# L'ESP32-S3 expose son propre USB, sans pont : tout PID sous ce VID compte.
VID_ESPRESSIF = 0x303A


def candidats():
    """Les ports qui ressemblent a une carte, et tous les autres."""
    retenus, ecartes = [], []
    for p in sorted(list_ports.comports(), key=lambda x: x.device):
        if p.vid == VID_ESPRESSIF:
            retenus.append((p, "USB natif Espressif"))
        elif (p.vid, p.pid) in PONTS:
            retenus.append((p, PONTS[(p.vid, p.pid)]))
        else:
            ecartes.append(p)
    return retenus, ecartes


def montre(retenus, ecartes):
    if not retenus and not ecartes:
        print("Aucun port serie sur cette machine.")
        return
    for p, quoi in retenus:
        print("  {:<8} {:<22} {}".format(p.device, quoi, p.description))
    for p in ecartes:
        print("  {:<8} {:<22} {}".format(p.device, "(pas une carte)", p.description))


def en_download_mode(port):
    """True si esptool se connecte sans reset et sans BOOT tenu.

    C'est le test decisif de la doc : s'il repond dans ces conditions, c'est
    que la carte dort dans le bootloader ROM au lieu d'executer le firmware.
    """
    r = subprocess.run(
        [sys.executable, "-m", "esptool", "--port", port,
         "--before", "no-reset", "--after", "no-reset", "flash-id"],
        capture_output=True, text=True, timeout=90,
    )
    return r.returncode == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="teste en plus si la carte est coincee en download mode")
    ap.add_argument("--ini", action="store_true",
                    help="affiche le bloc a coller dans platformio_local.ini")
    args = ap.parse_args()

    retenus, ecartes = candidats()
    print("Ports serie vus :")
    montre(retenus, ecartes)
    print()

    if not retenus:
        print("Aucune carte reconnue.")
        print("Verifie le cable : un cable de charge sans fils data ne montera")
        print("aucun port, et un chargeur C-vers-C n'alimente meme pas la carte")
        print("(voir PANDA_KNOMI_FINDINGS.md).")
        return 1

    if len(retenus) > 1:
        print("Plusieurs cartes possibles : PlatformIO risque de choisir la")
        print("mauvaise. Epingle le bon port dans platformio_local.ini avec --ini.")
        return 2

    port = retenus[0][0].device
    print("Carte trouvee sur {}.".format(port))

    if args.ini:
        print()
        print("A coller dans platformio_local.ini :")
        print()
        print("[env:esp32dev]")
        print("upload_port = {}".format(port))
        print("monitor_port = {}".format(port))

    if args.check:
        print()
        print("Test download mode sur {} ...".format(port))
        if en_download_mode(port):
            print()
            print("EN DOWNLOAD MODE : le firmware ne tourne pas.")
            print("C'est pour ca que l'ecran est noir alors que la LED est allumee.")
            print()
            print("Correctif : debranche physiquement le cable USB, attends deux")
            print("secondes, rebranche. Un reset logiciel ne suffit pas, GPIO0 est")
            print("cable sur la ligne DTR du CH340 qui garde son niveau tant que le")
            print("port n'a pas ete re-enumere. Compte 30 s pour voir la face.")
            return 3
        print("Pas en download mode : la carte execute son firmware.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
