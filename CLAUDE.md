# Notes for working on EFTP

- EFTP shares its runtime (`atari/`), its connection layer, charsets,
  time zones and secrets (`src/`) and its GEM building blocks (`ui/win.c`,
  `draw.c`, `dialogs.c`, `popup.c`, `font.c`, `about.c`, `theme.c`) with
  EMail (whomper/EMail) and ECal; keep the look and the conventions the
  same: one window with panes, EMail's Settings sidebar, `EFTP.INF` with
  a note above every setting, Claude ST's About box and the SM124 icon.
- **Every new build gets a new version.** `make` counts up the minor
  number in `VERSION` each time it links `binary/EFTP.PRG`; commit
  `VERSION` with the program, and never reuse a number for a different
  program.
- Two editions, as ECal: `EFTP.PRG` (BearSSL, Falcon mode, for Falcon
  and TT) and `EFTPST.PRG` (`src/tls_none.c`, through the Pi gateway
  only, for a 1 MB ST). The TLS code (`src/tls.c`) is EMail's and ECal's;
  keep it in step with them.
- **`common/` is shared AERA code** (BearSSL, the DSP RSA, bidi, dates,
  passwords, libc headers, build tools), a git subtree of
  github.com/whomper/AERA-Atari. Don't edit it here: change it in
  AERA-Atari, then run `tools/sync-common.sh`. Follow the AERA Atari
  app guidelines (AERA-Atari, `standards/ATARI-APP-GUIDELINES.md`).
- FTPS data connections: the TLS handshake comes after the 125/150
  reply (before it, servers deadlock), and resumes the control
  connection's session (vsftpd's require_ssl_reuse).
- `gateway/eftpgate.py` is standard Python plus paramiko (Raspberry Pi
  OS: python3-paramiko); keep it that way, like ECal's gateway.
- Before pushing: `make test`, `make itest` (pip install pyftpdlib
  pyopenssl paramiko) and, when the Atari side changes,
  `tests/hatari/net_test.py` (4 MB, and `EFTP_PRG=EFTPST.PRG` on 1 MB)
  and `tests/hatari/secure_test.py` in Hatari with EmuTOS. `make help
  HCP=...` rebuilds `binary/EFTP.HYP` when `docs/GUIDE.md` changes.
- The small C library has no `%o` or floating point: format octal by hand
  and keep `double` out (it pulls in libgcc code that needs a GOT).
- Memory is short on a 1 MB ST with STinG: keep big buffers out of BSS and
  the heap (the control connection reads 2 KB at a time, data 8 KB).
- **The name** reads "AERA | Erez Yaary" wherever it is given: copyright
  lines (`Copyright (C) 2026 AERA | Erez Yaary`), the About box, the help
  file's `@author`, the docs, and as the author of every commit:
  `git commit --author="AERA | Erez Yaary <yaary@me.com>"`, with Claude as
  the signing committer (AERA-Atari, `standards/GITHUB-IDENTITY.md`).
