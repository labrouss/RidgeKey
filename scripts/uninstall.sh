#!/bin/bash
# RidgeKey uninstaller:   sudo ./uninstall.sh [--purge]
#
#   (default)  stop and remove the daemon and binaries; keep the print record and the prints
#              on the sensor (a reinstall picks them up again)
#   --purge    first delete every RidgeKey print from the sensor, one by one, then remove the
#              record, the stored lock-screen passwords (System keychain) and the local
#              code-signing certificate too.
#              Windows Hello / BIOS prints are never touched.
set -euo pipefail

LABEL="io.github.labrouss.ridgekey"
DEST="/usr/local/ridgekey"
PLIST="/Library/LaunchDaemons/$LABEL.plist"
DATA="/Library/Application Support/RidgeKey"
PURGE=0
[ "${1:-}" = "--purge" ] && PURGE=1

[ "$(id -u)" = 0 ] || { echo "Run with sudo: sudo $0 ${1:-}"; exit 1; }

# First take pam_ridgekey out of sudo, BEFORE the module file disappears.
if [ -f /etc/pam.d/sudo_local ] && grep -q 'pam_ridgekey\.so' /etc/pam.d/sudo_local; then
  echo "==> Removing fingerprint from sudo"
  if [ -x "$DEST/bin/ridgekey-sudo" ]; then
    "$DEST/bin/ridgekey-sudo" disable
  else
    sed -i '' -e '/pam_ridgekey\.so/d' -e '/# RidgeKey fingerprint/d' /etc/pam.d/sudo_local
  fi
  grep -q 'pam_ridgekey\.so' /etc/pam.d/sudo_local && { echo "Could not remove pam_ridgekey from sudo_local - aborting"; exit 1; }
fi

# Then restore the normal admin dialogs, BEFORE the plugin disappears (authd cannot fall back
# to the password when a plugin listed in a rule is missing).
if ls "$DATA/authdb/"*.plist >/dev/null 2>&1; then
  echo "==> Restoring the normal admin dialogs"
  ADMIN="$DEST/bin/ridgekey-admin"; [ -x "$ADMIN" ] || ADMIN="$(cd "$(dirname "$0")" && pwd)/ridgekey-admin"
  [ -x "$ADMIN" ] || { echo "ridgekey-admin not found - cannot restore the admin rules; aborting"; exit 1; }
  "$ADMIN" disable || { echo "Could not restore every admin rule - aborting (nothing removed)"; exit 1; }
fi

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

AGENT="$LABEL.agent"
CONSOLE_UID="$(stat -f %u /dev/console)"
echo "==> Stopping and removing the lock-screen agent"
[ "$CONSOLE_UID" != 0 ] && launchctl bootout "gui/$CONSOLE_UID/$AGENT" 2>/dev/null || true
rm -f "/Library/LaunchAgents/$AGENT.plist"

echo "==> Stopping and removing the daemon"
launchctl bootout "system/$LABEL" 2>/dev/null || true
rm -f "$PLIST"
rm -rf "$DEST" /Applications/RidgeKey.app /Library/Security/SecurityAgentPlugins/RidgeKey.bundle
for l in ridgekeyctl ridgekey-sudo ridgekey-admin; do [ -L "/usr/local/bin/$l" ] && rm -f "/usr/local/bin/$l"; done
pkgutil --forget io.github.labrouss.ridgekey.pkg >/dev/null 2>&1 || true   # installed from the .pkg

if [ $PURGE = 1 ]; then
  n=0
  while security delete-generic-password -s io.github.labrouss.ridgekey.password \
          /Library/Keychains/System.keychain >/dev/null 2>&1; do n=$((n + 1)); done
  echo "Removed $n stored password(s) from the System keychain."
  rm -rf "$DATA" /Library/Logs/RidgeKey
  echo "Removed RidgeKey and its prints."
else
  echo "Removed RidgeKey. Kept the print record in '$DATA' and any stored password"
  echo "(use --purge to remove prints and passwords too)."
fi
