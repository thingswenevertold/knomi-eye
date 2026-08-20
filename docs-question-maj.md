# Léo — ta CI est cassée, et une question sur l'auto-update

Deux choses. La première est urgente et ne demande aucune décision.

---

## 1. Ta CI échoue depuis trois heures, et ton auto-update sert un binaire périmé

Tous les runs depuis 12h27 sont en `failure`. La cause :

```
src/statuspublish.cpp:94:57: error: 'GITHUB_STATUS_TOKEN' was not declared in this scope
     http.addHeader("Authorization", String("Bearer ") + GITHUB_STATUS_TOKEN);
src/statuspublish.cpp:119:16: error: 'GITHUB_STATUS_TOKEN' was not declared in this scope
     if (strlen(GITHUB_STATUS_TOKEN) == 0) return;
*** [.pio/build/esp32dev/src/statuspublish.cpp.o] Error 1
```

Le `secrets.h` que génère `.github/workflows/build-firmware.yml` définit
`WIFI_*`, `OTA_PASSWORD`, `OTA_HOSTNAME` et `ADMIN_PASSWORD` — mais pas
`GITHUB_STATUS_TOKEN`, que `statuspublish.cpp` utilise sans garde depuis le
commit `f680168`.

**Conséquence concrète :** le dernier `firmware/firmware.bin` publié date du
commit `4087a66`, il y a trois heures. Tout ce que tu as poussé depuis n'est
pas dans le binaire que tes cartes téléchargent. Un appui de 5 s sur BOOT
installe donc silencieusement une version périmée, et rien à l'écran ne le
signale.

Ce qui rend ça difficile à voir : ce sont tes propres commits `status: aconit`
qui déclenchent les runs, toutes les dix minutes. Le dépôt a l'air très
vivant.

Le correctif tient en une ligne dans le heredoc du workflow :

```yaml
#define GITHUB_STATUS_TOKEN ""
```

Et c'est le comportement correct, pas un contournement : `publish()` sort déjà
immédiatement quand le jeton est vide, donc un build CI ne publiera pas de
statut — ce qui est ce qu'on veut, un binaire partagé n'ayant pas à écrire
dans ton dépôt.

On peut ouvrir la PR si tu veux, dis-le.

---

## 2. Reprendre ton mécanisme de mise à jour

### Ce qu'on a compris

- la CI construit à chaque push sur `master` et commite
  `firmware/firmware.bin` + `firmware/version.txt`
- `updater.cpp` lit ces deux fichiers depuis
  `raw.githubusercontent.com/thingswenevertold/knomi-eye/master/...`
- 5 s sur BOOT vérifie, un clic confirme, la carte flashe et redémarre

Élégant, et ça enlève effectivement le PC de la boucle.

### Le point qui nous arrête

Ta proposition met la carte de Samuel à jour avec **ton** firmware. Or son
fork a beaucoup divergé : art ASCII animé avec mimiques, contrôle BLE,
provisionnement WiFi par Bluetooth, humeurs, réglages vivants, app téléphone
sur `/play`. Flasher ton binaire effacerait tout ça.

Ce qu'il veut, c'est ton **mécanisme**, pointé sur **sa** publication à lui :
sa CI, son `firmware.bin`, ses URL. Toi tu continues de publier le tien, vos
deux cartes se mettent à jour chacune depuis son propre dépôt.

### Les pièces à reprendre

- `updater.cpp/.h`, URL repointées
- `identity.cpp/.h` — **indispensable** : sans elle une auto-mise-à-jour
  réinitialise hostname et mots de passe aux placeholders de la CI. Sa carte
  s'appelle `zaza` et redeviendrait `knomi-eye`, ce qui recréerait exactement
  la collision de nom qui nous a coûté une matinée
- les événements `VeryLongPress` / `isHeld` / `heldForMs` de `button`
- le workflow CI, adapté à son dépôt
- `FIRMWARE_VERSION` via `PLATFORMIO_BUILD_FLAGS`

### L'obstacle : son dépôt est privé

Vérifié :

| | sans authentification |
|---|---|
| `thingswenevertold/knomi-eye` (public) | HTTP **200** |
| `saucissefarciehumaine-prog/tamagang` (privé) | HTTP **404** |

`updater.cpp` n'envoie aucun en-tête d'authentification, donc depuis un dépôt
privé il se prendrait un 404 à chaque vérification.

Mais tu as déjà résolu ce problème ailleurs : `statuspublish.cpp` utilise un
PAT fin en `Authorization: Bearer` sur l'API Contents. Le même motif rendrait
l'updater compatible avec un dépôt privé.

### Les deux questions

**1.** Pour un dépôt privé, tu partirais sur le jeton comme dans
`statuspublish`, ou tu considères qu'un firmware d'auto-update doit rester sur
du public, parce qu'un PAT en flash est extractible par quiconque tient la
carte trente secondes ?

**2.** Une objection à ce que Samuel fasse tourner le même mécanisme pointé
sur son propre dépôt ? Rien ne toucherait au tien, mais autant demander avant
de dupliquer ton travail plutôt qu'après.

---

## Par ailleurs

La PR [#1](https://github.com/thingswenevertold/knomi-eye/pull/1) est ouverte
chez toi : authentification du WebSocket, et les 324 ms de `statusJson()`.
Un fichier, +14 −2, rebasée sur `8e977f8` et recompilée. Détails et mesures
dans `docs-pour-leo.md`.
