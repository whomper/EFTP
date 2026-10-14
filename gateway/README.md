# The Raspberry Pi gateway

Most FTP servers on the internet now want FTPS (FTP over TLS) or SFTP
(file transfer over SSH), and an 8 MHz ST can do neither. So EFTP speaks
plain FTP to a Raspberry Pi on your home network, and the Pi speaks FTPS
or SFTP to the server. The Pi runs `eftpgate.py`, a small program in
Python 3 (with paramiko for SSH), set up as a service by `install.sh`.

```
Atari (EFTP) --plain FTP, home LAN--> Pi :2121 (eftpgate.py) --FTPS--> ftp.example.org
                                                            --SFTP--> my.server.net
```

Like ECal's gateway (and unlike EMail's stunnel to one mail server),
eftpgate can reach any server: the Atari names it in the login, the way
FTP proxies have always done it,

```
USER sftp:erez@my.server.net:22         also ftps:  and  ftps-implicit:
PASS (the server's own password)
```

and EFTP does that for you: a site's Protocol in File > Sites says which.
To the Atari, eftpgate is an ordinary FTP server that does everything
EFTP likes (exact sizes and UTC times in MLSD, REST to resume both ways,
MFMT to keep dates, UTF-8 names, passive mode). On the way it:

- checks FTPS servers' certificates against the Pi's root certificates,
  and resumes the TLS session on every data connection (vsftpd, FileZilla
  Server and others insist on that);
- remembers an SSH server's key the first time (in
  `/var/lib/eftpgate/known_hosts`) and refuses the server if the key
  changes later; if the server was really reinstalled, remove its line
  there;
- turns line ends for text files on SFTP, which has no text mode: CR LF
  on the Atari, LF on the server;
- keeps the server's connection awake while EFTP's keeps the Pi's.

Any computer with Debian, Ubuntu or Raspberry Pi OS works, and the same
Pi can run EMail's and ECal's gateways and Claude ST's bridge too.

Falcon mode doesn't need it for FTPS: EFTP.PRG on a Falcon or TT does
TLS itself. SFTP always goes through the Pi.

## Setting it up

On the Pi:

```
git clone https://github.com/whomper/EFTP
cd EFTP/gateway
sudo ./install.sh
```

It installs eftpgate as `/usr/local/bin/eftpgate.py` (and
`python3-paramiko` if it isn't there), runs it as the `eftpgate` service
(now and at every boot) and prints the address to type into EFTP.

Options:

```
sudo ./install.sh --allow 192.168.1.50    only the Atari at that address may use it
sudo ./install.sh --port 2122             another port
sudo ./install.sh --allow-local           also reach servers on your home network (a NAS)
sudo ./install.sh --remove                take it away
```

Run it again to change the options. `journalctl -u eftpgate` shows its
messages; add `-v` to the `ExecStart` line in
`/etc/systemd/system/eftpgate.service` to log every command (never the
passwords). A NAS whose FTPS certificate comes from its own CA: copy the
CA's certificate to the Pi and add `--cafile /path/to/ca.pem` there.

## In EFTP

Options > Settings > **Secure**: Pi gateway = the Pi's IP address (plain
TOS with STinG can't always look up names on the local network), port
2121. Then give your sites their real server names and choose FTPS or
SFTP as their Protocol in File > Sites: EFTP goes through the Pi for
those, and straight to the server for plain FTP.

## Keeping it private

Between the Atari and the Pi nothing is encrypted, as on every network in
the 1990s: keep the gateway on your home network and never forward its
port on your router. eftpgate answers only addresses on the private
networks (192.168.x.x, 10.x.x.x, 172.16-31.x.x), or with `--allow` only
your Atari; it refuses to connect onward to your own network unless
`--allow-local` is given, so it can't be used to reach other machines at
home. Data connections use ports 30000-30099; if the Pi's firewall (ufw)
is on, install.sh opens those and 2121 for the home network (or just the
`--allow` address).

## Testing

```
pip install paramiko pyftpdlib pyopenssl
python3 -m unittest discover -s gateway -p 'test_*.py'
```

starts an SFTP server and an FTPS server on the computer, eftpgate in
front of them, and talks to it as the Atari does (with EFTP's own FTP
core too, when `make` has built `build/ftp-cli`). `make itest` runs it.
