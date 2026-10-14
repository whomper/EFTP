#!/bin/bash
# Set up a Raspberry Pi (or any Debian/Ubuntu/Raspberry Pi OS computer)
# as EFTP's gateway: the Atari speaks plain FTP to the Pi on your home
# network, and eftpgate.py speaks FTPS (FTP over TLS) or SFTP (SSH) to
# the servers.
#
#   sudo ./install.sh                         anyone on the home network may use it
#   sudo ./install.sh --allow 192.168.1.50    only the Atari at that address
#   sudo ./install.sh --port 2122             another port (2121 is the default)
#   sudo ./install.sh --allow-local           also reach servers at home (a NAS)
#
# Run it again to change the settings; `sudo ./install.sh --remove` takes it away.
set -euo pipefail

PORT=2121
PASV=30000:30099      # the data connections' ports
ALLOW_IP=""
ARGS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --allow) ALLOW_IP=${2:?--allow needs the address of the Atari}; ARGS+=(--allow "$ALLOW_IP"); shift 2 ;;
    --port) PORT=${2:?--port needs a number}; shift 2 ;;
    --allow-local) ARGS+=(--allow-local); shift ;;
    --remove)
      systemctl disable --now eftpgate.service 2>/dev/null || true
      rm -f /etc/systemd/system/eftpgate.service /usr/local/bin/eftpgate.py
      systemctl daemon-reload
      echo "eftpgate removed (the SSH keys it remembered stay in /var/lib/eftpgate)."
      exit 0 ;;
    *) echo "usage: install.sh [--allow ATARI_IP] [--port N] [--allow-local] [--remove]" >&2; exit 2 ;;
  esac
done

if [ "$(id -u)" != 0 ]; then
  echo "Please run with sudo." >&2
  exit 1
fi
# Python, the root certificates for checking FTPS servers, paramiko for SFTP
NEED=()
command -v python3 >/dev/null || NEED+=(python3)
[ -f /etc/ssl/certs/ca-certificates.crt ] || NEED+=(ca-certificates)
python3 -c "import paramiko" 2>/dev/null || NEED+=(python3-paramiko)
if [ ${#NEED[@]} -gt 0 ]; then
  apt-get update -qq
  apt-get install -y -qq "${NEED[@]}"
fi

HERE=$(cd "$(dirname "$0")" && pwd)
install -m 755 "$HERE/eftpgate.py" /usr/local/bin/eftpgate.py
id eftpgate >/dev/null 2>&1 || useradd --system --no-create-home --shell /usr/sbin/nologin eftpgate

cat > /etc/systemd/system/eftpgate.service <<UNIT
[Unit]
Description=EFTP gateway for the Atari (FTPS and SFTP)
After=network-online.target
Wants=network-online.target

[Service]
User=eftpgate
ExecStart=/usr/bin/python3 /usr/local/bin/eftpgate.py --port $PORT --pasv-ports ${PASV/:/-} ${ARGS[*]:-}
StateDirectory=eftpgate
Restart=always
RestartSec=5
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes

[Install]
WantedBy=multi-user.target
UNIT

systemctl daemon-reload
systemctl enable eftpgate.service >/dev/null
systemctl restart eftpgate.service

if command -v ufw >/dev/null && ufw status | grep -q "Status: active"; then
  # the port, and the data connections' ports
  for FROM in ${ALLOW_IP:-192.168.0.0/16 10.0.0.0/8}; do
    ufw allow from "$FROM" to any port "$PORT" proto tcp >/dev/null
    ufw allow from "$FROM" to any port "$PASV" proto tcp >/dev/null
  done
fi

sleep 1
IP=$(hostname -I 2>/dev/null | awk '{print $1}')
IP=${IP:-PI-ADDRESS}
if systemctl is-active --quiet eftpgate.service; then
  echo
  echo "eftpgate is running. In EFTP on the Atari, Options > Settings > Secure:"
  echo "   Pi gateway:  $IP"
  echo "   Port:        $PORT"
  echo
  echo "Then give your FTPS and SFTP sites their real server names in File > Sites."
else
  echo "eftpgate didn't start; see: journalctl -u eftpgate" >&2
  exit 1
fi
