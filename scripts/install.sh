#!/bin/bash
# RidgeKey installer - run from an extracted bundle:   sudo ./install.sh
#
# Installs:
#   /usr/local/ridgekey/{bin,lib}                       daemon, client, bundled libraries
#   /usr/local/bin/ridgekeyctl                          symlink
#   /Library/LaunchDaemons/io.github.labrouss.ridgekey.plist
#   /Library/Logs/RidgeKey/                             daemon log
#   /Library/Application Support/RidgeKey/prints/       print record (created by the daemon, 0700)
# Re-running upgrades in place; the print record is kept.
set -euo pipefail

LABEL="io.github.labrouss.ridgekey"
DEST="/usr/local/ridgekey"
PLIST="/Library/LaunchDaemons/$LABEL.plist"
SRC="$(cd "$(dirname "$0")" && pwd)"

[ "$(id -u)" = 0 ] || { echo "Run with sudo: sudo $0"; exit 1; }
[ -x "$SRC/bin/ridgekeyd" ] && [ -f "$SRC/launchd/$LABEL.plist" ] \
  || { echo "Run this from the extracted RidgeKey bundle (bin/ridgekeyd not found next to it)"; exit 1; }

echo "==> Checking the bundle"
"$SRC/bin/ridgekeyd" --version

if launchctl print "system/$LABEL" >/dev/null 2>&1; then
  echo "==> Stopping the running daemon"
  launchctl bootout "system/$LABEL" 2>/dev/null || true
  sleep 1
fi

echo "==> Installing to $DEST"
rm -rf "$DEST.new"
mkdir -p "$DEST.new"
cp -R "$SRC/bin" "$SRC/lib" "$DEST.new/"
[ -d "$SRC/licenses" ] && cp -R "$SRC/licenses" "$DEST.new/"
cp "$SRC/BUILDINFO.txt" "$DEST.new/" 2>/dev/null || true
rm -rf "$DEST" && mv "$DEST.new" "$DEST"
chown -R root:wheel "$DEST"
chmod -R go-w "$DEST"
xattr -dr com.apple.quarantine "$DEST" 2>/dev/null || true

mkdir -p /usr/local/bin
ln -sf "$DEST/bin/ridgekeyctl" /usr/local/bin/ridgekeyctl

mkdir -p /Library/Logs/RidgeKey
chmod 755 /Library/Logs/RidgeKey

echo "==> Installing launchd job $LABEL"
install -o root -g wheel -m 644 "$SRC/launchd/$LABEL.plist" "$PLIST"
launchctl bootstrap system "$PLIST"
launchctl kickstart -k "system/$LABEL" 2>/dev/null || true
sleep 1

echo "==> Status"
/usr/local/bin/ridgekeyctl status || {
  echo "ridgekeyd did not answer - see /Library/Logs/RidgeKey/ridgekeyd.log"; exit 1; }

cat <<EOF

Installed. Try (as your normal user, no sudo):
  ridgekeyctl enroll right-index
  ridgekeyctl verify
  ridgekeyctl list
Log: /Library/Logs/RidgeKey/ridgekeyd.log      Uninstall: sudo ./uninstall.sh [--purge]
EOF
