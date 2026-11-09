# EFTP

<img src="docs/icon.png" width="96" align="right" alt="EFTP icon: two folders, a gold one for this Atari and a blue one for the server, with a blue arrow going up to the server and a green one coming down">

A two-pane FTP program for the Atari, written in C for GEM, that reaches FTPS and SFTP servers too.

**Runs on** any ST, STE, TT or Falcon with TOS 1.04 or newer, EmuTOS, MagiC or MiNT, in 640×200 or
larger, with about 350 KB free (a 1 MB ST with STinG will do), and STinG, STiK, MiNTnet or MagiC-Net.

**Highlights**

- **Two panes:** this Atari, the server, and the transfers below; drag files across.
- **Whole folders both ways**, with the time left, and Stop and Retry that resume.
- **Names TOS can take** (8+3), dates kept, text files by their extension.
- **FTPS and SFTP** through a Raspberry Pi, or FTPS by a TT or Falcon itself.
- **A Site Manager** with encrypted passwords, and file management on both sides.

![EFTP on a Falcon in 640x480 with 256 colours: the Atari's files on the left, the server's on the right, the transfers below](docs/screenshot.png)

## Getting started

Download from the **[latest release](https://github.com/whomper/EFTP/releases/latest)**, file by file
or all in the zip:

| File | What it is |
|---|---|
| `EFTP.PRG` | the program, for every Atari |
| `EFTP.HYP` | the guide, for EHypView or ST-Guide (the Help key) |
| `TLS.OVL`, `CACERT.PEM`, `ROOTS.DAT` | FTPS on a TT or Falcon itself (a Direct connection) |
| `EFTP.RSC`, `EFTPNV.RSC`, `EFTP.ICN`, `EFTPMK.ICN` | the desktop icon (`EFTPNV.RSC` under NVDI's own palette) |

1. Copy `EFTP.PRG` and `EFTP.HYP` (and on a TT or Falcon the TLS files) into a folder of their own.
2. Load STinG, or run MiNT with MiNTnet.
3. Start EFTP, choose **File > Sites** (^K), make a site and press **Connect**.
4. Press **Help** for the guide.

Under TOS use `FOLDR100.PRG` ([why](docs/GUIDE.md#what-you-need)); set STinG's Delay for the machine
([how](docs/GUIDE.md#when-something-goes-wrong)).

## How it reaches the servers

```
ST, STE:     Atari (EFTP) --plain FTP, home LAN--> Raspberry Pi (eftpgate.py) --FTPS or SFTP--> the server
Falcon, TT:  Atari (EFTP + TLS.OVL: TLS 1.2, RSA on the DSP) --------------------------FTPS--> the server
             (a Direct connection; or through the Pi, as an ST)
```

Plain FTP goes straight to the server. [AERA-Gateway](https://github.com/whomper/AERA-Gateway) sets up
the Pi; the [guide](docs/GUIDE.md#secure-servers-ftps-and-sftp) has the steps.

## Building

```sh
sudo apt install gcc-m68k-linux-gnu python3 zip   # no MiNTLib needed
make                         # binary/EFTP.PRG and its files
make help HCP=/path/to/hcp   # binary/EFTP.HYP
make dist                    # a release's files and the zip
```

More in [docs/DEVELOPING.md](docs/DEVELOPING.md).

## Source

```
src/      the FTP core: protocol, queue, settings
ui/       the GEM program
atari/    files and time on GEMDOS
common/   shared with the other AERA programs
gateway/  the Pi gateways (AERA-Gateway's copy)
tests/    unit, fuzz, integration, Hatari
docs/     the guide and DEVELOPING.md
```

## Tests

```sh
make test     # parsers, names, paths, text mode
make itest    # a real FTP server, FTPS, the gateway
```

`tests/hatari/` runs EFTP in Hatari ([each check](docs/DEVELOPING.md#tests)).

## Licence

GPL version 2 or later, without any warranty ([COPYING](COPYING)). Copyright (C) 2026 AERA | Erez Yaary.
It includes [BearSSL](common/third_party/bearssl) (MIT) and Mozilla's root certificates (MPL 2.0);
the gateway uses [paramiko](https://www.paramiko.org/) (LGPL 2.1) on the Pi.

---

Part of **AERA** for Atari: [ECal](https://github.com/whomper/ECal) · [EContact](https://github.com/whomper/EContact) · [EMail](https://github.com/whomper/EMail) · [EFTP](https://github.com/whomper/EFTP) · [EHypView](https://github.com/whomper/EHypView) · [ClaudeST](https://github.com/whomper/ClaudeST) · [EWeb](https://github.com/whomper/EWeb) · [EUpdate](https://github.com/whomper/EUpdate) · 8-bit: [EDOS](https://github.com/whomper/EDOS) · [EFTP_XL](https://github.com/whomper/EFTP_XL) · Pi: [AERA-Gateway](https://github.com/whomper/AERA-Gateway)
