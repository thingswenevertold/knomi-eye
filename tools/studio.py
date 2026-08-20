"""Studio : toute la chaine creature depuis le navigateur.

Une page statique ne peut ni ecrire un fichier ni lancer PlatformIO. Ce
petit serveur local comble exactement ce manque : il sert l'editeur de
zones et expose trois actions — enregistrer le manifeste, generer l'art,
envoyer le firmware.

    py -3.12 tools/studio.py

puis http://localhost:8010

Servir depuis localhost a un second effet utile : c'est une origine
securisee, donc la telecommande Web Bluetooth fonctionne aussi depuis ce
serveur, alors qu'un fichier ouvert en file:// serait refuse.

Le serveur n'ecoute que sur la boucle locale. Il execute des commandes de
build sur demande, ce qui n'a rien a faire sur une interface reseau.
"""
import json
import os
import subprocess
import sys
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
CREATURES = os.path.join(ROOT, "assets", "creatures")
PORT = 8010

PY = sys.executable

# Une seule compilation a la fois : deux pio simultanes se marcheraient
# dessus. Mais le REFUS est immediat — l interface reste vivante, elle ne
# fait pas la queue derriere un build.
BUILD_LOCK = threading.Lock()


def run(cmd, cwd=ROOT, timeout=900):
    """Lance une commande et rend (code, sortie fusionnee)."""
    try:
        p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                           timeout=timeout, errors="replace")
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    except subprocess.TimeoutExpired:
        return 1, "Delai depasse apres %d s." % timeout


class Handler(BaseHTTPRequestHandler):
    # Le journal par defaut inonde la console a chaque image recue.
    def log_message(self, fmt, *args):
        pass

    def _send(self, code, body, ctype="application/json"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body)
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _file(self, path, ctype):
        if not os.path.exists(path):
            self._send(404, {"error": "introuvable"})
            return
        with open(path, "rb") as fh:
            self._send(200, fh.read(), ctype)

    # --- lecture ---------------------------------------------------------
    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)

        if u.path in ("/", "/index.html"):
            self._file(os.path.join(TOOLS, "zone-editor.html"), "text/html; charset=utf-8")
        elif u.path == "/ble":
            self._file(os.path.join(TOOLS, "ble-remote.html"), "text/html; charset=utf-8")
        elif u.path == "/creatures":
            os.makedirs(CREATURES, exist_ok=True)
            names = sorted(f for f in os.listdir(CREATURES) if f.endswith(".zones.json"))
            self._send(200, {"creatures": names})
        elif u.path == "/manifest":
            self._file(os.path.join(CREATURES, os.path.basename(q.get("name", [""])[0])),
                       "application/json")
        elif u.path == "/image":
            name = os.path.basename(q.get("name", [""])[0])
            ext = os.path.splitext(name)[1].lower()
            ctype = {".png": "image/png", ".gif": "image/gif",
                     ".webp": "image/webp"}.get(ext, "image/jpeg")
            self._file(os.path.join(CREATURES, name), ctype)
        else:
            self._send(404, {"error": "route inconnue"})

    # --- actions ---------------------------------------------------------
    def do_POST(self):
        u = urlparse(self.path)
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b"{}"
        try:
            payload = json.loads(raw.decode("utf-8"))
        except Exception:
            payload = {}

        if u.path == "/save":
            self._save(payload)
        elif u.path == "/upload-image":
            self._upload_image(payload)
        elif u.path == "/generate":
            self._generate(payload)
        elif u.path == "/deploy":
            self._deploy()
        else:
            self._send(404, {"error": "route inconnue"})

    def _save(self, payload):
        name = os.path.basename(payload.get("name") or "creature.zones.json")
        if not name.endswith(".zones.json"):
            name = os.path.splitext(name)[0] + ".zones.json"
        os.makedirs(CREATURES, exist_ok=True)
        path = os.path.join(CREATURES, name)
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(payload.get("manifest", {}), fh, indent=2, ensure_ascii=False)
        self._send(200, {"ok": True, "path": "assets/creatures/" + name})

    def _upload_image(self, payload):
        """Copie l'image a cote du manifeste, pour que le generateur la trouve."""
        import base64
        name = os.path.basename(payload.get("name") or "")
        data = payload.get("data") or ""
        if not name or "," not in data:
            self._send(400, {"error": "image manquante"})
            return
        os.makedirs(CREATURES, exist_ok=True)
        with open(os.path.join(CREATURES, name), "wb") as fh:
            fh.write(base64.b64decode(data.split(",", 1)[1]))
        self._send(200, {"ok": True, "path": "assets/creatures/" + name})

    def _generate(self, payload):
        name = os.path.basename(payload.get("name") or "")
        path = os.path.join(CREATURES, name)
        if not os.path.exists(path):
            self._send(400, {"error": "manifeste absent : " + name})
            return
        if not BUILD_LOCK.acquire(blocking=False):
            self._send(200, {"ok": False,
                             "log": "Une generation ou un envoi tourne deja — attends la fin."})
            return
        try:
            code, out = run([PY, os.path.join("assets", "gen_from_image.py"), path])
        finally:
            BUILD_LOCK.release()
        self._send(200, {"ok": code == 0, "log": out[-6000:]})

    def _deploy(self):
        if not BUILD_LOCK.acquire(blocking=False):
            self._send(200, {"ok": False,
                             "log": "Une generation ou un envoi tourne deja — attends la fin."})
            return
        try:
            code, out = run([PY, "-m", "platformio", "run",
                             "-e", "esp32dev-ota", "-t", "upload"])
        finally:
            BUILD_LOCK.release()
        # La sortie de PlatformIO est enorme ; on ne garde que ce qui informe.
        keep = [l for l in out.splitlines()
                if any(k in l for k in ("Result:", "Success", "SUCCESS", "FAILED",
                                        "error:", "Error", "Uploading", "Authenticating",
                                        "RAM:", "Flash:"))]
        self._send(200, {"ok": code == 0, "log": "\n".join(keep[-40:]) or out[-3000:]})


if __name__ == "__main__":
    os.makedirs(CREATURES, exist_ok=True)
    # Multi-fils : un envoi dure plus d une minute, et un serveur mono-fil
    # ne servait plus RIEN pendant ce temps — ni la page, ni l enregistrement.
    srv = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    url = "http://localhost:%d/" % PORT
    print("Studio creature : %s" % url)
    print("Ctrl+C pour arreter.")
    threading.Timer(0.6, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nArrete.")
