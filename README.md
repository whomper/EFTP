# EFTP

<img src="docs/icon16.png" width="96" align="right" alt="EFTP icon: an Atari SM124-style monitor showing a folder and two arrows, up and down">

An FTP program for the Atari ST, STE, TT and Falcon, written in C for
GEM. Files and whole folders go between your Atari and an FTP server,
both ways, the way a modern FTP program does it, and to FTPS and SFTP
servers too: through a Raspberry Pi gateway, or FTPS by the Falcon
itself.

![EFTP on a Falcon: the Atari's files on the left, the server's on the right, the transfers below](docs/screenshot.png)

**Highlights**

- **Two panes, like FileZilla:** this Atari's files on the left, the
  server's on the right, the transfers below, with draggable dividers
- **Whole folders, both ways:** a folder is copied with everything in
  it, folder by folder
- **Drag and drop** from one side to the other, or double-click, or
  Upload and Download in the toolbar
- **A transfer queue** with a progress bar, the speed and the time left;
  Done, Skipped, Stopped and Failed in colour, and why
- **Resume:** Stop (Esc) and Retry go on where a file stopped (REST);
  when the line goes in the middle, EFTP connects again by itself
- **A file already there:** ask, replace, resume, replace if older, or
  skip, for one file or all of them
- **Names TOS can take:** long names come down as 8+3 (`screenshot-001.png`
  becomes `SCREEN~1.PNG`), always the same for the same folder; capitals
  go up in small letters; UTF-8 and Hebrew names
- **Dates kept** both ways (MDTM, MFMT), in your time zone by name
- **Text and binary:** texts go in ASCII mode by their extension
- **Secure servers:** FTPS and SFTP through a Raspberry Pi on your home
  network (`gateway/`), from any Atari; FTPS by the Falcon or TT itself
  in Falcon mode, TLS 1.2 with RSA on the DSP, as EMail and ECal
- **Site Manager:** your servers with their logins (passwords encrypted
  in EFTP.INF), the folders to start in, anonymous logins; Quick connect
  for the others
- **File management on both sides:** new folder, rename, delete (folders
  with what is in them), permissions on the server (`chmod` with tick
  boxes)
- **Modern but Atari:** one window, a toolbar with tooltips, right-click
  menus, column titles that sort, colour on 16- and 256-colour screens,
  a Settings dialog with a sidebar, the server's messages in a log pane
- **Small and quick:** the ST edition is 155 KB of plain 68000 code and
  runs on a 1 MB ST with STinG; both run in TT RAM when there is some
- Runs on TOS 1.04 to 4.x, EmuTOS, MagiC and MiNT, with STinG or
  MiNTnet, from an ST in medium resolution to a Falcon in 640×480

![The Site Manager, on an SFTP site that goes through the Pi gateway](docs/sites.png)

**In more detail**

- FTP as servers speak it today (RFC 959, 2389, 3659): FEAT, MLSD
  listings with exact sizes and UTC times, or LIST from older servers
  (Unix, Windows/IIS and `ls --time-style` formats), SIZE, MDTM, MFMT,
  REST, UTF8, EPSV with PASV as the fallback
- Passive mode only: the Atari makes every connection itself, so it
  works from behind any router; a server that gives an address only its
  own network can reach is reached by the address it was found at
- The window stays alive during a transfer: windows can be moved, the
  progress shows, Esc or the Stop button stops it
- Keeps the line open while you look around (NOOP every minute), and
  offers to connect again if the server hangs up anyway
- Hidden files on request, folders above files, sort by name, size or
  date
- A protocol log (EFTP.LOG) with every line timed to the hundredth of a
  second and the passwords left out
- EFTP.INF in capitals, each setting with a note above it, as EMail's
- The help: EFTP.HYP for ST-Guide, opened by the Help key

![Settings: a sidebar of parts, here Secure: Falcon mode and the Pi gateway](docs/settings.png)

## How it reaches secure servers

Like [EMail](https://github.com/whomper/EMail) and
[ECal](https://github.com/whomper/ECal), one of two ways:

```
any Atari:   Atari (EFTP) --plain FTP, home LAN--> Raspberry Pi (eftpgate.py) --FTPS or SFTP--> the server
Falcon, TT:  Falcon (EFTP.PRG, TLS 1.2, RSA on the DSP) --------------------------------FTPS--> the server
```

- **Through the Pi.** `gateway/eftpgate.py` is an FTP server for the
  Atari that speaks FTPS (Python's ftplib, resuming the TLS session on
  every data connection) or SFTP (paramiko) to the server named in the
  login, `sftp:user@host:port`; EFTP writes that for you from the
  site's Protocol. It checks certificates, remembers SSH host keys, and
  turns text files' line ends for SFTP. `gateway/install.sh` sets it up
  as a service; see [gateway/README.md](gateway/README.md).
- **Falcon mode.** `EFTP.PRG` does FTPS itself with
  [BearSSL](https://bearssl.org/), the same TLS code as EMail and ECal
  (ChaCha20 first, RSA certificates checked on the DSP56001, sessions
  resumed); explicit (AUTH TLS) and implicit (port 990). Every data
  connection resumes the control connection's session, which saves the
  slow part of the handshake and which vsftpd and others insist on.

EFTP is part of a family with [EMail](https://github.com/whomper/EMail),
[ECal](https://github.com/whomper/ECal) and
[Claude ST](https://github.com/whomper/ClaudeST): the same freestanding
TOS runtime, GEM bindings and STinG client, the same window with panes,
dialogs, colours and Settings, and the same monitor on the icon.

## Getting started

Everything to copy to the Atari is in each
[release](https://github.com/whomper/EFTP/releases/latest), each file on
its own and all together in `EFTP-x.y.zip`:

| File | |
|---|---|
| `EFTP.PRG`   | Falcon and TT: with Falcon mode (TLS, DSP); also works through the Pi |
| `EFTPST.PRG` | ST and STE: the same program without TLS (110 KB smaller), through the Pi |
| `CACERT.PEM`, `ROOTS.DAT` | the root certificates for Falcon mode |
| `EFTP.HYP`   | the user guide for ST-Guide, opened by the Help key |
| `EFTP.RSC`, `EFTP.ICN`, `EFTPMK.ICN` | the desktop icon (in the zip's `icons/` folder) |

1. Copy `EFTP.PRG` or `EFTPST.PRG` (and `EFTP.HYP`; for Falcon mode
   `CACERT.PEM` and `ROOTS.DAT`) into a folder of its own, e.g.
   `C:\EFTP\`.
2. Load STinG (or run MiNT with MiNTnet).
3. Start EFTP, press Connect (^K), make a site with the server's name
   and your login, and Connect.

The [user guide](docs/GUIDE.md) describes everything else.

![EFTPST.PRG on a 1 MB ST in high resolution](docs/st-mono.png)

## Building

```
sudo apt install gcc-m68k-linux-gnu      # Debian/Ubuntu
make                                     # binary/EFTP.PRG, EFTPST.PRG, ROOTS.DAT
make help HCP=/path/to/hcp               # binary/EFTP.HYP
make dist                                # a release's files in dist/
```

The ST-Guide help file `binary/EFTP.HYP` is made from `docs/GUIDE.md` by
`tools/mkhelp.py` and the HCP compiler from the
[hypview](https://github.com/th-otto/hypview) project
(`common/tools/build-hcp.sh DIR` builds it). `binary/` and `dist/` are
what make builds; they are never committed.

**Versions and releases.** `VERSION` holds the last release, major.minor
(e.g. 1.31), and the build never changes it. A release build
(`RELEASE=1`) shows it as it is in the About box; any other build shows
`1.31-dev`. To release: `tools/bump.py`, commit and push, then run the
Release workflow (Actions > Release > Run workflow, on main) with what
is new: [`.github/workflows/release.yml`](.github/workflows/release.yml)
tags the commit `v1.32`, tests, builds both editions and the help file,
and publishes them as a GitHub Release with those notes. Pushing a tag
`v1.32` made with `git tag -a` does the same, with the tag's message as
the notes.

No MiNTLib is needed: the program is freestanding (`atari/`) and
`common/tools/elf2tos.py` writes the TOS executable. It is built for the 68000,
so it runs on every Atari from the ST to the Falcon; BearSSL
(`common/third_party/bearssl`, in EFTP.PRG only) is built for the 68030.
`common/tools/mkroots.c` decodes `CACERT.PEM` into `ROOTS.DAT`. The desktop icon is
drawn by `tools/icon/make_icon.py` (which also writes `ui/icon.h`,
`ui/icon16.h` and the previews in `docs/`) and `tools/icon/make_rsc.py`.

## Source

```
atari/   TOS runtime: start-up, system calls, a small C library, AES/VDI,
         TCP over STinG/STiK or MiNTnet (with a DNS client)
src/     the FTP core: the protocol and its listings (ftp.c), the
         transfer queue and names (xfer.c), settings and sites (store.c),
         the connection (conn.c), TLS (tls.c; tls_none.c in EFTPST.PRG),
         charsets, time zones
common/  code shared with the other AERA programs (from AERA-Atari):
         BearSSL, the DSP RSA, bidi, dates, passwords, build tools
gateway/ eftpgate.py for the Raspberry Pi, install.sh, its tests
ui/      the GEM program: the window and its panes, the file lists, the
         transfers, dialogs, menus, the toolbar
tests/   unit and integration tests, the Hatari test rig
tools/   the icon generator, the help builder, sync-common.sh, bump.py,
         FAKESTNG.PRG (for testing in Hatari only)
icons/   the desktop icon (RSC and ICN files), from tools/icon
binary/  what make builds for the Atari (not committed: the releases have it)
```

## Tests

The FTP core (`src/`) also builds on Linux, where it is tested against a
real FTP server before it runs on the Atari:

```
make test           # listing parsers (Unix, DOS, MLSD), PASV/EPSV, 8+3 names, sorting;
                    # the fuzz inputs again (tests/fuzz/seeds)
make fuzz           # whole FTP sessions and listings from libFuzzer (clang),
                    # the control and data connections each with their own part
make itest          # against pyftpdlib: logins, MLSD and LIST, files and folders
                    # both ways, resume, text mode, UTF-8 names; FTPS with EFTP's
                    # own TLS (explicit, implicit, RSA and ECDSA, sessions resumed,
                    # wrong certificates refused); the gateway against an SFTP
                    # and an FTPS server (pip install pyftpdlib pyopenssl paramiko)
```

`tests/hatari/` runs EFTP.PRG itself in the
[Hatari](https://hatari.tuxfamily.org/) emulator with
[EmuTOS](https://emutos.sourceforge.io/), with Xvfb and xdotool:

```
make -C tools/fakesting
tests/hatari/net_test.py /tmp/eftp-net etos512us.img       # Falcon, a real FTP server
EFTP_PRG=EFTPST.PRG tests/hatari/net_test.py ...           # the ST edition on 1 MB
tests/hatari/secure_test.py /tmp/eftp-sec etos512us.img    # Falcon mode FTPS; SFTP via eftpgate
tests/hatari/screenshots.py /tmp/eftp-shots etos512us.img  # the pictures in docs/
```

`net_test.py` boots an emulated Falcon (4 MB; 1 MB for the ST edition), where EFTP connects
from the Site Manager, downloads a folder, uploads one by dragging it,
answers "already there" with Skip, and stops and resumes a download; the
test checks every byte on both sides. Hatari has no network card, so
`FAKESTNG.PRG` stands in for STinG and carries EFTP's connections over
the emulated serial port to `serial_bridge.py`. `secure_test.py` does
the same against secure servers: FTPS in Falcon mode (RSA on the
emulated DSP, every data connection resuming the TLS session) and SFTP
through `eftpgate.py`, and checks the files' bytes and line ends. It's for the emulator
only; never install it on a real Atari.

## Licence

EFTP is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version. It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See [COPYING](COPYING).

Copyright (C) 2026 AERA | Erez Yaary.

It includes, under their own licences (compatible with the GPL):

- [BearSSL](common/third_party/bearssl) by Thomas Pornin: MIT licence
  ([LICENSE.txt](common/third_party/bearssl/LICENSE.txt))
- `CACERT.PEM`, the root certificates for Falcon mode, from Mozilla's CA
  list (Mozilla Public License 2.0)
