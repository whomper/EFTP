#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EFTP's load against what TOS has free to start it, on a 1 MB ST (ST-1:
TOS 1.04, FAKESTNG.PRG in AUTO as STinG would be). TOS gives a program the
biggest free block and refuses it ("There is not enough memory in your
computer for the application you just tried to run") when the block is
smaller than the basepage and the program's text, data and BSS.

memprobe.prg (tests/hatari/memprobe.c) is started where EFTP.PRG would be
and says the block on Hatari's console; the test reads EFTP.PRG's load
from its header and checks:

  1. the probe said the block (TPA);
  2. EFTP.PRG starts on ST-1 with room to spare: TPA - 256 - load;
  3. EFTP.PRG's load is within LOADMAX (the Makefile's, make test's
     budget): EContact's accessory takes the most room of the family's
     beside it, and 2.15 (363,364 bytes) had less than 1 KB to spare there
     (the hub's family check, 22 Nov 2026); 2.16's first candidate, 1,020
     bytes more, didn't start.

    memory_test.py WORKDIR [MACHINE]   (ST-1 by default)
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import time

import rig
import server

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
work = os.path.abspath(sys.argv[1])
os.makedirs(work, exist_ok=True)
m = rig.machine(sys.argv[2] if len(sys.argv) > 2 else "ST-1", work)
print("on %s" % m.describe(), flush=True)
fails = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        fails.append(what)


def load(prg):
    """a program's text + data + bss, from its header"""
    with open(prg, "rb") as f:
        h = f.read(14)
    t, d, b = struct.unpack(">III", h[2:14])
    return t + d + b


subprocess.run(["make", "-s", "-C", ROOT, "build/mp/memprobe.prg"], check=True)
limit = int(subprocess.run(["make", "-s", "-C", ROOT, "loadmax"], check=True, capture_output=True,
                           text=True).stdout.strip())
eftp = load(os.path.join(ROOT, "binary", "eftp.prg"))
hd = server.drive_c(work)
shutil.copy(os.path.join(ROOT, "build", "mp", "memprobe.prg"), os.path.join(hd, "eftp.prg"))
r = rig.Rig(work, m, extra=m.network())
try:
    time.sleep(25)
finally:
    r.stop()
    m.stop()
said = re.findall(r"MEMPROBE tpa (\d+)", open(os.path.join(work, "hatari.log"), errors="replace").read())
check(bool(said), "1: the probe said the block TOS gave it")
if said:
    tpa = int(said[0])
    room = tpa - 256 - eftp
    print("     TPA %d bytes; EFTP.PRG's load %d (text, data, BSS): %d to spare" % (tpa, eftp, room))
    check(room > 0, "2: EFTP.PRG starts on %s (%d bytes to spare)" % (m.name, room))
check(eftp <= limit, "3: EFTP.PRG's load %d within LOADMAX %d (%d under)" % (eftp, limit, limit - eftp))
print("memory_test: %d failed" % len(fails))
sys.exit(1 if fails else 0)
