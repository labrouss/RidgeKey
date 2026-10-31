#!/bin/bash
# Feasibility test: can a ROOT launchd daemon (no GUI session, like ridgekeyd) type into the
# lock screen? Runs ridgekey-typetest once as a temporary system job, then removes it.
# It types the dummy word "ridgekey" (8 letters) and never presses Return.
#
#   sudo ./typetest-as-daemon.sh        then lock the screen (Ctrl-Cmd-Q) within 15 seconds
set -euo pipefail
LABEL="io.github.labrouss.ridgekey.typetest"
BIN="/usr/local/ridgekey/bin/ridgekey-typetest"
LOG="/tmp/ridgekey-typetest.log"
PLIST="$(mktemp /tmp/ridgekey-typetest.XXXXXX).plist"

[ "$(id -u)" = 0 ] || { echo "Run with sudo"; exit 1; }
[ -x "$BIN" ] || { echo "$BIN not found - install RidgeKey 0.3.0 first"; exit 1; }

cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key><array>
    <string>$BIN</string><string>-n</string><string>-d</string><string>15</string>
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><false/>
  <key>StandardOutPath</key><string>$LOG</string>
  <key>StandardErrorPath</key><string>$LOG</string>
</dict></plist>
EOF
chown root:wheel "$PLIST"; chmod 644 "$PLIST"
rm -f "$LOG"
launchctl bootout "system/$LABEL" 2>/dev/null || true
launchctl bootstrap system "$PLIST"
echo "Started as a root daemon. LOCK THE SCREEN NOW (Ctrl-Cmd-Q) and wake the password field."
echo "In 15 s it types 8 letters. Unlock normally afterwards (clear the field first)."
sleep 22
launchctl bootout "system/$LABEL" 2>/dev/null || true
rm -f "$PLIST"
echo "--- daemon output:"
cat "$LOG" 2>/dev/null || echo "(no output)"
echo "---"
echo "Report: did 8 dots appear in the lock screen password field?"
