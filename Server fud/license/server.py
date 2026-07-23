from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
import os, json, secrets, time, threading, datetime

PORT      = 6066
KEYS_FILE = os.path.join(os.path.dirname(__file__), "keys.json")
DLL_FILE  = os.path.join(os.path.dirname(__file__), "SpCrashReport.dll")

_tokens      = {}
_tokens_lock = threading.Lock()


def _load():
    if not os.path.exists(KEYS_FILE):
        with open(KEYS_FILE, "w") as f: json.dump({}, f)
    with open(KEYS_FILE) as f: return json.load(f)

def _save(keys):
    with open(KEYS_FILE, "w") as f: json.dump(keys, f, indent=2)

def _status(v):
    """Returns 'ok', 'expired', or 'banned'"""
    if v.get("banned", False):
        return "banned"
    exp = v.get("expires_at")
    if exp:
        try:
            if datetime.datetime.now() > datetime.datetime.strptime(exp, "%Y-%m-%d %H:%M:%S"):
                return "expired"
        except ValueError:
            pass
    return "ok"


class _H(BaseHTTPRequestHandler):

    # ── GET /auth?h=HWID  →  OK:TOKEN | EXPIRED | DENY ───────────
    def _auth(self, qs):
        hwid = qs.get("h", [""])[0].upper().strip()
        if not hwid:
            return self._r(400, "DENY")
        keys = _load()
        for v in keys.values():
            if (v.get("hwid") or "").upper() == hwid:
                s = _status(v)
                if s == "banned":
                    return self._r(200, "DENY")
                if s == "expired":
                    return self._r(200, "EXPIRED")
                # OK — issue one-time token
                tok = secrets.token_hex(30)
                with _tokens_lock:
                    _tokens[tok] = {"hwid": hwid, "expires": time.time() + 60}
                return self._r(200, f"OK:{tok}")
        # HWID hiçbir key'e bağlı değil → aktivasyon gerekiyor
        self._r(200, "UNKNOWN")

    # ── GET /dl?t=TOKEN  →  XOR-encrypted DLL | 403 ─────────────
    def _dl(self, qs):
        tok = qs.get("t", [""])[0].strip()
        with _tokens_lock:
            info = _tokens.pop(tok, None)
        if not info or time.time() > info["expires"]:
            return self._r(403, "DENY")
        if not os.path.exists(DLL_FILE):
            return self._r(503, "DENY")
        with open(DLL_FILE, "rb") as f:
            data = f.read()
        # XOR-encrypt with token as repeating key.
        # Token is single-use → every download produces unique bytes.
        # Dumped bytes are useless without the token.
        key = tok.encode()
        kl  = len(key)
        data = bytes(b ^ key[i % kl] for i, b in enumerate(data))
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    # ── GET /ping?h=HWID  →  OK[:expires_at] | EXPIRED | DENY ────
    def _ping(self, qs):
        hwid = qs.get("h", [""])[0].upper().strip()
        if not hwid:
            return self._r(400, "DENY")
        keys = _load()
        for v in keys.values():
            if (v.get("hwid") or "").upper() == hwid:
                s = _status(v)
                if s == "banned":
                    return self._r(200, "DENY")
                if s == "expired":
                    return self._r(200, "EXPIRED")
                # Include expiry and tag so client can display them
                # Format: OK:expires_at:tag  (either field may be empty)
                exp = v.get("expires_at", "")
                tag = v.get("tag", "")
                if exp or tag:
                    return self._r(200, f"OK:{exp}:{tag}")
                return self._r(200, "OK")
        self._r(200, "DENY")

    # ── GET /ui?h=HWID[&k=KEY]  →  HTML activation page ──────────
    def _ui(self, qs):
        hwid = qs.get("h", [""])[0].upper().strip()
        key  = qs.get("k", [""])[0].strip()

        # ── If key submitted, try to activate ──────────────────────
        msg   = ""
        color = ""
        if key and hwid:
            keys = _load()
            ok = False
            if key in keys:
                existing = (keys[key].get("hwid") or "")
                if not existing or existing.upper() == hwid:
                    if not keys[key].get("banned", False):
                        keys[key]["hwid"]         = hwid
                        keys[key]["activated_at"] = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                        if not keys[key].get("expires_at") and keys[key].get("duration_days"):
                            days = int(keys[key]["duration_days"])
                            if days > 0:
                                exp = datetime.datetime.now() + datetime.timedelta(days=days)
                                keys[key]["expires_at"] = exp.strftime("%Y-%m-%d %H:%M:%S")
                        _save(keys)
                        exp = keys[key].get("expires_at", "")
                        msg   = f"✓ Aktivasyon başarılı!{(' Bitiş: ' + exp) if exp else ' Ömür boyu lisans.'}"
                        color = "#4caf50"
                        ok    = True
            if not ok and not msg:
                msg   = "✗ Geçersiz key veya bu key başka bir cihaza bağlı."
                color = "#f44336"

        html = f"""<!DOCTYPE html>
<html lang="tr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Aktivasyon</title>
<style>
  *{{box-sizing:border-box;margin:0;padding:0}}
  body{{background:#0d0d0d;color:#e0e0e0;font-family:'Segoe UI',sans-serif;
       display:flex;align-items:center;justify-content:center;min-height:100vh}}
  .card{{background:#1a1a1a;border:1px solid #2a2a2a;border-radius:10px;
         padding:36px 40px;width:100%;max-width:420px;box-shadow:0 8px 32px #000a}}
  h2{{font-size:1.3rem;font-weight:600;margin-bottom:6px;color:#fff}}
  .sub{{font-size:.82rem;color:#666;margin-bottom:28px}}
  .hwid-box{{background:#111;border:1px solid #2a2a2a;border-radius:6px;
             padding:10px 14px;font-size:.78rem;color:#888;
             word-break:break-all;margin-bottom:24px}}
  .hwid-box span{{color:#aaa;user-select:all}}
  label{{font-size:.85rem;color:#aaa;display:block;margin-bottom:6px}}
  input[type=text]{{width:100%;background:#111;border:1px solid #333;border-radius:6px;
                    padding:10px 14px;color:#fff;font-size:.95rem;outline:none;
                    transition:border .2s}}
  input[type=text]:focus{{border-color:#5c6bc0}}
  button{{width:100%;margin-top:14px;padding:11px;background:#5c6bc0;
          border:none;border-radius:6px;color:#fff;font-size:1rem;
          font-weight:600;cursor:pointer;transition:background .2s}}
  button:hover{{background:#7986cb}}
  .msg{{margin-top:18px;padding:12px 14px;border-radius:6px;
        font-size:.88rem;background:#111;border:1px solid #2a2a2a;
        color:{color if color else '#aaa'}}}
</style>
</head>
<body>
<div class="card">
  <h2>Lisans Aktivasyonu</h2>
  <p class="sub">Bu cihaza bir key bağlamak için aşağıya girin.</p>
  <div class="hwid-box">HWID:&nbsp;<span>{hwid if hwid else '—'}</span></div>
  <form method="GET" action="/ui">
    <input type="hidden" name="h" value="{hwid}">
    <label for="k">Lisans Keyi</label>
    <input type="text" id="k" name="k" placeholder="XXXX-XXXX-XXXX-XXXX" autocomplete="off" spellcheck="false">
    <button type="submit">Aktifleştir</button>
  </form>
  {f'<div class="msg">{msg}</div>' if msg else ''}
</div>
</body>
</html>"""

        enc = html.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(enc)))
        self.end_headers()
        self.wfile.write(enc)

    # ── GET /activate?h=HWID&k=KEY  →  OK[:expires_at] | DENY ────
    def _activate(self, qs):
        hwid = qs.get("h", [""])[0].upper().strip()
        key  = qs.get("k", [""])[0].strip()
        if not hwid or not key:
            return self._r(400, "DENY")
        keys = _load()
        if key not in keys:
            return self._r(200, "DENY")
        existing = keys[key].get("hwid", "")
        if existing and existing.upper() != hwid:
            return self._r(200, "DENY")   # key bound to different HWID
        if keys[key].get("banned", False):
            return self._r(200, "DENY")

        keys[key]["hwid"]         = hwid
        keys[key]["activated_at"] = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        # Compute expires_at from duration_days (only on first activation)
        if not keys[key].get("expires_at") and keys[key].get("duration_days"):
            days = int(keys[key]["duration_days"])
            if days > 0:
                exp = datetime.datetime.now() + datetime.timedelta(days=days)
                keys[key]["expires_at"] = exp.strftime("%Y-%m-%d %H:%M:%S")

        _save(keys)
        exp = keys[key].get("expires_at", "")
        self._r(200, f"OK:{exp}" if exp else "OK")

    def do_GET(self):
        p  = urlparse(self.path)
        qs = parse_qs(p.query)
        if   p.path == "/auth":     self._auth(qs)
        elif p.path == "/dl":       self._dl(qs)
        elif p.path == "/ping":     self._ping(qs)
        elif p.path == "/activate": self._activate(qs)
        elif p.path == "/ui":       self._ui(qs)
        else:                       self._r(404, "NOT FOUND")

    def _r(self, code, body):
        enc = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(enc)))
        self.end_headers()
        self.wfile.write(enc)

    def log_message(self, fmt, *args):
        print(f"[{self.address_string()}] {fmt % args}")


if __name__ == "__main__":
    print(f"[*] License server on :{PORT}")
    HTTPServer(("0.0.0.0", PORT), _H).serve_forever()
