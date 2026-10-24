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

    net_test.py WORKDIR [MACHINE]     (Xvfb, xdotool, pip install pyftpdlib)

MACHINE is one of the family's test machines (common/tools/machines.py
list), the Falcon by default; every one runs EFTP.PRG. On the TT and the Falcon the downloads gather in TT
RAM, on the STs in ST RAM.
"""
import os
import sys
import time

import rig
import server
from rig import K

work = os.path.abspath(sys.argv[1])
os.makedirs(work, exist_ok=True)
m = rig.machine(sys.argv[2] if len(sys.argv) > 2 else "Falcon", work)
print("on %s, %s" % (m.describe(), m.prg), flush=True)
srv, home = server.ftp_server(work)
hd = server.drive_c(work, prg=m.prg)
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
r = rig.Rig(work, m, extra=m.network())      # FAKESTNG and its bridge
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


def wait_for(cond, secs, slow=True):
    t = time.time()
    while time.time() - t < secs * (SLOW if slow else 1):
        if cond():
            return True
        time.sleep(1)
    return False


# where things are in the window (ST font 8x16), the same on a 640x480
# screen and an ST's 640x400 mono one; dialogs are centred, so higher
if m.screen not in ("mono", "ttmed", "16", "256"):
    sys.exit("net_test.py: %s's %s screen isn't one this test knows (mono, ttmed, 16, 256)"
             % (m.name, m.screen))
HIGH = 480 - (400 if m.screen == "mono" else 480)   # dialogs this much higher
# an ST's serial port, which FAKESTNG carries the network over, runs at
# 19200 baud, about 2 KB/s: the network's waits are that much longer
SLOW = 8 if m.name in ("ST", "ST-1") else 1     # for the longest waits
PAUSE = 3 if m.name in ("ST", "ST-1") else 1    # for the short ones
ROW0 = 131            # the first row of a file list
ROWH = 16
CONNECT, DOWNLOAD = (50, 73), (240, 73)  # toolbar buttons
REMOTE_X, LOCAL_X = 400, 60
SITE1 = (60, 92)      # the first site in Connect's menu


def row(n):
    return ROW0 + n * ROWH


def stamp(line):
    h, mi, sec = line[:11].split(":")
    return int(h) * 3600 + int(mi) * 60 + float(sec)


def speed(log, name, size):
    """bytes a second this Atari took name down at, by EFTP.LOG (RETR to 226)"""
    lines = log.splitlines()
    i = max(k for k, l in enumerate(lines) if "RETR " + name in l)
    j = next(k for k in range(i, len(lines)) if " 226 " in lines[k])
    return size / max(stamp(lines[j]) - stamp(lines[i]), 0.5)


try:
    time.sleep(30)                                  # boot, FAKESTNG, EFTP
    r.shot("t1-start")
    r.click(*CONNECT)                               # the sites in a menu
    time.sleep(2)
    r.shot("t1b-connect-menu")
    r.click(*SITE1)                                 # "Test server", /pub: one click
    elog = os.path.join(hd, "EFTP.LOG")
    wait_for(lambda: os.path.exists(elog) and "CWD /pub" in open(elog, errors="replace").read(), 30)
    time.sleep(6)
    r.shot("t2-connected")
    check("USER 'atari' logged in" in open(os.path.join(work, "ftpd.log")).read(), "logged in")

    # /pub/atari; once more if the double click went by as one (it did
    # once on ST-1 under TOS 1.04, and the whole folder came down)
    for _ in range(2):
        r.click(REMOTE_X, row(1), double=True)
        if wait_for(lambda: "CWD atari" in open(elog, errors="replace").read(), 6 * PAUSE, slow=False):
            break
    time.sleep(3 * PAUSE)
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
    oids = os.path.join(hd, "GAMES", "OIDS.ZIP")    # oids.zip comes first (it sorts first),
    check(wait_for(lambda: same(oids, os.path.join(home, "pub", "atari", "games", "oids.zip")), 60),
          "the folder's new file came")             # then xenon2.prg asks
    time.sleep(6)
    r.shot("t5-exists")
    r.click(200, 297 - HIGH // 2)                   # Skip
    time.sleep(4 * PAUSE)                            # the batch ends, the panes are read again
    check("RETR /pub/atari/games/xenon2.prg" not in open(elog, errors="replace").read(),
          "Skip: the file that was there didn't come again")

    # stopped part way: EFTP makes the file as the download starts, and
    # the stop comes when about 40% has come at the speed this machine
    # took Cuddly Demos.st down at (a fixed wait would be too short on an
    # ST, or come after the end on a fast one)
    bps = speed(open(elog, errors="replace").read(), "/pub/atari/demos/Cuddly Demos.st",
                os.path.getsize(os.path.join(demos, "Cuddly Demos.st")))
    sting = os.path.join(home, "pub", "atari", "STinG 1.26.zip")
    print("downloads at %.1f KB/s" % (bps / 1024), flush=True)
    part = os.path.join(hd, "STING_~1.ZIP")
    r.click(REMOTE_X, row(4))                       # STinG 1.26.zip, 244 KB
    time.sleep(1.5)
    r.click(*DOWNLOAD)
    wait_for(lambda: os.path.exists(part), 60)
    time.sleep(0.4 * os.path.getsize(sting) / bps)
    r.key(K["ESC"])                                 # Stop, and Esc again as a hurried
    time.sleep(0.3)                                 # hand (or a repeating key) does: the
    r.key(K["ESC"])                                 # second mustn't stop what follows
    time.sleep(5)
    size = os.path.getsize(part) if os.path.exists(part) else -1
    check(0 < size < 250000, "stopped half way (%d bytes)" % size)
    r.shot("t6-stopped")
    r.click(300, 9)                                 # Transfers > Retry the failed ones
    time.sleep(1)
    r.click(320, 27)
    check(wait_for(lambda: same(part, sting), 60 + 2 * os.path.getsize(sting) / bps, slow=False),
          "resumed and complete")
    log = open(os.path.join(hd, "EFTP.LOG"), errors="replace").read()
    check("REST %d" % size in log, "resumed with REST")
    # the downloads gathered in memory and went to disk in one go (a 1 MB
    # ST may have too little to spare: then straight to disk, as before)
    ram = "TT RAM" if m.name in ("TT", "Falcon") else "ST RAM"
    if m.name != "ST-1" or "the file gathers in" in log:
        check("the file gathers in " + ram in log, "downloads gathered in " + ram)
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
    m.stop()
    srv.close_all()

print("%d failed" % len(fails))
sys.exit(1 if fails else 0)
