#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Bring common/ up to date with the shared AERA code
# (github.com/whomper/AERA-Atari, branch common: its common/ folder).
# Change shared code there, never here: a change made only in this copy
# drifts again.
set -e
cd "$(dirname "$0")/.."
before=$(git rev-parse HEAD)
git subtree pull --prefix=common https://github.com/whomper/AERA-Atari common --squash \
    -m "Update common/ from AERA-Atari"
# git subtree leaves its squash commit unsigned (Unverified on GitHub):
# make it again signed, with the merge on top
[ ! -f common/tools/sign-subtree.sh ] || sh common/tools/sign-subtree.sh "$before"
