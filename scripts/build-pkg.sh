#!/bin/bash
# scripts/build-pkg.sh BUNDLE_DIR OUT.pkg VERSION - a double-click macOS installer for RidgeKey.
#
# The package carries the same bundle as the .tar.gz. It copies it to
#   /Library/Application Support/RidgeKey/Installer/<version>/
# and its postinstall script runs that bundle's install.sh (the one code path for every way of
# installing). The staged copy is kept: it holds uninstall.sh for later.
#
# Not signed with an Apple Developer ID, so the first open needs: right-click -> Open (or System
# Settings -> Privacy & Security -> Open Anyway).
set -euo pipefail

B="$(cd "$1" && pwd)"
OUT="$2"
VER="$3"
ID="io.github.labrouss.ridgekey"
STAGE="Library/Application Support/RidgeKey/Installer/$VER"

die() { echo "build-pkg: $*" >&2; exit 1; }
[ -x "$B/install.sh" ] || die "$B/install.sh not found"
command -v pkgbuild >/dev/null && command -v productbuild >/dev/null || die "pkgbuild/productbuild missing"

T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT

# payload: the bundle under the staging folder
mkdir -p "$T/root/$STAGE" "$T/scripts" "$T/res"
cp -a "$B/." "$T/root/$STAGE/"

# Installer would otherwise "relocate" bundles it finds elsewhere on the disk (an existing
# /Applications/RidgeKey.app) and put our staged copies there. Keep every bundle in place.
pkgbuild --analyze --root "$T/root" "$T/components.plist" >/dev/null
n=0
while /usr/libexec/PlistBuddy -c "Print :$n" "$T/components.plist" >/dev/null 2>&1; do
  # not every entry carries the key, so remove-then-add instead of Set
  /usr/libexec/PlistBuddy -c "Delete :$n:BundleIsRelocatable" "$T/components.plist" >/dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c "Add :$n:BundleIsRelocatable bool false" "$T/components.plist"
  /usr/libexec/PlistBuddy -c "Set :$n:BundleOverwriteAction upgrade" "$T/components.plist" 2>/dev/null || true
  n=$((n + 1))
done

cat > "$T/scripts/postinstall" <<EOF
#!/bin/bash
# RidgeKey $VER - run the bundle's own installer (output goes to /var/log/install.log).
cd "/$STAGE" || exit 1
./install.sh || exit 1
# keep only this version's staged copy (it holds uninstall.sh)
for d in "/Library/Application Support/RidgeKey/Installer/"*; do
  [ "\$d" = "/$STAGE" ] || rm -rf "\$d"
done
EOF
chmod 755 "$T/scripts/postinstall"

pkgbuild --root "$T/root" --component-plist "$T/components.plist" --scripts "$T/scripts" \
  --identifier "$ID.pkg" --version "$VER" --install-location / --ownership recommended \
  "$T/ridgekey-component.pkg" >/dev/null

cat > "$T/res/welcome.txt" <<EOF
RidgeKey $VER - fingerprint authentication for macOS on laptops with a Synaptics
match-in-sensor reader (tested: HP EliteBook 840 G8, 06cb:00f0).

Installs the RidgeKey daemon, the RidgeKey app (Applications), the lock-screen agent and the
admin-dialog plugin. Nothing changes how you log in until you turn it on in the RidgeKey app:
fingerprint for sudo, the lock screen and admin dialogs are each optional, and your password
always keeps working.

Upgrading keeps your enrolled fingers and settings.

RidgeKey never erases the whole sensor: Windows Hello and BIOS fingerprints are left alone.
EOF
cat > "$T/res/conclusion.txt" <<EOF
Next: open RidgeKey from Applications to enroll a finger.

To uninstall later:
  sudo "/$STAGE/uninstall.sh"            (keeps prints)
  sudo "/$STAGE/uninstall.sh" --purge    (also removes RidgeKey's prints from the sensor)

Log: /Library/Logs/RidgeKey/ridgekeyd.log
EOF
cp "$B/LICENSE" "$T/res/license.txt" 2>/dev/null || true

# Reader check (a warning only - you can install before plugging in a USB reader). The IDs come
# from the drivers this build contains (supported-devices.txt, written by the build).
IDS=""
if [ -f "$B/supported-devices.txt" ]; then
  IDS="$(sed -n 's/^\([0-9a-f]\{4\}\):\([0-9a-f]\{4\}\) .*/"\1:\2"/p' "$B/supported-devices.txt" | paste -sd, -)"
fi
CHECK=""
if [ -n "$IDS" ]; then
  CHECK='<installation-check script="rkReaderCheck()"/>'
fi

cat > "$T/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>RidgeKey $VER</title>
  <welcome file="welcome.txt" mime-type="text/plain"/>
  $( [ -f "$T/res/license.txt" ] && echo '<license file="license.txt" mime-type="text/plain"/>' )
  <conclusion file="conclusion.txt" mime-type="text/plain"/>
  <options customize="never" require-scripts="true" hostArchitectures="x86_64"/>
  <domains enable_localSystem="true" enable_currentUserHome="false" enable_anywhere="false"/>
  <volume-check>
    <allowed-os-versions><os-version min="13.0"/></allowed-os-versions>
  </volume-check>
  $CHECK
  <script><![CDATA[
var rkSupported = [$IDS];
function rkHex(n) { var h = (Number(n) & 0xffff).toString(16); while (h.length < 4) h = "0" + h; return h; }
function rkReaderCheck() {
  try {
    var classes = ["IOUSBHostDevice", "IOUSBDevice"];
    for (var c = 0; c < classes.length; c++) {
      var devs = system.ioregistry.matchingClass(classes[c]) || [];
      for (var i = 0; i < devs.length; i++) {
        var d = devs[i];
        if (d["idVendor"] === undefined || d["idProduct"] === undefined) continue;
        var id = rkHex(d["idVendor"]) + ":" + rkHex(d["idProduct"]);
        if (rkSupported.indexOf(id) >= 0) { system.log("RidgeKey: supported reader " + id); return true; }
      }
    }
  } catch (e) {
    system.log("RidgeKey: reader check skipped: " + e);
    return true;
  }
  my.result.type = "Warn";
  my.result.title = "No supported fingerprint reader found";
  my.result.message = "RidgeKey supports match-on-chip readers from Synaptics, Goodix, Elan, FPC, " +
    "Egis, FocalTech and Realtek (" + rkSupported.length + " models). None is connected right now. " +
    "You can install anyway: RidgeKey starts using a supported reader as soon as one is connected.";
  return false;
}
]]></script>
  <choices-outline><line choice="default"/></choices-outline>
  <choice id="default" title="RidgeKey"><pkg-ref id="$ID.pkg"/></choice>
  <pkg-ref id="$ID.pkg" version="$VER" onConclusion="none">ridgekey-component.pkg</pkg-ref>
</installer-gui-script>
EOF

productbuild --distribution "$T/distribution.xml" --resources "$T/res" --package-path "$T" "$OUT" >/dev/null
pkgutil --check-signature "$OUT" >/dev/null 2>&1 || true   # unsigned: informational only
echo "built $OUT ($(du -h "$OUT" | cut -f1))"
