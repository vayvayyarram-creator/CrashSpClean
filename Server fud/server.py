from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
import os, random

PORT      = 6067
USERS_DIR = "users"
os.makedirs(USERS_DIR, exist_ok=True)

NAMES = ["Shadow","Ghost","Phantom","Viper","Storm","Blaze","Frost","Nova","Apex","Raven",
         "Cobra","Reaper","Hawk","Wolf","Titan","Dusk","Ember","Spike","Neon","Wraith"]

def name_file(h): return os.path.join(USERS_DIR, h + ".name")
def cfg_file(h):  return os.path.join(USERS_DIR, h + ".cfg")

def get_name(h):
    nf = name_file(h)
    if not os.path.exists(nf):
        name = random.choice(NAMES) + str(random.randint(1000, 9999))
        open(nf, "w").write(name)
        print(f"[+] Yeni kullanici: {h} -> {name}")
    return open(nf).read().strip()

class H(BaseHTTPRequestHandler):
    def hwid(self):
        q = parse_qs(urlparse(self.path).query)
        h = q.get("h", [""])[0]
        return h if h and len(h) <= 32 and h.isalnum() else ""

    def do_GET(self):
        h = self.hwid()
        if not h:
            self.send_error(400); return

        name = get_name(h)
        cfg  = open(cfg_file(h), "rb").read() if os.path.exists(cfg_file(h)) else b""

        body = f"NAME:{name}\n".encode() + cfg
        self.send_response(200)
        self.send_header("Content-Length", len(body))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        h = self.hwid()
        if not h or not os.path.exists(name_file(h)):
            self.send_error(400); return

        n    = int(self.headers.get("Content-Length", 0))
        data = self.rfile.read(n)
        open(cfg_file(h), "wb").write(data)

        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"ok")

    def log_message(self, fmt, *args):
        pass  # sessiz

print(f"[*] Config server basliyor: 0.0.0.0:{PORT}")
HTTPServer(("0.0.0.0", PORT), H).serve_forever()
