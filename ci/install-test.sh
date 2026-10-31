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
expect_rc 1 "enroll without a sensor" ridgekeyctl enroll right-index
expect_out "no-device" "enroll reports no-device"
expect_rc 1 "delete of a print that is not recorded" ridgekeyctl delete left-thumb
expect_out "not-found" "delete reports not-found"
expect_rc 2 "unknown finger rejected by client" ridgekeyctl enroll left-toe
expect_rc 0 "purge with nothing recorded" sudo ridgekeyctl purge --yes
expect_out "deleted 0" "purge deletes nothing"

echo "== RidgeKey.app"
expect_rc 0 "app installed and starts (--version)" /Applications/RidgeKey.app/Contents/MacOS/RidgeKey --version
expect_out "RidgeKey" "app reports its version"

echo "== lock-screen agent"
AG=/usr/local/ridgekey/bin/ridgekey-agent
expect_rc 0 "agent runs" "$AG" --version
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
expect_rc 0 "upgrade in place while enabled" sudo "$B/install.sh"
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
if grep -q pam_ridgekey /etc/pam.d/sudo_local 2>/dev/null; then fail "sudo_local still mentions pam_ridgekey"; else pass "sudo_local clean"; fi
expect_rc 0 "sudo works after uninstall" sudo -n true
[ ! -e /usr/local/ridgekey ] && pass "files removed" || fail "/usr/local/ridgekey still there"
if sudo launchctl print system/io.github.labrouss.ridgekey >/dev/null 2>&1; then fail "launchd job still loaded"; else pass "launchd job gone"; fi

echo
if [ "$FAILS" -ne 0 ]; then echo "$FAILS check(s) FAILED"; exit 1; fi
echo "all install checks passed"
