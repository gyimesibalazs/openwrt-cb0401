#!/bin/sh
# Extract the stock WiFi files the OpenWrt image needs into
# package/firmware/xiaomi-cb0401[v2]-wifi-firmware/files/ (git-ignored: these
# are vendor binaries, take them from your own unit's stock firmware).
#
#   extract-stock-firmware.sh [-m cb0401v2|cb0401] <stock rootfs>
#
# -m picks the model (default cb0401v2). The CB0401 (non-V2) uses the same
# signed Q6 firmware but a different IPQ5018 2.4 GHz board-data index
# (bdwlan.b24 vs .b23) and its own calibration variant.
#
# <stock rootfs> is one of:
#   - a dump of the stock "rootfs" (or "rootfs_1") partition (UBI image;
#     needs ubireader_extract_images from ubi_reader, "pip install ubi_reader")
#   - the stock ubi_rootfs squashfs image
#   - a directory holding the unpacked stock rootfs
#
# Produces:
#   IPQ5018/q6_fw.*, m3_fw.*  signed WCSS firmware (WLAN.HK.2.6-02451); the
#                             unit is secure-boot fused and TZ refuses others
#   IPQ5018/board-2.bin       bdwlan.b23, board-id 35 and 255 (the OTP is not
#                             programmed, the chip reports 0xff)
#   QCN9074/board-2.bin       qcn9000/bdwlan.ba0, board-id 255 (0xff reported)

set -e

MODEL=cb0401v2
if [ "$1" = "-m" ]; then MODEL=$2; shift 2; fi

case "$MODEL" in
cb0401v2) VARIANT=Xiaomi-CB0401V2; IPQ_BDF=bdwlan.b23 ;;
cb0401)   VARIANT=Xiaomi-CB0401;   IPQ_BDF=bdwlan.b24 ;;
*) echo "unknown model $MODEL (cb0401v2|cb0401)" >&2; exit 1 ;;
esac

TOPDIR=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$TOPDIR/package/firmware/xiaomi-$MODEL-wifi-firmware/files
ENC=$TOPDIR/tools/cb0401/ath11k-board2.py
QCN9074=bus=pci,qmi-chip-id=0,qmi-board-id=255

die() { echo "$*" >&2; exit 1; }

[ -e "$1" ] || die "usage: $0 <stock rootfs: UBI dump | squashfs | directory>"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

unsquash() {
	local u

	for u in "$TOPDIR/staging_dir/host/bin/unsquashfs4" unsquashfs; do
		command -v "$u" >/dev/null 2>&1 || [ -x "$u" ] || continue
		"$u" -q -d "$TMP/root" "$1" 'lib/firmware/IPQ5018' 'lib/firmware/qcn9000' >/dev/null 2>&1 || \
			"$u" -q -d "$TMP/root" "$1" 'lib/firmware/IPQ5018' >/dev/null
		return
	done
	die "unsquashfs not found (build the tree's host tools or install squashfs-tools)"
}

if [ -d "$1" ]; then
	ROOT=$1
elif [ "$(head -c 4 "$1")" = "UBI#" ]; then
	command -v ubireader_extract_images >/dev/null ||
		die "ubireader_extract_images not found (pip install ubi_reader)"
	ubireader_extract_images -o "$TMP/ubi" "$1" >/dev/null
	SQ=$(find "$TMP/ubi" -name '*vol-ubi_rootfs*' | head -n1)
	[ -n "$SQ" ] || die "no ubi_rootfs volume in $1"
	unsquash "$SQ"
	ROOT=$TMP/root
else
	unsquash "$1"
	ROOT=$TMP/root
fi

FW=$ROOT/lib/firmware
[ -e "$FW/IPQ5018/q6_fw.mdt" ] && [ -e "$FW/IPQ5018/$IPQ_BDF" ] &&
	[ -e "$FW/qcn9000/bdwlan.ba0" ] || die "stock WiFi files not found in $1"

grep -aq 'WLAN.HK.2.6-02451' "$FW"/IPQ5018/q6_fw.b* ||
	echo "warning: Q6 firmware is not WLAN.HK.2.6-02451 (the tested version)" >&2

rm -rf "$OUT"
mkdir -p "$OUT/IPQ5018" "$OUT/QCN9074"
cp -L "$FW"/IPQ5018/q6_fw.* "$FW"/IPQ5018/m3_fw.* "$OUT/IPQ5018/"

python3 "$ENC" "$OUT/IPQ5018/board-2.bin" "$FW/IPQ5018/$IPQ_BDF" \
	"bus=ahb,qmi-chip-id=0,qmi-board-id=35,variant=$VARIANT" \
	"bus=ahb,qmi-chip-id=0,qmi-board-id=35" \
	"bus=ahb,qmi-chip-id=0,qmi-board-id=255,variant=$VARIANT" \
	"bus=ahb,qmi-chip-id=0,qmi-board-id=255"
python3 "$ENC" "$OUT/QCN9074/board-2.bin" "$FW/qcn9000/bdwlan.ba0" \
	"$QCN9074,variant=$VARIANT" "$QCN9074"

echo "stock WiFi files written to $OUT:"
ls "$OUT/IPQ5018" "$OUT/QCN9074"
