# Question à Léo — reprendre ton mécanisme de mise à jour

Court, trois questions à la fin. Le reste est là pour montrer ce qu'on a
compris de ton code, pour que tu corriges si on s'est trompés.

---

## Ce qu'on a compris

Ton auto-update marche comme ça :

- la CI construit à chaque push sur `master` et commite
  `firmware/firmware.bin` + `firmware/version.txt`
- `updater.cpp` lit ces deux fichiers depuis
  `raw.githubusercontent.com/thingswenevertold/knomi-eye/master/...`
- un appui de 5 s sur BOOT déclenche la vérification, un clic confirme, la
  carte télécharge, flashe et redémarre

Élégant, et ça enlève effectivement le PC de la boucle.

## Le point qui nous arrête

Ta proposition met la carte de Samuel à jour avec **ton** firmware. Or son
fork a beaucoup divergé : art ASCII animé avec mimiques, contrôle BLE,
provisionnement WiFi par Bluetooth, humeurs, réglages vivants, app téléphone
sur `/play`. Flasher ton binaire effacerait tout ça.

Ce qu'il veut, c'est ton **mécanisme**, pointé sur **sa** publication à lui :
sa CI, son `firmware.bin`, ses URL. Toi tu continues de publier le tien, vos
deux cartes se mettent à jour chacune depuis son propre dépôt, et le
mécanisme reste identique.

## Ce qu'on a repéré comme pièces à reprendre

- `updater.cpp/.h` — avec les URL repointées
- `identity.cpp/.h` — **indispensable** : sans elle, une auto-mise-à-jour
  réinitialise hostname et mots de passe aux placeholders de la CI. Sa carte
  s'appelle `zaza` et redeviendrait `knomi-eye`, ce qui recréerait exactement
  la collision de nom qui nous a coûté une matinée
- les événements `VeryLongPress` / `isHeld` / `heldForMs` de `button`, que son
  fork n'a pas
- le workflow CI, adapté à son dépôt
- `FIRMWARE_VERSION` via `PLATFORMIO_BUILD_FLAGS`

## Le vrai obstacle : son dépôt est privé

Vérifié :

| | sans authentification |
|---|---|
| `thingswenevertold/knomi-eye` (public) | HTTP **200** |
| `saucissefarciehumaine-prog/tamagang` (privé) | HTTP **404** |

`updater.cpp` n'envoie aucun en-tête d'authentification, donc depuis un dépôt
privé il se prendrait un 404 à chaque vérification.

Mais on a vu que tu as déjà résolu ce problème ailleurs : `statuspublish.cpp`
utilise `GITHUB_STATUS_TOKEN` en `Authorization: Bearer` sur l'API Contents,
avec un PAT fin limité à un seul dépôt en `Contents: Read and write`. Le même
motif rendrait l'updater compatible avec un dépôt privé.

---

## Les trois questions

**1.** Pour un dépôt privé, tu partirais sur le jeton comme dans
`statuspublish` — API Contents en `Bearer` — ou tu considères qu'un firmware
d'auto-update doit rester sur du public parce qu'un PAT en flash est
extractible par quiconque tient la carte ?

**2.** Comment ta CI gère-t-elle `GITHUB_STATUS_TOKEN` ? Le `secrets.h` qu'elle
génère ne le définit pas, donc on ne voit pas comment le build passe. Il y a
un `#ifndef` quelque part qu'on a raté, ou bien la publication de statut est
simplement désactivée sur les builds CI ?

**3.** Une objection à ce que Samuel fasse tourner le même mécanisme pointé
sur son propre dépôt ? Rien ne toucherait au tien, mais autant demander avant
de dupliquer ton travail plutôt qu'après.

---

Deux correctifs mesurés qui s'appliquent à ta base sont prêts sur une branche,
détaillés dans `docs-pour-leo.md` : l'authentification du WebSocket, et les
324 ms de `statusJson()`. Rien n'a été poussé chez toi.
