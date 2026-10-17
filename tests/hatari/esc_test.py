#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Esc and Undo cancel EFTP's dialogs (the AERA guidelines, 3.5), in Hatari.

Each dialog is opened, something is typed into a text field, and Esc or
Undo closes it: the dialog must be gone (the window seen again), the
typing not kept in the field's place and nothing written: EFTP.INF is
the same, byte for byte, as before.

    esc_test.py WORKDIR EMUTOS.IMG     (Xvfb, xdotool, pip install pyftpdlib)
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig  # noqa: E402
import server  # noqa: E402
from rig import K  # noqa: E402

work, tos = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
os.makedirs(work, exist_ok=True)
srv, home = server.ftp_server(work)
hd = server.drive_c(work)
inf = os.path.join(hd, "EFTP.INF")
bridge, serial = server.bridge(work)
r = rig.Rig(work, tos, serial=serial, extra=["--memsize", "4"])
fails = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        fails.append(what)


UNDO, Z = 0x61, 0x2c    # scancodes


def pixels(path):
    from PIL import Image
    return Image.open(path).convert("RGB").tobytes()


def changed(a, b):
    """pixels that differ: an open dialog changes tens of thousands; after
    it closes, only light (dimmed) text redrawn with its pattern shifted"""
    return sum(1 for i in range(0, len(a), 3) if a[i:i + 3] != b[i:i + 3])


def closed(opened, after, plain):
    return changed(opened, plain) > 20000 and changed(after, plain) < 5000


def typed(n):
    for _ in range(n):
        r.key(Z)
        time.sleep(0.15)


try:
    time.sleep(30)                                  # boot, FAKESTNG, EFTP
    before = open(inf, "rb").read()
    r.move(600, 450)                                # the pointer out of the way, for
    time.sleep(1)                                   # screens to compare
    plain = pixels(r.shot("plain"))
    shots = []
    for key, name in ((K["ESC"], "Esc"), (UNDO, "Undo")):
        # the Site Manager: the cursor in the site's name; type, then cancel
        r.ctrl("k")
        time.sleep(2)
        typed(3)
        opened = pixels(r.shot("sites-typed-%s" % name))
        r.key(key)
        time.sleep(2)
        after = pixels(r.shot("sites-after-%s" % name))
        check(closed(opened, after, plain), "%s closes the Site Manager, the typing gone" % name)
        # Settings (Options > Settings), from the menu
        r.click(404, 9)
        time.sleep(1)
        r.click(424, 27)
        r.move(600, 450)
        time.sleep(2)
        opened = pixels(r.shot("settings-open-%s" % name))
        r.key(key)
        time.sleep(2)
        after = pixels(r.shot("settings-after-%s" % name))
        check(closed(opened, after, plain), "%s closes Settings" % name)
        shots += ["sites-after-%s" % name, "settings-after-%s" % name]
    time.sleep(2)
    after = open(inf, "rb").read()
    check(after == before, "Esc and Undo in the Site Manager and Settings: EFTP.INF unchanged")
    # still running and answering: the Site Manager opens again, and Esc
    # leaves it once more
    r.ctrl("k")
    time.sleep(2)
    r.shot("sites-again")
    r.key(K["ESC"])
    time.sleep(2)
    check(open(inf, "rb").read() == before, "a third time: EFTP.INF still unchanged")
finally:
    r.stop()
    bridge.terminate()
    srv.close_all()

print("screenshots in %s: %s" % (os.path.join(work, "shots"), ", ".join(shots)))
print("%d failed" % len(fails))
sys.exit(1 if fails else 0)
