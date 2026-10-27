#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Esc and Undo in EFTP's dialogs (the AERA guidelines, 3.5, GEM's way
since 27 Oct 2026), in Hatari.

Each dialog is opened and something typed into the text field the cursor
is in. Esc empties that field and the dialog stays open; Undo closes it:
the dialog gone (the window seen again) and nothing written, EFTP.INF the
same, byte for byte, as before. An alert, which has no field, closes on
Esc and Undo alike.

    esc_test.py WORKDIR [MACHINE]     (Xvfb, xdotool, pip install pyftpdlib)

MACHINE: Falcon by default, or another with a 640x480 screen (Falcon-4,
TT; common/tools/machines.py list), on Erez's TOS when it is in
~/.aera/tos.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig  # noqa: E402
import server  # noqa: E402
from rig import K  # noqa: E402

work = os.path.abspath(sys.argv[1])
os.makedirs(work, exist_ok=True)
m = rig.machine(sys.argv[2] if len(sys.argv) > 2 else "Falcon", work)
if m.screen not in ("16", "256", "ttmed"):
    sys.exit("esc_test.py: a 640x480 screen (Falcon, Falcon-4, TT): its clicks are placed for one")
print("on %s, %s" % (m.describe(), m.prg), flush=True)
srv, home = server.ftp_server(work)
hd = server.drive_c(work, prg=m.prg)
inf = os.path.join(hd, "EFTP.INF")
r = rig.Rig(work, m, extra=m.network())      # FAKESTNG and its bridge
fails = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        fails.append(what)


UNDO, Z = 0x61, 0x2c    # scancodes
ROW0, ROWH, REMOTE_X = 131, 16, 400     # as net_test.py


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
    # the Site Manager: the cursor in the site's name. Esc empties what
    # was typed and the dialog stays; Undo closes it
    r.ctrl("k")
    time.sleep(2)
    empty = pixels(r.shot("sites-open"))
    typed(3)
    opened = pixels(r.shot("sites-typed"))
    r.key(K["ESC"])
    time.sleep(2)
    esc = pixels(r.shot("sites-esc"))
    check(changed(esc, plain) > 20000 and changed(esc, opened) > 50 and changed(esc, empty) > 50,
          "Esc in the Site Manager's field: the field emptied, the dialog still open")
    typed(2)
    r.key(UNDO)
    time.sleep(2)
    after = pixels(r.shot("sites-after-Undo"))
    check(closed(opened, after, plain), "Undo closes the Site Manager, the typing gone")
    # Settings: from the menu (Options > Settings); Esc empties the field
    # with the cursor and stays, Undo closes
    r.click(404, 9)
    time.sleep(1)
    r.click(424, 27)
    r.move(600, 450)
    time.sleep(2)
    opened = pixels(r.shot("settings-open"))
    r.key(K["ESC"])
    time.sleep(2)
    esc = pixels(r.shot("settings-esc"))
    check(changed(esc, plain) > 20000, "Esc in Settings' field: Settings still open")
    r.key(UNDO)
    time.sleep(2)
    after = pixels(r.shot("settings-after-Undo"))
    check(closed(opened, after, plain), "Undo closes Settings")
    # by ^, the second time, and the window's way out: Undo again
    r.key(0x33, ctrl=True)
    r.move(600, 450)
    time.sleep(2)
    opened = pixels(r.shot("settings-open-2"))
    r.key(UNDO)
    time.sleep(2)
    check(closed(opened, pixels(r.shot("settings-after-2")), plain), "^, then Undo: Settings open and closed")
    time.sleep(2)
    after = open(inf, "rb").read()
    check(after == before, "Esc and Undo in the Site Manager and Settings: EFTP.INF unchanged")
    # still running and answering: the Site Manager opens again, and Undo
    # leaves it once more
    r.ctrl("k")
    time.sleep(2)
    r.shot("sites-again")
    r.key(UNDO)
    time.sleep(2)
    check(open(inf, "rb").read() == before, "a third time: EFTP.INF still unchanged")

    # an alert (the shared alert(), its own box now): Delete on a file on
    # the left asks [Delete|Cancel]; Esc and Undo are Cancel
    notes = os.path.join(hd, "NOTES.TXT")
    for key, name in ((K["ESC"], "Esc"), (UNDO, "Undo")):
        r.click(60, ROW0 + 7 * ROWH)                # NOTES.TXT
        time.sleep(1.5)
        r.key(K["DELETE"])
        r.move(600, 450)
        time.sleep(2)
        opened = pixels(r.shot("delete-alert-%s" % name))
        r.key(key)
        time.sleep(2)
        after = pixels(r.shot("delete-after-%s" % name))
        check(changed(opened, after) > 2000 and os.path.exists(notes),
              "%s in the Delete alert: closed, NOTES.TXT still there" % name)

    # "already there" while a transfer runs: Esc stops it (3.5, 7.1); the
    # file stays as it was and nothing more comes down
    r.ctrl("k")
    time.sleep(2)
    r.key(K["RETURN"])                              # Connect: "Test server", /pub
    time.sleep(15)
    r.click(REMOTE_X, ROW0 + ROWH)                  # /pub/atari
    time.sleep(1.5)
    r.key(K["RETURN"])
    time.sleep(6)
    got = os.path.join(hd, "STING_~1.ZIP")
    src = os.path.join(home, "pub", "atari", "STinG 1.26.zip")
    r.click(REMOTE_X, ROW0 + 4 * ROWH)              # STinG 1.26.zip
    time.sleep(1.5)
    r.key(K["RETURN"])                              # down, the first time
    t = time.time()
    while time.time() - t < 90 and not (os.path.exists(got) and os.path.getsize(got) == os.path.getsize(src)):
        time.sleep(1)
    time.sleep(4)
    first = open(got, "rb").read() if os.path.exists(got) else b""
    stamp = os.path.getmtime(got) if os.path.exists(got) else 0
    r.click(REMOTE_X, ROW0 + 4 * ROWH)
    time.sleep(1.5)
    r.key(K["RETURN"])                              # again: already there
    r.move(600, 450)
    time.sleep(6)
    r.shot("exists-asked")
    r.key(K["ESC"])
    time.sleep(5)
    r.shot("exists-stopped")
    r.ctrl("d")                                     # the log written out
    time.sleep(5)
    log = open(os.path.join(hd, "EFTP.LOG"), errors="replace").read()
    check(first and open(got, "rb").read() == first and os.path.getmtime(got) == stamp,
          "Esc in 'already there': the file as it was")
    check(log.count("RETR /pub/atari/STinG") == 1, "and it didn't come down again")
finally:
    r.stop()
    m.stop()
    srv.close_all()

print("screenshots in %s: %s" % (os.path.join(work, "shots"), ", ".join(shots)))
print("%d failed" % len(fails))
sys.exit(1 if fails else 0)
