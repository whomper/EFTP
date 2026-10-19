#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EFTP's clock on an Atari whose 200 Hz counter runs fast, in Hatari.

On Erez's Falcon a transfer of 16 real seconds showed as 25: TOS's 200 Hz
counter (_hz_200), which everything EFTP times counts, ran about 1.56
times too fast. EFTP measures it against MFP timer C's count-down at
start-up and corrects its times (atari/plat_tos.c, pf_ms). Here a file
C:\\FASTCLK makes FAKESTNG set timer C to 312 Hz, as on that Falcon; one
download's time in EFTP.LOG is then compared with the time this computer
saw it take (Hatari runs in real time), and once more on a 200 Hz clock.

    clock_test.py WORKDIR [MACHINE]     (Xvfb, xdotool, pip install pyftpdlib)

MACHINE: Falcon by default, or another with a 640x480 screen (Falcon-4,
TT; common/tools/machines.py list), on Erez's TOS when it is in
~/.aera/tos.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig  # noqa: E402
import server  # noqa: E402
from rig import K  # noqa: E402

work = os.path.abspath(sys.argv[1])
spec = sys.argv[2] if len(sys.argv) > 2 else "Falcon"
fails = []
ROW0, ROWH, REMOTE_X = 131, 16, 400     # as net_test.py


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        fails.append(what)


def stamp(line):
    h, m, s = line[:11].split(":")
    return int(h) * 3600 + int(m) * 60 + float(s)


def run(fast):
    tag = "312 Hz" if fast else "200 Hz"
    w = os.path.join(work, "fast" if fast else "true")
    os.makedirs(w, exist_ok=True)
    m = rig.machine(spec, w)
    if m.screen not in ("16", "256", "ttmed"):
        sys.exit("clock_test.py: a 640x480 screen (Falcon, Falcon-4, TT): its clicks are placed for one")
    print("%s: on %s, %s" % (tag, m.describe(), m.prg), flush=True)
    srv, home = server.ftp_server(w)
    hd = server.drive_c(w, prg=m.prg)
    if fast:
        open(os.path.join(hd, "FASTCLK"), "w").close()
    r = rig.Rig(w, m, extra=m.network())      # FAKESTNG and its bridge
    got = os.path.join(hd, "STING_~1.ZIP")
    want = os.path.getsize(os.path.join(home, "pub", "atari", "STinG 1.26.zip"))
    try:
        time.sleep(30)                                  # boot, FAKESTNG, EFTP
        r.ctrl("k")                                     # Site Manager
        time.sleep(2)
        r.key(K["RETURN"])                              # Connect: "Test server", /pub
        time.sleep(15)
        # a click and Return, not a double click: the AES times double
        # clicks with the same timer, so at 312 Hz they are too short
        r.click(REMOTE_X, ROW0 + ROWH)                  # /pub/atari
        time.sleep(1.5)
        r.key(K["RETURN"])
        time.sleep(6)
        r.click(REMOTE_X, ROW0 + 4 * ROWH)              # STinG 1.26.zip, 244 KB: down
        time.sleep(1.5)
        r.key(K["RETURN"])
        t0 = t1 = None
        t = time.time()
        while time.time() - t < 120:
            if os.path.exists(got):
                t0 = t0 or time.time()
                if os.path.getsize(got) == want:
                    t1 = time.time()
                    break
            time.sleep(0.1)
        time.sleep(3)
        r.shot("clock-%s" % ("fast" if fast else "true"))
        r.ctrl("d")                                     # disconnect: the log is written out
        time.sleep(5)
    finally:
        r.stop()
        m.stop()
        srv.close_all()
    check(t1 is not None, "%s: the file came down" % tag)
    log = open(os.path.join(hd, "EFTP.LOG"), errors="replace").read().splitlines()
    note = [l for l in log if "200 Hz clock" in l]
    if fast:
        m = re.search(r"runs at (\d+)\.\d Hz", note[0]) if note else None
        check(m is not None and 300 <= int(m.group(1)) <= 325,
              "312 Hz: EFTP measured its clock (%s)" % (note[0][12:] if note else "no note in EFTP.LOG"))
    else:
        check(not note, "200 Hz: nothing to correct, nothing in the log")
    if t1 is None:
        return
    # the download, by EFTP's log (RETR to its 226) and by this computer
    # (the file appearing to the file whole)
    i = max(k for k, l in enumerate(log) if "RETR /pub/atari/STinG" in l)
    j = next(k for k in range(i, len(log)) if " 226 " in log[k])
    eftp, real = stamp(log[j]) - stamp(log[i]), t1 - t0
    check(abs(eftp - real) <= 0.15 * real + 0.5,
          "%s: EFTP's time for the download, %.1f s, is the real one, %.1f s" % (tag, eftp, real))


run(True)
run(False)
print("%d failed" % len(fails))
sys.exit(1 if fails else 0)
