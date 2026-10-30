#!/bin/bash
# Record how libfprint's "synaptics" driver talks to a Synaptics Prometheus match-in-sensor reader
# (default 06cb:00f0, HP EliteBook G8) - reference for the RidgeKey macOS backend.
#
# Run from an Ubuntu live USB (needs internet for apt):   sudo bash capture-synaptics-00f0.sh
# Output: ./synaptics-capture-<date>.tar.gz  (USB trace + libfprint debug log + descriptors)
#
# Privacy: this is a match-IN-sensor chip - fingerprint images and templates never leave the sensor,
# so the trace holds commands, status codes, finger/user IDs and the sensor's version info only.
# WARNING: fprintd WIPES THE SENSOR'S WHOLE PRINT DATABASE (BMKT_CMD_DEL_FULL_DB) before the first
# enrollment when it has no prints of its own for the device. Prints enrolled in Windows Hello or in
# HP's BIOS / Client Security on this sensor are erased and must be enrolled again there.
# (Confirmed in a capture: "clear all prints in database ... Successfully deleted all enrolled user".)
set -u
[ "$(id -u)" = 0 ] || { echo "Run with sudo"; exit 1; }
VIDPID="${VIDPID:-06cb:00f0}"
USR="${SUDO_USER:-ubuntu}"
OUT="$(cd "$(dirname "$0")" && pwd)/synaptics-capture-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"; LOG="$OUT/run.log"
log() { echo "[$(date +%T)] $*" | tee -a "$LOG"; sync; }

echo "This wipes ALL fingerprints stored on the sensor (Windows Hello / BIOS prints included)."
read -r -p "Continue? [y/N] " a; [ "$a" = y ] || exit 0
lsusb -d "$VIDPID" >/dev/null || { log "No USB device $VIDPID found (lsusb below)"; lsusb | tee -a "$LOG"; exit 1; }
BUS=$(lsusb -d "$VIDPID" | awk '{print $2+0; exit}'); DEV=$(lsusb -d "$VIDPID" | awk '{gsub(":","",$4); print $4+0; exit}')
log "Sensor $VIDPID on bus $BUS device $DEV"
lsusb -v -d "$VIDPID" > "$OUT/lsusb-v.txt" 2>&1
uname -a > "$OUT/uname.txt"

log "Installing fprintd / libfprint / fwupd tools (apt)"
apt-get update -qq >>"$LOG" 2>&1
apt-get install -y -qq fprintd libpam-fprintd fwupd >>"$LOG" 2>&1 || log "apt install failed - check network"
dpkg -l | grep -E 'libfprint|fprintd' > "$OUT/packages.txt"
fwupdmgr get-devices --json > "$OUT/fwupd-devices.json" 2>/dev/null || fwupdmgr get-devices > "$OUT/fwupd-devices.txt" 2>&1

log "Starting usbmon capture on bus $BUS"
modprobe usbmon; mount -t debugfs none /sys/kernel/debug 2>/dev/null
cat "/sys/kernel/debug/usb/usbmon/${BUS}u" > "$OUT/usbmon-bus${BUS}.txt" &
MON=$!

log "Restarting fprintd in debug mode"
systemctl stop fprintd 2>/dev/null; pkill -x fprintd 2>/dev/null; sleep 1
FPRINTD=$(ls /usr/libexec/fprintd /usr/lib/fprintd/fprintd 2>/dev/null | head -1)
G_MESSAGES_DEBUG=all "$FPRINTD" -t > "$OUT/fprintd-debug.log" 2>&1 &
FPD=$!; sleep 3

log "Prints already known to fprintd for $USR (sensor storage may also hold Windows/BIOS prints):"
fprintd-list "$USR" 2>&1 | tee -a "$LOG"

log "=== ENROLL: touch the sensor repeatedly when asked (right index finger) ==="
fprintd-enroll -f right-index-finger "$USR" 2>&1 | tee -a "$LOG"

log "=== VERIFY 1: touch with the ENROLLED finger ==="
fprintd-verify -f right-index-finger "$USR" 2>&1 | tee -a "$LOG"
log "=== VERIFY 2: touch with a DIFFERENT finger (should not match) ==="
fprintd-verify -f right-index-finger "$USR" 2>&1 | tee -a "$LOG"

log "=== DELETE: remove only the print enrolled above ==="
fprintd-delete "$USR" 2>&1 | tee -a "$LOG"
fprintd-list "$USR" 2>&1 | tee -a "$LOG"

sleep 2; kill $FPD $MON 2>/dev/null; sleep 1
dmesg | grep -iE 'usb|synaptics|fprint' | tail -100 > "$OUT/dmesg.txt"
log "Packets captured: $(wc -l < "$OUT/usbmon-bus${BUS}.txt")  (device $DEV lines: $(grep -c " ${BUS}:0*${DEV}:" "$OUT/usbmon-bus${BUS}.txt" 2>/dev/null))"
cd "$(dirname "$OUT")" && tar czf "$OUT.tar.gz" "$(basename "$OUT")" && log "Done: $OUT.tar.gz"
