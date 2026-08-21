#pragma once

// For PROGMEM. This header is included before <Arduino.h> in admin_server.cpp,
// so it cannot rely on a translation unit having pulled the macro in already.
#include <Arduino.h>

// The phone app, served at /play.
//
// Kept in its own header, as one raw string literal, because the admin
// dashboard builds its HTML by concatenating a hundred String += lines. That
// is tolerable for a table of numbers and unreadable for a page with a
// layout, so this one is written as HTML and left alone.
//
// It speaks to the same /api/* endpoints as the dashboard and listens on the
// same /ws, so there is no second protocol to keep in sync. Being served by
// the device also means same-origin: the browser carries the Basic Auth
// credentials by itself after a single prompt, which is what makes it usable
// as a home-screen icon rather than a login chore.
//
// The button labels come from /api/list, i.e. from the firmware, so adding an
// animation in face.cpp makes it appear here with no edit to this file.
namespace playpage {

const char HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0c0b0a">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black">
<meta name="apple-mobile-web-app-title" content="Zaza">
<title>Zaza</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body {
    background: #0c0b0a; color: #ff5a00; margin: 0;
    padding: max(16px, env(safe-area-inset-top)) 16px
             max(24px, env(safe-area-inset-bottom));
    font-family: ui-monospace, Consolas, monospace;
    -webkit-tap-highlight-color: transparent;
  }
  .wrap { max-width: 520px; margin: 0 auto; }
  h2 { font-size: 12px; letter-spacing: .12em; color: #c8763a;
       margin: 28px 0 10px; font-weight: normal; }
  /* Le miroir : ce que le boitier dessine, en glyphes, rafraichi par le WS. */
  .mirror {
    width: 168px; height: 168px; margin: 4px auto 12px; border-radius: 50%;
    border: 2px solid #3a2410; display: flex; flex-direction: column;
    align-items: center; justify-content: center; gap: 6px;
  }
  .mirror .eyes { font-size: 34px; letter-spacing: .16em; }
  .mirror .mouth { font-size: 22px; }
  .mood { text-align: center; font-size: 12px; color: #c8763a;
          letter-spacing: .1em; margin-bottom: 4px; min-height: 1.4em; }
  .link { text-align: center; font-size: 11px; margin-bottom: 8px; min-height: 1.4em; }
  .link.down { color: #8a4a1a; }
  button {
    background: #221708; border: 1px solid #3a2410; color: #c8763a;
    font-family: inherit; font-size: 13px; border-radius: 3px;
    min-height: 48px; padding: 10px 12px; cursor: pointer; width: 100%;
  }
  button:active { background: #3a2410; }
  button.on { background: #ff5a00; color: #0c0b0a; border-color: #ff5a00; }
  .pet { background: #ff5a00; color: #0c0b0a; border-color: #ff5a00;
         font-size: 15px; letter-spacing: .1em; min-height: 60px; }
  .grid2 { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
  .chips { display: flex; flex-wrap: wrap; gap: 6px; }
  .chips button { width: auto; flex: 1 1 auto; min-width: 30%; font-size: 12px; }
  label { display: flex; align-items: center; justify-content: space-between;
          gap: 12px; margin-bottom: 12px; font-size: 12px; color: #c8763a; }
  input[type=color] { width: 60px; height: 40px; background: none;
                      border: 1px solid #3a2410; padding: 2px; border-radius: 3px; }
  input[type=range] { width: 100%; accent-color: #ff5a00; height: 40px; }
  .rowlbl { display: flex; justify-content: space-between; font-size: 12px;
            color: #c8763a; margin-bottom: 2px; }
  .studio { display: flex; align-items: center; justify-content: center;
            min-height: 48px; background: #221708; border: 1px solid #3a2410;
            color: #c8763a; border-radius: 3px; text-decoration: none;
            font-size: 13px; }
</style>
</head>
<body>
<div class="wrap">

  <div class="mirror"><span class="eyes" id="eyes">. .</span><span class="mouth" id="mouth">-</span></div>
  <div class="mood" id="mood">&nbsp;</div>
  <div class="link" id="link">&nbsp;</div>

  <button class="pet" id="pet">CARESSER</button>

  <h2>ANIMATIONS</h2>
  <div class="grid2" id="anims"></div>

  <h2>SKIN</h2>
  <div class="chips" id="skins"></div>

  <h2>COULEURS</h2>
  <label>FOND <input type="color" id="c-bg"></label>
  <label>VISAGE <input type="color" id="c-fg"></label>
  <label>ACCENT <input type="color" id="c-acc"></label>
  <button id="reset-col">COULEURS DU SKIN</button>

  <h2>REGLAGES</h2>
  <div class="rowlbl"><span>LUMINOSITE</span><span id="bri-val">--</span></div>
  <input type="range" min="10" max="255" id="bri">
  <div class="rowlbl"><span>VITESSE</span><span id="spd-val">--</span></div>
  <input type="range" min="25" max="400" step="5" id="spd">

  <!-- Revele seulement si le studio repond sur cette machine. Un lien fixe
       serait mort en permanence sur un telephone, ou localhost designe le
       telephone lui-meme. -->
  <div id="studio-box" hidden>
    <h2>CRÉATURE</h2>
    <a class="studio" href="http://localhost:8010" target="_blank" rel="noopener">
      DÉTOURER UNE CRÉATURE</a>
  </div>

</div>
<script>
var $ = function (id) { return document.getElementById(id); };

// Etiquettes lisibles pour les animations que le firmware annonce. Un nom
// inconnu s'affiche tel quel plutot que de disparaitre : ajouter une
// animation cote firmware suffit a la voir apparaitre ici.
var LABELS = {
  wink: "CLIN D'OEIL", dance: 'DANSE', wobble: 'DODELINE', surprised: 'SURPRISE'
};

function h2(n) { return ('0' + n.toString(16)).slice(-2); }
function hex(a) { return '#' + h2(a[0]) + h2(a[1]) + h2(a[2]); }
function rgb(v) {
  return [parseInt(v.substr(1, 2), 16), parseInt(v.substr(3, 2), 16),
          parseInt(v.substr(5, 2), 16)];
}

// Un seul appel en vol : les sliders emettent bien plus vite que le boitier
// ne traite, et la derniere valeur est la seule qui compte.
var busy = false, next = null;
function api(path) {
  if (busy) { next = path; return Promise.resolve(); }
  busy = true;
  return fetch(path, { credentials: 'same-origin' })
    .catch(function () { setLink(false); })
    .then(function () {
      busy = false;
      if (next) { var n = next; next = null; api(n); }
    });
}

function setLink(up, txt) {
  var el = $('link');
  el.className = 'link' + (up ? '' : ' down');
  el.textContent = txt || (up ? '' : 'boitier injoignable');
}

var skinIndex = -1;
function markSkin(i) {
  skinIndex = i;
  var bs = $('skins').children;
  for (var k = 0; k < bs.length; k++) {
    bs[k].className = (+bs[k].dataset.i === i) ? 'on' : '';
  }
}

// Le catalogue vient du boitier : noms de skins et d'animations. Une seule
// requete au chargement, puis on ne redemande plus.
function buildFromList(d) {
  var box = $('anims');
  box.innerHTML = '';
  (d.animNames || []).forEach(function (n) {
    var b = document.createElement('button');
    b.textContent = LABELS[n] || n.toUpperCase();
    b.onclick = function () { api('/api/anim?name=' + encodeURIComponent(n)); };
    box.appendChild(b);
  });

  var sk = $('skins');
  sk.innerHTML = '';
  (d.skinNames || []).forEach(function (n, i) {
    // Nom vide = creneau d'une creature retiree. On garde l'index i, qui
    // reste l'index reel du skin, et on n'affiche simplement rien.
    if (!n) { return; }
    var b = document.createElement('button');
    b.textContent = n;
    b.dataset.i = i;
    b.onclick = function () { api('/api/skin?index=' + i); markSkin(i); };
    sk.appendChild(b);
  });
  if (skinIndex >= 0) markSkin(skinIndex);
}

function showTune(t) {
  $('c-bg').value = hex(t.bg);
  $('c-fg').value = hex(t.fg);
  $('c-acc').value = hex(t.accent);
  $('bri').value = t.brightness;
  $('bri-val').textContent = Math.round(t.brightness / 255 * 100) + '%';
  $('spd').value = t.speedPct;
  $('spd-val').textContent = t.speedPct + '%';
  if (typeof t.skin === 'number') markSkin(t.skin);
}

function pushColors() {
  var b = rgb($('c-bg').value), f = rgb($('c-fg').value), a = rgb($('c-acc').value);
  api('/api/tune?bgR=' + b[0] + '&bgG=' + b[1] + '&bgB=' + b[2] +
      '&fgR=' + f[0] + '&fgG=' + f[1] + '&fgB=' + f[2] +
      '&accR=' + a[0] + '&accG=' + a[1] + '&accB=' + a[2]);
}

$('pet').onclick = function () { api('/api/pet'); };
$('reset-col').onclick = function () {
  api('/api/tune?colorOverride=false').then(loadTune);
};
['c-bg', 'c-fg', 'c-acc'].forEach(function (id) {
  $(id).addEventListener('input', pushColors);
});
$('bri').addEventListener('input', function (e) {
  $('bri-val').textContent = Math.round(e.target.value / 255 * 100) + '%';
  api('/api/tune?brightness=' + e.target.value);
});
$('spd').addEventListener('input', function (e) {
  $('spd-val').textContent = e.target.value + '%';
  api('/api/tune?speedPct=' + e.target.value);
});

function loadTune() {
  return fetch('/api/tune', { credentials: 'same-origin' })
    .then(function (r) { return r.json(); }).then(showTune);
}

function boot() {
  fetch('/api/list', { credentials: 'same-origin' })
    .then(function (r) { return r.json(); })
    .then(buildFromList)
    .then(loadTune)
    .then(function () { setLink(true); })
    .catch(function () { setLink(false); });
}

// Le meme flux que le dashboard : le boitier republie son etat a chaque
// changement, d'ou qu'il vienne — bouton physique, BLE, ou un autre telephone.
function connectWs() {
  var ws = new WebSocket('ws://' + location.host + '/ws');
  ws.onmessage = function (ev) {
    try {
      var d = JSON.parse(ev.data);
      if (d.eyes) $('eyes').textContent = d.eyes;
      if (d.mouth) $('mouth').textContent = d.mouth;
      if (d.mood) {
        // La cadence a cote de l'humeur : c'est la seule facon de savoir si
        // une animation qui parait saccadee vient du dessin ou du reseau.
        $('mood').textContent = d.mood.toUpperCase() +
          (typeof d.fps === 'number' ? '  ·  ' + d.fps.toFixed(0) + ' fps' : '');
      }
      if (typeof d.skin === 'number' && d.skin !== skinIndex) markSkin(d.skin);
      setLink(true);
    } catch (e) { /* trame partielle : la suivante corrigera */ }
  };
  // Le WS ne porte que le miroir. S'il tombe, les boutons continuent de
  // marcher en HTTP, donc on ne declare pas le boitier injoignable pour
  // autant : seul un appel /api/ en echec merite ce mot-la.
  var lost = function () { setLink(false, 'miroir hors ligne'); };
  ws.onclose = function () { lost(); setTimeout(connectWs, 4000); };
  ws.onerror = lost;
}

// Le studio tourne-t-il sur la machine qui affiche cette page ? Une sonde en
// no-cors suffit : elle aboutit si quelque chose repond sur le port, et echoue
// si la connexion est refusee. On ne lit pas la reponse, on ne veut savoir que
// ca — donc pas besoin que le studio expose des en-tetes CORS.
//
// Ainsi le bouton apparait sur le poste de dev et reste absent du telephone,
// au lieu d'y proposer un lien qui ne menerait nulle part.
function probeStudio() {
  var ctl = new AbortController();
  var t = setTimeout(function () { ctl.abort(); }, 1500);
  fetch('http://localhost:8010/targets', { mode: 'no-cors', signal: ctl.signal })
    .then(function () { $('studio-box').hidden = false; })
    .catch(function () { /* absent : on ne montre rien */ })
    .then(function () { clearTimeout(t); });
}

boot();
connectWs();
probeStudio();
</script>
</body>
</html>
)HTML";

}
