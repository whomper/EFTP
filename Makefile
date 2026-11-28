# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# EFTP - an FTP program for the Atari ST/STE/TT/Falcon, in C.
#
#   make            builds binary/eftp.prg, one program for every 68000
#                   and 68030 Atari, and binary/eftpfb.prg, its native
#                   FireBee build (the FireBee package's eftp.prg: the
#                   guidelines, 7.4), and copies
#                   tls.ovl and tlsfb.ovl next to them (a Direct
#                   connection's TLS, loaded on a TT, a Falcon or a FireBee),
#                   with a stock m68k ELF cross compiler
#                   (Debian/Ubuntu: apt install gcc-m68k-linux-gnu).
#                   No MiNTLib needed: the program is freestanding and
#                   common/tools/elf2tos.py writes the TOS header.
#   make test       builds the FTP core for this computer and runs the
#                   unit tests; `make itest` also runs it against a real
#                   FTP server (pip install pyftpdlib).
#   make help       binary/eftp.hyp, the guide for ST-Guide (needs hcp:
#                   common/tools/build-hcp.sh builds it)
#   make dist       a release's files in dist/: each file, eftp-x.y.zip and
#                   eftp-x.y-firebee.zip
#
# binary/ is what make builds, never committed: releases carry the
# programs (.github/workflows/release.yml).
#
# The code EFTP shares with the other AERA programs (TLS.OVL, BearSSL for
# the tests on this computer, bidi, dates, passwords, the libc headers, the build tools) is in
# common/, a git subtree of github.com/whomper/AERA-Atari: change it there,
# then tools/sync-common.sh. The rest of the Atari runtime, the STinG
# client and the GEM building blocks still come from EMail
# (github.com/whomper/EMail), until they move to common/ too.

CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include)
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin \
           -fno-pic -fno-pie -fno-tree-loop-distribute-patterns -fno-store-merging \
           -fdisable-tree-bswap \
           -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -Wall -Wno-pointer-sign \
           -DFTP_TOS -DDLG_CPU -DDLG_CLICK -nostdinc -isystem $(GCCINC) -Iatari/include -Icommon/atari/include \
           -Isrc -Icommon/src -Iatari -Icommon/atari -Iui -Icommon/ui -Icommon/third_party/bearssl/inc
LDFLAGS := -Wl,-u,hotpad -Wl,-u,queuepad -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,common/atari/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments -Wl,--gc-sections

# BearSSL 0.6, for the tests on this computer: on the Atari the TLS is
# TLS.OVL's (common/tlsovl, built in AERA-Atari, never here)
BSSL    := common/third_party/bearssl
BSSLSRC := $(wildcard $(BSSL)/src/*/*.c)
HBSSLOBJ = $(patsubst $(BSSL)/src/%.c,$(OBJDIR)/hbssl/%.o,$(BSSLSRC))

CORE    := util charset conn ftp xfer store futil bidi secret ctime tz ovl
# linked after everything else: code new since 2.17 that the speed rig's
# actions don't use, so it moves none of theirs (room.c in CORE's place moved
# the UI's code by 700 bytes; a file's start in the folder of 400 575 -> 600
# ms on the Falcons, 28 Nov 2026)
LAST    := room room_tos roomask
ATARI   := libc tos plat_tos net_tos sting gem tls_ovl crash bigbuf
UI      := main draw win files queue queuepad dialogs popup font fontsel about theme colours dsp_ovl help av alert dialog field mbar aboutbox iconify clip clipedit dd panebar text notify aboutline offdraw click hebkey tooltip arrowed progbar offpage eye cpucheck

OBJDIR  := build
# make PROFILE=20 build/prof/EFTP.PRG: the speed rig's build
# (tests/hatari/speed.py), which reports each stretch of 20 ms or more
# between event calls to Hatari's console (common/atari/gem.c, AESGAP).
# Only gem.c and main.c are built again with AES_PROFILE; the rest is the
# shipped build's, and common/tools/profpad.py links it at the shipped
# places modulo 256, so the rig times the layout that ships (the hub,
# 17 Nov 2026; guidelines, Speed)
PROFILE ?= 20
# the main flows' hot loops first, right after the startup objects: the
# 68030's 256-byte instruction cache sees them in the same places whatever
# else grows (the guidelines' Speed, the link order; decided 13 Nov 2026).
# local_names, the sort and the folder listing (xfer), the list parser
# and flist_add (ftp), a pane's rows (files) and the text (draw); then the
# window's scrolling (win), the shared text and off-screen page (text,
# offpage) and the connection with its charsets (conn, charset), so the
# shared code growing behind them moves none of them (17 Nov 2026: common's
# sync moved a file's start in the folder of 400 575 -> 585 ms on
# Falcon-4:256 with NVDI; with them here, 570-580)
HOT     := xfer ftp files draw win text offpage conn charset
# then a pad (common/atari/codepad.S) that keeps the code after the hot
# block where 2.11's speed was measured, modulo 256: common's conn.c grew by
# 426 bytes (17 Nov 2026) and moved all of it by 170, a file's start in the
# folder of 400 575 -> 595 ms on Falcon-4:256 with NVDI (with the pad:
# 575-580; 212 since common a62cb16's text.c; 60 since common c0fa029 and the resize's event call, 19 Nov 2026; 30 since common 7029ef0, text.c's scr_rows by the CPU, 22 Nov 2026; room.c, the room check out of the hot block and xfer_run as long as before, 28 Nov 2026, kept it at 30). When a hot object grows, set HOTPAD so str_copy keeps its place
# modulo 256 (0x...3e, as at 8864e9d) and run the speed pair against 2.11
HOTPAD  := 30
OBJS0   := $(OBJDIR)/crt0.o $(OBJDIR)/sting_s.o $(OBJDIR)/nf.o $(OBJDIR)/crash_s.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(HOT))) $(OBJDIR)/hotpad.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(filter-out $(HOT),$(CORE) $(ATARI) $(UI)))) \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(LAST)))
OBJS    := $(OBJS0)
HDRS    := $(wildcard src/*.h atari/*.h atari/include/*.h ui/*.h \
           common/src/*.h common/atari/*.h common/atari/include/*.h common/ui/*.h common/dsp/*.h)

# what goes onto the Atari: binary/ (built, never committed)
all: binary/eftp.prg binary/eftpfb.prg binary/tls.ovl binary/tlsfb.ovl binary/roots.dat

binary/eftp.prg: $(OBJDIR)/eftp.elf common/tools/elf2tos.py | binary
	PRGFLAGS=7 python3 common/tools/elf2tos.py $< $@
# the FireBee edition (every app native on the FireBee, decided 22 Nov 2026;
# the guidelines, 7.4): every object of eftp.elf built again
# as ColdFire code (-mcpu=5475) by common's mkfirebee.py, from these very
# rules, with no pad (the 68030's caches aren't the ColdFire's)
binary/eftpfb.prg: $(OBJDIR)/eftp.elf common/tools/mkfirebee.py common/atari/cpucheck_cf.S common/atari/ll64.c | binary
	python3 common/tools/mkfirebee.py --elf $(OBJDIR)/eftp.elf --make "HOTPAD=0" $@
build/prof/EFTP.PRG: build/prof/eftp.elf common/tools/elf2tos.py
	PRGFLAGS=7 python3 common/tools/elf2tos.py $< $@
TIMED   := $(patsubst build/gem.o,build/prof/gem.o,$(patsubst build/main.o,build/prof/main.o,$(OBJS)))
build/prof/gem.o: common/atari/gem.c $(HDRS) build/prof/profile
	$(CC) $(CFLAGS) -DAES_PROFILE=$(PROFILE) -c -o $@ $<
build/prof/main.o: ui/main.c $(HDRS) build/prof/profile
	$(CC) $(CFLAGS) -DAES_PROFILE=$(PROFILE) -c -o $@ $<
build/prof/eftp.elf: build/eftp.elf $(TIMED) build/build.o common/tools/profpad.py common/atari/codepad.S
	python3 common/tools/profpad.py --ship build/eftp.elf --out $@ \
	    --cpu=-m68000 --dir build/prof/pads \
	    --exclude build/main.o --exclude build/prof/main.o \
	    --exclude build/gem.o --exclude build/prof/gem.o \
	    -- $(CC) $(LDFLAGS) $(TIMED) build/build.o -lgcc
# PROFILE's value, written again only when it changes: the two objects
# are built again then
build/prof/profile: FORCE
	@mkdir -p build/prof
	@echo $(PROFILE) > $@.new
	@if cmp -s $@.new $@; then rm $@.new; else mv $@.new $@; fi
# the family's tls.ovl, as it comes with common/: copied, never built here.
# Every file of a release, and every name EFTP gives its own files, in
# lowercase (the guidelines, 6.6: a MiNT ext2 drive tells the cases apart)
binary/tls.ovl: common/tlsovl/tls.ovl | binary
	cp $< $@
# and its ColdFire build, which a FireBee loads instead (the _CF_ cookie;
# decided 15 Nov 2026): copied, never built here either
binary/tlsfb.ovl: common/tlsovl/tlsfb.ovl | binary
	cp $< $@

# the version: VERSION holds the last release (major.minor, e.g. 1.31),
# and the build never writes it. Every build shows it as it is, in the
# About box (only releases leave the build machine, so there
# is nothing else to mark). tools/bump.py starts a release.
VERSION_STR := $(shell cat VERSION)

# written again whenever the string changes, so a new VERSION rebuilds it
$(OBJDIR)/build.c: FORCE | $(OBJDIR)
	@printf '/* written by the Makefile: do not edit */\nconst char ftp_version[] = "%s";\n' "$(VERSION_STR)" > $@.new
	@if cmp -s $@.new $@; then rm $@.new; else mv $@.new $@; fi
FORCE:
$(OBJDIR)/build.o: $(OBJDIR)/build.c
	$(CC) $(CFLAGS) -c -o $@ $<

# EFTP's icon for the desktop: icons/, ui/icon*.h and docs/icon*.png
# from common/icons/EFTP.txt (the family's, guideline 5.5), committed; a
# release carries icons/
icons:
	python3 common/tools/appicon.py EFTP --rsc icons/EFTP.RSC --rscnv icons/EFTPNV.RSC --icn icons/EFTP.ICN --mask icons/EFTPMK.ICN --headers ui --docs docs

$(OBJDIR)/eftp.elf: $(OBJS) $(OBJDIR)/build.o common/atari/link.ld
	$(CC) $(LDFLAGS) -Wl,-Map,$@.map -o $@ $(OBJS) $(OBJDIR)/build.o -lgcc

$(OBJDIR)/hotpad.o: common/atari/codepad.S Makefile | $(OBJDIR)
	$(CC) -m68000 -DPAD_NAME=hotpad -DPAD_TEXT=$(HOTPAD) -DPAD_RO=0 -DPAD_DATA=0 -DPAD_BSS=0 -c -o $@ $<
# after queue.o, which grew by 54 bytes for the room check's status line and
# the Transfers header's fill_forget (28 Nov 2026): the code after it at its
# place modulo 256, as the speed rig measured it (QUEUEPAD + 54 = 256)
QUEUEPAD := 202
$(OBJDIR)/queuepad.o: common/atari/codepad.S Makefile | $(OBJDIR)
	$(CC) -m68000 -DPAD_NAME=queuepad -DPAD_TEXT=$(QUEUEPAD) -DPAD_RO=0 -DPAD_DATA=0 -DPAD_BSS=0 -c -o $@ $<
$(OBJDIR)/%.o: src/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: atari/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: ui/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: common/src/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: common/atari/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: common/ui/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
# a 24 KB stack (48 KB by default): EFTP's deepest is a folder deleted
# inside folders, and memory is short on a 1 MB ST
$(OBJDIR)/crt0.o: common/atari/crt0.S | $(OBJDIR)
	$(CC) -m68000 -DSTACK_SIZE=24576 -c -o $@ $<
$(OBJDIR)/sting_s.o: common/atari/sting.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
$(OBJDIR)/crash_s.o: common/atari/crash.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
$(OBJDIR)/nf.o: common/atari/nf.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<

$(OBJDIR) binary:
	mkdir -p $@

# ---- host build: the same FTP core, for the tests and ftp-cli ----
HOSTCC     ?= cc
HOSTCFLAGS := -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
              -Wno-format-truncation -Isrc -Icommon/src -fsanitize=address,undefined
# each core file is the app's own (src/) or shared (common/src/)
HOSTSRC    := $(foreach m,$(CORE) $(LAST) tls,$(firstword $(wildcard src/$(m).c common/src/$(m).c))) \
              tests/host/plat_posix.c
HOSTLIB    := $(OBJDIR)/libbearssl-host.a

$(OBJDIR)/hbssl/%.o: $(BSSL)/src/%.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -w -I$(BSSL)/inc -I$(BSSL)/src -c -o $@ $<

$(HOSTLIB): $(HBSSLOBJ)
	rm -f $@
	ar rcs $@ $^

$(OBJDIR)/unit: tests/unit.c $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -I$(BSSL)/inc -o $@ tests/unit.c $(HOSTSRC) $(HOSTLIB)

$(OBJDIR)/ftp-cli: tests/host/ftp_cli.c $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -I$(BSSL)/inc -o $@ tests/host/ftp_cli.c $(HOSTSRC) $(HOSTLIB)

$(OBJDIR)/mkroots: common/tools/mkroots.c $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -I$(BSSL)/inc -o $@ common/tools/mkroots.c $(HOSTSRC) $(HOSTLIB)

# the root certificates (Mozilla's list, kept in common/) next to the
# program, and decoded on this computer so the Atari needn't
binary/cacert.pem: common/certs/CACERT.PEM | binary
	cp $< $@
binary/roots.dat: binary/cacert.pem
	$(MAKE) $(OBJDIR)/mkroots
	$(OBJDIR)/mkroots binary/cacert.pem $@

# the help file for ST-Guide and HypView, from docs/GUIDE.md; needs hcp
# (github.com/th-otto/hypview; common/tools/build-hcp.sh builds it):
# make help HCP=/path/to/hcp
HCP     ?= hcp
help: binary/eftp.hyp
# The front page (the AERA guidelines, 4.3) is common/tools/helpfront.py's,
# from docs/front.json.
binary/eftp.hyp: docs/GUIDE.md tools/mkhelp.py VERSION docs/front.json docs/help/WINDOW.IMG \
		common/help/EFTP.IMG common/tools/helpfront.py | $(OBJDIR) binary
	python3 tools/mkhelp.py docs/GUIDE.md VERSION $(OBJDIR)/EFTP.STG
	python3 common/tools/helpfront.py page docs/front.json $(OBJDIR)/EFTP.STG $(OBJDIR)/EFTP.STG
	$(HCP) -o $@ $(OBJDIR)/EFTP.STG

# ---- fuzzing (the AERA guidelines, 7.1): what servers send, from
# libFuzzer, with AddressSanitizer and UBSan. The network is
# common/tests/fuzz/fuzz_net.c: the server says the fuzz input. Needs
# clang with libFuzzer (Debian/Ubuntu: clang, libclang-rt-dev); make fuzz
# runs each target FUZZTIME seconds from tests/fuzz/seeds (mkseeds.py)
FUZZCC    ?= clang
FUZZTIME  ?= 60
FUZZERS   := ftp list
FUZZFLAGS := -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -DFUZZ_NET \
             -Isrc -Icommon/src -Icommon/tests/fuzz -I$(BSSL)/inc

$(OBJDIR)/fuzz_%: tests/fuzz/fuzz_%.c $(HOSTSRC) $(HDRS) $(HOSTLIB) common/tests/fuzz/fuzz_net.c | $(OBJDIR)
	$(FUZZCC) $(FUZZFLAGS) -o $@ $< $(HOSTSRC) common/tests/fuzz/fuzz_net.c $(HOSTLIB)

fuzz: $(addprefix $(OBJDIR)/fuzz_,$(FUZZERS))
	@for f in $(FUZZERS); do \
	  mkdir -p $(OBJDIR)/corpus/$$f; \
	  echo "fuzz_$$f: $(FUZZTIME) s"; \
	  $(OBJDIR)/fuzz_$$f -max_total_time=$(FUZZTIME) -timeout=10 -rss_limit_mb=1024 \
	    -artifact_prefix=$(OBJDIR)/crash-$$f- $(OBJDIR)/corpus/$$f tests/fuzz/seeds/$$f \
	    $$(ls -d tests/fuzz/found/$$f 2>/dev/null) \
	    > $(OBJDIR)/fuzz_$$f.log 2>&1 || { tail -40 $(OBJDIR)/fuzz_$$f.log; exit 1; }; \
	  grep -E "^#[0-9]+.*DONE" $(OBJDIR)/fuzz_$$f.log | tail -1; \
	done

# the same targets without libFuzzer (common/tests/fuzz/replay.c), with
# this computer's compiler: make test runs the seeds again, and the inputs
# that once broke a parser (tests/fuzz/found)
REPLAYFLAGS := $(HOSTCFLAGS) -fno-sanitize-recover=undefined -DFUZZ_NET -Icommon/tests/fuzz -I$(BSSL)/inc
REPLAYSRC   := common/tests/fuzz/fuzz_net.c common/tests/fuzz/replay.c

$(OBJDIR)/replay_%: tests/fuzz/fuzz_%.c $(HOSTSRC) $(HDRS) $(HOSTLIB) $(REPLAYSRC) | $(OBJDIR)
	$(HOSTCC) $(REPLAYFLAGS) -o $@ $< $(HOSTSRC) $(REPLAYSRC) $(HOSTLIB)

# make test also looks for word and long accesses at an odd offset from
# the stack in EFTP.PRG's code: a 68000's address error (the guidelines,
# The machines; common's oddaccess.py). And EFTP.PRG's load (text, data
# and BSS: what TOS must find free in one block to start it) against
# LOADMAX: on a 1 MB ST beside EContact's accessory, 2.15's 363,364 bytes
# had less than 1 KB to spare, and 2.16's first candidate, 1,020 more,
# didn't start (the hub's family check, 22 Nov 2026). LOADMAX keeps 1 KB
# under 2.15's; tests/hatari/memory_test.py measures the room on ST-1
LOADMAX := 362340
test: $(OBJDIR)/unit $(addprefix $(OBJDIR)/replay_,$(FUZZERS)) $(OBJDIR)/eftp.elf build/prof/EFTP.PRG binary/eftp.prg
	$(OBJDIR)/unit
	@for f in $(FUZZERS); do \
	  $(OBJDIR)/replay_$$f tests/fuzz/seeds/$$f $$(ls -d tests/fuzz/found/$$f 2>/dev/null) || exit 1; \
	done
	python3 common/tools/oddaccess.py $(OBJDIR)/eftp.elf
	@python3 -c 'import struct,sys; t,d,b=struct.unpack(">III",open("binary/eftp.prg","rb").read(14)[2:]); n=t+d+b; \
	  print("load: text %d + data %d + bss %d = %d bytes, LOADMAX %d (%d under)" % (t,d,b,n,$(LOADMAX),$(LOADMAX)-n)); \
	  sys.exit(n > $(LOADMAX))' || { echo "FAIL EFTP.PRG's load is over LOADMAX: a 1 MB ST with EContact's accessory can't start it"; exit 1; }
loadmax:
	@echo $(LOADMAX)

# tests/hatari/memory_test.py's probe: the block TOS gives a program, on
# Hatari's console
build/mp/memprobe.prg: tests/hatari/memprobe.c $(OBJDIR)/crt0.o $(OBJDIR)/nf.o $(OBJDIR)/libc.o $(OBJDIR)/tos.o
	@mkdir -p build/mp
	$(CC) $(CFLAGS) -c -o build/mp/memprobe.o $<
	$(CC) -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,common/atari/link.ld -Wl,--gc-sections \
	    -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments -o build/mp/memprobe.elf $(OBJDIR)/crt0.o $(OBJDIR)/nf.o \
	    build/mp/memprobe.o $(OBJDIR)/libc.o $(OBJDIR)/tos.o -lgcc
	PRGFLAGS=7 python3 common/tools/elf2tos.py build/mp/memprobe.elf $@

# make cftest: the FireBee edition's FTP core under qemu-m68k -cpu cfv4e
# (the guidelines, 7.4; apt install qemu-user): tests/coldfire/core.c
# linked with eftpfb.prg's own objects (build/cf/), with eftp.prg's
# (build/, run as a 68030) and built for this computer, the listing
# parsers and whole sessions on tests/fuzz's seeds and 2000 changed copies
# of each; tests/coldfire/compare.py says where they differ
CFT     := $(OBJDIR)/cft
CFTOBJ  := ftp conn charset ctime util tz libc
CFTFL   = -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
          -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -ffunction-sections \
          -fdata-sections -Wall -Wno-pointer-sign -DFTP_TOS -nostdinc -isystem $(GCCINC) \
          -Icommon/atari/include -Isrc -Icommon/src -Iatari -Icommon/atari -Icommon/tests/fuzz -I$(CFT)
CFTSRC  := tests/coldfire/core.c tests/coldfire/tls_none.c common/tests/fuzz/fuzz_net.c common/tests/coldfire/gemdos.c
CFTLD   := -nostdlib -static -no-pie -Wl,-z,noexecstack -Wl,--gc-sections -Wl,-Ttext-segment=0x400000
$(CFT)/seeds.h: tests/coldfire/mkseeds.py $(wildcard tests/fuzz/seeds/*/*)
	@mkdir -p $(CFT)
	python3 tests/coldfire/mkseeds.py $@
# (each built for the CPU: -mcpu=5475 with build/cf/'s objects, -m68000 with build/'s)
$(CFT)/core_cf: $(CFTSRC) $(CFT)/seeds.h binary/eftpfb.prg common/tests/coldfire/rt.S
	$(CC) -mcpu=5475 $(CFTFL) -Icommon/tests/coldfire -o $@ $(CFTLD) common/tests/coldfire/rt.S $(CFTSRC) \
	    $(addprefix build/cf/,$(addsuffix .o,$(CFTOBJ))) build/cf/rt/ll64.o
$(CFT)/core_68k: $(CFTSRC) $(CFT)/seeds.h $(OBJDIR)/eftp.elf common/tests/coldfire/rt.S
	$(CC) -m68000 $(CFTFL) -Icommon/tests/coldfire -o $@ $(CFTLD) common/tests/coldfire/rt.S $(CFTSRC) \
	    $(addprefix $(OBJDIR)/,$(addsuffix .o,$(CFTOBJ))) -lgcc
$(CFT)/core_host: tests/coldfire/core.c $(CFT)/seeds.h $(HOSTSRC) $(HDRS) $(HOSTLIB)
	$(HOSTCC) $(REPLAYFLAGS) -DHOST -I$(CFT) -Wl,--wrap=pf_now -o $@ tests/coldfire/core.c $(HOSTSRC) \
	    common/tests/fuzz/fuzz_net.c $(HOSTLIB)
cftest: $(CFT)/core_cf $(CFT)/core_68k $(CFT)/core_host
	qemu-m68k -cpu cfv4e $(CFT)/core_cf > $(CFT)/cf.txt
	qemu-m68k -cpu m68030 $(CFT)/core_68k > $(CFT)/68k.txt
	$(CFT)/core_host > $(CFT)/host.txt
	python3 tests/coldfire/compare.py $(CFT)/cf.txt $(CFT)/68k.txt $(CFT)/host.txt

itest: $(OBJDIR)/ftp-cli
	python3 tests/integration.py
	python3 tests/ftps_test.py
	python3 -m unittest discover -s gateway -p 'test_aeragate.py'
	python3 -m unittest discover -s gateway -p 'test_eftpgate.py'

# a release is two packages and nothing else, the same names in both, no
# file beside them (decided 22 Nov 2026; the guidelines, 7.4), for the release workflow: dist/eftp-x.y.zip for the ST,
# TT and Falcon, and dist/eftp-x.y-firebee.zip, whose eftp.prg is
# binary/eftpfb.prg (the ColdFire build) and tls.ovl is tlsfb.ovl. Every
# name in lowercase (6.6), the icons' too, in icons/
DIST    := binary/eftp.prg binary/tls.ovl binary/cacert.pem binary/roots.dat binary/eftp.hyp
DISTFB  := binary/cacert.pem binary/roots.dat binary/eftp.hyp
ICONS   := icons/EFTP.RSC icons/EFTPNV.RSC icons/EFTP.ICN icons/EFTPMK.ICN
dist: all help
	rm -rf dist
	mkdir -p dist/zip/icons dist/zipfb/icons
	cp $(DIST) dist/zip/
	cp $(DISTFB) dist/zipfb/
	cp binary/eftpfb.prg dist/zipfb/eftp.prg
	cp binary/tlsfb.ovl dist/zipfb/tls.ovl
	for f in $(ICONS); do l=$$(basename $$f | tr A-Z a-z); cp $$f dist/zip/icons/$$l; cp $$f dist/zipfb/icons/$$l; done
	cd dist/zip && zip -q -9 -r ../eftp-$(VERSION_STR).zip .
	cd dist/zipfb && zip -q -9 -r ../eftp-$(VERSION_STR)-firebee.zip .
	rm -rf dist/zip dist/zipfb

clean:
	rm -rf $(OBJDIR) binary dist

.PHONY: all help icons dist test itest cftest loadmax clean fuzz FORCE
