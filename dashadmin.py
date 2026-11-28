#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""dashadmin - what the dashboard's admin pages ask of the Gateway (decided
28 Nov 2026: per service, its log, its settings changed, a restart, from
the dashboard, behind a password).

dashgate never runs as root. It writes one request, and systemd runs this
as root for it (dashgate-admin.path, PathExists=), the way translategate's
Languages page asks for its models:

    /var/lib/dashgate/admin/request.json   dashgate's, 8 KB at most:
        {"id": "HEX", "do": "log" | "restart" | "set", "key": "web",
         "allow": ["192.168.1.60"], "allow_local": false, "verbose": true}
    /var/lib/dashgate-admin/result.json    this one's (root:dashgate 0640):
        {"id": "HEX", "ok": true, "text": "..."}

What it does, and nothing else:
  - log: the service's last 200 lines from the journal, as the service
    keeps them (who connected where, never paths, contents or passwords:
    guideline 7.3), so the page can't show more than the log holds;
  - restart: systemctl restart, the service's unit only;
  - set: aera-gateway install SERVICE with the options the page offers
    (who may connect, stations at home, every request logged), through
    the installer's own checks and what an update keeps; a gateway that
    doesn't answer afterwards has its settings before back. Ports stay as
    they are: each Atari app has its own in its settings.

The request is read as dashgate's own (not a link, its owner dashgate), is
checked again here as if it came from anyone, and is gone once read. The
password is stored hashed (PBKDF2-SHA256, salted), here too. Standard
Python 3 only.
"""

import grp
import hashlib
import hmac
import ipaddress
import json
import os
import pwd
import re
import stat
import subprocess
import sys

USER = "dashgate"
PASSWORD = "/etc/aera-gateway/dash-admin"      # root:dashgate 0640, one line: the hash
REQUEST = "/var/lib/dashgate/admin/request.json"
RESULTS = "/var/lib/dashgate-admin"            # root:dashgate 0750
RESULT = RESULTS + "/result.json"
INSTALLER = "/usr/local/bin/aera-gateway"      # update-pi.sh's link to the release's installer

# the services: key -> its unit
UNITS = {"mail": "emailgate", "dav": "ecalgate", "ftp": "eftpgate", "web": "webgate", "pdf": "pdfgate",
         "translate": "translategate", "radio": "radiogate", "dash": "dashgate", "claude": "claude-st"}
CHANGE = ("mail", "dav", "ftp", "web", "pdf", "translate", "radio", "dash")   # ClaudeST's bridge: its own recipe
LOCAL = ("dav", "ftp", "web", "radio")                  # those with --allow-local
VERBOSE = ("dav", "ftp", "web", "pdf", "translate", "radio", "dash")         # those with -v
LOG_LINES = 200
MAX_REQUEST = 8192
MAX_TEXT = 64 << 10
ITERATIONS = 600000


class Bad(Exception):
    """a request this doesn't take"""


# ---------------------------------------------------------------- the password

def hash_password(password, salt=None, iterations=ITERATIONS):
    salt = salt or os.urandom(16)
    h = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, iterations)
    return "pbkdf2_sha256$%d$%s$%s" % (iterations, salt.hex(), h.hex())


def check_password(password, stored):
    try:
        kind, n, salt, want = (stored or "").strip().split("$")
        if kind != "pbkdf2_sha256" or not 100000 <= int(n) <= 10000000:
            return False
        got = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), bytes.fromhex(salt), int(n))
        return hmac.compare_digest(got, bytes.fromhex(want))
    except (ValueError, TypeError):
        return False


# ---------------------------------------------------------------- the request

def allow_ok(a):
    if a == "any":
        return True
    try:
        ipaddress.ip_network(a, strict=False)
        return len(a) <= 49
    except ValueError:
        return False


def request(d):
    """the request d, checked: {id, do, key[, allow, allow_local, verbose]}"""
    if not isinstance(d, dict):
        raise Bad("not a request")
    rid, do, key = d.get("id"), d.get("do"), d.get("key")
    if not isinstance(rid, str) or not re.fullmatch(r"[0-9a-f]{8,64}", rid):
        raise Bad("no id")
    if key not in UNITS:
        raise Bad("no such service")
    out = {"id": rid, "do": do, "key": key}
    if do in ("log", "restart"):
        return out
    if do != "set" or key not in CHANGE:
        raise Bad("not something this does")
    allow = d.get("allow")
    if not isinstance(allow, list) or not 1 <= len(allow) <= 16 or not all(
            isinstance(a, str) and allow_ok(a) for a in allow) or ("any" in allow and len(allow) > 1):
        raise Bad("who may connect isn't a list of addresses (or 'any')")
    out["allow"] = allow
    for k, keys in (("allow_local", LOCAL), ("verbose", VERBOSE)):
        v = d.get(k)
        if v is not None and (not isinstance(v, bool) or key not in keys):
            raise Bad("%s isn't a setting of %s" % (k, key))
        if v is not None:
            out[k] = v
    return out


def command(req, installer=INSTALLER):
    """the one command a checked request runs"""
    key, unit = req["key"], UNITS[req["key"]] + ".service"
    if req["do"] == "log":
        return ["journalctl", "-u", unit, "-n", str(LOG_LINES), "--no-pager", "-q", "-o", "short-iso"]
    if req["do"] == "restart":
        return ["systemctl", "restart", unit]
    argv = [installer, "install", key, "--allow", ",".join(req["allow"])]
    if "allow_local" in req:
        argv.append("--allow-local" if req["allow_local"] else "--no-allow-local")
    if "verbose" in req:
        argv.append("-v" if req["verbose"] else "--no-verbose")
    return argv


def handle(req, run=subprocess.run, installer=INSTALLER):
    """(ok, text) of a checked request"""
    if req["do"] == "set" and not os.access(installer, os.X_OK):
        return False, "aera-gateway isn't at %s (update-pi.sh puts it there)" % installer
    try:
        r = run(command(req, installer), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, text=True, errors="replace", timeout=1800)
    except (OSError, subprocess.SubprocessError) as e:
        return False, "couldn't run it (%s)" % type(e).__name__
    text = (r.stdout or "")[-MAX_TEXT:]
    if req["do"] == "restart" and r.returncode == 0:
        text = text or "%s restarted." % UNITS[req["key"]]
    return r.returncode == 0, text


# ---------------------------------------------------------------- the files

def read_request(path=REQUEST, owner=USER):
    """the request's JSON, as dashgate's own file, then gone; None if none"""
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except FileNotFoundError:
        return None
    except OSError:
        raise Bad("the request isn't a file")
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_size > MAX_REQUEST:
            raise Bad("the request isn't one")
        if owner is not None and st.st_uid != pwd.getpwnam(owner).pw_uid:
            raise Bad("the request isn't dashgate's")
        data = os.read(fd, MAX_REQUEST + 1)
    finally:
        os.close(fd)
        try:
            os.unlink(path)
        except OSError:
            pass
    try:
        return json.loads(data.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        raise Bad("the request isn't JSON")


def write_result(result, path=RESULT, group=USER):
    """the result, whole, readable by root and dashgate's group only"""
    tmp = path + ".new"
    try:
        os.unlink(tmp)
    except FileNotFoundError:
        pass
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "w") as f:
        if group is not None:
            os.fchown(f.fileno(), 0, grp.getgrnam(group).gr_gid)
        os.fchmod(f.fileno(), 0o640)
        json.dump(result, f)
    os.replace(tmp, path)


def run_once(request_path=REQUEST, result_path=RESULT, owner=USER, run=subprocess.run, installer=INSTALLER):
    rid = None
    try:
        d = read_request(request_path, owner)
        if d is None:
            return 0
        rid = d.get("id") if isinstance(d, dict) and isinstance(d.get("id"), str) else None
        req = request(d)
        ok, text = handle(req, run, installer)
    except Bad as e:
        ok, text = False, "dashadmin: %s" % e
    write_result({"id": rid if rid and re.fullmatch(r"[0-9a-f]{8,64}", rid) else "", "ok": ok, "text": text},
                 result_path, owner)
    return 0


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv == ["run"]:
        return run_once()
    if argv == ["hash"]:                    # (the installer's: a password on stdin, its hash out)
        print(hash_password(sys.stdin.readline().rstrip("\n")))
        return 0
    print("usage: dashadmin.py run | hash", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
