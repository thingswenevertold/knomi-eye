# Fork de Samuel — état au 20/08/2026, et ce qui vaut la peine de remonter

Dépôt : **https://github.com/saucissefarciehumaine-prog/tamagang**, branche `main`.
Parti de ton commit initial `c282c43` ce matin, 11 commits ajoutés depuis.

Ce document est écrit pour être lu par un assistant avant de toucher au code.
Tout ce qui est marqué « mesuré » l'a été sur la carte, pas déduit.

---

## 1. Deux bugs qui sont aussi chez toi — vérifiés dans ton `master`

Ce sont les deux choses les plus utiles de ce document. Elles ne dépendent
d'aucun choix créatif, elles sont dans la base commune.

### a. Le WebSocket contourne entièrement l'authentification du dashboard

`src/web/admin_server.cpp` — `server.addHandler(&ws)` n'a aucune garde, alors
que toutes les routes HTTP passent par `requireAuth()`. Une connexion
WebSocket **sans le moindre identifiant** reçoit l'état complet : IP, humeur,
mémoire, skin actif.

Constaté en se connectant depuis un poste du réseau, sans en-tête.

Correctif, une ligne avant `addHandler` :

```cpp
ws.setAuthentication("admin", ADMIN_PASSWORD);
```

Le navigateur reprend ses identifiants tout seul, la page étant servie par le
même hôte. Chez toi ce serait `identity::adminPassword()` plutôt que la macro.

### b. `statusJson()` coûte 324 ms et effondre la boucle de rendu

**Mesuré : 324 ms par appel.** Avec `BROADCAST_INTERVAL_MS = 200`, soit cinq
appels par seconde, la boucle tombe de 30 à **2,8 images par seconde dès qu'un
dashboard est ouvert**.

Le symptôme est vicieux : l'animation ne s'effondre que quand on la regarde.
Impossible à diagnostiquer à l'œil, on ne peut pas observer le problème sans
le causer.

En cause, ces deux lignes de `statusJson()` :

```cpp
uint32_t sketchSize = ESP.getSketchSize();
uint32_t freeSketch = ESP.getFreeSketchSpace();
```

Elles parcourent l'image en flash avec le cache désactivé, ce qui gèle les
deux cœurs. Or elles sont **constantes pour un firmware donné**. Les passer en
`static const` fait tomber `statusJson()` à 0,00 ms.

Découpage mesuré, avec un client attaché :

| | avant | après |
|---|---|---|
| `ws.cleanupClients()` | 0,02 ms | 0,02 ms |
| `statusJson()` | **324 ms** | **0,00 ms** |
| `ws.textAll()` | 2–5 ms | 2–5 ms |
| total par image | 358 ms | 33 ms |

---

## 2. Le piège qui nous est déjà tombé dessus : deux cartes, un seul nom

`OTA_HOSTNAME` sert à la fois de nom WiFi et de nom mDNS. Nos deux cartes
portaient `knomi-eye`, et `knomi-eye.local` résolvait vers **la tienne**.

Ça a produit un diagnostic entièrement faux pendant une matinée :

- un flash série annonce `Hash of data verified` et `SUCCESS`
- les routes du nouveau firmware répondent quand même `500`
- un envoi OTA avec le mot de passe fraîchement flashé répond `Authentication Failed`
- un power cycle n'y change rien

Tout pointe vers un flash silencieusement raté. Le flash allait très bien :
les requêtes partaient vers l'autre carte.

Le contrôle qui tranche en une ligne — comparer le MAC derrière le nom résolu
avec celui qu'`esptool` affiche en USB :

```
ping -n 1 <nom>.local
arp -a | findstr c8-2e-18
```

À noter : sur ESP32 le MAC BT et le MAC WiFi diffèrent d'une unité sur le
dernier octet, donc `...:24` en USB et `...:26` en scan BLE sont la même puce.

La carte de Samuel s'appelle maintenant **`zaza`**. Ton `identity.cpp` règle
le problème pour l'avenir, mais une carte qui n'a jamais eu de clé `identity`
en NVS sèmera depuis le `secrets.h` de la CI, dont le placeholder est
`knomi-eye` — donc la collision reviendrait pour toute carte qui rejoint ton
auto-update depuis un firmware antérieur.

Autre détail utile pour déboguer sans mot de passe : sur cette version
d'ESPAsyncWebServer, **une route qui existe répond `401` sans identifiants,
une route absente répond `500`**. `401` contre `500` dit donc si un firmware
expose une route donnée.

---

## 3. Ce que le fork de Samuel a et que ton `master` n'a pas

Ce sont des modules entiers, pas des retouches.

| Module | Rôle |
|---|---|
| `src/net/ble.cpp/.h` | Surface de contrôle BLE, UUID Nordic UART. Même JSON que le dashboard, donc une seule définition par valeur. Utilisable depuis n'importe quelle appli terminal BLE. |
| `src/net/wifiprov.cpp/.h` | Provisionnement WiFi par BLE : `scan`, `join`, `forget`. Le réseau se configure sans câble et sans recompiler. |
| `src/ui/mood.cpp/.h` | Couche de personnalité : `Engaged`, `Idle`, `Uneasy`, `Bored`, `Asleep`, `Lost`. Pilote les cadences de clignement et d'animation. |
| `src/ui/tuning.cpp/.h` | Réglages vivants persistés en NVS : couleurs, luminosité, vitesse, skin. Un seul chemin de persistance, partagé HTTP et BLE. |
| `src/ui/asciiart.cpp/.h` | Moteur d'art ASCII multi-lignes animé, avec jeux d'expressions. |
| `src/assets/cat_ascii.*` | Art généré : 80 frames d'attente, 24 de sommeil, et 3 mimiques de 24 frames. |
| `src/web/play_page.h` | App téléphone servie sur `/play`, même origine donc Basic Auth repris par le navigateur. Installable en icône d'accueil. |
| `src/util/minijson.h` | Parseur JSON minimal, sans allocation. |
| `tools/ble-remote.html` | Télécommande Web Bluetooth, avec provisionnement WiFi. |
| `tools/find_port.py` | Trouve la carte par VID:PID, et `--check` dit si elle dort en ROM download mode. |
| `tools/presence-windows.ps1` | Déclencheurs Windows : la créature dort quand la session se verrouille ou que le poste passe en veille. |

Endpoints ajoutés, exposés des deux côtés avec la même sémantique :

```
HTTP  /api/anim?name=dance   /api/pet   /api/list   /api/presence?away=1
BLE   {"cmd":"anim","name":"dance"}  {"cmd":"pet"}  {"cmd":"list"}
      {"cmd":"presence","away":true}
```

`/api/list` publie les noms de skins et d'animations depuis le firmware, pour
qu'un client construise ses boutons à partir de ce que la carte sait faire au
lieu d'embarquer une copie qui dérive.

---

## 4. Le chat est un prototype, pas un skin à intégrer

Point important pour situer tout le reste : **`"Cat"` est un proto.** Il sert
à valider une idée, il n'a pas vocation à entrer dans ta table de skins en
l'état.

Ce que Samuel construit, c'est un **système d'art ASCII animé** — un moteur
où une image devient une créature avec ses mimiques, pas un dessin de chat en
particulier. Le chat est simplement le premier sujet de test, celui qui a
servi à prouver que la chaîne tient de bout en bout : génération, expressions
paramétrées par œil et par museau, mouvement continu, déclenchement à
distance.

### Ce que ça implique côté `SKINS[]`

Le fork a inséré `"Cat"` en **position 0**, décalant tes 17 skins d'un cran —
exactement ce que ton README déconseille, puisque l'index est ce qui est
persisté en NVS. C'était avant d'avoir lu ton avertissement, et c'est signalé
ici plutôt que caché.

Une carte passant du fork à ton `master` verrait donc son skin choisi glisser
d'un cran. Mais comme le chat est un proto, la question ne se pose pas
vraiment : il n'y a pas à lui trouver une place définitive maintenant.

Ce qui mérite d'être discuté, c'est le **moteur** — `asciiart` et son système
d'expressions — et lui n'impose aucun réordonnancement : un art ASCII n'est
qu'un layout de plus.

Si tu veux quand même le chat tel quel pour essayer, la ligne va **en fin de
table** :

```cpp
{ "Cat", 4, 8, 18,  90, 175, 255,  30, 70, 130, "/\\_/\\", "w", false, LAYOUT_ASCIIART, 0 },
```

---

## 5. Mesures de performance, pour éviter de refaire les essais ratés

Trois hypothèses testées et **écartées** sur la carte :

1. « Le bus SPI sature. » Non — il est déjà à 80 MHz avec DMA et autorise
   près de 90 images par seconde. On en faisait 25.
2. « Le sprite en PSRAM coûte trop cher. » Non — mis en RAM interne, la
   cadence n'a pas bougé d'un dixième, et les 115 Ko pris au tas ont suffi à
   faire **échouer une mise à jour OTA en plein transfert**. Revenu en PSRAM.
3. « `startWrite()`/`endWrite()` autour de l'image vont aider. » Non — aucun
   gain. Le coût des 1200 glyphes n'est pas dans l'ouverture des transactions.

Le vrai plafond : **33 ms de dessin par image**, soit 30 images par seconde,
dépensées à rendre l'art comme du texte glyphe par glyphe — 30 lignes de 40
caractères. Le franchir demande de pré-calculer les frames en bitmap et de ne
plus faire qu'un blit par image.

Deuxième constat, sur les sauts d'image : `delay(16)` s'ajoute au temps de
rendu au lieu de le compenser, donc les « ~60 fps » du commentaire ne sont
jamais atteints et l'intervalle suit le temps de dessin. Pire, si la période
de l'animation n'est pas un multiple exact de la période de rendu, chaque
frame dure tantôt une image tantôt deux — un battement régulier qui se voit
comme des sauts. Le fork pace à 40 ms fixes avec un `frameMs` d'art à 40 ms :
une frame d'art par image affichée, zéro erreur de quantification.

Un découpage des temps est exposé dans `/api/status` du fork : `draw_us`,
`ble_us`, `ota_us`, `admin_us`, et le détail `clean_us` / `json_us` /
`send_us`. Ton README note déjà que la sortie série est peu fiable sur cette
carte et que `/api/status` est le bon endroit pour ce genre d'observation.

---

## 6. Où en est Samuel, et ce qu'il propose

**Sa priorité n'est pas la fusion.** Il veut d'abord pousser son système
d'art ASCII animé, et travailler de **petits éléments d'interaction** — ce qui
se passe quand on touche la créature, comment elle réagit, ce qu'elle fait
d'elle-même quand on la laisse tranquille. Le chat, ses mimiques et le
déclenchement à distance sont les premières briques de ça.

Donc rien ne presse côté intégration, et il ne te demande aucune décision
maintenant.

Ce qui est utilisable **tout de suite et sans rien engager**, ce sont les deux
correctifs de la section 1 : ils sont dans votre base commune, ils sont
mesurés, et ils ne dépendent d'aucun choix créatif. Prends-les quand tu veux.

Ensuite, si le sujet t'intéresse, du moins au plus engageant :

1. La documentation de la collision de hostname, et `tools/find_port.py`.
2. Les modules moteur — BLE, provisionnement WiFi, tuning, humeurs. C'est ce
   qui manque le plus à ton `master`, et rien là-dedans n'est spécifique au
   chat.
3. Le moteur d'art ASCII, quand il aura pris sa forme. C'est là que Samuel
   veut discuter avec toi plutôt que livrer du fini.

Rien n'a été poussé chez toi. Tout est sur son dépôt, à toi de voir.
