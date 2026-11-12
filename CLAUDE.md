# Notes for working on EFTP

EFTP is one of the AERA Atari apps (ECal, EContact, EMail, EFTP, EHypView, ClaudeST). They are meant to
look and behave as one family.

## For every AERA app

These come from AERA-Atari's repo template and are the same in every app's CLAUDE.md.

- **Follow the AERA Atari app guidelines** ([ATARI-APP-GUIDELINES.md](https://github.com/whomper/AERA-Atari/blob/main/standards/ATARI-APP-GUIDELINES.md)). Pay most
  attention to:
  - About box
  - family shortcuts
  - PRG/ACC rules
  - context menus
  - VA_START, help, alerts
  - INF location

  When a change touches one of these, do it the guideline's way, even if the old code here did it
  differently.
- **Speed is set by the stock machines** (Erez, 26 Oct 2026; the guidelines' "Speed"): an 8 MHz ST
  and a stock Falcon (`ST`, `ST-1`, `Falcon-4`), not the DFB1. Design and code for them: draw only
  what changed, keep the 68000's slow operations out of inner loops, do work once. A change to
  drawing, a loop over the data or the start gives its `AESGAP` stretches on `ST` and `Falcon-4`,
  before and after, in its commit message. A GEM release passes the speed rig within the
  guidelines' times.
- **Shared code lives in [AERA-Atari](https://github.com/whomper/AERA-Atari).** Files under `common/` come from it. Change them
  there, not here, and pull them in with `tools/sync-common.sh`. A fix made only in this repo will drift
  again.
- **Build and test before every push:** `make` with no warnings and `make test`, plus this repo's
  own checks (below): `make itest` when network code changed, the Hatari rigs in `tests/hatari/` when
  the GEM side changed.
- **Versions and releases:** `VERSION` changes only in a release commit. A release: `tools/bump.py`,
  commit, push, then run the Release workflow on `main` with the notes (or push a tag `vX.Y` made with
  `git tag -a`); it tests, builds, tags vX.Y and publishes each file and the zip as a
  GitHub Release. A normal build must leave every tracked file unchanged and shows the version in `VERSION` as it is (there is no `-dev` label). Don't
  commit built programs, help files or certificates (`binary/`, `dist/`). Never reuse a number.
- **Only formal releases leave the build machine.** Erez gets vX.Y releases made by the Release workflow from `main`, nothing else: no test builds, no CI artifacts, no builds from a branch. Work lands on `main`; to try a fix on real hardware, release it. If a release doesn't work, roll back with a release: revert on `main`, release the next number, and say in the bad release's notes that it is withdrawn and which one to use.
- **Releases go out without asking Erez** (his decision, 23 Oct 2026; guidelines 1.2). Release when
  work on `main` is ready:
  - this repo's checks pass;
  - the rigs on his TOS pass, where this file asks for them;
  - the hub's family checks pass.

  Then give Erez the link in one line, with anything he has to do, and send the hub the link. Roll
  back a bad release the same way. Decisions that are his still go to him first.
- **Who decides what** (Erez, 31 Oct 2026; the guidelines' "Who decides what"). Erez's part, put
  to him before the work as a decision with a short context:
  - new programs and ideas, their core capabilities, and how they work with the other programs;
  - the user experience: the look, the layout, the way through a task, the words, the icons, the
    defaults;
  - phases: work stays inside the phase he approved; at its end, one phase report (what it set out
    to do, what was done with the release links, what is left, the next phase as a proposal), and
    the next phase starts when he says so;
  - priorities and conflicts between programs or sessions (through the hub, with its options and
    recommendation);
  - packages on the Pi and ports.

  How it is made is the team's, without asking: code, structure, shared code, tests, measurements,
  fixes, docs and releases inside an approved phase.
- **No test runs with another AERA program or accessory** (Erez, 8 Nov 2026; guidelines 1.2). EFTP's
  own tests run EFTP alone: nothing else of the family is loaded beside it, started by it or fetched
  for it (no `getrelease.py`). What it does with another program is tested against a stand-in
  (the AV test desktop `THING.ACC`, built from `common/tests/av`); the real programs together are the
  hub's family checks (`acc_all.py`, joint scenarios). `tests/hatari/rig.py` ignores `AERA_WITH_ACCS`.
- **Every new source file starts with the SPDX line and copyright** (see any `.c` file); a generator
  writes them into the files it makes.
- **The name** reads "AERA | Erez Yaary" wherever it is given: copyright lines
  (`Copyright (C) 2026 AERA | Erez Yaary`), the About box, the help file's `@author`, the docs, and as
  the author of every commit: `git commit --author="AERA | Erez Yaary <yaary@me.com>"`, with Claude as
  the signing committer, so GitHub shows the commits as Verified (AERA-Atari,
  `standards/GITHUB-IDENTITY.md`).
- **Never commit:**
  - real addresses from the owner's network: use `192.168.1.x`, `PI_IP`, `ATARI_IP`
  - passwords, session keys, `.KEY` / `.INF` files from a test run
  - photos that still carry EXIF/GPS data
- **README:** keep the section order from the template (Highlights, Getting started, How it reaches the
  servers, Building, Source, Tests, Licence, family footer). Use British English.

## Talking with Erez

Erez's rule for the whole family (18 Oct 2026): no progress or in-between messages. Write to him
only when he has to decide something, with a short context and the decision. Findings: the
information, your assessment and what he should do, nothing more. Messages to the hub stay complete,
with the evidence.

## EFTP

- EFTP shares a lot with EMail (whomper/EMail) and ECal: its runtime,
  connection layer, charsets, time zones and secrets come from `common/`
  (only `atari/plat_tos.c` is its own), and its GEM building blocks
  (`ui/win.c`, `draw.c`, `dialogs.c`, `font.c`, `about.c`, `theme.c`)
  follow EMail's. Keep the look and the conventions the same: one window
  with panes, EMail's Settings sidebar, `EFTP.INF` with a note above
  every setting, the family's About box and its icon (`common/icons/EFTP.txt`,
  `make icons`).
- **A release** carries `EFTP.PRG`, `TLS.OVL`, `EFTP.HYP` (hcp from `common/tools/build-hcp.sh`),
  `CACERT.PEM`, `ROOTS.DAT` and the icon. The icons are committed in `icons/` (`make icons`).
- One `EFTP.PRG` for every Atari (guidelines 7.4): the TLS of a Direct
  connection (the user never sees "Falcon mode"; the INF keeps `FALCON=`)
  is the family's `TLS.OVL`, which a TT or Falcon loads when needed. It
  is built in AERA-Atari (`common/tlsovl/TLS.OVL`), copied to `binary/`,
  never built here; the host tests link `common/src/tls.c` and BearSSL.
  Keep the program small enough for a 1 MB ST with STinG.
- FTPS data connections: the TLS handshake comes after the 125/150
  reply (before it, servers deadlock), and resumes the control
  connection's session (vsftpd's require_ssl_reuse).
- `gateway/` is a read-only copy of
  [AERA-Gateway](https://github.com/whomper/AERA-Gateway)'s (eftpgate,
  the other gateways, `aera-gateway`): change it there, with that
  repository's owner session, and bring it here with
  `tools/sync-gateway.sh`. EFTP decides eftpgate's protocol (the login
  form, FTPS session reuse, SFTP and known_hosts, ports 2121 and
  30000-30099): review changes to it there. eftpgate is standard Python
  plus paramiko (Raspberry Pi OS: python3-paramiko).
- Before pushing: `make test`, `make itest` (pip install pyftpdlib
  pyopenssl paramiko) and, when the Atari side changes,
  `tests/hatari/net_test.py` and `tests/hatari/secure_test.py` in
  Hatari on the family's machines (`common/tools/machines.py`), all
  running EFTP.PRG: secure_test's ftps and noovl on the Falcon or TT,
  sftp and net_test on `ST-1` too. `make help
  HCP=...` rebuilds `binary/EFTP.HYP` when `docs/GUIDE.md` changes.
- The speed rig, `tests/hatari/speed.py WORK MACHINE`, builds
  `make PROFILE=20 build/prof/EFTP.PRG` and prints each action's longest
  stretch between event calls, on `ST:mono`, `ST:med` and `Falcon-4:16`
  (`EFTP_PROF_PRG=` runs an earlier such build, for before and after).
  Text goes on multiples of 8 (`text_x()`, `win_x0()`): TOS draws the
  system font 6 times as fast there.
- The small C library has no `%o` or floating point: format octal by hand
  and keep `double` out (it pulls in libgcc code that needs a GOT).
- Memory is short on a 1 MB ST with STinG: keep big buffers out of BSS and
  the heap (the control connection reads 2 KB at a time, data 8 KB).

## Maintenance mode (Erez, 1 Nov 2026)

EFTP is in maintenance mode while EWeb, EPDF and EPlay go to 1.0 (AERA-Atari, ATARI-APP-GUIDELINES.md, section 8). Until Erez ends it:
- No new features, speed rounds, clean-ups, or catching up with new or changed guidelines.
- No sync of the shared code (`common/`, `xl/`, `gateway/`). Shared code moving is no reason to rebuild or release.
- A release only for:
  - a security or data-loss fault;
  - a crash or breakage Erez reports;
  - something outside that stops it working (a certificate, a server);
  - something EWeb, EPDF or EPlay need from it.

  Such a release takes only that fix (one change from AERA-Atari, not a sync), and its notes say why.
- Work on `main` that isn't released stays unreleased.
- Nothing runs in the background; the session waits for Erez or the hub.
- Only Erez's explicit word ends this mode, for instance that every app is to take the latest shared code and release. The hub passes it on quoting him.

## UI backlog from Erez's decisions of 9 Nov 2026

**State, 12 Nov 2026: released in 2.5** (maintenance was lifted for it and is back since; the hub, 12 Nov)
(093097693, 9443153d5): 1-10, 11, 12, 13, 14, 15, 16's keyboard reach in the pick list and the window
arrows. Left: 16's keyboard reach in the other dialogs (a focus ring) and `form_loop` onto `dlg_do`
(C11): both wait for hub code (the shared dialog loop, the password eye and long fields in `dlg_do`);
the hub says when, and they go in with the next release made anyway.

AERA-Atari a261597 (`standards/ui/DECISIONS-2026-11-09.md`, MENUS/CONTROLS/WINDOWS, DEVIATIONS rows).
Not work to start while in maintenance mode: take it with a release made anyway (a fault fix or one
Erez asked for), the S items freely, the H and M items only when that release is otherwise untouched.
Each done item: one line to the hub with the commit. "Hub first" = wait until the shared code is in
`common/`.

1. `Write a log`, in the menu and in Settings (D8, MD3). S.
2. `Direct connection`, without `(TLS)` (D9, MD6). S.
3. `Sites... ^K` moves to Options, before Settings; File keeps Quick connect, Disconnect, Quit (D13, MD27).
4. The file pane's context menu says `Sites...`, not `Connect... ^K` (D14, MD19). S.
5. Already there: default Skip (D29, C17); buttons 10 wide (D26, C2). S.
6. About tagline "File transfer for the ST, TT and Falcon" (39; Erez approved it 9 Nov) (D49, W22). S.
7. `12 KB of 3.4 MB`, `45%`, `2 min left` (D35, W4); the Transfers rows' `100/200KB` stays until decided. S.
8. Shift+Up/Down in the file panes page by rows - 1 (W6). S, hub first (key_shift).
9. One minimum window size: the open check (40 x 16) and `WM_SIZED` (50 x 16) agree (D38, W10). S.
10. Insert opens the context menu; Space still ticks (D16, MD20).
11. While a run goes, the commands that would start another are greyed and their keys do nothing;
    Stop stays live (D19).
12. A black title band in every dialog: Already there, New folder, Rename, pick lists (D22, C4). H.
13. Pop-ups open with the ticked choice over the button (D27, C8); the padding goes once `popup()` adds
    its margin (D15, MD18). S, hub first.
14. One bar per running row drawn with the shared `progbar`, in the shared colours (D48, C22). M.
15. INF `WINDOW=x,y,w,h`, `PANES=`, `FONT=name,points`, the old keys read once (D43, D44, W18). M.
    `SORT=` stays.
16. Keyboard reach in dialogs (D25, C6) M; own `form_loop` onto `dlg_do` (C11) H; window arrows to the
    pane under the pointer (D37). All hub first.

Settled, nothing to do: Stop acts on the press during a run (D32), the toolbar look (D33), `-DDLG_CLICK`.
