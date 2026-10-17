#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EFTP's FTP core against a real FTP server (pyftpdlib), through
build/ftp-cli: logins, listings (MLSD and LIST), downloads and uploads
of files and whole folders, resuming, text mode, UTF-8 names, the
folder commands. make itest (pip install pyftpdlib)
"""
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(ROOT, "build", "ftp-cli")
fails = 0
checks = 0


def check(cond, what):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print("FAIL:", what)


def cli(port, *args, user="atari", pw="falcon", env=None, ok=True):
    e = dict(os.environ)
    e.update(env or {})
    r = subprocess.run([CLI, "127.0.0.1", str(port), user, pw] + list(args),
                       capture_output=True, text=True, env=e, cwd=work, timeout=60)
    if ok and r.returncode != 0:
        print("ftp-cli", " ".join(args), "failed:", r.stdout, r.stderr)
    return r


def server(home, mlsd=True):
    auth = DummyAuthorizer()
    auth.add_user("atari", "falcon", home, perm="elradfmwMT")
    auth.add_anonymous(home)

    class H(FTPHandler):
        pass
    H.authorizer = auth
    H.passive_ports = range(30000, 30100)
    if not mlsd:
        H.proto_cmds = {k: v for k, v in FTPHandler.proto_cmds.items() if k not in ("MLSD", "MLST")}
    s = FTPServer(("127.0.0.1", 0), H)
    t = threading.Thread(target=s.serve_forever, kwargs={"timeout": 0.2}, daemon=True)
    t.start()
    return s, s.socket.getsockname()[1]


def main():
    global work
    tmp = tempfile.mkdtemp(prefix="eftp-it-")
    home = os.path.join(tmp, "home")
    work = os.path.join(tmp, "atari")
    os.makedirs(os.path.join(home, "pub", "demos", "deep"))
    os.makedirs(work)
    big = os.urandom(300000)
    with open(os.path.join(home, "pub", "BIG.DAT"), "wb") as f:
        f.write(big)
    with open(os.path.join(home, "pub", "demos", "screenshot-001.png"), "wb") as f:
        f.write(b"one")
    with open(os.path.join(home, "pub", "demos", "screenshot-002.png"), "wb") as f:
        f.write(b"two")
    with open(os.path.join(home, "pub", "demos", "deep", "readme.txt"), "wb") as f:
        f.write(b"line one\nline two\n")
    with open(os.path.join(home, "pub", "שלום.txt"), "wb") as f:
        f.write(b"hebrew")
    os.utime(os.path.join(home, "pub", "BIG.DAT"), (1700000000, 1700000000))

    for mlsd in (True, False):
        srv, port = server(home, mlsd)
        tag = "mlsd" if mlsd else "list"
        # listing
        r = cli(port, "ls", "/pub")
        check(r.returncode == 0, tag + " ls")
        check("d " in r.stdout and " demos" in r.stdout, tag + " ls shows the folder")
        check("300000" in r.stdout and "BIG.DAT" in r.stdout, tag + " ls shows the size")
        check("2023-11-14" in r.stdout, tag + " ls shows the date: " + r.stdout)
        # anonymous
        r = cli(port, "ls", "/pub", user="anonymous", pw="")
        check(r.returncode == 0 and "BIG.DAT" in r.stdout, tag + " anonymous")
        # wrong password
        r = cli(port, "ls", user="atari", pw="wrong", ok=False)
        check(r.returncode != 0 and "log in" in r.stderr, tag + " wrong password: " + r.stderr)
        srv.close_all()
    srv, port = server(home)

    # a file down, with its date kept
    r = cli(port, "get", "/pub/BIG.DAT", ".")
    got = os.path.join(work, "BIG.DAT")
    check(r.returncode == 0 and open(got, "rb").read() == big, "get BIG.DAT")
    check(abs(os.path.getmtime(got) - 1700000000) < 120, "the date kept: %s" % os.path.getmtime(got))

    # stopped half way, then resumed
    os.remove(got)
    r = cli(port, "get", "/pub/BIG.DAT", ".", env={"STOPAT": "100000"}, ok=False)
    check(r.returncode != 0 and "stop" in r.stdout, "stopped: " + r.stdout)
    have = os.path.getsize(got)
    check(100000 <= have < 300000 and open(got, "rb").read() == big[:have], "part there: %d" % have)
    r = cli(port, "get", "/pub/BIG.DAT", ".", env={"EXISTS": "2"})
    check(r.returncode == 0 and open(got, "rb").read() == big, "resumed: " + r.stdout)
    # the memory a download gathers in (pf_bigbuf): smaller than the file,
    # so full several times over, and none at all; each stopped half way,
    # what came is on the disk, and resumed
    for buf in ("10000", "0"):
        os.remove(got)
        r = cli(port, "get", "/pub/BIG.DAT", ".", env={"EFTP_BIGBUF": buf})
        check(r.returncode == 0 and open(got, "rb").read() == big, "get BIG.DAT, buffer %s" % buf)
        os.remove(got)
        r = cli(port, "get", "/pub/BIG.DAT", ".", env={"STOPAT": "100000", "EFTP_BIGBUF": buf}, ok=False)
        have = os.path.getsize(got)
        check(r.returncode != 0 and have >= 100000 and open(got, "rb").read() == big[:have],
              "buffer %s: stopped, the %d bytes that came are there" % (buf, have))
        r = cli(port, "get", "/pub/BIG.DAT", ".", env={"EXISTS": "2", "EFTP_BIGBUF": buf})
        check(r.returncode == 0 and open(got, "rb").read() == big, "buffer %s: resumed" % buf)
    # skip when there
    r = cli(port, "get", "/pub/BIG.DAT", ".", env={"EXISTS": "4"})
    check("skip" in r.stdout, "skipped when there")
    # asked, and stopped (Esc in the question): the run ends, the file as it was
    stamp = os.path.getmtime(got)
    r = cli(port, "get", "/pub/BIG.DAT", ".", env={"EXISTS": "0", "ANSWER": "5"}, ok=False)
    check(r.returncode != 0 and open(got, "rb").read() == big and os.path.getmtime(got) == stamp,
          "stopped at the question, the file untouched: " + r.stdout)

    # a whole folder down: 8+3 names, nested folders, text
    r = cli(port, "get", "/pub/demos", ".")
    check(r.returncode == 0, "get folder: " + r.stdout + r.stderr)
    check("files at start: 3" in r.stdout, "every file listed before the first: " + r.stdout)
    check(open(os.path.join(work, "DEMOS", "SCREEN~1.PNG"), "rb").read() == b"one", "8+3 name 1")
    check(open(os.path.join(work, "DEMOS", "SCREEN~2.PNG"), "rb").read() == b"two", "8+3 name 2")
    txt = open(os.path.join(work, "DEMOS", "DEEP", "README.TXT"), "rb").read()
    check(txt == b"line one\r\nline two\r\n", "text came with CR LF: %r" % txt)
    # long names kept
    r = cli(port, "get", "/pub/demos", ".", env={"SHORTEN": "0"})
    check(os.path.exists(os.path.join(work, "demos", "screenshot-001.png")), "long names kept")

    # UTF-8 name
    r = cli(port, "get", "/pub/שלום.txt", ".", env={"SHORTEN": "0"})
    check(r.returncode == 0 and os.path.exists(os.path.join(work, "\xd6\xcd\xc7\xda.txt".encode("latin-1").decode("latin-1"))) or
          any(n.endswith(".txt") and len(n) == 8 for n in os.listdir(work)), "hebrew name down")

    # up: a file (lower-cased name, text with bare LF made CR LF) and a folder
    up = os.path.join(work, "UPME")
    os.makedirs(os.path.join(up, "SUB"))
    with open(os.path.join(up, "NOTES.TXT"), "wb") as f:
        f.write(b"atari\r\nunix\n")
    with open(os.path.join(up, "SUB", "GAME.PRG"), "wb") as f:
        f.write(bytes(range(256)) * 10)
    r = cli(port, "put", os.path.join(up, "NOTES.TXT"), "/pub")
    data = open(os.path.join(home, "pub", "notes.txt"), "rb").read()
    check(r.returncode == 0 and data == b"atari\nunix\n", "put text: %r" % data)
    r = cli(port, "put", up, "/pub")
    check(r.returncode == 0, "put folder: " + r.stdout + r.stderr)
    check("files at start: 2" in r.stdout, "every file listed before the first, up: " + r.stdout)
    check(open(os.path.join(home, "pub", "upme", "sub", "game.prg"), "rb").read() == bytes(range(256)) * 10,
          "put folder contents")
    # upload resume
    part = os.path.join(home, "pub", "BIGUP.DAT")
    with open(os.path.join(work, "BIGUP.DAT"), "wb") as f:
        f.write(big)
    with open(part, "wb") as f:
        f.write(big[:120000])
    r = cli(port, "put", os.path.join(work, "BIGUP.DAT"), "/pub", env={"EXISTS": "2", "LOWER": "0"})
    check(r.returncode == 0 and open(part, "rb").read() == big, "upload resumed: " + r.stdout)

    # folder commands
    check(cli(port, "mkdir", "/pub/newdir").returncode == 0, "mkdir")
    check(os.path.isdir(os.path.join(home, "pub", "newdir")), "mkdir made it")
    check(cli(port, "mv", "/pub/newdir", "/pub/renamed").returncode == 0, "rename")
    check(cli(port, "rmdir", "/pub/renamed").returncode == 0, "rmdir")
    check(cli(port, "rm", "/pub/notes.txt").returncode == 0, "delete")
    check(not os.path.exists(os.path.join(home, "pub", "notes.txt")), "deleted")
    r = cli(port, "rm", "/pub/nothere.txt", ok=False)
    check(r.returncode != 0 and "550" not in r.stderr and "delete" in r.stderr, "delete error: " + r.stderr)

    srv.close_all()
    shutil.rmtree(tmp)
    print("%d checks, %d failed" % (checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
