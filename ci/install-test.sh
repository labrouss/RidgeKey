#!/bin/bash
# ci/install-test.sh BUNDLE_TARBALL - install RidgeKey on a macOS CI runner and exercise it end to end
# (no sensor on the runner: covers the daemon, XPC, permissions, the PAM module inside OpenPAM,
# sudo_local enable/disable and the uninstaller).  Needs passwordless sudo (GitHub runners have it).
set -euo pipefail

TARBALL="$1"
WORK="$(mktemp -d)"
FAILS=0
ME="$(id -un)"

pass() { echo "  ok   $*"; }
fail() { echo "  FAIL $*"; FAILS=$((FAILS + 1)); [ -n "${GITHUB_ACTIONS:-}" ] && echo "::error::$*"; return 0; }
expect_rc() { # expected_rc description command...
  local want=$1 what=$2 out rc; shift 2
  set +e; out="$("$@" 2>&1)"; rc=$?; set -e
  echo "$out" | sed 's/^/       | /'
  if [ "$rc" = "$want" ]; then pass "$what (exit $rc)"; else fail "$what: exit $rc, wanted $want"; fi
  LAST_OUT="$out"
}
expect_out() { # pattern description
  if echo "$LAST_OUT" | grep -q -- "$1"; then pass "$2"; else fail "$2 (no '$1' in output)"; fi
}
pam_service() { # name lines...
  local name=$1; shift
  printf '%s\n' "$@" | sudo tee "/etc/pam.d/$name" >/dev/null
  sudo chmod 444 "/etc/pam.d/$name"
}

echo "== extract"
tar -xzf "$TARBALL" -C "$WORK"
B="$(echo "$WORK"/ridgekey-*)"
[ -d "$B" ] || { echo "bundle not found in $TARBALL"; exit 1; }

echo "== install"
expect_rc 0 "install.sh" sudo "$B/install.sh"
expect_out "sensor:   none found" "daemon answers status (no sensor on runner)"
expect_rc 0 "launchd job loaded" sudo launchctl print system/io.github.labrouss.ridgekey

echo "== ridgekeyctl"
expect_rc 0 "status as user" ridgekeyctl status
expect_rc 0 "list as user" ridgekeyctl list
expect_out "0 fingers enrolled" "no prints yet"
expect_rc 1 "verify without prints" ridgekeyctl verify
expect_out "no-prints" "verify reports no-prints"
expect_rc 1 "identify as non-root refused" ridgekeyctl identify
expect_out "permission" "identify is root-only"
expect_rc 1 "list for another user refused" ridgekeyctl -u root list
expect_out "permission" "other users need root"
expect_rc 0 "root may list another user" sudo ridgekeyctl -u "$ME" list
expect_rc 1 "enroll without a sensor (root: no confirmation needed)" sudo ridgekeyctl -u "$ME" enroll right-index
expect_out "no-device" "enroll reports no-device"
expect_rc 1 "delete of a print that is not recorded (root)" sudo ridgekeyctl -u "$ME" delete left-thumb
expect_out "not-found" "delete reports not-found"

echo "== 0.9: confirmation before enroll/delete, trusted clients"
[ -f /usr/local/ridgekey/etc/trusted-clients ] && pass "trusted-clients recorded" || fail "no trusted-clients file"
sudo cat /usr/local/ridgekey/etc/trusted-clients | sed 's/^/       | /'
grep -q "^/usr/local/ridgekey/bin/ridgekeyctl	" /usr/local/ridgekey/etc/trusted-clients \
  && pass "ridgekeyctl is trusted" || fail "ridgekeyctl missing from trusted-clients"
grep -q "^/Applications/RidgeKey.app	" /usr/local/ridgekey/etc/trusted-clients \
  && pass "RidgeKey.app is trusted" || fail "RidgeKey.app missing from trusted-clients"
expect_rc 1 "enroll with --finger but no finger enrolled" ridgekeyctl enroll right-index --finger
expect_out "auth-required" "a first finger needs the password"
expect_rc 1 "enroll with a wrong password" sh -c 'echo not-the-password | ridgekeyctl enroll right-index --password --stdin'
expect_out "bad-password" "wrong password refused"
expect_rc 1 "second wrong password" sh -c 'echo nope2 | ridgekeyctl delete left-thumb --password --stdin'
expect_rc 1 "third wrong password" sh -c 'echo nope3 | ridgekeyctl enroll right-index --password --stdin'
expect_rc 1 "fourth attempt is locked out" sh -c 'echo nope4 | ridgekeyctl enroll right-index --password --stdin'
expect_out "locked-out" "lockout after 3 wrong passwords"
cp /usr/local/ridgekey/bin/ridgekeyctl "$WORK/rkctl-copy"
expect_rc 1 "a copy of ridgekeyctl elsewhere is not trusted" sh -c "echo x | '$WORK/rkctl-copy' enroll right-index --password --stdin"
expect_out "untrusted-client" "untrusted copy refused"
expect_rc 1 "untrusted copy cannot set a password either" sh -c "echo x | '$WORK/rkctl-copy' password set --stdin"
expect_out "untrusted-client" "password-set needs a trusted client"
expect_rc 0 "untrusted copy may still read status" "$WORK/rkctl-copy" status

echo "== 0.10: finger actions (changes need confirmation; root is exempt - the runner has no prints)"
RK="sudo ridgekeyctl -u $ME"
expect_rc 0 "no actions yet" ridgekeyctl actions
expect_out "0 finger actions" "empty list (listing needs no confirmation)"
expect_rc 1 "a user cannot change actions without confirming" sh -c 'echo not-the-password | ridgekeyctl action add left-index tap lock --password --stdin'
if echo "$LAST_OUT" | grep -qE "bad-password|locked-out"; then pass "password required for changes"; else fail "change was not refused"; fi
expect_rc 1 "finger confirmation without enrolled prints" ridgekeyctl action add left-index tap lock --finger
expect_out "auth-required" "first finger: password only"
expect_rc 0 "add: tap left-index -> lock (root)" $RK action add left-index tap lock
expect_rc 0 "add: hold right-thumb 2 s -> Shortcut" $RK action add right-thumb hold 2 shortcut "Work mode"
expect_rc 0 "list" ridgekeyctl actions
expect_out "2 finger actions" "two rules stored"
expect_out 'Work mode' "shortcut name kept"
expect_rc 0 "replacing the same finger + gesture" $RK action add left-index tap shortcut "Focus"
expect_rc 0 "list again" ridgekeyctl actions
expect_out "2 finger actions" "replaced, not added"
expect_rc 0 "add: double tap left-index -> Shortcut" $RK action add left-index double shortcut "Two"
expect_rc 0 "add: triple tap left-index -> lock" $RK action add left-index triple lock
expect_rc 0 "list with multi-tap" ridgekeyctl actions
expect_out "4 finger actions" "single, double and triple tap are separate rules"
expect_out "double" "double tap listed"
expect_out "triple" "triple tap listed"
expect_rc 0 "remove only the triple tap" $RK action remove left-index triple
expect_rc 0 "list" ridgekeyctl actions
expect_out "3 finger actions" "only the triple tap removed"
expect_rc 1 "hold 0 s refused" $RK action add left-index hold 0 lock
expect_out "bad-request" "validation"
expect_rc 1 "shortcut without a name refused" $RK action add left-index tap shortcut
expect_rc 1 "unknown action refused" $RK action add left-index tap reboot
expect_rc 1 "untrusted copy cannot change actions" sh -c "echo x | '$WORK/rkctl-copy' action add left-index tap lock --password --stdin"
expect_out "untrusted-client" "actions-set needs a trusted client"
[ "$(sudo stat -f %Lp "/Library/Application Support/RidgeKey/actions/$(id -u).conf")" = 600 ] \
  && pass "rules file is 0600 (root)" || fail "rules file permissions"
expect_rc 0 "status: not listening (no enrolled prints on the runner)" ridgekeyctl status
if echo "$LAST_OUT" | grep -q "listening for finger actions"; then fail "listening without prints"; else pass "idle without prints"; fi
expect_rc 0 "remove one" $RK action remove right-thumb
expect_rc 0 "clear" $RK action clear
expect_rc 0 "list after clear" ridgekeyctl actions
expect_out "0 finger actions" "cleared"
expect_rc 2 "unknown finger rejected by client" ridgekeyctl enroll left-toe
expect_rc 0 "purge with nothing recorded" sudo ridgekeyctl purge --yes
expect_out "deleted 0" "purge deletes nothing"

echo "== RidgeKey.app"
expect_rc 0 "app installed and starts (--version)" /Applications/RidgeKey.app/Contents/MacOS/RidgeKey --version
expect_out "RidgeKey" "app reports its version"

echo "== lock-screen agent"
AG=/usr/local/ridgekey/RidgeKeyAgent.app
[ -f "$AG/Contents/Resources/RidgeKey.icns" ] && pass "agent bundle has the RidgeKey icon" || fail "agent bundle has no icon"
expect_rc 0 "agent bundle signature verifies (strict)" codesign --verify --strict "$AG"
expect_rc 0 "agent runs" /usr/local/ridgekey/bin/ridgekey-agent --version
expect_rc 0 "agent signature" codesign -dv "$AG"
expect_out "runtime" "agent has the hardened runtime"
expect_rc 0 "agent designated requirement" codesign -dr - "$AG"
expect_out "certificate leaf" "agent signed with the local certificate (stable across upgrades)"
LEAF1="$(codesign -dr - "$AG" 2>&1 | grep -o 'certificate leaf = H"[0-9a-f]*"' || true)"
expect_rc 1 "unlock refused for ridgekeyctl (not the agent)" ridgekeyctl unlock
expect_out "permission" "only the agent may unlock"
expect_rc 1 "unlock refused for root too" sudo ridgekeyctl unlock
expect_out "permission" "root is not the agent either"
CU="$(stat -f %u /dev/console)"
if [ "$CU" != 0 ]; then
  expect_rc 0 "agent loaded in the console user's GUI session" sudo launchctl print "gui/$CU/io.github.labrouss.ridgekey.agent"
  sleep 2
  if sudo grep -q "agent verified" /Library/Logs/RidgeKey/ridgekeyd.log; then
    pass "daemon verified the running agent (path, signature, hardened runtime, root-owned)"
  else
    fail "daemon did not verify the agent"; sudo grep -i agent /Library/Logs/RidgeKey/ridgekeyd.log || true
  fi
else
  echo "  skip no GUI user on the console"
fi

echo "== console state"
expect_rc 0 "status shows the console" ridgekeyctl status
expect_out "^console:" "console line present"

echo "== stored password (throwaway user)"
TU=rktest
TP="Rk-$(uuidgen | cut -c1-8)-x9"
sudo sysadminctl -addUser "$TU" -password "$TP" >/dev/null 2>&1 || true
if id "$TU" >/dev/null 2>&1; then
  expect_rc 1 "wrong password rejected" sh -c "echo not-the-password | sudo ridgekeyctl -u $TU password set --stdin"
  expect_out "bad-password" "rejected as bad-password"
  expect_rc 1 "nothing stored after a rejection" sudo ridgekeyctl -u "$TU" password check
  expect_out "no password stored" "check reports nothing stored"
  expect_rc 1 "non-root cannot set another user's password" sh -c "echo x | ridgekeyctl -u $TU password set --stdin"
  expect_out "permission" "permission enforced"
  expect_rc 0 "correct password stored (checked + read back)" sh -c "printf '%s\n' '$TP' | sudo ridgekeyctl -u $TU password set --stdin"
  expect_rc 0 "check: valid" sudo ridgekeyctl -u "$TU" password check
  expect_out "stored password is valid" "keychain read + PAM check ok"
  expect_rc 0 "list shows it" sudo ridgekeyctl -u "$TU" list
  expect_out "lock-screen password: stored" "list reports stored"
  # upgrade = a differently signed ridgekeyd must still read the item without a prompt
  sudo codesign --force --sign - --identifier io.github.labrouss.ridgekey.resigned /usr/local/ridgekey/bin/ridgekeyd >/dev/null 2>&1
  sudo launchctl kickstart -k system/io.github.labrouss.ridgekey
  sleep 2
  expect_rc 0 "re-signed daemon still reads it" sudo ridgekeyctl -u "$TU" password check
  expect_out "stored password is valid" "keychain ACL survives re-signing"
  sudo dscl . -passwd "/Users/$TU" "$TP" "${TP}-new" >/dev/null 2>&1 || true
  expect_rc 1 "outdated after a password change" sudo ridgekeyctl -u "$TU" password check
  expect_out "OUTDATED" "stale password detected"
  expect_rc 0 "clear" sudo ridgekeyctl -u "$TU" password clear
  expect_rc 1 "gone after clear" sudo ridgekeyctl -u "$TU" password check
  expect_out "no password stored" "cleared"
  sudo sysadminctl -deleteUser "$TU" >/dev/null 2>&1 || true
else
  echo "  skip could not create a throwaway user on this runner"
fi

echo "== PAM module inside OpenPAM"
MOD=/usr/local/ridgekey/lib/pam/pam_ridgekey.so
pam_service ridgekey-ci "auth       sufficient     $MOD timeout=5 tries=1 allow_remote debug" "auth       required       pam_deny.so"
START=$(date +%s)
expect_rc 0 "pam stack runs with daemon up" /usr/local/ridgekey/bin/ridgekey-pamcheck ridgekey-ci "$ME"
expect_out "result=PAM_AUTH_ERR" "no prints -> module ignored -> falls through (pam_deny)"
[ $(( $(date +%s) - START )) -lt 10 ] && pass "no-prints path is immediate" || fail "no-prints path took too long"

sudo launchctl bootout system/io.github.labrouss.ridgekey
START=$(date +%s)
expect_rc 0 "pam stack runs with daemon down" /usr/local/ridgekey/bin/ridgekey-pamcheck ridgekey-ci "$ME"
expect_out "result=PAM_AUTH_ERR" "daemon down -> module ignored -> falls through"
[ $(( $(date +%s) - START )) -lt 20 ] && pass "daemon-down path does not hang" || fail "daemon-down path took too long"
expect_rc 3 "ridgekeyctl reports daemon unreachable" ridgekeyctl status
sudo launchctl bootstrap system /Library/LaunchDaemons/io.github.labrouss.ridgekey.plist
sleep 1
expect_rc 0 "daemon back" ridgekeyctl status
sudo rm -f /etc/pam.d/ridgekey-ci

echo "== ridgekey-sudo (real /etc/pam.d/sudo_local)"
expect_rc 0 "enable (includes the OpenPAM load check)" sudo ridgekey-sudo enable
expect_out "module load check passed" "load check ran"
expect_rc 0 "status" ridgekey-sudo status
expect_out "^enabled" "sudo_local has pam_ridgekey"
expect_rc 0 "sudo still works with the module in sudo_local" sudo -n true
expect_rc 0 "enable again is a no-op" sudo ridgekey-sudo enable
expect_out "already enabled" "idempotent"

echo "== admin dialogs: plugin + ridgekey-admin (real authd)"
CIR="io.github.labrouss.ridgekey.citest"
FPR="io.github.labrouss.ridgekey.fingerprint"
PLUG=/Library/Security/SecurityAgentPlugins/RidgeKey.bundle
# Normalised dump of a right (authd adds created/modified timestamps on every write).
dump_right() {
  local f; f="$(mktemp)"
  sudo security authorizationdb read "$1" > "$f" 2>/dev/null || { rm -f "$f"; return 1; }
  /usr/libexec/PlistBuddy -c "Delete :created" "$f" >/dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c "Delete :modified" "$f" >/dev/null 2>&1 || true
  plutil -convert xml1 -o - "$f"; rm -f "$f"
}
[ -f "$PLUG/Contents/MacOS/RidgeKey" ] && pass "plugin installed" || fail "plugin missing at $PLUG"
expect_rc 0 "plugin signature verifies" codesign --verify --strict "$PLUG"
expect_rc 0 "status (nothing enabled yet)" ridgekey-admin status
expect_out "none" "no rights enabled after install"
expect_rc 0 "selftest: macOS loads and runs RidgeKey.bundle" sudo ridgekey-admin selftest
expect_out "selftest: ok" "plugin mechanism ran"

# A throwaway admin-password right, shaped like authenticate-admin-nonshared.
printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
  '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
  '<plist version="1.0"><dict><key>class</key><string>user</string><key>group</key><string>admin</string>' \
  '<key>shared</key><false/><key>timeout</key><integer>0</integer>' \
  '<key>comment</key><string>RidgeKey CI test right</string></dict></plist>' > "$WORK/citest.plist"
expect_rc 0 "create test right" sudo security authorizationdb write "$CIR" < "$WORK/citest.plist"
CI_BEFORE="$(dump_right "$CIR")"
SP_BEFORE="$(dump_right system.preferences)"
expect_rc 1 "test right before: denied without interaction" sudo /usr/local/ridgekey/bin/ridgekey-authcheck "$CIR"

expect_rc 0 "enable the test right + system.preferences" sudo ridgekey-admin enable "$CIR" system.preferences
expect_out "ok $CIR" "test right switched"
dump_right "$CIR" | grep -q "<string>$FPR</string>" && pass "test right now tries the fingerprint first" \
  || fail "test right does not reference $FPR"
dump_right "$CIR" | grep -q "<string>io.github.labrouss.ridgekey.orig.$CIR</string>" && pass "original kept as fallback" \
  || fail "fallback to the original rule missing"
expect_rc 0 "enable again is a no-op" sudo ridgekey-admin enable "$CIR"
expect_out "already enabled" "idempotent"
expect_rc 1 "a non-admin right is refused" sudo ridgekey-admin enable system.login.console
expect_out "skipped" "refusal explained"
# The full chain, for real: fingerprint rule -> plugin -> ridgekeyd (no sensor / no prints) ->
# Deny -> authd falls back to the original rule -> no UI allowed -> denied. Exit 2 here would
# mean an internal error, i.e. the password fallback would NOT appear for a real user.
T0=$(date +%s)
expect_rc 1 "wrapped right: plugin denies, fallback runs, no internal error" \
  sudo perl -e 'alarm 60; exec @ARGV' /usr/local/ridgekey/bin/ridgekey-authcheck "$CIR"
T1=$(date +%s); [ $((T1 - T0)) -lt 45 ] && pass "answered in $((T1 - T0))s" || fail "took $((T1 - T0))s"
log show --last 2m --style compact --predicate 'subsystem == "io.github.labrouss.ridgekey"' 2>/dev/null \
  | tail -5 | sed 's/^/       | /' || true
expect_rc 1 "authorize as root: refused cleanly (no enrolled admin on the runner)" \
  sudo ridgekeyctl authorize ci-test
expect_rc 1 "authorize refused for non-root" ridgekeyctl authorize ci-test
expect_out "only available to root" "non-root refused"
expect_rc 0 "status lists the enabled rights" ridgekey-admin status
expect_out "on   $CIR" "status shows the test right"

expect_rc 0 "upgrade in place while enabled" sudo "$B/install.sh"
expect_out "checking the new plugin" "installer re-checked the plugin"
expect_out "selftest: ok" "plugin still loads after upgrade"
LEAF2="$(codesign -dr - "$AG" 2>&1 | grep -o 'certificate leaf = H"[0-9a-f]*"' || true)"
if [ -n "$LEAF1" ] && [ "$LEAF1" = "$LEAF2" ]; then
  pass "reinstall keeps the same signing certificate (Accessibility survives upgrades)"
else
  fail "signing certificate changed on reinstall: '$LEAF1' -> '$LEAF2'"
fi
expect_rc 0 "sudo still works after upgrade" sudo -n true

echo "== daemon log (tail)"
sudo tail -30 /Library/Logs/RidgeKey/ridgekeyd.log 2>/dev/null || true

echo "== uninstall"
expect_rc 0 "uninstall --purge (disables sudo first)" sudo "$B/uninstall.sh" --purge
expect_out "Removing fingerprint from sudo" "sudo_local cleaned before files removed"
expect_out "Restoring the normal admin dialogs" "admin rules restored before files removed"
[ "$(dump_right "$CIR")" = "$CI_BEFORE" ] && pass "test right restored exactly" || fail "test right differs after uninstall"
[ "$(dump_right system.preferences)" = "$SP_BEFORE" ] && pass "system.preferences restored exactly" \
  || { fail "system.preferences differs after uninstall"; diff <(echo "$SP_BEFORE") <(dump_right system.preferences) || true; }
dump_right "io.github.labrouss.ridgekey.orig.$CIR" >/dev/null 2>&1 && fail "orig copy left behind" || pass "orig copy removed"
dump_right "$FPR" >/dev/null 2>&1 && fail "fingerprint rule left behind" || pass "fingerprint rule removed"
[ ! -e "$PLUG" ] && pass "plugin removed" || fail "plugin still installed"
sudo security authorizationdb remove "$CIR" >/dev/null 2>&1 || true
if grep -q pam_ridgekey /etc/pam.d/sudo_local 2>/dev/null; then fail "sudo_local still mentions pam_ridgekey"; else pass "sudo_local clean"; fi
expect_rc 0 "sudo works after uninstall" sudo -n true
[ ! -e /usr/local/ridgekey ] && pass "files removed" || fail "/usr/local/ridgekey still there"
if sudo launchctl print system/io.github.labrouss.ridgekey >/dev/null 2>&1; then fail "launchd job still loaded"; else pass "launchd job gone"; fi

echo "== double-click installer (.pkg)"
PKG="$(ls "$(dirname "$TARBALL")"/RidgeKey-*.pkg 2>/dev/null | head -1)"
if [ -n "$PKG" ]; then
  expect_rc 0 "pkg lists its payload" pkgutil --payload-files "$PKG"
  expect_out "Installer/.*/install.sh" "pkg carries the bundle"
  expect_rc 0 "installer -pkg (fresh install)" sudo installer -pkg "$PKG" -target /
  expect_rc 0 "daemon answers after pkg install" ridgekeyctl status
  [ -d /Applications/RidgeKey.app ] && pass "app installed by the pkg" || fail "app missing after pkg install"
  [ -d /usr/local/ridgekey/RidgeKeyAgent.app ] && pass "agent installed by the pkg" || fail "agent missing after pkg install"
  expect_rc 0 "installer -pkg again (upgrade in place)" sudo installer -pkg "$PKG" -target /
  expect_rc 0 "daemon answers after pkg upgrade" ridgekeyctl status
  U="$(ls -d "/Library/Application Support/RidgeKey/Installer/"*/uninstall.sh 2>/dev/null | head -1)"
  [ -n "$U" ] && pass "staged uninstaller present" || fail "no staged uninstall.sh"
  expect_rc 0 "uninstall via the staged uninstaller" sudo "$U" --purge
  [ ! -e /usr/local/ridgekey ] && pass "pkg install removed cleanly" || fail "files left after uninstall"
else
  fail "no RidgeKey-*.pkg next to $TARBALL"
fi

echo
if [ "$FAILS" -ne 0 ]; then echo "$FAILS check(s) FAILED"; exit 1; fi
echo "all install checks passed"
