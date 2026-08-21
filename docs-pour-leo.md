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

Parti de ton commit initial `c282c43`, **39 commits depuis**. Une branche
`samuel/tamagang` te le pose chez toi si tu veux le parcourir sans cloner.

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

### Le plafond est tombé : 75 images par seconde

Ce document annonçait 30 fps et un mur dans le rendu de texte. **Les deux
étaient faux**, et le chemin vaut d'être écrit puisqu'il te concerne
directement.

1. Le moteur de texte n'était **pas** le goulot. Frames pré-rendues en bitmap
   1 bit, collées par `drawBitmap` : aucun gain. Par `pushImage` : pire.
   Toutes les voies d'API écrivent pixel par pixel.
2. Une sonde `push_us` a séparé dessin et transfert : l'essentiel des ~20 ms
   de dessin était le **`fillScreen`** — 115 Ko de PSRAM réécrits à chaque
   image pour des pixels recouverts juste après. Supprimé : chaque rangée du
   cadre est écrite exactement une fois.
3. Le plancher restant, 11 ms de composition et 13 ms de push, était la
   **bande passante de la PSRAM** où vit le sprite plein écran, dans les deux
   sens.
4. L'art éveillé court-circuite donc le sprite : composition de chaque bande
   en RAM interne, DMA double tampon, couleurs pré-permutées, droit au
   panneau. Il ne reste que le SPI — **12 ms, le plafond physique du bus**.

Le sprite demeure pour tout le reste : mimiques, sommeil, statuts, autres
layouts. Le sommeil à 25 fps ne dérange personne, elle dort.

**Sauts d'image.** Ton `delay(16)` s'ajoute au temps de rendu au lieu de le
compenser, donc les « ~60 fps » du commentaire ne sont jamais atteints et
l'intervalle suit le temps de dessin. Pire : si la période de l'animation
n'est pas un multiple exact de la période de rendu, chaque frame dure tantôt
N images tantôt N+1, et ce battement se voit. Le fork pace à **13 ms** et les
périodes d'art en sont des multiples exacts — 39, 117, 130, 260 ms. Les deux
doivent bouger ensemble : c'est leur rapport qui compte, pas leur valeur.

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
| `tools/presence-windows.ps1` | La créature dort quand la session PC se verrouille ou passe en veille. Cinq déclencheurs, dont un battement — voir plus bas. |
| `tools/studio.py` + `zone-editor.html` | **Studio créature** : une photo détourée devient un skin complet, sans toucher au firmware. |
| `assets/gen_from_image.py` | Six jeux de frames par **déformation des zones tracées** — aucun dessin par créature. |

### Le studio, puisque c'est le cœur du sujet

Un serveur local sert un éditeur de zones — silhouette, oreilles, yeux,
sourcils, bouche, nez — avec un aperçu 40x30 fidèle, puis trois actions :
enregistrer, générer, envoyer.

`gen_from_image.py` produit idle avec clignements, sommeil, clin d'œil,
surprise, contentement et colère, **en déformant les zones tracées**. Rien
n'est redessiné par créature : un œil s'écrase vers son axe pour cligner, une
bouche s'étire, un sourcil descend. Ça marche donc sur n'importe quelle image.

Il tient `assets/creatures/registry.json`, **append-only** pour la raison que
ton README donne, et émet lui-même le câblage `src/assets/photo_*.inc|h`.

Deux recettes de rendu ont coûté cher et méritent d'être connues :
postérisation par **quantiles** — à bandes de population égale, sinon la
fourrure écrase l'histogramme et tout sort monochrome — et éclairage aplati
par division par un flou large, sans quoi la conversion lit le dégradé
d'illumination plutôt que l'animal. La rampe de 81 caractères est **mesurée
dans `glcdfont.h`** : sur cette police `:` est plus léger que `.` et `*` plus
dense que `$`.

### Retirer une créature sans casser les index

Le pendant du problème que ton README signale. Supprimer une entrée est
impossible — l'index de skin est persisté en NVS, retirer la ligne N ferait
glisser tout ce qui suit. Une créature est donc marquée `retired` et **garde
sa place** : son art cesse d'être référencé, l'éditeur de liens l'élimine, et
un créneau bouchon garde l'alignement des index d'animation.

Mesure utile au passage : **une créature coûte 19 Ko**, pas 200. Son art fait
3 240 lignes dont **129 distinctes** — le compilateur fusionne les chaînes
identiques, et le gros du coût n'est pas le texte (5,3 Ko) mais les pointeurs
(13 Ko).

Endpoints, exposés des deux côtés avec la même sémantique :

```
HTTP  /api/anim?name=dance   /api/pet   /api/list   /api/presence?away=1
      /play  (app telephone)   /  (dashboard, avec un lien vers le studio)
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

La PR [#1](https://github.com/thingswenevertold/knomi-eye/pull/1) est ouverte
avec les deux correctifs de la section 1 — un fichier, +14 −2, rebasée sur ton
dernier `master` et recompilée. Rien d'autre n'a été poussé chez toi.
