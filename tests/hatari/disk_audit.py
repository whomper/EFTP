#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EFTP's disk access against the AERA guidelines, 6.5, in Hatari: every
open and lookup costs real time on an SD card on SCSI or an ACSI drive.

Each job runs in a Hatari of its own with the GEMDOS trace on; only the
calls made from EFTP's own program memory count (not the AES's, the
desktop's or FAKESTNG's), less those of a run that only starts, connects
and quits:

    dl      one 1 MB download: one open, writes of 8 KB or more, the
            server's date on the file
    ul      one 1 MB upload: one open, reads of 16 KB or more, the
            Atari's date on the server (MFMT)
    folder  a folder of 20 files down: one open a file, no lookup a
            file, every date right
    list    a local folder of 200 files: one Fsfirst, no Dfree
    and after a job, at most one Dfree (the free room in the header)

    disk_audit.py WORKDIR [MACHINE] [JOB...]   (Xvfb, xdotool, pip install pyftpdlib)

MACHINE is one of the family's (common/tools/machines.py list), Falcon by
default; ST-1 and ST run EFTPST.PRG at about 2 KB/s, so a 1 MB job takes
minutes there.
"""
import collections
import datetime
import os
import re
import sys
import time
import zoneinfo

import rig
import server

MB = 1024 * 1024
ZONE = zoneinfo.ZoneInfo("Asia/Jerusalem")      # the rig's EFTP.INF
server.FILES["pub/audit/big.bin"] = MB
for i in range(20):
    server.FILES["pub/audit/twenty/f%02d.bin" % (i + 1)] = 20000
server.LOCAL["BIG.UP"] = MB
for i in range(200):
    server.LOCAL["MANY/F%03d.DAT" % i] = 100

work = os.path.abspath(sys.argv[1])
spec = sys.argv[2] if len(sys.argv) > 2 else "Falcon"
jobs = sys.argv[3:] or ["dl", "ul", "folder", "list"]
fails = []
FTPD_LOG = None

ROW0, ROWH = 131, 16
CONNECT, UPLOAD, DOWNLOAD = (50, 73), (150, 73), (240, 73)
REMOTE_X, LOCAL_X = 400, 60


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        fails.append(what)


def row(n):
    return ROW0 + n * ROWH


def size(p):
    return os.path.getsize(p) if os.path.exists(p) else -1


def run(job):
    """one job in a Hatari of its own; its trace's lines, the FTP
    server's files and drive C:"""
    w = os.path.join(work, job)
    os.makedirs(w, exist_ok=True)
    trace = os.path.join(w, "gemdos.trace")
    m = rig.machine(spec, w)
    slow = 8 if m.name in ("ST", "ST-1") else 1
    srv, home = server.ftp_server(w)
    global FTPD_LOG
    FTPD_LOG = FTPD_LOG or os.path.join(w, "ftpd.log")
    hd = server.drive_c(w, prg=m.prg)
    inf = os.path.join(hd, "EFTP.INF")
    text = open(inf, newline="").read().replace("LOG=1", "LOG=0")
    open(inf, "w", newline="").write(text)
    r = rig.Rig(w, m, extra=list(m.network()) + ["--trace", "gemdos", "--trace-file", trace])

    def wait_for(cond, secs):
        t = time.time()
        while time.time() - t < secs * slow:
            if cond():
                return True
            time.sleep(1)
        return False
    done = True
    try:
        time.sleep(30)
        r.click(*CONNECT)
        time.sleep(2)
        r.click(60, 92)                             # Test server, /pub
        # the server logs to the first job's ftpd.log (logging is set up once)
        seen = open(FTPD_LOG).read().count("USER 'atari' logged in")
        wait_for(lambda: open(FTPD_LOG).read().count("USER 'atari' logged in") > seen, 60)
        time.sleep(8 * min(slow, 3))
        if job in ("dl", "folder"):
            r.click(REMOTE_X, row(2), double=True)  # /pub/audit
            time.sleep(8 * min(slow, 3))
            r.click(REMOTE_X, row(2 if job == "dl" else 1))   # big.bin, twenty
            time.sleep(1.5)
            r.click(*DOWNLOAD)
            last = os.path.join(hd, "BIG.BIN") if job == "dl" else os.path.join(hd, "TWENTY", "F20.BIN")
            done = wait_for(lambda: size(last) == (MB if job == "dl" else 20000), 600)
        elif job == "ul":
            r.click(LOCAL_X, row(5))                # C:\BIG.UP
            time.sleep(1.5)
            r.click(*UPLOAD)
            done = wait_for(lambda: size(os.path.join(home, "pub", "big.up")) == MB, 600)
        elif job == "list":
            r.click(LOCAL_X, row(4), double=True)   # C:\MANY
        time.sleep(10 * min(slow, 3))
        r.shot(job)
        r.ctrl("q")
        time.sleep(8)
        r.cmd("hatari-shortcut", "quit")            # a clean exit writes all the trace
        time.sleep(5)
    finally:
        r.stop()
        m.stop()
        srv.close_all()
    check(done, "%s: the job finished" % job)
    return open(trace, errors="replace").read().splitlines(), home, hd


def own(lines):
    """EFTP's calls: from its own program, after its Pexec"""
    start = next(i for i, l in enumerate(lines) if 'Pexec(0, "C:\\EFTP.PRG"' in l)
    base = top = None
    for l in lines[start:]:
        m = re.search(r"Mshrink\(0x([0-9A-F]+), 0x([0-9A-F]+)\)", l)
        if m:
            base = int(m.group(1), 16)
            top = base + int(m.group(2), 16)
            break
    calls = []
    for l in lines[start:]:
        m = re.search(r"GEMDOS 0x[0-9A-F]+ (\w+)\((.*)\) at PC[ =]0x([0-9A-F]+)", l)
        if m and base <= int(m.group(3), 16) < top:
            calls.append((m.group(1), m.group(2)))
    return calls


def tally(calls):
    """per call, per file; the sizes of the reads and writes of each file"""
    count = collections.Counter(c for c, _ in calls)
    per = collections.defaultdict(collections.Counter)
    io = collections.defaultdict(list)
    handle = {}
    for c, a in calls:
        p = re.match(r'"([^"]*)"', a)
        if p:
            per[p.group(1).upper()][c] += 1
        if c in ("Fopen", "Fcreate") and p:
            handle["?"] = p.group(1).upper()            # its handle comes back unseen:
        elif c in ("Fread", "Fwrite"):                  # the next read or write is it
            h, n = a.split(", ")[:2]
            name = handle.pop("?", None)
            if name:
                handle[h] = name
            io[(handle.get(h), c)].append(int(n))
        elif c == "Fclose":
            handle.pop(a, None)
    return count, per, io


def opens(per, name):
    return per[name]["Fopen"] + per[name]["Fcreate"]


def big_pieces(sizes, least):
    return bool(sizes) and all(n >= least for n in sizes[:-1])


def wall(epoch):
    """a UTC time as the Atari shows it, to the minute"""
    return datetime.datetime.fromtimestamp(epoch, ZONE).replace(tzinfo=None, second=0, microsecond=0)


def on_drive(path):
    """an Atari file's date (Hatari's drive keeps it as this computer's
    local time), to the minute"""
    return datetime.datetime.fromtimestamp(os.path.getmtime(path)).replace(second=0, microsecond=0)


os.makedirs(work, exist_ok=True)
m0 = rig.machine(spec, os.path.join(work, "base"))
print("on %s, %s" % (m0.describe(), m0.prg), flush=True)
base, _, _ = run("base")
b_count, b_per, _ = tally(own(base))
check(b_count["Dfree"] <= 1, "start: Dfree once (%d)" % b_count["Dfree"])

for job in jobs:
    lines, home, hd = run(job)
    count, per, io = tally(own(lines))
    more = {c: count[c] - b_count[c] for c in ("Fsfirst", "Dfree", "Fopen", "Fcreate")}
    print("%s: %s" % (job, dict(count.most_common())), flush=True)
    if job == "dl":
        f = "C:\\BIG.BIN"
        check(opens(per, f) == 1, "dl: BIG.BIN opened once (%d)" % opens(per, f))
        check(per[f]["Fsfirst"] == 0, "dl: BIG.BIN not looked up (%d)" % per[f]["Fsfirst"])
        w = io[(f, "Fwrite")]
        check(big_pieces(w, 8192), "dl: %d writes, 8 KB or more each" % len(w))
        check(on_drive(os.path.join(hd, "BIG.BIN")) == wall(os.path.getmtime(os.path.join(home, "pub", "audit", "big.bin"))),
              "dl: the server's date on the file")
    elif job == "ul":
        f = "C:\\BIG.UP"
        check(opens(per, f) == 1, "ul: BIG.UP opened once (%d)" % opens(per, f))
        rd = io[(f, "Fread")]
        check(big_pieces(rd, 16384), "ul: %d reads, 16 KB or more each" % len(rd))
        check(wall(os.path.getmtime(os.path.join(home, "pub", "big.up"))) == on_drive(os.path.join(hd, "BIG.UP")),
              "ul: the Atari's date on the server")
    elif job == "folder":
        names = ["C:\\TWENTY\\F%02d.BIN" % (i + 1) for i in range(20)]
        check(all(opens(per, n) == 1 for n in names), "folder: each file opened once (%d opens)"
              % sum(opens(per, n) for n in names))
        check(sum(per[n]["Fsfirst"] for n in names) == 0, "folder: no file looked up")
        check(all(on_drive(os.path.join(hd, "TWENTY", "F%02d.BIN" % (i + 1))) ==
                  wall(os.path.getmtime(os.path.join(home, "pub", "audit", "twenty", "f%02d.bin" % (i + 1))))
                  for i in range(20)), "folder: every file has the server's date")
        check(all(big_pieces(io[(n, "Fwrite")], 8192) for n in names), "folder: each file in one write")
    elif job == "list":
        check(more["Fsfirst"] == 1, "list: one Fsfirst for 200 files (%d)" % more["Fsfirst"])
        check(more["Dfree"] == 0, "list: no Dfree on the same drive (%d)" % more["Dfree"])
    if job != "list":
        check(more["Dfree"] <= 1, "%s: Dfree at most once after the job (%d)" % (job, more["Dfree"]))
        check(more["Fsfirst"] <= 2, "%s: at most a listing of the folder and of the pane (%d Fsfirst)"
              % (job, more["Fsfirst"]))

print("%d failed" % len(fails))
sys.exit(1 if fails else 0)
