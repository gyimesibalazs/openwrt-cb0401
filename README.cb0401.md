> **Experimental branch `cb0401-nss`.** This is the CB0401 / CB0401V2 port
> rebased onto kuncy7's [openwrt-nss-edma](https://github.com/kuncy7/openwrt-nss-edma)
> (`c3po-tag-8021q`, kernel 6.18, upstream stmmac/DSA ethernet, NSS offload;
> see [README.ipq50xx.md](README.ipq50xx.md)). The aim is NSS acceleration of
> the wired ports, the 5G modem's raw-IP WAN and the 5 GHz radio. A CB0401V2
> runs it from the NAND: the NSS core boots despite secure boot, both
> ethernet ports and the 5G modem's WAN are NSS accelerated (ECM offloads
> the modem's traffic through the NSS rmnet path), and both radios use
> NSS Wi-Fi offload. The modem WAN runs over QMAP (see "The modem data
> path" below): measured ~40 % less CPU per GB of forwarded traffic than
> raw-IP, and the modem's receive work sits on the second core. The 2.4 GHz radio runs the multi-PD WLAN.HK.2.7.0.1
> image instead of the stock 2.6 one: under NSS it completes associations and
> takes more than one AP. It is still
> experimental; the stable port is the `cb0401` branch on OpenWrt 25.12. The
> install procedure below is the same on both.

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
  the WAN by default (APN `online`, IPv4 + IPv6, QMAP) and has LuCI pages
  for cell and signal info, carrier aggregation and band locking
  (3ginfo-lite and modemband).
- **lan1 and lan2**, both gigabit (lan2 is the SGMII port of the QCA8337
  switch).
- **WiFi**: 5 GHz on the QCN9074 and 2.4 GHz on the IPQ5018 (ath11k).
- **LEDs** (`cb0401v2-leds`): 4G or 5G while the WAN is up on it (slow
  blink below RSRP -110 dBm), WiFi while an AP is up. The daemon checks
  the WiFi and WAN link state every 10 s (sysfs and one ubus call, cheap)
  but samples the modem's signal, one `mmcli` run that also sets
  ModemManager's QMI polling rate, only every 30 s by default. With
  `option signalrate` on the wwan interface (`network.wwan.signalrate`,
  the modemmanager proto's own option) it keeps to that rate instead of
  overriding it, so the weak-signal blink lags at most max(signalrate,
  10) s. The value is picked up when the wwan interface next comes up
  (`uci commit network && ifup wwan`, or `reload_config`).
- **NFC**: the tag always holds the current WiFi credentials. Android phones
  join by touching the box; iOS cannot join from a tag, but it can open an
  optional URL record.
- **Reset button**: a press of 5 s or more is a factory reset.
- **sysupgrade**, with settings kept.

Limits:

- On the stable `cb0401` branch 2.4 GHz takes one AP interface, and there
  is no NSS offload. This branch lifts both (see the note at the top).
- The WiFi board files are Xiaomi's binaries. They are not in the tree: you
  extract them from your own unit (step 4). The WiFi firmware itself is
  OpenWrt's.

## The original CB0401 (experimental)

The non-V2 CB0401 is supported too, **experimentally**. It is the same
mp03.6-c1 platform with the same RG520N modem (`17cb:0308`), so the install
works the same way; it differs in the flash layout (larger system
partitions, no `modem_image`, 128 MiB NAND), in the RAM (1 GB) and in the
NFC tag's I2C address. A unit has booted the OpenWrt 25.12 RAM image: both
radios, the ports, the modem (MHI data and its USB AT ports) and the
partition layout check all came up. The modem WAN, LEDs and NFC tag are
set up as on the V2 (the ethernet WAN stays as a fallback), but are not
tried on it yet, nor is the install to the NAND. The install scripts and `sysupgrade` detect either layout
automatically. Build its WiFi files from a CB0401 unit's own stock with
`extract-stock-firmware.sh -m cb0401`. See the `xiaomi,cb0401` device.

## The modem data path

The modem's downlink is where this SoC spends its CPU, so the branch
carries the stock firmware's recipe on upstream drivers, tuned by
measurement (CPU seconds per GB of forwarded traffic, the number that does
not depend on the cell's mood; all A/B figures below from one automated
load, eight parallel downloads pulled through the router by a wired client
at ~85 Mbit/s):

- **QMAP** (`network.wwan.multiplex='required'`, set by a uci-default): the
  modem aggregates up to 32 packets / 31744 bytes into one MHI receive
  buffer (ModemManager asks for that on `mhi_net`, patch 0104), and `rmnet`
  splits the frame into one packet per skb. Twice the stock frame size
  (15360 / 20 on a 16 KiB MRU) measured 16.0 -> 14.6 s/GB. The receive
  buffer's size is the MHI controller's MRU, 32256 bytes on this modem
  (patch 0914; the 16 KiB MTU ModemManager sets on `mhi_net` has nothing to
  do with it): the largest value whose buffer, with 128 bytes of DMA
  headroom and the 320-byte `skb_shared_info`, still fits an order-3
  (32 KiB) page block (128 + 32256 + 320 = 32704). The usual 32768 would
  cost a 64 KiB block per buffer (~127 on the ring: 8 MiB instead of
  4 MiB) and order-4 allocations, which the page allocator gives up on
  sooner. On this kernel (SLUB, 4 KiB pages) vanilla `netdev_alloc_skb()`
  is itself a page-allocator call at these sizes, with the same block
  orders, so the value stands on its own; what patch 0915 changes is that
  `mhi_net` asks for the block with GFP_KERNEL from its refill workqueue
  (vanilla asks GFP_ATOMIC), gives it 128 instead of 64 bytes of headroom,
  and returns it through the page-frag free path. With the 32256 MRU the
  device's writes (offsets 128..32384 of the block) and the
  `skb_shared_info` (at 32448) never share a cache line. The 31744-byte
  frames asked of the modem fit even if the modem counts the MAP headers
  on top (32 x 8 = 256 bytes). A board whose modem keeps the 32768 MRU
  gets one informational line from `mhi_net` about order-4 buffers, and
  nothing else changes for it. The 16.0 -> 14.6 figure was measured with
  the 32768 MRU; the 32256 MRU is not yet measured, and no difference is
  expected since the frames asked of the modem are the same. In raw-IP
  mode (no `rmnet` on the device) `mhi_net` copies a lone small packet
  out of such a buffer (patch 0916), so that a host-bound flow is not
  charged 32 KiB per packet; on the QMAP link `rmnet` already copies every
  deaggregated packet out into an skb of its own (patch 0918 sizes those
  copies, see "Buffers the firmware keeps"), so `mhi_net`
  skips its own copy when a receive handler is attached. That relies on
  the link being created with `INGRESS_DEAGGREGATION`, which libqmi always
  sets.
- **Buffers the firmware keeps**: a packet handed to the NSS is offered as
  one of its own receive buffers (`package/nss/qca-nss-drv` patch 0150,
  module parameter `rmnet_reusable`), so it is not returned, freed and
  replaced by a fresh allocation per packet; `rmnet` sizes its copies of
  full-size packets so that they qualify (kernel patch 0918,
  `rmnet.deagg_min_alloc`, default 1792; 0 turns the reuse off) and copies
  small ones exactly (`rmnet.deagg_reuse_min_len`, default 1024 bytes). The
  firmware holds the buffers for the duration of a burst (~10k, 40 MB in
  the first measurement; patch 0150's own count ~13k held idle, ~23k
  during a download, back within a minute); above `rmnet_reuse_max` held
  it is offered no more.
  - *What qualifies*: the NSS keeps a buffer whose `skb_end_offset()` is
    at least 2048 (its 1984-byte receive buffer at MTU 1500 plus
    `NET_SKB_PAD`, cache-line aligned; the buffer stays 1984 up to MTU
    1522 and is 2048 from 1523 on, threshold 2112) and below one page.
    `alloc_skb()` puts the 320-byte `skb_shared_info` at the end of the
    kmalloc object, so a kmalloc-2048 head ends at 1728 and never
    qualifies, whatever is asked for: the exact copy of a full-MTU packet
    (1500 + 8 bytes of MAP headers + 64 spacing = 1572) lands there. Any
    request from 1729 to 3776 gives a kmalloc-4096 head ending at 3776,
    which qualifies at either threshold; `deagg_min_alloc` defaults to
    1792, the alignment boundary at which the head first leaves
    kmalloc-2048, and is capped at 3776 when read, since no larger head can
    qualify and a value kmalloc could not serve would silently fail every
    copy. Above 3776 the head is kmalloc-8192 and fails the page bound.
  - *Cost*: a reusable copy is a 4 KiB head whether or not the firmware
    keeps it (truesize 4352, against 2304 for the exact copy of a
    full-MTU packet). The ones it keeps are held on top of its pool for
    the duration of a burst, as kmalloc-4096 objects in order-3 slabs of
    eight, bounded by `rmnet_reuse_max`. For a small packet the sized-up
    copy is a loss: the 4 KiB head and the cache clean of the firmware's
    whole receive buffer when it is handed over cost the same whatever
    the packet's size, which is why packets under `deagg_reuse_min_len`
    are copied exactly.
  - *Measured* on 2026-11-05/06 with precise IRQ time accounting and the
    PMU cycle counter (the earlier 14.6 -> ~12 s/GB came from tick-based
    accounting, which on this kernel misses about a third of the softirq
    time and scatters by +-1 s/GB), with a test injector feeding QMAP
    aggregates of 1400-byte packets at 200-1300 Mbit/s, on one boot: the
    sized copy with the preset flow hash (patch 0919) costs 9.8 / 9.6 /
    11.8 Gcycles/GB at 400 / 800 / 1300 Mbit/s against 11.6 / 11.2 / 13.1
    for the copy as it stood before, with both cores within 3 points of
    each other (~75 % each at 1.3 Gbit/s instead of ~83 %); the exact,
    never-reused copy (`deagg_min_alloc=0`) costs a few percent fewer
    cycles but loads the receiving core up to 20 points more. With
    512-byte packets the exact copy of small packets took 6-8 % fewer
    cycles per GB on the same boot (17.9-21.4 against 19.1-23.3
    Gcycles/GB). Over 40 minutes at 900 Mbit/s the free memory settled at
    59 MB on a RAM-booted image, with no growth. On a live cell with a
    cable client the sized copy measured 11.4 against 12.1 s/GB.
- **Nothing in the way between rmnet and the NSS** (`cb0401v2-wwan-affinity`,
  run on every QMAP link, see "Interrupts"): GRO off, because a segment GRO
  merges can only be forwarded by the host (the NSS bind handler leaves it
  there, patch 0150, as the firmware returned such segments without their
  checksum state), and with GRO on most of the downlink went that way (31.5
  CPU-s/GB instead of 15.3); and RPS to the CPU the modem's interrupts are
  *not* on. The NSS default RPS mask would steer every packet through a
  flow hash and a backlog queue to the CPU it is already on (18.9 s/GB
  instead of 12.7 over Wi-Fi); steering to the other CPU costs the same per
  GB as no RPS (12.1-12.5 vs 11.2-13.6), but the loaded core drops from
  13 % to 9 % at 90 Mbit/s, ~20 % more ceiling. The receive backlog that
  RPS feeds is 16384 packets on this board (`/etc/sysctl.d/13-cb0401-backlog.conf`,
  package `xiaomi-cb0401v2-modem`, after the 4096 of 12-nss-networking.conf):
  the receiving CPU sees short bursts of other work about every 44 s, and a
  4-minute 260-420 Mbit/s download over Wi-Fi lost 13598 packets in two of
  them at 4096, none at 16384 with 360-510 Mbit/s (2026-11-06). The price of GRO off falls
  on the flows that end on the router itself (opkg, a speedtest run from
  the box, LuCI): their segments reach the stack one by one instead of
  merged, so they cost more CPU per byte than they would with GRO. How
  much, on this box, is not yet measured; those flows are a small part of a
  CPE's traffic, and the forwarded path is where the 31.5 -> 15.3 came
  from.
- **Interrupts**: two scripts share the placement, each the only writer
  of its side, so the end state is the same whichever of the NSS bring-up,
  the modem bring-up and a link add runs last: the modem's MSI vectors on
  CPU1; `nss_queue1`, the N2H `pri_map` and the QMAP links' RPS on CPU0.
  `cb0401v2-wwan-affinity` (package `xiaomi-cb0401v2-modem`, `/usr/sbin`)
  owns the modem side: it pins the RG520N's MHI MSI vectors
  (`/sys/bus/pci/devices/0001:01:00.0/msi_irqs/`, `bhi` and `mhi` alike)
  to the last CPU, sets GRO off and RPS "all CPUs but the last" on every
  QMAP link, then runs `nss-irq-affinity`. `cb0401v2-pcie-modem` runs it
  once the driver is bound, `hotplug.d/net/30-cb0401-rmnet-link` on every
  `qmapmux*` link ModemManager creates (so a reconnect after a modem reset
  puts everything back), and it can be run by hand (no arguments: every
  existing `qmapmux*` link); the hotplug runs it in the background, as its
  last step can wait on the NSS firmware. It re-pins only the vectors not
  already on the last CPU, skips the one vector MSI allocates but the
  controller never requests (8 allocated, 7 used, no `/proc/irq` entry) and
  logs only what it moved. Without the pin all of the modem's
  receive work lands on CPU0 next to the NSS and Wi-Fi work: with it, the
  same CPU-s/GB and the busy core ~54 % -> ~19 % at the same rate.
  `nss-irq-affinity` (package `nss-tools`, run by the NSS bring-up after
  every arm as well) is the only writer of `nss_queue1` and the N2H
  `pri_map`. Its runs are serialized with `flock`, so one that
  read the modem's core before a pin cannot overwrite a newer one. With a modem on a two-core SoC it sends all four `pri_map`
  priorities to queue 1 and places `nss_queue1` on the core the modem's
  interrupts are *not* on, read from the effective affinity of the modem's
  MSI vectors (0 when there is nothing to read, e.g. a USB modem), or from
  `nss.general.modem_cpu` when that names a CPU the board has (ignored with
  a log line otherwise; not needed on the CB0401). It is idempotent down
  to the firmware: `dev.nss.rps.enable` and `pri_map` are read first and
  written only when they differ, so the reconnect-time hotplug calls send
  nothing to the firmware; before the NSS plane is armed, or while the
  core is still booting, it does nothing and says so, and the NSS bring-up
  runs it again after the arm (that run is the only one that waits for
  ath11k's copy engines, `NSS_IRQ_WAIT_CE=1`). Both cores then carry the
  same load, as on the stock firmware: 10.4-10.9 -> 8.1-10.9 CPU-s/GB and
  the busiest core 18 % -> 12-16 % over 5 GHz Wi-Fi at ~205 Mbit/s. Moving
  the modem interrupt to CPU0 with RPS on `mhi_hwip0` instead (the stock
  firmware's split) measured no better. Until now the hotplug script wrote
  `nss_queue1` itself while `nss-irq-affinity` assumed the modem on CPU0,
  so an NSS re-arm after the link existed (`/etc/init.d/nss restart`, the
  wedge watch's recovery) put the delivery back on the modem's core, and
  the modem pin was one-shot, so a re-probe could leave the interrupts on
  CPU0 with RPS steering there too (the 18.9 s/GB case). Both are gone.

Against the stock firmware, same cell and client, back to back, ~200 Mbit/s
over Wi-Fi: stock 3.0.23 9.4 CPU-s/GB (12 % / 12 %), this branch 8.1-10.9
(12-13 % / 12-16 %). Earlier figures that put this branch ~30 % ahead of
the stock came from a load the stock firmware does not accelerate and are
withdrawn.

Over 5 GHz Wi-Fi with a phone speedtest (150-190 Mbit/s): raw-IP 21.0 s/GB
with everything on CPU0, 19.5 with the interrupts on CPU1, 12.7 over QMAP
before the aggregation and buffer changes above (those were measured on the
wired path only, where everything costs ~25 % more per GB at its lower
rate). Projected from the loaded core's share, the modem path runs out of
CPU around 1 Gbit/s, which is also the wired port's speed. The cost does
not follow the CPU clock (the same at 1008 and 1296 MHz): it is memory and
I/O bound, so the way down is fewer per-packet operations, not a faster
core.

Tried and measured worse or useless, so not in the tree: deaggregation by
reference (one skb head plus a frag into the frame's page instead of a copy
per packet: 15.1 s/GB with the headers in a 128-byte head; a headless
packet is not even looked up by the firmware, which reads the IP header
from the first segment, and came back as an exception on 20 % of the
packets); ringing the firmware once per batch of injected packets instead
of per packet (no change); asking the modem for a minimum padding between
aggregated packets, which would have made room for an Ethernet header in
place (the RG520N accepts and echoes the WDA setting but pads nothing).

To go back to raw-IP: `uci delete network.wwan.multiplex; uci commit
network; reboot` (the modem keeps its data format until it is reset, so a
plain `ifup` is not enough).

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

On the CB0401V2, OpenWrt lives in the stock `modem_image` partition (147 MiB
at `0x69c0000`) as one UBI (kernel, rootfs and settings). The stock
firmware's two A/B system slots and its overlay (`rootfs`, `rootfs_1`,
`overlay`) stay on the flash untouched, so the stock U-Boot's own
`bootmiwifi` still boots the stock firmware: that is the way back if the C3
is ever removed, dead or forgotten. U-Boot keeps seeing its stock
partitions; the C3 gives it a RAM-only `mtdparts` over the OpenWrt range and
boots from it:

    setenv mtdids nand0=nand0 && setenv mtdparts mtdparts=nand0:0x93a0000@0x69c0000(fs) && ubi part fs && ubi read 0x44000000 kernel && bootm 0x44000000

`modem_image` only held a copy of the modem's firmware update package. The
modem runs its firmware from its own flash, the same package is inside
Xiaomi's `full_all` image as `modem_image.zip`, and the stock firmware
neither reads the partition at boot nor writes it (see below).

The original CB0401 has no `modem_image`, so there the three stock
partitions (`rootfs`, `rootfs_1`, `overlay`, together 115 MiB) become the
OpenWrt UBI: `0x7300000@0xa80000(fs)` in the command above, and the stock
firmware is gone from the flash once OpenWrt is installed.

Nothing else on the flash is written: the boot chain (SBL1 .. APPSBL_1), the
U-Boot environment and the calibration data (ART, bdata) stay as they came.

### Why OpenWrt moved to `modem_image` (CB0401V2)

Until 2026-10-29 OpenWrt went over `rootfs`, `rootfs_1` and `overlay` on the
CB0401V2 too, as one 95 MiB UBI at `0xa80000`. That left the box with a
single way to boot: the C3. Without it (unplugged, dead, or with its NVS
cleared) there was no bootable system on the flash, and the stock U-Boot
cannot bring one back over the network either: `tftpboot` loads at
0x44000000 while U-Boot itself runs at 0x4a92xxxx, so a file of more than
about 100 MiB overwrites the running U-Boot; the 153 MiB stock `full_all`
image can never be loaded that way, and the kernel loader refuses anything
over 16 MiB. With OpenWrt in `modem_image` the stock firmware stays in
place and `bootmiwifi` boots it with no C3 at all, which also makes the
first install reversible without a flash programmer. OpenWrt gets 147 MiB
instead of 95 MiB as well. The price: `modem_image` has to be in the backup (`--modem`, step 3),
because that is the partition the install erases now. The stock firmware
itself does not touch it: nothing reads it at boot (the stock 3.0.23 booted
with OpenWrt in it, `/data` and the modem up), and its system upgrade
(checked in the 3.0.23 and 3.0.100 scripts, `/lib/upgrade/boardupgrade.sh`)
flashes the modem from the `modem_image.zip` inside the upgrade package
with `QFirehose` / `quectel_fota`, never from or to the partition. Only an
upgrade package of the "modem image stored in flash" kind (`IMAGE_MODULE=3`)
would read it, and it fails with "the firmware have no modem image" instead
of flashing anything.

An image built before that date boots from `0x5f40000@0xa80000(fs)` and
expects its `ubi` partition there; its `sysupgrade` refuses the new layout's
image and vice versa (`cb0401-check-layout`), so a unit on the old layout
moves to the new one by a fresh install: restore the stock partitions from
the backup (see "Going back to stock"), RAM-boot the new image, install it,
and store the new boot command in the C3.

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

**Do this before anything else.** On the CB0401V2 the install erases
`modem_image`; on the original CB0401 it erases both stock system slots and
the stock overlay. This backup is the only way to put those back, and it is
also where the WiFi files in step 4 come from.

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
.. `mtd20_overlay.bin`. On the CB0401V2 pass `--modem`: it adds `modem_image`
(147 MiB, in 64 MiB pieces `mtd21_modem_image.bin.part0..2`), the partition
OpenWrt is going to take. The 105 MiB take about 10 minutes, `modem_image`
about 15 more.

Keep these files private and in more than one place. `0:ART`, `bdata` and
`overlay` hold the unit's MAC addresses, serial number and keys.

### 4. Extract the stock WiFi board files

    tools/cb0401/extract-stock-firmware.sh mtd18_rootfs.bin

The script unpacks the stock system from the UBI image in the dump. It copies
the board files of both radios into
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

    !bootcmd setenv mtdids nand0=nand0 && setenv mtdparts mtdparts=nand0:0x93a0000@0x69c0000(fs) && ubi part fs && ubi read 0x44000000 kernel && bootm 0x44000000
    !nand
    !status

On the original CB0401 the partition is `0x7300000@0xa80000(fs)` (the three
stock system partitions); the rest of the command is the same.

The boot command lives in the C3's flash (NVS), not in the U-Boot
environment, which stays untouched. From now on the router boots OpenWrt
from NAND on every boot, power-cycles included, so go on with step 8 right
away: until then there is no OpenWrt on the NAND to boot.

### 8. Install to NAND

In LuCI (http://192.168.1.1), open System -> Backup / Flash Firmware and
flash `...-squashfs-sysupgrade.bin` with **Keep settings** unticked. Or from
a shell, after copying the file over:

    sysupgrade -n /tmp/openwrt-qualcommax-ipq50xx-xiaomi_cb0401v2-squashfs-sysupgrade.bin

sysupgrade first runs `cb0401-check-layout` and refuses the image on an
unexpected partition table. Then it sees that the `ubi` partition holds no
OpenWrt UBI yet (on the CB0401V2 the stock modem package's own UBI, volume
`ubi_modem`; on the CB0401 the three stock UBI images), formats it, writes
the image and reboots into OpenWrt from NAND.

On the CB0401V2 this step rewrites `modem_image` (0x69c0000-0xfd60000); the
stock firmware stays on the flash, bootable with `bootmiwifi`. On the
original CB0401 it is the step that erases the stock firmware: it rewrites
0xa80000-0x7d80000, the stock `rootfs`, `rootfs_1` and `overlay`.

The manual way does the same with the factory image:

    scp -O ...-squashfs-factory.ubi root@192.168.1.1:/tmp/
    ssh root@192.168.1.1
    cb0401-check-layout
    grep '"ubi"' /proc/mtd           # CB0401V2: mtd21: 093a0000 00020000 "ubi" (CB0401: mtd18, 07300000)
    ubiformat /dev/mtd21 -y -f /tmp/openwrt-qualcommax-ipq50xx-xiaomi_cb0401v2-squashfs-factory.ubi
    reboot

The modem needs its stock PCIe data path (`AT+QCFG="data_interface",1,0`).
Units that were switched to USB elsewhere need it set back.

## Updates

    sysupgrade /tmp/openwrt-...-xiaomi_cb0401v2-squashfs-sysupgrade.bin

Before it writes anything, sysupgrade runs `cb0401-check-layout` and refuses
the image if the stock partition table is not the expected one. Then it
replaces the `kernel` and `rootfs` volumes inside the UBI partition. The
settings (`rootfs_data` volume) are kept, unless you pass `-n`. Nothing
outside the UBI partition is touched.

If an update does not boot, use `!tftp` on the C3 to RAM-boot a working
initramfs, and run sysupgrade from there.

## Going back to stock

### CB0401V2

The stock firmware is still on the flash. To boot it once, type `!hold` on
the C3 console, restart the router and type `bootmiwifi` (the stock
`bootcmd`) at the U-Boot prompt. To leave OpenWrt for good, remove the C3
(or at least its wire to the router's RX): the stock U-Boot autoboots the
stock firmware again. Nothing has to be written for either. `bootmiwifi`
picks the system slot from the `flag_boot_rootfs` variable and counts
failed attempts in `flag_try_sys1_failed` / `flag_try_sys2_failed`; the
backup's values are 0 (`printenv` shows them, `setenv ...; saveenv` puts
them back).

The stock firmware does not need `modem_image`: nothing reads it at boot,
and its system upgrade flashes the modem from the upgrade package, not from
the partition. To put it back anyway, restore `mtd21_modem_image.bin.part0..2` from the step 3 backup
the way described below for the CB0401, into the `ubi` partition (mtd21)
from a RAM boot (`ubidetach -m 21` first, the pieces at offsets 0,
`0x4000000` and `0x8000000`). It erases OpenWrt.

A CB0401V2 still on the old layout (OpenWrt over the stock partitions, see
"Why OpenWrt moved to `modem_image`") gets its stock firmware back the same
way as the CB0401 below, but from a RAM boot of a current image, where the
three stock partitions are separate (`stock_rootfs`, `stock_rootfs_1`,
`stock_overlay` = mtd18, mtd19, mtd20) and each dump goes to its own
partition without `-s`.

### CB0401

Restore the three partitions the install replaced from the step 3 backup.
**The empty pages must be skipped while writing.** The stock kernel's NAND
driver cannot read a page that was *programmed* with all 0xff bytes (it
reports an ECC error on it), while the dump holds the stock image's erased
pages as 0xff. A plain `nand write` or `mtd write` of the dump therefore
gives a stock system that starts but fails to mount `/data` with
`ubi_io_read: error -74 (ECC error)` and reboots in a loop.

RAM-boot OpenWrt (`!tftp`, step 6), copy the dump files to `/tmp` and write
them with `nandwrite --skip-all-ffs` (`nand-utils` is in the image). The
three stock partitions are one `ubi` partition (mtd18) in OpenWrt's view,
and the kernel may have attached it, so detach it first:

    ubidetach -m 18
    flash_erase /dev/mtd18 0 0
    nandwrite -p --skip-all-ffs /dev/mtd18 /tmp/mtd18_rootfs.bin
    nandwrite -p --skip-all-ffs -s 0x2400000 /dev/mtd18 /tmp/mtd19_rootfs_1.bin
    nandwrite -p --skip-all-ffs -s 0x4800000 /dev/mtd18 /tmp/mtd20_overlay.bin

(`nandwrite` skips bad blocks as `nanddump` skipped them. If a bad block
sits inside `rootfs` or `rootfs_1`, the next partition's start moves by one
block: check with `nand bad` at the U-Boot prompt first.) Then remove the C3
(or at least its wire to the router's RX) so that the stock U-Boot can
autoboot again.

Never write the boot chain (SBL1 .. APPSBL_1). The secure boot fuse is blown
(`is_sec_boot_enabled` at the U-Boot prompt: "secure boot fuse is enabled"),
and SBL1, QSEE and U-Boot (APPSBL) are all signed with Xiaomi's key
(`MIWIFI ROOT CA`). SBL1 checks the signature of what it loads, so a patched
U-Boot will not start. A broken boot chain cannot be repaired in software:
the backup then has to go back with an external SPI-NAND programmer.
