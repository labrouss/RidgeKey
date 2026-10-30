#!/bin/bash
# RidgeKey installer - run from an extracted bundle:   sudo ./install.sh
#
# Installs:
#   /usr/local/ridgekey/{bin,lib}                       daemon, client, bundled libraries
#   /usr/local/bin/ridgekeyctl, ridgekey-sudo           symlinks
#   /usr/local/ridgekey/lib/pam/pam_ridgekey.so         PAM module (sudo is NOT changed - run
#                                                       'sudo ridgekey-sudo enable' for that)
#   /Library/LaunchDaemons/io.github.labrouss.ridgekey.plist
#   /Library/LaunchAgents/io.github.labrouss.ridgekey.agent.plist   lock-screen agent (per user)
#   /Library/Logs/RidgeKey/                             daemon log
#   /Library/Application Support/RidgeKey/prints/       print record (created by the daemon, 0700)
# Re-running upgrades in place; the print record is kept.
set -euo pipefail

LABEL="io.github.labrouss.ridgekey"
AGENT="$LABEL.agent"
AGENT_PLIST="/Library/LaunchAgents/$AGENT.plist"
CONSOLE_UID="$(stat -f %u /dev/console)"
DEST="/usr/local/ridgekey"
PLIST="/Library/LaunchDaemons/$LABEL.plist"
SRC="$(cd "$(dirname "$0")" && pwd)"

[ "$(id -u)" = 0 ] || { echo "Run with sudo: sudo $0"; exit 1; }
[ -x "$SRC/bin/ridgekeyd" ] && [ -f "$SRC/launchd/$LABEL.plist" ] \
  || { echo "Run this from the extracted RidgeKey bundle (bin/ridgekeyd not found next to it)"; exit 1; }

echo "==> Checking the bundle"
"$SRC/bin/ridgekeyd" --version

if [ "$CONSOLE_UID" != 0 ] && launchctl print "gui/$CONSOLE_UID/$AGENT" >/dev/null 2>&1; then
  echo "==> Stopping the lock-screen agent"
  launchctl bootout "gui/$CONSOLE_UID/$AGENT" 2>/dev/null || true
fi
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
install -m 755 "$SRC/ridgekey-sudo" "$DEST.new/bin/ridgekey-sudo"
chown -R root:wheel "$DEST.new"
chmod -R go-w "$DEST.new"
chmod 444 "$DEST.new/lib/pam/pam_ridgekey.so"
xattr -dr com.apple.quarantine "$DEST.new" 2>/dev/null || true
# Swap in one step: if sudo_local already uses pam_ridgekey.so, the module path must never
# be missing (OpenPAM fails the whole sudo stack when a listed module cannot be loaded).
if [ -d "$DEST" ]; then
  rm -rf "$DEST.old"; mv "$DEST" "$DEST.old"; mv "$DEST.new" "$DEST"; rm -rf "$DEST.old"
else
  mv "$DEST.new" "$DEST"
fi

mkdir -p /usr/local/bin
ln -sf "$DEST/bin/ridgekeyctl" /usr/local/bin/ridgekeyctl
ln -sf "$DEST/bin/ridgekey-sudo" /usr/local/bin/ridgekey-sudo

mkdir -p /Library/Logs/RidgeKey
chmod 755 /Library/Logs/RidgeKey

echo "==> Installing launchd job $LABEL"
install -o root -g wheel -m 644 "$SRC/launchd/$LABEL.plist" "$PLIST"
launchctl bootstrap system "$PLIST"
launchctl kickstart -k "system/$LABEL" 2>/dev/null || true
sleep 1

echo "==> Installing the lock-screen agent $AGENT"
install -o root -g wheel -m 644 "$SRC/launchd/$AGENT.plist" "$AGENT_PLIST"
if [ "$CONSOLE_UID" != 0 ]; then
  launchctl bootout "gui/$CONSOLE_UID/$AGENT" 2>/dev/null || true
  # Loading into a user's GUI domain from root can fail right after install (seen on Tahoe);
  # doing it as that user works, so fall back to that.
  if ! launchctl bootstrap "gui/$CONSOLE_UID" "$AGENT_PLIST" 2>/dev/null; then
    launchctl asuser "$CONSOLE_UID" sudo -u "#$CONSOLE_UID" \
      launchctl bootstrap "gui/$CONSOLE_UID" "$AGENT_PLIST" \
      || echo "    could not load the agent now - it will start at your next login"
  fi
  launchctl kickstart -k "gui/$CONSOLE_UID/$AGENT" 2>/dev/null || true
  sleep 1
  if launchctl print "gui/$CONSOLE_UID/$AGENT" 2>/dev/null | grep -q 'state = running'; then
    echo "    agent running"
  else
    echo "    WARNING: agent not running - see: launchctl print gui/$CONSOLE_UID/$AGENT"
  fi
fi

echo "==> Status"
/usr/local/bin/ridgekeyctl status || {
  echo "ridgekeyd did not answer - see /Library/Logs/RidgeKey/ridgekeyd.log"; exit 1; }

cat <<EOF

Installed. Try (as your normal user, no sudo):
  ridgekeyctl enroll right-index
  ridgekeyctl verify
  ridgekeyctl list
Fingerprint for sudo (optional, password still works):  sudo ridgekey-sudo enable
Lock screen: 1) ridgekeyctl password set
             2) System Settings -> Privacy & Security -> Accessibility -> enable ridgekey-agent
                (after an upgrade, remove it with "-" and add it again with "+"; it is in
                 /usr/local/ridgekey/bin)
             3) lock the screen (Ctrl-Cmd-Q) and touch the sensor
Log: /Library/Logs/RidgeKey/ridgekeyd.log      Uninstall: sudo ./uninstall.sh [--purge]
EOF
