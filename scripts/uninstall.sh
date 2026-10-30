#!/bin/bash
# RidgeKey uninstaller:   sudo ./uninstall.sh [--purge]
#
#   (default)  stop and remove the daemon and binaries; keep the print record and the prints
#              on the sensor (a reinstall picks them up again)
#   --purge    first delete every RidgeKey print from the sensor, one by one, then remove the
#              record too. Windows Hello / BIOS prints are never touched.
set -euo pipefail

LABEL="io.github.labrouss.ridgekey"
DEST="/usr/local/ridgekey"
PLIST="/Library/LaunchDaemons/$LABEL.plist"
DATA="/Library/Application Support/RidgeKey"
PURGE=0
[ "${1:-}" = "--purge" ] && PURGE=1

[ "$(id -u)" = 0 ] || { echo "Run with sudo: sudo $0 ${1:-}"; exit 1; }

if [ $PURGE = 1 ]; then
  if launchctl print "system/$LABEL" >/dev/null 2>&1 && [ -x "$DEST/bin/ridgekeyctl" ]; then
    echo "==> Deleting RidgeKey prints from the sensor"
    "$DEST/bin/ridgekeyctl" purge --yes || {
      echo "Purge failed - keeping everything so it can be retried. See /Library/Logs/RidgeKey/ridgekeyd.log"
      exit 1; }
  else
    echo "ridgekeyd is not running - cannot delete prints from the sensor. Reinstall, then run --purge again."
    exit 1
  fi
fi

echo "==> Stopping and removing the daemon"
launchctl bootout "system/$LABEL" 2>/dev/null || true
rm -f "$PLIST"
rm -rf "$DEST"
[ -L /usr/local/bin/ridgekeyctl ] && rm -f /usr/local/bin/ridgekeyctl

if [ $PURGE = 1 ]; then
  rm -rf "$DATA" /Library/Logs/RidgeKey
  echo "Removed RidgeKey and its prints."
else
  echo "Removed RidgeKey. Kept the print record in '$DATA' (use --purge to remove prints too)."
fi
