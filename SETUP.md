# Repartir sur une autre machine

Ce dépôt ne contient **aucun secret**, par conception. Deux fichiers sont
donc absents après un clone et doivent être recréés à la main. Sans eux la
compilation échoue tout de suite, avec un message explicite.

## 1. Outils

```bash
py -3.12 -m pip install --user platformio esptool
```

PlatformIO télécharge la chaîne de compilation ESP32 au premier build :
environ 1,6 Go, comptez quelques minutes. Les suivants prennent 10 secondes.

## 2. Cloner

```bash
git clone git@github.com:saucissefarciehumaine-prog/tamagang.git
```

Il faut que la clé SSH de la machine soit enregistrée sur le compte GitHub
(`https://github.com/settings/keys`).

## 3. Recréer les deux fichiers manquants

```bash
cp include/secrets.h.example include/secrets.h
cp platformio_local.ini.example platformio_local.ini
```

| Fichier | À remplir |
|---|---|
| `include/secrets.h` | mots de passe OTA et admin, nom et code BLE. Les champs WiFi peuvent rester vides : le réseau se configure par Bluetooth. |
| `platformio_local.ini` | le `--auth=`, **identique** à `OTA_PASSWORD` de `secrets.h`. |

Si les deux valeurs `OTA_PASSWORD` diffèrent, l'envoi par WiFi est refusé
sans explication utile.

## 4. Envoyer le firmware

**Par WiFi**, quand le boîtier est joignable sur le réseau :

```bash
py -3.12 -m platformio run -e esp32dev-ota -t upload
```

Vérifiez `upload_port` dans `platformio.ini` : il vaut `knomi-eye.local`, ce
qui suppose que le mDNS résout. Sinon, mettez l'adresse IP.

**Par câble**, quand il n'y a pas de réseau — et c'est le cas le plus
probable sur une machine neuve :

1. Débranchez l'USB-C
2. Maintenez **BOOT**
3. Rebranchez en maintenant
4. Relâchez

L'écran doit rester **noir** : c'est le signe qu'il est en mode
téléchargement. Puis :

```bash
py -3.12 -m platformio run -e esp32dev -t upload
```

Ajustez `upload_port` / `monitor_port` au bon port COM.

## 5. Sans réseau, tout passe par le Bluetooth

C'est la situation par défaut ailleurs que chez soi. Le boîtier s'annonce
sous le nom défini par `BLE_NAME`, sur les UUID Nordic UART — donc pilotable
depuis n'importe quelle appli de terminal BLE gratuite (nRF Connect, Serial
Bluetooth Terminal), sans rien développer.

Écrivez du JSON dans la caractéristique `6E400002-…` :

```json
{"cmd":"scan"}
{"cmd":"join","ssid":"MonReseau","pass":"…"}
{"cmd":"forget"}
{"fgR":0,"fgG":255,"fgB":120}
{"brightness":120}
{"speedPct":200}
{"skin":3}
```

Le boîtier répond son état complet sur `6E400003-…`.

Une fois un réseau rejoint, les identifiants sont conservés en NVS et le
serveur web démarre dans la foulée — sans redémarrage.

**Si `BLE_PASSKEY` n'est pas nul, il faut appairer d'abord**, sinon les
écritures sont rejetées en silence, ce qui ressemble beaucoup à un service
en panne.

Pour une interface graphique plutôt que du JSON, `tools/ble-remote.html`
fait la même chose via Web Bluetooth. Attention : ce standard exige une
origine sécurisée. Un `file://` ne marchera jamais, il faut du `https://` ou
`http://localhost` :

```bash
cd tools && py -3.12 -m http.server 8000
```

## 6. Modifier le chat

L'art ASCII est **généré**, pas écrit à la main :

```bash
py -3.12 assets/gen_cat_ascii.py
```

Le script affiche un aperçu texte du premier cadre, ce qui permet de juger
sans rien déployer. Les réglages utiles sont commentés en tête de fichier.
`deploie.bat` enchaîne génération puis envoi par WiFi.

## À lire avant de déboguer

`PANDA_KNOMI_FINDINGS.md` documente les pièges de cette carte, dont deux qui
coûtent des heures : elle démarre parfois en mode téléchargement au lieu
d'exécuter le firmware — écran noir, aucun réseau, aucune sortie série — et
un chargeur USB-C ne l'alimente pas du tout.
