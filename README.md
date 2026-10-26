# OpenWrt on the Xiaomi CB0401V2 (Magenta 5G AX5400 V2)

This is a fork of [OpenWrt](https://github.com/openwrt/openwrt): the
`cb0401` branch is OpenWrt v25.12.6 plus the support for this device. For
OpenWrt itself, see its [README](https://github.com/openwrt/openwrt#readme)
and [openwrt.org](https://openwrt.org/).

The CB0401V2 is an indoor 5G CPE: Qualcomm IPQ5018 (2x Cortex-A53), 512 MiB
RAM, 256 MiB SPI-NAND, a Quectel RG520N-EB 5G modem on PCIe, QCN9074 5 GHz
and IPQ5018 2.4 GHz WiFi, two gigabit ports and an NFC tag. This branch runs
OpenWrt 25.12 on it as a complete 5G router, installed on the NAND and
updated with plain `sysupgrade`.

## What works

- **5G/4G modem**: RG520N-EB on PCIe (MHI), managed by ModemManager. It is
  the WAN by default (APN `online`, IPv4 + IPv6) and has LuCI pages for cell
  and signal info, carrier aggregation and band locking (3ginfo-lite and
  modemband).
- **lan1 and lan2**, both gigabit (lan2 is the SGMII port of the QCA8337
  switch).
- **WiFi**: 5 GHz on the QCN9074 and 2.4 GHz on the IPQ5018 (ath11k).
- **LEDs**: 4G or 5G while the WAN is up on it (slow blink on a weak
  signal), WiFi while an AP is up.
- **NFC**: the tag always holds the current WiFi credentials. Android phones
  join by touching the box; iOS cannot join from a tag, but it can open an
  optional URL record.
- **Reset button**: a press of 5 s or more is a factory reset.
- **sysupgrade**, with settings kept.

Limits:

- 2.4 GHz takes one AP interface. A second one crashes the WCSS firmware.
- No NSS offload: routing runs on the CPU (software flow offloading can be
  turned on in the firewall settings).
- The WiFi firmware and board files are Xiaomi's signed binaries. They are
  not in the tree: you extract them from your own unit (step 4).

## How it works, and why a hardware mod is needed

The unit is secure-boot fused: the boot chain only starts kernels signed by
Xiaomi, so an OpenWrt image cannot simply be flashed over the stock one. The
stock U-Boot, however, accepts `bootm` of an image loaded into RAM, and that
path skips the signature check. All it needs is someone at the serial console
who stops the autoboot and types the boot command, on every boot.

That someone is a small ESP32-C3 board
([c3-rescue](tools/cb0401/c3-rescue/)). It stays in the box, wired to the
router's serial console. On every boot it:

- stops the U-Boot autoboot;
- loads the OpenWrt kernel from NAND and starts it with `bootm`.

It also opens a WiFi AP for 90 s after power-on. The console is reachable
over that AP, and so are an OTA update page and a TFTP RAM boot mode for
tests and recovery.

The stock firmware's two A/B system slots and its overlay (`rootfs`,
`rootfs_1`, `overlay`, together 95 MiB) become one UBI partition for OpenWrt.
U-Boot keeps seeing its three stock partitions; the C3 gives it a RAM-only
`mtdparts` over the same range:

    setenv mtdids nand0=nand0 && setenv mtdparts mtdparts=nand0:0x5f40000@0xa80000(fs) && ubi part fs && ubi read 0x44000000 kernel && bootm 0x44000000

Nothing else on the flash is written: the boot chain (SBL1 .. APPSBL_1), the
U-Boot environment, the calibration data (ART, bdata) and the modem's own
partition (`modem_image`) stay as they came.

## What you need

Hardware:

- An ESP32-C3 Super Mini and two resistors (1k and 1.2k) for the 3.3 V ->
  1.8 V divider. See [c3-rescue](tools/cb0401/c3-rescue/).
- A USB-C cable to flash the C3 once; later updates go over WiFi.
- A soldering iron and thin wire for the four console wires.

A PC with two connections at the same time:

- **Wired Ethernet** to one of the router's LAN ports, with the fixed address
  **192.168.31.100/24**. U-Boot is 192.168.31.1 and expects its TFTP server
  there.
- **WiFi** to the C3's AP (`cb0401-rescue`, 192.168.4.1) for the console
  (TCP 2323).

Software on the PC:

- A TFTP server that **accepts uploads** (for the backup), with `ow.itb` in
  its root (for the RAM boot). On Linux use tftpd-hpa with `-c` (create); on
  Windows use Tftpd64. Allow UDP 69 and the data ports through the PC's
  firewall.
- A raw TCP client for the console: `nc`, or PuTTY ("Raw") on Windows.
- Python 3, for `uboot-backup.py` (standard library only) and for
  `ubi_reader` (`pip install ubi_reader`), which `extract-stock-firmware.sh`
  uses to unpack the stock dump.
- Docker, to build the C3 firmware (`c3-rescue/build.sh`).
- An OpenWrt build environment: Linux, or WSL2 on Windows.

## Step by step

### 1. Open the box and wire the C3

Take off the top cover (6 screws, then the latches) and lift off the NFC
module. **Leave the board in its frame.** The 4-pin console header can be
reached with the board in place. Taking the board out of the frame means
detaching antenna cables that do not unplug from the mainboard, and that is
how these units get damaged.

The header is 115200 8N1 at **1.8 V**:

| pin | signal | C3 |
|---|---|---|
| 1 | VCC | not connected |
| 2 | RX | GPIO7 (TX) through the divider: 1k in series, 1.2k to GND |
| 3 | GND | GND |
| 4 | TX | GPIO6 (RX), direct |

Never drive the router's RX with 3.3 V directly; the divider is the important
part. Build the C3 firmware and flash it over USB once, as described in
[c3-rescue](tools/cb0401/c3-rescue/README.md); later updates go over its OTA
page.

### 2. Get to the U-Boot prompt

Power the router on and join the C3's AP (`cb0401-rescue`) within 90 s. Then
open the console with a raw TCP client:

    nc 192.168.4.1 2323        # or PuTTY, connection type "Raw"

Type the unlock string `::cb0401-magic::`, press Ctrl-C (the C3 passes the
string on to the router too, so cancel it there), then `!hold`, and
restart the router. The C3 stops the autoboot and leaves you at `IPQ5018#`.

**Restart, do not power-cycle.** `!hold` and `!tftp` are kept only in the
C3's RAM. If the C3 is powered from the router, a power-cycle restarts it too,
and the mode is lost. Restart the router from its web UI (stock or LuCI),
with `reboot` from a shell, or with `reset` at the U-Boot prompt.

### 3. Back up the whole NAND

**Do this before anything else.** The install erases both stock system slots
and the stock overlay. This backup is the only way back to the stock firmware,
and it is also where the WiFi files in step 4 come from.

The backup runs from U-Boot, so it does not depend on the stock firmware's
SSH (which may be closed). With the router at the prompt, run on the PC:

    tools/cb0401/uboot-backup.py --dir <TFTP upload directory>

For every partition the script runs `nand read` into RAM, `crc32`, and
`tftpput` to the PC, and then checks the size and CRC of the uploaded file.
The serial link can drop characters, so commands are typed slowly and their
echo is checked before Enter. The script reads only: it never erases or
writes the flash.

The first file is the partition table (MIBIB). The script compares it with
the expected stock layout and **stops if it differs**: a unit with another
layout must not get this image. You get the nanddump names: `mtd0_0_SBL1.bin`
.. `mtd20_overlay.bin`. `modem_image` (147 MiB, never written by OpenWrt) is
included only with `--modem`. The 105 MiB take about 10 minutes.

Keep these files private and in more than one place. `0:ART`, `bdata` and
`overlay` hold the unit's MAC addresses, serial number and keys.

### 4. Extract the stock WiFi files

    tools/cb0401/extract-stock-firmware.sh mtd18_rootfs.bin

The script unpacks the stock system from the UBI image in the dump. It copies
the signed IPQ5018 WCSS firmware and the board files of both radios into
`package/firmware/xiaomi-cb0401v2-wifi-firmware/files/` (git-ignored).
`mtd19_rootfs_1.bin` works too.

To look into a dump yourself:

- `ubireader_extract_images mtd18_rootfs.bin` gives the stock kernel and the
  squashfs root, and `unsquashfs` opens the squashfs.
- `ubireader_extract_files mtd20_overlay.bin` unpacks the stock overlay
  (UBIFS), which holds the stock settings.

### 5. Build

    cp tools/cb0401/diffconfig .config
    make defconfig
    make -j$(nproc)

The images are in `bin/targets/qualcommax/ipq50xx/`:

- `...-xiaomi_cb0401v2-initramfs-uImage.itb`: RAM boot, for the install.
- `...-xiaomi_cb0401v2-squashfs-sysupgrade.bin`: for the install and for
  updates.
- `...-xiaomi_cb0401v2-squashfs-factory.ubi`: for a manual `ubiformat`
  install.

### 6. RAM-boot OpenWrt

Copy the initramfs image to the TFTP root as `ow.itb`. On the C3 console,
type `!tftp` and restart the router (see step 2). The C3 runs
`tftpboot 0x44000000 ow.itb; bootm 0x44000000`. Nothing is written: the
stock firmware is still on the flash until step 8.

OpenWrt comes up at 192.168.1.1. Move the PC to DHCP (or 192.168.1.2/24).

### 7. Switch the C3 to NAND boot

On the C3 console, store the NAND boot command once, and switch it back to
NAND mode. This only changes what the C3 does at the next boot; the running
RAM system is not affected:

    !bootcmd setenv mtdids nand0=nand0 && setenv mtdparts mtdparts=nand0:0x5f40000@0xa80000(fs) && ubi part fs && ubi read 0x44000000 kernel && bootm 0x44000000
    !nand
    !status

The boot command lives in the C3's flash (NVS), not in the U-Boot
environment, which stays untouched. From now on the router boots OpenWrt
from NAND on every boot, power-cycles included, so go on with step 8 right
away: until then there is no OpenWrt on the NAND to boot.

### 8. Install to NAND

In LuCI (http://192.168.1.1), open System -> Backup / Flash Firmware and
flash `...-squashfs-sysupgrade.bin` with **Keep settings** unticked. Or from
a shell, after copying the file over:

    sysupgrade -n /tmp/openwrt-qualcommax-ipq50xx-xiaomi_cb0401v2-squashfs-sysupgrade.bin

sysupgrade first runs `cb0401v2-check-layout` and refuses the image on an
unexpected partition table. Then it sees that the `ubi` partition holds no
OpenWrt UBI yet, formats it, writes the image and reboots into OpenWrt from
NAND.

This is the step that erases the stock firmware: it rewrites
0xa80000-0x69c0000, the stock `rootfs`, `rootfs_1` and `overlay`.

The manual way does the same with the factory image:

    scp -O ...-squashfs-factory.ubi root@192.168.1.1:/tmp/
    ssh root@192.168.1.1
    cb0401v2-check-layout
    grep '"ubi"' /proc/mtd           # mtd18: 05f40000 00020000 "ubi"
    ubiformat /dev/mtd18 -y -f /tmp/openwrt-qualcommax-ipq50xx-xiaomi_cb0401v2-squashfs-factory.ubi
    reboot

The modem needs its stock PCIe data path (`AT+QCFG="data_interface",1,0`).
Units that were switched to USB elsewhere need it set back.

## Updates

    sysupgrade /tmp/openwrt-...-xiaomi_cb0401v2-squashfs-sysupgrade.bin

Before it writes anything, sysupgrade runs `cb0401v2-check-layout` and refuses
the image if the stock partition table is not the expected one. Then it
replaces the `kernel` and `rootfs` volumes inside the UBI partition. The
settings (`rootfs_data` volume) are kept, unless you pass `-n`. Nothing
outside the UBI partition is touched.

If an update does not boot, use `!tftp` on the C3 to RAM-boot a working
initramfs, and run sysupgrade from there.

## Going back to stock

Restore the three partitions the install replaced from the step 3 backup, at
the U-Boot prompt (`!hold`, then restart). Do it one file at a time, because a
file has to fit in the 96 MiB RAM window at 0x44000000:

    tftpboot 0x44000000 mtd18_rootfs.bin
    nand erase 0xa80000 0x1f00000
    nand write 0x44000000 0xa80000 0x1f00000

    tftpboot 0x44000000 mtd19_rootfs_1.bin
    nand erase 0x2980000 0x1f00000
    nand write 0x44000000 0x2980000 0x1f00000

    tftpboot 0x44000000 mtd20_overlay.bin
    nand erase 0x4880000 0x2140000
    nand write 0x44000000 0x4880000 0x2140000

Then remove the C3 (or at least its wire to the router's RX) so that the
stock U-Boot can autoboot again.

Never write the boot chain (SBL1 .. APPSBL_1). The secure boot fuse is blown
(`is_sec_boot_enabled` at the U-Boot prompt: "secure boot fuse is enabled"),
and SBL1, QSEE and U-Boot (APPSBL) are all signed with Xiaomi's key
(`MIWIFI ROOT CA`). SBL1 checks the signature of what it loads, so a patched
U-Boot will not start. A broken boot chain cannot be repaired in software:
the backup then has to go back with an external SPI-NAND programmer.
