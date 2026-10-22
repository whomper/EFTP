#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Text fields that hold more than they show (the AERA guidelines, 5.4:
Erez, 22 Oct 2026; common/ui/field.c), typed into with the keyboard alone:

  1. the Site Manager (^K), the first site: a user name longer than its
     field shows (28), a password of 60 (it shows 22), a server folder
     of 52 (23), finished after ClrHome, Delete, typing at the start and
     Shift+Right, then Backspace;
     ^C there, then ^V and ^X in the Atari folder (18) take and give the
     whole text; Connect
  2. EFTP.INF has the whole user and folder, the server took the whole
     user and password, and the folder opened is the whole one; the
     clipboard has the whole folder
  3. Rename (^E) on the server's file: the cursor moved left in it and
     a name typed in that ends up 54 long (the field shows 40); the
     server has the whole name

    field_test.py WORKDIR [MACHINE]     (Xvfb, xdotool, pip install pyftpdlib)

MACHINE: Falcon (the default, EFTP.PRG) or ST-1 (EFTPST.PRG), on Erez's
TOS when ~/.aera/tos has it (common/tools/machines.py). Screenshots in
WORKDIR/shots.
"""
import os
import sys
import time

import rig
import server
from rig import K

D1, D2 = "a-folder-with-a-long-name", "and-another-one-in-it"
FOLDER = "/pub/%s/%s" % (D1, D2)                        # 52: the field shows 23
USER = "a-user-name-longer-than-its-field"              # 33: it shows 28
PASS = "Pw-0123456789-abcdefghij-0123456789-ABCDEFGHIJ-0123456789xyz"  # 60: it shows 22
TAIL = "-renamed-to-a-name-longer-than-the-field-shows"
NEW = "file" + TAIL + ".txt"                            # 54: the field shows 40
assert len(PASS) == 60 and len(FOLDER) == 52

server.FILES["pub/%s/%s/file.txt" % (D1, D2)] = b"a file to rename\n"
work = os.path.abspath(sys.argv[1])
os.makedirs(work, exist_ok=True)
m = rig.machine(sys.argv[2] if len(sys.argv) > 2 else "Falcon", work)
print("on %s, %s" % (m.describe(), m.prg), flush=True)
srv, home = server.ftp_server(work)
srv.handler.authorizer.add_user(USER, PASS, home, perm="elradfmwMT")
hd = server.drive_c(work, prg=m.prg)
SLOW = 3 if m.name in ("ST", "ST-1") else 1
elog, flog, inf = os.path.join(hd, "EFTP.LOG"), os.path.join(work, "ftpd.log"), os.path.join(hd, "EFTP.INF")
fails = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        fails.append(what)


def read(path):
    try:
        return open(path, errors="replace").read()
    except OSError:
        return ""


def wait_for(cond, secs):
    t = time.time()
    while time.time() - t < secs * SLOW:
        if cond():
            return True
        time.sleep(1)
    return False


def setting(name):
    """the first site's NAME= from EFTP.INF"""
    part = read(inf).split("\n[SITE]")             # not the notes' "one [SITE] part"
    for line in (part[1] if len(part) > 1 else "").splitlines():
        if line.upper().startswith(name + "="):
            return line[len(name) + 1:]
    return None


def scrap():
    for d, _, files in os.walk(hd):
        for f in files:
            if f.upper() == "SCRAP.TXT":
                return read(os.path.join(d, f)).strip("\r\n")
    return None


def keys(name, n=1, **kw):
    for _ in range(n):
        r.key(K[name], **kw)
        time.sleep(0.1)


r = rig.Rig(work, m, extra=m.network())
try:
    time.sleep(30)                                  # boot, FAKESTNG, EFTP
    r.shot("1-start")
    r.ctrl("k")                                     # the Site Manager, the cursor in Name
    time.sleep(3 * SLOW)
    keys("TAB", 3)                                  # Server, Port, User
    keys("BACKSPACE", len("atari"))
    r.type(USER)
    keys("TAB")                                     # Password
    keys("BACKSPACE", len("falcon"))
    r.type(PASS)
    keys("TAB")                                     # Server folder: /pub
    r.type("/%s/%s" % (D1, D2[:-2]))                # its "it" comes last
    keys("HOME")                                    # ClrHome: to the start
    keys("DELETE", 4)                               # "/pub" gone...
    r.type("/pub")                                  # ...and typed in again at the start
    keys("RIGHT", shift=True)                       # to the end (TOS 1.04: the key's '6' says Shift)
    r.type("itQ")
    keys("BACKSPACE")
    r.ctrl("c")                                     # the whole folder to the clipboard
    time.sleep(1)
    keys("TAB")                                     # Atari folder, empty
    r.ctrl("v")                                     # the whole folder in...
    time.sleep(1)
    r.shot("2-pasted")
    r.ctrl("x")                                     # ...and out again: it stays empty
    time.sleep(1)
    r.shot("3-site")
    keys("RETURN")                                  # Connect (it saves the sites first)
    check(wait_for(lambda: "CWD " + FOLDER in read(elog), 40), "the whole folder opened (CWD %s)" % FOLDER)
    time.sleep(4 * SLOW)
    r.shot("4-connected")
    check("USER '%s' logged in" % USER in read(flog), "the server took the whole user (%d) and password (%d)"
          % (len(USER), len(PASS)))
    check(setting("USER") == USER, "EFTP.INF: USER= the whole user (%r)" % setting("USER"))
    check(setting("REMOTEDIR") == FOLDER, "EFTP.INF: REMOTEDIR= the whole folder (%r)" % setting("REMOTEDIR"))
    check(setting("LOCALDIR") == "", "EFTP.INF: LOCALDIR= empty after ^V ^X (%r)" % setting("LOCALDIR"))
    check(scrap() == FOLDER, "the clipboard has the whole folder (%r)" % scrap())

    # Rename on the server: the cursor before ".txt", a long piece typed in
    keys("TAB")                                     # the server's pane
    time.sleep(1)
    r.type("f")                                     # to file.txt
    time.sleep(1)
    r.ctrl("e")
    time.sleep(3 * SLOW)
    keys("LEFT", 4)
    r.type(TAIL)
    r.shot("5-rename")
    keys("RETURN")
    there = os.path.join(home, "pub", D1, D2)
    check(wait_for(lambda: NEW in os.listdir(there), 20), "renamed to the whole name, %d long (%s)"
          % (len(NEW), ", ".join(os.listdir(there))))
    time.sleep(2 * SLOW)
    r.shot("6-renamed")
    r.ctrl("q")
    time.sleep(3)
finally:
    r.stop()
    m.stop()
    srv.close_all()

print("screenshots in", os.path.join(work, "shots"))
print("field_test:", "all passed" if not fails else "%d failed" % len(fails))
sys.exit(1 if fails else 0)
