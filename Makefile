# EFTP - an FTP program for the Atari ST/STE/TT/Falcon, in C.
#
#   make            builds binary/EFTP.PRG with a stock m68k ELF cross
#                   compiler (Debian/Ubuntu: apt install gcc-m68k-linux-gnu).
#                   No MiNTLib needed: the program is freestanding and
#                   tools/elf2tos.py writes the TOS header.
#   make test       builds the FTP core for this computer and runs the
#                   unit tests; `make itest` also runs it against a real
#                   FTP server (pip install pyftpdlib).
#
# The Atari runtime, the STinG client, the GEM interface's building
# blocks and the build itself come from EMail (github.com/whomper/EMAIL),
# which took them from Claude ST (github.com/whomper/atari_claude).

CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include)
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin \
           -fno-pic -fno-pie -fno-tree-loop-distribute-patterns -fno-store-merging \
           -fno-asynchronous-unwind-tables -Wall -Wno-pointer-sign \
           -DFTP_TOS -nostdinc -isystem $(GCCINC) -Iatari/include -Isrc -Iatari
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,atari/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

CORE    := util charset conn ftp xfer store futil bidi secret ctime tz
ATARI   := libc tos plat_tos net_tos sting gem
UI      := main draw win files queue dialogs popup font about theme

OBJDIR  := build
OBJS    := $(OBJDIR)/crt0.o $(OBJDIR)/sting_s.o $(OBJDIR)/nf.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(CORE) $(ATARI) $(UI)))
HDRS    := $(wildcard src/*.h atari/*.h atari/include/*.h ui/*.h)

all: binary/EFTP.PRG

binary/EFTP.PRG: $(OBJDIR)/eftp.elf tools/elf2tos.py
	PRGFLAGS=7 python3 tools/elf2tos.py $< $@

# Every build of EFTP.PRG gets the next version: the minor number in
# VERSION (major.minor, e.g. 1.6) goes up by one each time the program is
# linked, for the About box. Commit VERSION with the program.
$(OBJDIR)/eftp.elf: $(OBJS) atari/link.ld
	v=$$(cat VERSION); v=$${v%%.*}.$$(( $${v#*.} + 1 )); echo $$v > VERSION; \
	printf 'const char ftp_version[] = "%s";\n' $$v > $(OBJDIR)/build.c
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/build.o $(OBJDIR)/build.c
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(OBJDIR)/build.o -lgcc
	@echo "EFTP $$(cat VERSION)"

$(OBJDIR)/%.o: src/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: atari/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: ui/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/crt0.o: atari/crt0.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
$(OBJDIR)/sting_s.o: atari/sting.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
$(OBJDIR)/nf.o: atari/nf.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<

$(OBJDIR):
	mkdir -p $@

# ---- host build: the same FTP core, for the tests and ftp-cli ----
HOSTCC     ?= cc
HOSTCFLAGS := -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
              -Wno-format-truncation -Isrc -fsanitize=address,undefined
HOSTSRC    := $(addprefix src/,$(addsuffix .c,$(CORE))) tests/host/plat_posix.c

$(OBJDIR)/unit: tests/unit.c $(HOSTSRC) $(HDRS) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ tests/unit.c $(HOSTSRC)

$(OBJDIR)/ftp-cli: tests/host/ftp_cli.c $(HOSTSRC) $(HDRS) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ tests/host/ftp_cli.c $(HOSTSRC)

# the help file for ST-Guide and HypView, from docs/GUIDE.md; needs hcp
# (github.com/th-otto/hypview): make help HCP=/path/to/hcp
HCP     ?= hcp
help: binary/EFTP.HYP
binary/EFTP.HYP: docs/GUIDE.md tools/mkhelp.py VERSION | $(OBJDIR)
	python3 tools/mkhelp.py docs/GUIDE.md VERSION $(OBJDIR)/EFTP.STG
	$(HCP) -o $@ $(OBJDIR)/EFTP.STG

test: $(OBJDIR)/unit
	$(OBJDIR)/unit

itest: $(OBJDIR)/ftp-cli
	python3 tests/integration.py

clean:
	rm -rf $(OBJDIR)

.PHONY: all help test itest clean
