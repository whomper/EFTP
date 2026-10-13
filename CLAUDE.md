# Notes for working on EFTP

- EFTP shares its runtime (`atari/`), its connection layer, charsets,
  time zones and secrets (`src/`) and its GEM building blocks (`ui/win.c`,
  `draw.c`, `dialogs.c`, `popup.c`, `font.c`, `about.c`, `theme.c`) with
  EMail (whomper/EMAIL) and ECal; keep the look and the conventions the
  same: one window with panes, EMail's Settings sidebar, `EFTP.INF` with
  a note above every setting, Claude ST's About box and the SM124 icon.
- **Every new build gets a new version.** `make` counts up the minor
  number in `VERSION` each time it links `binary/EFTP.PRG`; commit
  `VERSION` with the program, and never reuse a number for a different
  program.
- Before pushing: `make test`, `make itest` (pip install pyftpdlib) and,
  when the Atari side changes, `tests/hatari/net_test.py` in Hatari with
  EmuTOS. `make help HCP=...` rebuilds `binary/EFTP.HYP` when
  `docs/GUIDE.md` changes.
- The small C library has no `%o` or floating point: format octal by hand
  and keep `double` out (it pulls in libgcc code that needs a GOT).
- Memory is short on a 1 MB ST with STinG: keep big buffers out of BSS and
  the heap (the control connection reads 2 KB at a time, data 8 KB).
