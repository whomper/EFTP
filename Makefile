# EFTP - an FTP program for the Atari ST/STE/TT/Falcon, in C.
#
#   make            builds binary/EFTP.PRG (with Falcon mode: FTPS with TLS
#                   on the Atari) and EFTPST.PRG (the ST edition: secure
#                   servers through the Pi gateway, 150 KB smaller), as
#                   ECal does, with a stock m68k ELF cross compiler
#                   (Debian/Ubuntu: apt install gcc-m68k-linux-gnu).
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
           -DFTP_TOS -nostdinc -isystem $(GCCINC) -Iatari/include -Isrc -Iatari \
           -Ithird_party/bearssl/inc
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,atari/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

# BearSSL 0.6 for Falcon mode, built for the 68030 (the rest stays 68000
# code and runs on any ST), as in EMail and ECal
BSSL    := third_party/bearssl
BSSLSRC := $(wildcard $(BSSL)/src/*/*.c)
BSSLDEF := -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_UNIX_TIME=0 \
           -DBR_USE_WIN32_TIME=0 -DBR_USE_WIN32_RAND=0 -DBR_RDRAND=0
BSSLCF  := -m68030 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -w \
           -nostdinc -isystem $(GCCINC) -Iatari/include -I$(BSSL)/inc -I$(BSSL)/src $(BSSLDEF)
BSSLOBJ  = $(patsubst $(BSSL)/src/%.c,$(OBJDIR)/bssl/%.o,$(BSSLSRC))
HBSSLOBJ = $(patsubst $(BSSL)/src/%.c,$(OBJDIR)/hbssl/%.o,$(BSSLSRC))

CORE    := util charset conn ftp xfer store futil bidi secret ctime tz
ATARI   := libc tos plat_tos net_tos sting gem dsprsa
UI      := main draw win files queue dialogs popup font about theme dsp

OBJDIR  := build
OBJS0   := $(OBJDIR)/crt0.o $(OBJDIR)/sting_s.o $(OBJDIR)/nf.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(CORE) $(ATARI) $(UI)))
# the ST edition: tls_none.c instead of tls.c and BearSSL
STOBJS  := $(OBJS0) $(OBJDIR)/tls_none.o
OBJS    := $(OBJS0) $(OBJDIR)/tls.o
HDRS    := $(wildcard src/*.h atari/*.h atari/include/*.h ui/*.h dsp/*.h)

all: binary/EFTP.PRG binary/EFTPST.PRG binary/ROOTS.DAT

binary/EFTP.PRG: $(OBJDIR)/eftp.elf tools/elf2tos.py
	PRGFLAGS=7 python3 tools/elf2tos.py $< $@
binary/EFTPST.PRG: $(OBJDIR)/eftpst.elf tools/elf2tos.py
	PRGFLAGS=7 python3 tools/elf2tos.py $< $@

# Every build gets the next version: the minor number in VERSION
# (major.minor, e.g. 1.6) goes up by one whenever the program is linked
# again, for the About box; both editions carry the same number. Commit
# VERSION with the program.
$(OBJDIR)/build.o: $(OBJS) $(STOBJS) atari/link.ld
	v=$$(cat VERSION); v=$${v%%.*}.$$(( $${v#*.} + 1 )); echo $$v > VERSION; \
	printf 'const char ftp_version[] = "%s";\n' $$v > $(OBJDIR)/build.c
	$(CC) $(CFLAGS) -c -o $@ $(OBJDIR)/build.c
	@echo "EFTP $$(cat VERSION)"

$(OBJDIR)/eftp.elf: $(OBJS) $(OBJDIR)/build.o $(OBJDIR)/libbearssl.a atari/link.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(OBJDIR)/build.o $(OBJDIR)/libbearssl.a -lgcc

$(OBJDIR)/eftpst.elf: $(STOBJS) $(OBJDIR)/build.o atari/link.ld
	$(CC) $(LDFLAGS) -o $@ $(STOBJS) $(OBJDIR)/build.o -lgcc

$(OBJDIR)/bssl/%.o: $(BSSL)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BSSLCF) -c -o $@ $<

$(OBJDIR)/libbearssl.a: $(BSSLOBJ)
	rm -f $@
	$(CROSS)ar rcs $@ $^

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
HOSTSRC    := $(addprefix src/,$(addsuffix .c,$(CORE) tls)) tests/host/plat_posix.c
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

$(OBJDIR)/mkroots: tools/mkroots.c $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -I$(BSSL)/inc -o $@ tools/mkroots.c $(HOSTSRC) $(HOSTLIB)

# the root certificates decoded on this computer, so the Atari needn't
binary/ROOTS.DAT: binary/CACERT.PEM
	$(MAKE) $(OBJDIR)/mkroots
	$(OBJDIR)/mkroots binary/CACERT.PEM $@

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
	python3 tests/ftps_test.py
	python3 -m unittest discover -s gateway -p 'test_*.py'


clean:
	rm -rf $(OBJDIR) binary/EFTP.PRG binary/EFTPST.PRG

.PHONY: all help test itest clean
