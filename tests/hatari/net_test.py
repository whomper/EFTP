#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""End-to-end test of EFTP.PRG on an emulated Falcon against a real FTP
server (pyftpdlib, on this computer). Hatari has no network card, so
FAKESTNG.PRG in C:\\AUTO tunnels EFTP's connections (the control one and
each data one) over the emulated serial port to serial_bridge.py.

EFTP connects to a site from the Site Manager, opens a folder with a
double click, downloads a whole folder (8+3 names, dates kept), uploads
a folder by dragging it to the server's pane, answers "already there"
with Skip, stops a download half way and resumes it; the test checks
every byte on both sides. At the end a stand-in desktop (THING.ACC from
common/tests/av: make -C common/tests/av) hands EFTP a file with the AV
protocol's VA_START, which goes to the server folder shown.

    net_test.py WORKDIR EMUTOS.IMG     (Xvfb, xdotool, pip install pyftpdlib)
    EFTP_PRG=EFTPST.PRG net_test.py ...  the ST edition, on a 1 MB machine
"""
import os
import sys
import time

import rig
import server
from rig import K

work, tos = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
os.makedirs(work, exist_ok=True)
srv, home = server.ftp_server(work)
hd = server.drive_c(work)
# the stand-in desktop: it waits for EFTP to tell it it takes VA_START,
# then for C:\GO.TXT, then sends itself, C:\THING.ACC (binary: a .TXT
# would go as text, its line ends changed)
thing = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "common", "tests", "av",
                     "build", "THING.ACC")
av = os.path.exists(thing)
if av:
    import shutil
    shutil.copy(thing, os.path.join(hd, "THING.ACC"))
    with open(os.path.join(hd, "AVSEND.TXT"), "w", newline="") as f:
        f.write("until EFTP\r\nawait GO.TXT\r\nsend EFTP|C:\\THING.ACC\r\nwait 60\r\ndone\r\n")
else:
    print("skip VA_START: make -C common/tests/av first")
bridge, serial = server.bridge(work)
# EFTP.PRG (with TLS) on a 4 MB Falcon; EFTP_PRG=EFTPST.PRG, the ST
# edition, on 1 MB, the smallest machine it is for
r = rig.Rig(work, tos, serial=serial,
            extra=["--memsize", "1" if os.environ.get("EFTP_PRG") == "EFTPST.PRG" else "4"])
fails = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        fails.append(what)


def same(a, b):
    try:
        return open(a, "rb").read() == open(b, "rb").read()
    except OSError:
        return False


def wait_for(cond, secs):
    t = time.time()
    while time.time() - t < secs:
        if cond():
            return True
        time.sleep(1)
    return False


# where things are in the 640x480 window (ST font 8x16)
ROW0 = 131            # the first row of a file list
ROWH = 16
CONNECT, DOWNLOAD = (50, 73), (240, 73)  # toolbar buttons
REMOTE_X, LOCAL_X = 400, 60
SITE1 = (60, 92)      # the first site in Connect's menu


def row(n):
    return ROW0 + n * ROWH


try:
    time.sleep(30)                                  # boot, FAKESTNG, EFTP
    r.shot("t1-start")
    r.click(*CONNECT)                               # the sites in a menu
    time.sleep(2)
    r.shot("t1b-connect-menu")
    r.click(*SITE1)                                 # "Test server", /pub: one click
    time.sleep(15)
    r.shot("t2-connected")
    check("USER 'atari' logged in" in open(os.path.join(work, "ftpd.log")).read(), "logged in")

    r.click(REMOTE_X, row(1), double=True)          # /pub/atari
    time.sleep(6)
    r.click(REMOTE_X, row(1))                       # demos
    time.sleep(1.5)
    r.click(*DOWNLOAD)
    demos = os.path.join(home, "pub", "atari", "demos")
    got = os.path.join(hd, "DEMOS")
    check(wait_for(lambda: same(os.path.join(got, "SCREEN~2.PNG"), os.path.join(demos, "screenshot-002.png")), 90),
          "a folder downloaded")
    check(same(os.path.join(got, "CUDDLY~1.ST"), os.path.join(demos, "Cuddly Demos.st")), "8+3 name, every byte")
    check(time.localtime(os.path.getmtime(os.path.join(got, "CUDDLY~1.ST"))).tm_year ==
          time.localtime(os.path.getmtime(os.path.join(demos, "Cuddly Demos.st"))).tm_year, "the date kept")
    time.sleep(3)
    r.shot("t3-downloaded")

    r.click(LOCAL_X, row(3))                        # C:\GAMES
    time.sleep(1.5)
    r.drag(LOCAL_X, row(3), 450, 260)               # dropped on the server's side
    up = os.path.join(home, "pub", "atari", "games", "xenon2.prg")
    check(wait_for(lambda: same(up, os.path.join(hd, "GAMES", "XENON2.PRG")), 90), "a folder uploaded by dragging")
    time.sleep(3)
    r.shot("t4-uploaded")

    r.click(REMOTE_X, row(2))                       # games, again: xenon2.prg is there now
    time.sleep(1.5)
    r.click(*DOWNLOAD)
    time.sleep(12)
    r.shot("t5-exists")
    r.click(200, 297)                               # Skip
    oids = os.path.join(hd, "GAMES", "OIDS.ZIP")
    check(wait_for(lambda: same(oids, os.path.join(home, "pub", "atari", "games", "oids.zip")), 60),
          "the rest came after Skip")
    time.sleep(4)                                   # oids.zip came first (it sorts first): let the
                                                    # batch end and the panes be read again

    r.click(REMOTE_X, row(4))                       # STinG 1.26.zip, 244 KB
    time.sleep(1.5)
    r.click(*DOWNLOAD)
    time.sleep(9)
    r.key(K["ESC"])                                 # Stop, and Esc again as a hurried
    time.sleep(0.3)                                 # hand (or a repeating key) does: the
    r.key(K["ESC"])                                 # second mustn't stop what follows
    time.sleep(5)
    part = os.path.join(hd, "STING_~1.ZIP")
    size = os.path.getsize(part) if os.path.exists(part) else -1
    check(0 < size < 250000, "stopped half way (%d bytes)" % size)
    r.shot("t6-stopped")
    r.click(300, 9)                                 # Transfers > Retry the failed ones
    time.sleep(1)
    r.click(320, 27)
    check(wait_for(lambda: same(part, os.path.join(home, "pub", "atari", "STinG 1.26.zip")), 90),
          "resumed and complete")
    check("REST %d" % size in open(os.path.join(hd, "EFTP.LOG"), errors="replace").read(), "resumed with REST")
    time.sleep(3)
    r.shot("t7-done")

    if av:
        # VA_START: a file from the desktop goes to the server folder shown
        open(os.path.join(hd, "GO.TXT"), "w").write("go")
        atari = os.path.join(home, "pub", "atari")

        def thing():
            for n in os.listdir(atari):
                if n.lower() == "thing.acc":
                    return os.path.join(atari, n)
            return ""
        check(wait_for(lambda: same(thing(), os.path.join(hd, "THING.ACC")), 60),
              "a file handed over with VA_START is uploaded, every byte")
        r.shot("t8-va-start")
        avlog = open(os.path.join(hd, "AVLOG.TXT"), errors="replace").read()
        check("AV_PROTOKOLL bits 0016 name EFTP" in avlog, "EFTP told the desktop it takes VA_START")
        check("AV_STARTED" in avlog, "and answered it")
finally:
    r.stop()
    bridge.terminate()
    srv.close_all()

print("%d failed" % len(fails))
sys.exit(1 if fails else 0)
