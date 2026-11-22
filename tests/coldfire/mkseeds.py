#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""tests/fuzz's seeds as a C header for tests/coldfire/core.c, which has no
files to read under qemu-m68k: mkseeds.py OUT.h"""
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
out = ["/* written by tests/coldfire/mkseeds.py: do not edit */",
       "typedef struct { const char *name; const unsigned char *data; long size; } SEED;"]
for case in ("list", "ftp"):
    d = os.path.join(here, "..", "fuzz", "seeds", case)
    names = sorted(os.listdir(d))
    for i, n in enumerate(names):
        b = open(os.path.join(d, n), "rb").read()
        out.append("static const unsigned char %s_%d[] = {%s};" % (case, i, ",".join(str(x) for x in b) or "0"))
    out.append("#define %s_SEEDS %d" % (case.upper(), len(names)))
    out.append("static const SEED %s_seeds[] = {%s};" % (case, ", ".join(
        '{"%s", %s_%d, %d}' % (n, case, i, os.path.getsize(os.path.join(d, n))) for i, n in enumerate(names))))
open(sys.argv[1], "w").write("\n".join(out) + "\n")
