# Pour Léo — répartition, accès, et ce qui te sert tout de suite

Document écrit pour être lu par ton assistant avant de toucher au code.
Tout ce qui est marqué **mesuré** l'a été sur la carte, pas déduit.

---

## En un mot

Samuel se concentre sur **les skins et l'interactivité** : ce qu'on voit à
l'écran, et comment on dialogue avec la créature — télécommande Bluetooth
depuis le téléphone, et pilotage depuis le PC quand la carte y est branchée.

**Il te laisse le côté matériel.** Bring-up, alimentation, bootloader,
portage de dalle, auto-update : c'est ton terrain, il n'y touchera pas.

Son dépôt : **https://github.com/saucissefarciehumaine-prog/tamagang**,
branche `main`. Il t'a **invité en collaborateur** — l'invitation t'attend sur
GitHub. Le dépôt reste privé, tu y auras un accès en écriture.

Parti de ton commit initial `c282c43` ce matin, 13 commits depuis.

---

## 1. Deux correctifs qui sont dans ton code aussi

Le plus utile de ce document, et ça ne t'engage à rien : c'est dans votre base
commune, vérifié dans ton `master`, mesuré sur la carte.

### a. Le WebSocket contourne l'authentification du dashboard

`src/web/admin_server.cpp` — `server.addHandler(&ws)` n'a aucune garde, alors
que toutes les routes HTTP passent par `requireAuth()`. Une connexion
WebSocket **sans le moindre identifiant** reçoit l'état complet : IP, humeur,
mémoire, skin actif.

Constaté en s'y connectant depuis un poste du réseau, sans en-tête.

Une ligne avant `addHandler` :

```cpp
ws.setAuthentication("admin", ADMIN_PASSWORD);
```

Chez toi ce serait `identity::adminPassword()` plutôt que la macro, depuis ton
commit `50c2372`.

### b. `statusJson()` coûte 324 ms et effondre la boucle de rendu

**Mesuré : 324 ms par appel.** Avec `BROADCAST_INTERVAL_MS = 200`, soit cinq
appels par seconde, la boucle tombe de 30 à **2,8 images par seconde dès qu'un
dashboard est ouvert**.

Le symptôme est vicieux : l'animation ne s'effondre **que quand on la
regarde**. On ne peut pas observer le problème sans le causer, donc il est
invisible à l'œil nu.

En cause, ces deux lignes :

```cpp
uint32_t sketchSize = ESP.getSketchSize();
uint32_t freeSketch = ESP.getFreeSketchSpace();
```

Elles parcourent l'image en flash avec le cache désactivé, ce qui gèle les
deux cœurs. Or elles sont **constantes pour un firmware donné**. Les passer en
`static const` suffit.

Découpage mesuré, avec un client attaché :

| | avant | après |
|---|---|---|
| `ws.cleanupClients()` | 0,02 ms | 0,02 ms |
| `statusJson()` | **324 ms** | **0,00 ms** |
| `ws.textAll()` | 2–5 ms | 2–5 ms |
| total par image | 358 ms | 33 ms |

---

## 2. Matériel : ce qu'on a appris aujourd'hui, puisque c'est ton terrain

### Deux cartes, un seul nom — ça nous est tombé dessus

`OTA_HOSTNAME` sert à la fois de nom WiFi et de nom mDNS. Nos deux cartes
portaient `knomi-eye`, et `knomi-eye.local` résolvait vers **la tienne**.

Ça a produit un diagnostic entièrement faux pendant une matinée :

- un flash série annonce `Hash of data verified` et `SUCCESS`
- les routes du nouveau firmware répondent quand même `500`
- un OTA avec le mot de passe fraîchement flashé répond `Authentication Failed`
- un power cycle n'y change rien

Tout pointe vers un flash silencieusement raté. Le flash allait très bien :
les requêtes partaient vers l'autre carte.

Le contrôle qui tranche en une ligne — comparer le MAC derrière le nom résolu
avec celui qu'`esptool` affiche en USB :

```
ping -n 1 <nom>.local
arp -a | findstr c8-2e-18
```

Sur ESP32 le MAC BT et le MAC WiFi diffèrent d'une unité sur le dernier
octet : `...:24` en USB et `...:26` en scan BLE sont la même puce.

La carte de Samuel s'appelle maintenant **`zaza`**. Ton `identity.cpp` règle
le problème pour l'avenir, mais une carte sans clé `identity` en NVS sèmera
depuis le `secrets.h` de la CI, dont le placeholder est `knomi-eye` — donc la
collision reviendrait pour toute carte rejoignant ton auto-update depuis un
firmware antérieur. À voir si ça mérite un dérivé du MAC.

### Performance : trois hypothèses plausibles, et fausses

Testées et écartées sur la carte, pour que tu ne les refasses pas :

1. **« Le bus SPI sature. »** Non. Déjà à 80 MHz avec DMA, il autorise près de
   90 images par seconde. On en faisait 25.
2. **« Le sprite en PSRAM coûte trop cher. »** Non. Mis en RAM interne, la
   cadence n'a pas bougé d'un dixième — et les 115 Ko pris au tas ont suffi à
   faire **échouer une mise à jour OTA en plein transfert**. Revenu en PSRAM
   volontairement.
3. **« `startWrite()`/`endWrite()` autour de l'image vont aider. »** Non.
   Aucun gain, le coût des 1200 glyphes n'est pas dans l'ouverture des
   transactions.

Le vrai plafond : **33 ms de dessin par image**, soit 30 images par seconde,
dépensées à rendre l'art comme du texte glyphe par glyphe — 30 lignes de 40
caractères. Le franchir demande de pré-calculer les frames en bitmap et de ne
faire qu'un blit par image.

**Sauts d'image.** `delay(16)` s'ajoute au temps de rendu au lieu de le
compenser, donc les « ~60 fps » du commentaire ne sont jamais atteints et
l'intervalle suit le temps de dessin. Pire : si la période de l'animation
n'est pas un multiple exact de la période de rendu, chaque frame dure tantôt
une image tantôt deux, et ce battement se voit comme des sauts. Le fork pace à
40 ms fixes avec un `frameMs` d'art à 40 ms — une frame d'art par image
affichée.

### Astuce de diagnostic sans mot de passe

Sur cette version d'ESPAsyncWebServer, une route **qui existe** répond `401`
sans identifiants, une route **absente** répond `500`. Donc `401` contre `500`
dit si un firmware expose une route donnée. Utile, vu que la sortie série est
peu fiable sur cette carte.

Un découpage des temps sort dans `/api/status` du fork : `draw_us`, `ble_us`,
`ota_us`, `admin_us`, et le détail `clean_us` / `json_us` / `send_us`.

---

## 3. Ce que Samuel a construit, côté skins et interactivité

Des modules entiers, pas des retouches. Ton `master` n'a rien de tout ça.

| Module | Rôle |
|---|---|
| `src/net/ble.cpp/.h` | Surface de contrôle BLE, UUID Nordic UART. Même JSON que le dashboard, donc une seule définition par valeur. Marche depuis n'importe quelle appli terminal BLE. |
| `src/net/wifiprov.cpp/.h` | Provisionnement WiFi par BLE : `scan`, `join`, `forget`. Le réseau se configure sans câble et sans recompiler. |
| `src/ui/mood.cpp/.h` | Humeurs : `Engaged`, `Idle`, `Uneasy`, `Bored`, `Asleep`, `Lost`. Pilote les cadences de clignement et d'animation. |
| `src/ui/tuning.cpp/.h` | Réglages vivants persistés en NVS : couleurs, luminosité, vitesse, skin. Un seul chemin de persistance, partagé HTTP et BLE. |
| `src/ui/asciiart.cpp/.h` | Moteur d'art ASCII multi-lignes animé, avec jeux d'expressions. |
| `src/web/play_page.h` | App téléphone servie sur `/play`, même origine donc Basic Auth repris par le navigateur. Installable en icône d'accueil. |
| `tools/ble-remote.html` | Télécommande Web Bluetooth, provisionnement WiFi compris. |
| `tools/presence-windows.ps1` | La créature dort quand la session PC se verrouille ou passe en veille. |

Endpoints, exposés des deux côtés avec la même sémantique :

```
HTTP  /api/anim?name=dance   /api/pet   /api/list   /api/presence?away=1
BLE   {"cmd":"anim","name":"dance"}  {"cmd":"pet"}  {"cmd":"list"}
      {"cmd":"presence","away":true}
```

`/api/list` publie les noms de skins et d'animations depuis le firmware, pour
qu'un client construise ses boutons à partir de ce que la carte sait faire au
lieu d'embarquer une copie qui dérive.

Deux directions d'interaction déjà en place, et c'est là que Samuel continue :
**la télécommande Bluetooth** depuis le téléphone, et **le PC branché** — le
verrouillage de session endort la créature, le déverrouillage la réveille.

---

## 4. Le chat est un prototype

**`"Cat"` n'a pas vocation à entrer dans ta table de skins.** Il a servi à
prouver que la chaîne tient de bout en bout : génération, expressions
paramétrées par œil et par museau, mouvement continu, déclenchement à
distance.

Ce que Samuel construit, c'est un **système d'art ASCII animé** — un moteur où
une image devient une créature expressive, pas un dessin de chat en
particulier.

Un point à signaler quand même : le fork a inséré `"Cat"` en **position 0** de
`SKINS[]`, décalant tes 17 skins d'un cran — exactement ce que ton README
déconseille, puisque l'index est persisté en NVS. C'était avant d'avoir lu ton
avertissement, et c'est dit ici plutôt que caché. Comme le chat est un proto,
il n'y a pas de place définitive à lui trouver ; et le moteur, lui, n'impose
aucun réordonnancement — un art ASCII n'est qu'un layout de plus.

---

## 5. Ce que ça donne concrètement

Samuel ne te demande **aucune décision maintenant**. Il travaille sur son
moteur de skins et ses interactions, tu gardes le matériel, et le dépôt est
ouvert pour que tu regardes quand tu veux.

Ce qui est prenable tout de suite, sans rien engager : les deux correctifs de
la section 1.

Le reste — BLE, provisionnement WiFi, tuning, humeurs — te manque et n'est
spécifique à aucun personnage. À discuter quand ça t'arrange.

Rien n'a été poussé chez toi.
