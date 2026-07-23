#!/usr/bin/env python3
"""
Usage:
  python add_key.py add  [KEY] [DAYS] [TAG]  # DAYS=0 or omit = lifetime, TAG = username
  python add_key.py list
  python add_key.py del   KEY
  python add_key.py reset KEY           (unbind HWID — transfer to new machine)
  python add_key.py expire KEY DAYS     (extend/set expiry from today)
  python add_key.py ban   KEY           (DENY + client self-destructs at reboot)
  python add_key.py unban KEY
  python add_key.py tag   KEY TAG       (set/update display name)

Examples:
  python add_key.py add 30 Shadow1234   → random key, 30 days, tag=Shadow1234
  python add_key.py add MYKEY-0001 90  → specific key, 90 days
  python add_key.py add                 → random key, lifetime
  python add_key.py add 0 VIP_User      → random key, lifetime, tag=VIP_User
  python add_key.py expire MYKEY 30    → extend: expires 30 days from today
  python add_key.py tag MYKEY-0001 Shadow1234  → update tag for existing key
"""
import sys, json, os, secrets, string, datetime

KEYS_FILE = os.path.join(os.path.dirname(__file__), "keys.json")

FMT = "%Y-%m-%d %H:%M:%S"


def _load():
    if not os.path.exists(KEYS_FILE):
        return {}
    with open(KEYS_FILE) as f:
        return json.load(f)

def _save(d):
    with open(KEYS_FILE, "w") as f:
        json.dump(d, f, indent=2)

def _gen():
    chars = string.ascii_uppercase + string.digits
    seg = lambda n: "".join(secrets.choice(chars) for _ in range(n))
    return f"{seg(4)}-{seg(4)}-{seg(4)}-{seg(4)}"

def _days_left(exp_str):
    if not exp_str:
        return None
    try:
        delta = datetime.datetime.strptime(exp_str, FMT) - datetime.datetime.now()
        return max(0, delta.days)
    except ValueError:
        return None


def cmd_add(key=None, days=0, tag=None):
    keys = _load()
    if key is None:
        key = _gen()
    if key in keys:
        print(f"[!] Key already exists: {key}")
        return
    days = int(days)
    keys[key] = {
        "hwid":         None,
        "activated_at": None,
        "expires_at":   None,
        "duration_days": days if days > 0 else None,
        "banned":       False,
        "tag":          tag or "",
    }
    _save(keys)
    dur = f"{days} days after activation" if days > 0 else "lifetime"
    tag_str = f"  tag={tag}" if tag else ""
    print(f"[+] Added: {key}  ({dur}){tag_str}")

def cmd_list():
    keys = _load()
    if not keys:
        print("(no keys)")
        return
    now = datetime.datetime.now()
    for k, v in keys.items():
        hwid   = v.get("hwid") or "—"
        exp    = v.get("expires_at", "")
        dur    = v.get("duration_days")
        tag    = v.get("tag") or ""
        banned = "  [BANNED]" if v.get("banned") else ""

        if exp:
            left = _days_left(exp)
            if left == 0:
                exp_label = f"EXPIRED ({exp})"
            else:
                exp_label = f"{exp}  ({left}d left)"
        elif dur:
            exp_label = f"lifetime after {dur}d activation"
        else:
            exp_label = "lifetime"

        when = v.get("activated_at") or "not activated"
        tag_str = f"  tag={tag}" if tag else ""
        print(f"  {k}  hwid={hwid}  activated={when}  expiry={exp_label}{tag_str}{banned}")

def cmd_del(key):
    keys = _load()
    if key not in keys:
        print(f"[!] Not found: {key}")
        return
    del keys[key]
    _save(keys)
    print(f"[-] Deleted: {key}")

def cmd_reset(key):
    keys = _load()
    if key not in keys:
        print(f"[!] Not found: {key}")
        return
    keys[key]["hwid"]         = None
    keys[key]["activated_at"] = None
    keys[key]["expires_at"]   = None   # expiry resets; recalculated on next activation
    _save(keys)
    print(f"[~] Reset: {key}  (HWID unbound, expiry cleared)")

def cmd_expire(key, days):
    keys = _load()
    if key not in keys:
        print(f"[!] Not found: {key}")
        return
    days = int(days)
    if days <= 0:
        keys[key]["expires_at"]    = None
        keys[key]["duration_days"] = None
        _save(keys)
        print(f"[~] {key}  → lifetime (expiry removed)")
        return
    exp = datetime.datetime.now() + datetime.timedelta(days=days)
    keys[key]["expires_at"]    = exp.strftime(FMT)
    keys[key]["duration_days"] = days
    _save(keys)
    print(f"[~] {key}  → expires {keys[key]['expires_at']}  ({days}d from now)")

def cmd_ban(key):
    keys = _load()
    if key not in keys:
        print(f"[!] Not found: {key}")
        return
    keys[key]["banned"] = True
    _save(keys)
    print(f"[x] Banned: {key}  (next auth/ping → DENY → client self-destructs at reboot)")

def cmd_unban(key):
    keys = _load()
    if key not in keys:
        print(f"[!] Not found: {key}")
        return
    keys[key]["banned"] = False
    _save(keys)
    print(f"[v] Unbanned: {key}")

def cmd_tag(key, tag):
    keys = _load()
    if key not in keys:
        print(f"[!] Not found: {key}")
        return
    keys[key]["tag"] = tag
    _save(keys)
    print(f"[~] {key}  → tag={tag}")


if __name__ == "__main__":
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(0)

    cmd = args[0].lower()

    if cmd == "add":
        # add [KEY_OR_DAYS] [DAYS] [TAG]
        # Detect if first arg is a number (days) or a key string
        key, days, tag = None, 0, None
        if len(args) >= 2:
            try:
                days = int(args[1]); key = None   # add DAYS [TAG]
                if len(args) >= 3:
                    tag = args[2]
            except ValueError:
                key = args[1]                      # add KEY [DAYS] [TAG]
                if len(args) >= 3:
                    try:
                        days = int(args[2])
                        if len(args) >= 4:
                            tag = args[3]
                    except ValueError:
                        tag = args[2]              # add KEY TAG (no days = lifetime)
        cmd_add(key, days, tag)

    elif cmd == "list":   cmd_list()
    elif cmd == "del":    cmd_del(args[1])
    elif cmd == "reset":  cmd_reset(args[1])
    elif cmd == "expire": cmd_expire(args[1], args[2] if len(args) > 2 else 0)
    elif cmd == "ban":    cmd_ban(args[1])
    elif cmd == "unban":  cmd_unban(args[1])
    elif cmd == "tag":    cmd_tag(args[1], args[2] if len(args) > 2 else "")
    else:
        print(__doc__)
