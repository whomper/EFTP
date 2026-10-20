# EFTP user guide

EFTP is an FTP program for the Atari ST, STE, Mega ST/STE, TT and
Falcon. It copies files and whole folders between your Atari and an FTP
server, both ways, and looks after the details a modern FTP program
does: resuming a transfer that stopped, names TOS can take, the files'
dates, text and binary files. Secure servers too: FTPS (FTP over TLS)
and SFTP (over SSH), through a Raspberry Pi on your home network, or
FTPS by the Falcon itself.

## What you need

- An Atari with TOS 1.04 or newer (TOS 4 on the Falcon), EmuTOS, MagiC or
  MiNT, and a screen of at least 640×200 (ST medium); 640×400 (ST high)
  or the Falcon's 640×480 show more of each list
- Free memory when EFTP starts (after your TCP/IP stack and
  accessories): about 350 KB for `EFTPST.PRG` (it runs on a 1 MB ST
  with STinG), about 500 KB for `EFTP.PRG` with Falcon mode. More
  memory makes downloads faster: it holds the file until it is all
  there (see [Moving files](#moving-files))
- Under TOS, Atari's `FOLDR100.PRG` first in your `AUTO` folder,
  renamed for more room (`FOLDR400.PRG`): TOS keeps an entry in a
  small pool for every folder a program has looked into, and halts
  with "OUT OF INTERNAL MEMORY" when it runs out
- A TCP/IP stack:
  - **STinG** on plain TOS (or STiK), or
  - **MiNTnet** under FreeMiNT (with or without GlueSTiK), or MagiC-Net
- A server: on the internet, on your NAS or router, or on a Linux
  computer at home. Plain FTP needs nothing else, on any Atari. For FTPS
  and SFTP servers, see [Secure servers](#secure-servers-ftps-and-sftp):
  a Raspberry Pi gateway (any Atari), or Falcon mode for FTPS (a Falcon
  or TT)

## Installing

The files are in the latest release on EFTP's GitHub page
(github.com/whomper/EFTP/releases), each on its own and all together in
`EFTP-x.y.zip`. There are two editions, as with ECal:

| Your Atari        | Copy                         | Secure servers                       |
|-------------------|------------------------------|--------------------------------------|
| Falcon, TT        | `EFTP.PRG`, `CACERT.PEM`, `ROOTS.DAT` | FTPS by itself (Falcon mode), SFTP through the Pi |
| ST, STE, Mega ST  | `EFTPST.PRG`                 | FTPS and SFTP through the Pi         |

`EFTPST.PRG` is the same program without TLS, about 105 KB smaller: rename it
to `EFTP.PRG` if you like.

EFTP can live in any folder, on any drive: there is no folder it must
be in, and none to make first. A folder of its own keeps its files
together, because EFTP keeps everything in the folder it is started
from (from the desktop, the folder `EFTP.PRG` is in):

```
EFTP.PRG      the program (or EFTPST.PRG)
EFTP.HYP      this guide, for EHypView or ST-Guide (the Help key opens it)
CACERT.PEM    Falcon mode: the root certificates (copy them here)
ROOTS.DAT     Falcon mode: the same, decoded ahead of time
EFTP.INF      settings and sites (each setting has a note above it)
EFTP.KEY      the key the passwords in EFTP.INF are encrypted with
EFTP.LOG      protocol log, when switched on
CRASH.LOG     what happened, if EFTP ever crashes
SEED.DAT      Falcon mode: randomness kept for the next start
```

You copy the program and the guide, and for Falcon mode the two
certificate files; EFTP makes the others when it needs them.
`ROOTS.DAT` only saves time: without it, or when `CACERT.PEM` has
changed, EFTP reads `CACERT.PEM` itself the first time (slower) and
writes a new `ROOTS.DAT`.

To update EFTP, copy the new release's `EFTP.PRG` (or `EFTPST.PRG`),
`EFTP.HYP`, `CACERT.PEM` and `ROOTS.DAT` over the old ones. `EFTP.INF`
and `EFTP.KEY` stay as they are, and with them your settings, sites
and passwords.

Start it. This guide is also in `EFTP.HYP`, a hypertext: with EHypView,
ST-Guide or HypView running (as an accessory, or as a program under
MultiTOS or MagiC), the Help key or **Help > EFTP help** opens it, from
next to `EFTP.PRG` or from the viewer's own help folder. Without a
viewer, Help says how to get one and offers the list of keys instead;
**Help > Keys** shows that list at any time.

The desktop icon, the same monitor as EMail's and ECal's showing a
folder and the two ways files go, is in the release (in the zip, in `icons/`): `EFTP.RSC` for
the resource editors, `EFTP.ICN` and its mask `EFTPMK.ICN`.

## The window

One window, like a modern FTP program:

- **The toolbar** along the top: Connect (Disconnect while connected),
  Upload and Download with arrows in the colours files go in (terracotta
  up to the server, green down to the Atari), the server's name with a
  light that is green while connected, and Stop, red while a transfer
  runs. Rest the mouse on a button to see what it does.
- **This Atari** on the left: the folder you're in, folders first with
  amber folder icons, then the files with their size and date; the
  header says how many there are and how much room the drive has left.
  Above a drive's root is the list of drives.
- **The server** on the right, the same way: the folder on the server,
  with the permissions in the tooltip of each file.
- **Transfers** across the bottom: every file and folder you send, with
  a progress bar and the speed while it goes, then Done (in green),
  Skipped, Stopped or Failed (in red) and why. While a transfer runs,
  its header says how many files and bytes have gone of all of them,
  and the time left for the whole transfer.
- **The info line** under the title: what EFTP is doing, and at the
  end of a transfer how much went in what time.

The dividers between the panes can be dragged, the size box is at the
bottom right, and EFTP remembers both. The pane you clicked last has the
keyboard: its header is dark. Tab moves the keyboard to the next pane.

Where the AES can iconify windows (MultiTOS, MagiC, XaAES, N.AES), the
window has the iconify button: EFTP shrinks to its icon, with how far
the transfer has got under it ("42%"). Transfers go on while it is
iconified.

Click a column title to sort the lists by name, size or date; click it
again for the other way round.

## Connecting

**Connect** in the toolbar lists your sites: click one and EFTP
connects to it. Under them are **Sites...** and **Quick connect...**.

**File > Sites** (^K) opens the Site Manager: your sites on the left,
the chosen one's settings on the right. Double-click a site, or choose
it and press Connect.

| Field          | What to type                                                     |
|----------------|------------------------------------------------------------------|
| Name           | a name for the list, e.g. "Atari archive"                        |
| Server, Port   | the server's name (ftp.example.org) or IP address, port 21 as a rule |
| Protocol       | FTP; FTPS (AUTH TLS, port 21), FTPS implicit (port 990), or SFTP (port 22): see Secure servers; choosing one sets its usual port |
| Log in anonymously | for public archives; no user or password needed                |
| User, Password | your login; the eye button shows the password                    |
| Server folder  | where to start on the server, e.g. `/pub/atari`; empty: where the login starts |
| Atari folder   | where to start on the Atari, e.g. `D:\DOWNLOAD`; the `...` button picks it with the file selector |
| Files go       | by type (text files as text, see below), all as text, or all binary |

New, Copy and Del under the list add, copy and remove sites; Save keeps
the changes without connecting, Cancel forgets them. Passwords are kept
encrypted in EFTP.INF, with a key in EFTP.KEY.

**File > Quick connect** connects to a server without making a site:
its name, a port and protocol, and a login (none: anonymous). Tick "Keep it in the
site list" to make it a site after all.

EFTP uses passive mode, as every modern FTP program does: the Atari
opens every connection itself, so it works from behind any home router.
When a server behind a router gives an address only its own network can
reach, EFTP uses the address it reached the server by.

While you look at things, EFTP tells the server every minute that it's
still there (Settings > Connection), as servers hang up on a quiet line.
If one does, EFTP says so and offers to connect again, to the folder
you were in. **File > Disconnect** (^D) closes the connection.

## Secure servers: FTPS and SFTP

Plain FTP sends everything as it is, the password too. Most servers on
the internet now want **FTPS** (FTP over TLS, the same encryption as
https) or **SFTP** (file transfer over SSH, a different protocol on port
22). An 8 MHz ST can do neither, so EFTP gets there one of two ways,
like EMail and ECal:

```
any Atari:   Atari (EFTP) --plain FTP, home LAN--> Raspberry Pi (eftpgate.py) --FTPS or SFTP--> the server
Falcon, TT:  Falcon (EFTP.PRG, TLS 1.2, RSA on the DSP) --------------------------------FTPS--> the server
```

- **Through the Pi gateway.** `gateway/eftpgate.py` on a Raspberry Pi
  (or any Linux computer at home) takes EFTP's plain FTP and speaks FTPS
  or SFTP to the server. On the Pi, fetch AERA-Gateway
  (github.com/whomper/AERA-Gateway) and set it up with
  `sudo gateway/aera-gateway install ftp` (its README says how, and
  what to do when something doesn't work), then type the Pi's address
  in Options > Settings > Secure, port 2121. Any Atari can use it, a Falcon too, and SFTP always
  goes this way. The Pi goes only to servers on the internet: for one
  on your home network (a NAS, say), install it with `--allow-local`.
  The Pi checks the servers' certificates, and remembers
  an SSH server's key the first time and refuses the server if it
  changes. Everything else is as with FTP: resuming, dates, text files
  (the Pi turns their line ends for SFTP servers, which have no text
  mode), permissions.
- **Falcon mode.** `EFTP.PRG` on a Falcon or TT speaks FTPS itself, TLS
  1.2 with BearSSL, as EMail and ECal do. Switch it on with **Options >
  Falcon mode (TLS)** or in Settings > Secure. It needs a 68020 or
  better (a TT or Falcon) and `CACERT.PEM` next to `EFTP.PRG`, with
  `ROOTS.DAT` to start faster. **Check certificates
  on the DSP** lets the Falcon's DSP56001 do the RSA arithmetic, much
  faster than the 68030. Every list and file goes over its own
  encrypted connection; EFTP resumes the first connection's TLS session
  for them, which saves the slow part of the handshake (and which many
  servers insist on).

Give the site its real server name and choose its Protocol in File >
Sites; EFTP decides the way from the Protocol and the settings. The
status line says which: "Connected to ftp.example.org - TLS 1.2,
ECDHE-RSA, ChaCha20-Poly1305", or "... through the Pi - SFTP", and the
server's name in the toolbar shows it in its tooltip.

In Falcon mode the 68030 decrypts every byte itself, so transfers are
slower than through the Pi. The clock must be right for certificates
to check out.

## Moving files

To copy files or folders from one side to the other:

- double-click a file (or press Return), or
- select some and press **Upload** or **Download** in the toolbar
  (^T sends the selection of the pane with the keyboard), or
- **drag** them to the other pane, or onto the transfers.

Select with a click; Shift+click adds or removes one, Control+click
selects everything from the last one clicked; Space selects the file
under the cursor and moves on; ^A selects everything. A letter jumps to
the next name starting with it.

A folder is copied with everything in it. Before the first file goes,
EFTP lists the folder and every folder in it, all the way down, so the
transfers show every file from the start, and the count and the time
left are right. Then each folder is made on the other side, and its
files follow.

Double-click a folder (or Return) to open it, `..` or Backspace goes up.

While files go, the window stays alive: other windows can be moved and
the progress shows. Esc, Undo or the red Stop button stops the transfer.

A download gathers in memory and goes to the disk in one go when it
has all come, so the disk doesn't hold up the line while it works. EFTP
uses TT RAM when the computer has it; otherwise as much ST RAM as is
free, leaving a little for GEM and your accessories. A file bigger than
that memory goes to the disk each time the memory is full. The log
shows which memory was used ("the file gathers in TT RAM"), and there
is a short pause at 100% while the file is written.

## From other programs

Files and folders handed to EFTP while it runs, by a desktop such as
Thing, jinnee or Gemini (or any program, with the AV protocol's
VA_START), go to the server folder shown, as if dragged there from the
left. Connect first: without a connection EFTP says so. Under MultiTOS,
MagiC or XaAES you can drag them onto EFTP's window too. While a
transfer runs, a drop is refused: drop them again when it is done.

**Copy** (^C, Edit > Copy) puts the full names of the files you chose on
the clipboard, a line each: `C:\GAMES\STARGLD.PRG` on the left,
`/pub/atari/demo.zip` on the server. In dialogs, ^C copies a field, ^X
cuts it and ^V pastes into it (a host name or a long folder, say);
nothing is ever copied out of a password field, but you can paste one
in.

## Names

TOS takes names of 8 letters and an extension of 3, in capitals, and no
spaces. Names coming from the server are made into such names, as
Windows does with long names: `screenshot-001.png` becomes
`SCREEN~1.PNG`, `screenshot-002.png` `SCREEN~2.PNG`, `Cuddly
Demos.st` `CUDDLY~1.ST`. The same server folder always gives the same
names, so a file downloaded again lands on its earlier copy.

Under MiNT or MagiC with a drive that takes long names, switch off
Settings > Names > Shorten long names, and names come as they are.

Going up, the Atari's capitals become small letters, as on most servers
(`README.TXT` becomes `readme.txt`); Settings > Names switches that off.
Names with Hebrew or accented letters are sent in UTF-8 when the server
says it uses it, and shown in the Atari's letters.

## When a file is already there

Settings > Transfers says what happens when a file is already on the
other side:

- **Ask** (the first time): EFTP shows the file, the size of both, and
  asks: **Replace**, **Resume** (go on where the copy there stopped,
  when it is smaller) or **Skip**; tick "The same for the others" to
  answer for the rest of the transfer; Esc stops the transfer there,
  the file left as it is
- **Replace it**, **Resume where it stopped**, **Skip it**: always
- **Replace it if older**: only when the one coming is newer

## Stopping and resuming

Stop (Esc) ends a transfer at once; what has come so far is written to
the disk, and the file shows "Stopped at ..." in the list. **Transfers > Retry the failed ones** goes on where each one
stopped (FTP's REST: only what is missing comes over the line). If the
connection goes in the middle of a transfer, EFTP connects again by
itself and goes on the same way.

**Transfers > Clear the finished ones** and **Clear the list** tidy the
list; Del removes the selected row, and so does its right-click menu.

## Text and binary

Most files are sent as they are (binary). Texts go "as text", in FTP's
ASCII mode: the server changes the line ends as its system wants, and
EFTP adds the Atari's CR to lines that end in LF alone. Which files are
texts goes by their extension, the list in Settings > Transfers (TXT,
ASC, DOC, ME, NFO, DIZ, HTM, HTML, INF at first). A site can send
all its files as text, or all as binary, instead.

## Managing files

The **Local** menu works on this Atari's files (the left pane), the
**Remote** menu on the server's (the right pane), whichever pane has
the keyboard. The right-click menu and the keys work on the pane they
are used in:

- **Upload** (Local) and **Download** (Remote), ^T
- **New folder** (^N)
- **Rename** (^E)
- **Delete** (Del): files, and folders with everything in them, after
  asking
- **Permissions** (^I), on the server: read, write and run for the
  owner, the group and the others, with the number (`644`) as you tick
- **Refresh** (^R, Esc): read the list again (the keys read both)

**Options > Show hidden files** shows names starting with a dot on the
server and files with the hidden attribute on the Atari.

## Dates and the time zone

Copies keep their originals' dates (Settings > Transfers): files coming
down get the date the server has, files going up get theirs on the
server too, when the server can (MFMT). Servers give times in UTC, so
EFTP needs your time zone, by its name, in Settings > Connection:
`Europe/London`, `Asia/Jerusalem`, ... summer time follows by itself.
Under MiNT or MagiC EFTP finds it from TZ when it is set; otherwise it
uses UTC until you choose one.

## Server messages

**Transfers > Server messages** (^L) shows the conversation with the
server in the transfers pane: what EFTP sends in grey, the server's
answers in black, errors in red. ^L again shows the transfers.

## Settings

Options > Settings (^,), in five parts:

- **Transfers**: a file that is already there, keeping dates, a bell
  when a transfer of more than half a minute ends, the extensions sent
  as text
- **Names**: shortening long names to 8+3, lower-case names going up
- **Look**: colours (on screens with 16 colours or more: the selection,
  the folders, the progress), hidden files, folders above files, the
  Hebrew keyboard at start
- **Connection**: the time zone, how long to wait for the server, how
  often to keep the line open, and a protocol log (`EFTP.LOG`, with the
  passwords left out)
- **Secure**: Falcon mode (FTPS on this Atari) and the DSP, and the Pi
  gateway's address and port (2121) for SFTP, and for FTPS without
  Falcon mode

Options > Font picks the font for the lists in the family's Font
dialog: the font families on the left, the chosen one's styles and
sizes beside them (type a size after Pt:), its kind and a sample below.
EFTP lays its lists out in even columns, so only the system font (in
two sizes) and the fixed-width GDOS fonts are listed. A vector font's
Hebrew letters come from the font through NVDI, or from the system
font when it has none. If the Hebrew in a bitmap font shows as other
letters, set HEBFONT= in EFTP.INF: 1 for fonts with ISO-8859-8
Hebrew, 2 for DOS 862, 0 for the Atari's own places.

**Options > Falcon mode (TLS)** switches Falcon mode on and off, the
same as the tick in Settings > Secure, and **Options > Protocol log**
the log, the same as the tick in Settings > Connection.

F10 switches to the Israeli keyboard for typing Hebrew names.

## Keys

| Key            | Does                                           |
|----------------|------------------------------------------------|
| ^K / ^D        | the Site Manager / disconnect                  |
| ^T             | upload or download what is selected            |
| Up, Down       | the file above or below (Shift: a page)        |
| Clr/Home       | the first file (Shift: the last)               |
| Return         | open the folder, send the file                 |
| Backspace      | the folder up                                  |
| Tab            | the next pane                                  |
| ^N / ^E        | new folder / rename                            |
| Del            | delete                                         |
| ^I             | permissions                                    |
| ^A             | select all                                     |
| ^C             | copy the names of the files chosen             |
| ^C ^X ^V       | in a dialog: copy, cut, paste a field          |
| ^R, Esc        | read the lists again                           |
| Esc, Undo      | in a dialog or an alert: Cancel                |
| Space, Insert  | select the file under the cursor and move on   |
| ^L             | server messages / transfers                    |
| ^,             | Settings                                       |
| F10            | Hebrew keyboard                                |
| Help           | this guide, or the keys                        |
| ^Q, ^W         | quit (^W closes the window, EFTP's only one)  |

While EFTP waits for the server (connecting, a list, a transfer), Esc,
Undo or the Stop button stops it.

## When something goes wrong

- **"Can't connect"**: is STinG (or MiNTnet) loaded and working? Try the
  server's IP address instead of its name. Check the port.
- **"Can't log in"**: the user or password; many servers want
  anonymous logins only on some folders.
- **"Can't open the data connection"**: a firewall between you and the
  server blocks the extra connections FTP makes for every list and
  file. EFTP only connects out (passive mode); the server must allow
  that. Options > Settings > Connection > Write a protocol log, then
  `EFTP.LOG` shows the conversation.
- **A transfer stays at 0%**: STinG's own settings (its TCP receive
  window and MTU).
- **Downloads are slow, or slow down as they go**: STinG's TCP receive
  window can be too big for the network card. A NetUSBee holds only
  about four full packets, so a window of 32000 lets the server send
  more at once than it can keep: packets are lost, and the server
  waits before it sends them again. With an MSS of 1460, a window of
  5840 (four packets) was the fastest on a Falcon with a NetUSBee:
  about 40 KB/s, against 14 with 32000 and 25 with 7300.
- **Not enough memory**: EFTPST.PRG needs about 350 KB free when it
  starts, EFTP.PRG about 500 KB for Falcon mode; on a 1 MB machine use
  EFTPST.PRG, and leave out accessories you don't need.
- **"... needs the Raspberry Pi gateway"** or **"FTPS needs Falcon
  mode or the Raspberry Pi gateway"**: the site is FTPS or SFTP; set up
  the gateway (Settings > Secure), or switch on Falcon mode for FTPS.
- **A certificate that "didn't check out"** (Falcon mode, or the Pi's
  log): the Atari's clock, or a server whose certificate is made by its
  own authority (a NAS): through the Pi, `--cafile` (AERA-Gateway's
  README).
- **"... is on the home network"** (through the Pi): the Pi doesn't go
  back into your home network unless it is installed with
  `--allow-local` (AERA-Gateway's README).
- **"... SSH key has changed"**: the server isn't the one the Pi met the
  first time. If it was really reinstalled, remove its line from
  `/var/lib/eftpgate/known_hosts` on the Pi.
- **A crash**: if EFTP itself goes wrong (what would be 2 to 8 bombs),
  it says so, writes what happened to `CRASH.LOG` next to `EFTP.PRG` and
  quits, leaving the Atari running. A crash in the system or a driver
  still gives the bombs; TOS keeps a record of them, and EFTP adds it to
  `CRASH.LOG` the next time it starts (and says so if the crash was in
  EFTP). After a crash EFTP offers, the next time it starts, to show
  what happened. Send `CRASH.LOG`, with `EFTP.LOG` if the log was on, to the author. Under MiNT or MagiC, which
  handle crashes themselves, there is no `CRASH.LOG`.

## EFTP.INF

The settings and sites are kept in `EFTP.INF`, a text file that explains
itself: each setting has a note above it saying what it is and what the
numbers mean. EFTP writes it again whenever settings change, with the
notes. Passwords are encrypted with `EFTP.KEY`; type one in plain text
in `PASS=` and EFTP encrypts it the next time it saves. Without
`EFTP.KEY` the passwords can't be read: type them again in File > Sites.
When EFTP.KEY can't be written (a locked disk, a full one, a folder in its
place), the passwords go into EFTP.INF as they are, and EFTP says so once
each time it starts; once EFTP.KEY can be written, they are encrypted the
next time EFTP saves its settings.
