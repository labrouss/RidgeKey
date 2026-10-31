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
#   /Library/Security/SecurityAgentPlugins/RidgeKey.bundle   admin-dialog plugin (admin dialogs are
#                                                       NOT changed - 'sudo ridgekey-admin enable')
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
[ -d "$SRC/RidgeKeyAgent.app" ] && cp -R "$SRC/RidgeKeyAgent.app" "$DEST.new/"
[ -d "$SRC/licenses" ] && cp -R "$SRC/licenses" "$DEST.new/"
cp "$SRC/BUILDINFO.txt" "$DEST.new/" 2>/dev/null || true
install -m 755 "$SRC/ridgekey-sudo" "$DEST.new/bin/ridgekey-sudo"
[ -f "$SRC/ridgekey-admin" ] && install -m 755 "$SRC/ridgekey-admin" "$DEST.new/bin/ridgekey-admin"
chown -R root:wheel "$DEST.new"
chmod -R go-w "$DEST.new"
chmod 444 "$DEST.new/lib/pam/pam_ridgekey.so"
install -m 755 "$SRC/ridgekey-sign" "$DEST.new/bin/ridgekey-sign"
# Stable local signature for the agent, so Accessibility survives upgrades (falls back to the
# ad-hoc CI signature if anything goes wrong - the agent works either way).
echo "==> Signing RidgeKey Agent with the local certificate"
RK_SIGN_STATUS=ok
"$DEST.new/bin/ridgekey-sign" sign "$DEST.new/RidgeKeyAgent.app" || {
  RK_SIGN_STATUS=adhoc
  echo "    kept the ad-hoc signature (Accessibility must be granted again after upgrades)"; }
xattr -dr com.apple.quarantine "$DEST.new" 2>/dev/null || true
# Swap in one step: if sudo_local already uses pam_ridgekey.so, the module path must never
# be missing (OpenPAM fails the whole sudo stack when a listed module cannot be loaded).
if [ -d "$DEST" ]; then
  rm -rf "$DEST.old"; mv "$DEST" "$DEST.old"; mv "$DEST.new" "$DEST"; rm -rf "$DEST.old"
else
  mv "$DEST.new" "$DEST"
fi

if [ -d "$SRC/RidgeKey.app" ]; then
  echo "==> Installing RidgeKey.app to /Applications"
  rm -rf /Applications/RidgeKey.app
  cp -R "$SRC/RidgeKey.app" /Applications/
  xattr -dr com.apple.quarantine /Applications/RidgeKey.app 2>/dev/null || true
fi

# Trusted clients: only these exact, installed programs may enroll/delete prints and manage the
# stored password (ridgekeyd checks each caller's running code against the requirement recorded
# here). Root-owned; rewritten on every install.
echo "==> Recording the trusted RidgeKey clients"
mkdir -p "$DEST/etc"
TC_TMP="$(mktemp "$DEST/etc/.trusted-clients.XXXXXX")"
for c in "$DEST/bin/ridgekeyctl" /Applications/RidgeKey.app; do
  [ -e "$c" ] || continue
  # Ad-hoc signatures have no embedded requirement; codesign then prints the implicit one as a
  # comment ("# designated => cdhash H\"...\"") - accept both forms.
  req="$(codesign -d -r- "$c" 2>&1 | sed -n 's/^#* *designated => //p' | head -1)"
  if [ -n "$req" ]; then
    printf '%s\t%s\n' "$c" "$req" >> "$TC_TMP"
    echo "    $c"
  else
    echo "    WARNING: no code signature on $c - it will not be able to manage prints"
  fi
done
chown root:wheel "$DEST/etc" "$TC_TMP"; chmod 755 "$DEST/etc"; chmod 644 "$TC_TMP"
mv -f "$TC_TMP" "$DEST/etc/trusted-clients"

mkdir -p /usr/local/bin
ln -sf "$DEST/bin/ridgekeyctl" /usr/local/bin/ridgekeyctl
ln -sf "$DEST/bin/ridgekey-sudo" /usr/local/bin/ridgekey-sudo
[ -x "$DEST/bin/ridgekey-admin" ] && ln -sf "$DEST/bin/ridgekey-admin" /usr/local/bin/ridgekey-admin

AUTH_PLUGINS=/Library/Security/SecurityAgentPlugins
if [ -d "$SRC/RidgeKey.bundle" ]; then
  echo "==> Installing the admin-dialog plugin to $AUTH_PLUGINS"
  mkdir -p "$AUTH_PLUGINS"
  rm -rf "$AUTH_PLUGINS/RidgeKey.bundle.new"
  cp -R "$SRC/RidgeKey.bundle" "$AUTH_PLUGINS/RidgeKey.bundle.new"
  chown -R root:wheel "$AUTH_PLUGINS/RidgeKey.bundle.new"
  chmod -R go-w "$AUTH_PLUGINS/RidgeKey.bundle.new"
  xattr -dr com.apple.quarantine "$AUTH_PLUGINS/RidgeKey.bundle.new" 2>/dev/null || true
  # Swap quickly: while admin rights use the plugin it must not be missing for long.
  if [ -d "$AUTH_PLUGINS/RidgeKey.bundle" ]; then
    rm -rf "$AUTH_PLUGINS/RidgeKey.bundle.old"
    mv "$AUTH_PLUGINS/RidgeKey.bundle" "$AUTH_PLUGINS/RidgeKey.bundle.old"
    mv "$AUTH_PLUGINS/RidgeKey.bundle.new" "$AUTH_PLUGINS/RidgeKey.bundle"
    rm -rf "$AUTH_PLUGINS/RidgeKey.bundle.old"
  else
    mv "$AUTH_PLUGINS/RidgeKey.bundle.new" "$AUTH_PLUGINS/RidgeKey.bundle"
  fi
fi

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
STATUS_OUT="$(/usr/local/bin/ridgekeyctl status)" || {
  echo "ridgekeyd did not answer - see /Library/Logs/RidgeKey/ridgekeyd.log"; exit 1; }
echo "$STATUS_OUT"
if echo "$STATUS_OUT" | grep -q 'sensor:   none found'; then
  echo
  echo "Note: no supported fingerprint reader is connected right now. RidgeKey supports these"
  echo "      match-on-chip readers (and picks one up as soon as it is connected):"
  [ -f "$SRC/supported-devices.txt" ] && cut -d'|' -f2 "$SRC/supported-devices.txt" | sort | uniq -c | sed 's/^/       /'
fi

# Upgrade with admin dialogs enabled: prove the new plugin loads, or switch the feature off
# (a plugin macOS cannot load would break those dialogs instead of falling back).
if [ -x "$DEST/bin/ridgekey-admin" ] && ls "/Library/Application Support/RidgeKey/authdb/"*.plist >/dev/null 2>&1; then
  echo "==> Admin dialogs by fingerprint are enabled - checking the new plugin"
  if ! "$DEST/bin/ridgekey-admin" selftest; then
    echo "    WARNING: the plugin failed its self-test - restoring the normal admin dialogs"
    "$DEST/bin/ridgekey-admin" disable || true
  fi
fi

if [ "$RK_SIGN_STATUS" = adhoc ]; then
  echo "Note: RidgeKey Agent kept its ad-hoc signature - re-grant Accessibility after upgrades."
fi
cat <<EOF

Installed. Open the RidgeKey app (Applications), or use the command line (no sudo):
  ridgekeyctl enroll right-index
  ridgekeyctl verify
  ridgekeyctl list
Fingerprint for sudo (optional, password still works):  sudo ridgekey-sudo enable
Fingerprint for admin dialogs (optional, same):        sudo ridgekey-admin enable
Lock screen: 1) ridgekeyctl password set
             2) System Settings -> Privacy & Security -> Accessibility -> enable "RidgeKey Agent"
                (/usr/local/ridgekey/RidgeKeyAgent.app). Needed once; it survives upgrades.
                Coming from 0.7.0 or older: remove the old "ridgekey-agent" entry with "-".
             3) lock the screen (Ctrl-Cmd-Q) and touch the sensor
Log: /Library/Logs/RidgeKey/ridgekeyd.log      Uninstall: sudo ./uninstall.sh [--purge]
EOF
