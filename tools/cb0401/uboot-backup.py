#!/usr/bin/env python3
"""Back up the CB0401V2 NAND from U-Boot, through the C3 rescue console.

Read-only: for every stock partition it runs `nand read` into RAM, `crc32`,
and `tftpput` to the TFTP server (which must accept uploads), then checks the
uploaded file's size and CRC32 against U-Boot's. The partition table (MIBIB)
is backed up and checked first; on an unexpected layout nothing else runs.

The serial link drops characters, so commands are typed slowly, the echo is
checked before Enter, and a mangled line is cancelled and typed again.

  uboot-backup.py --dir /mnt/c/.../cb0401-dump [--host 192.168.4.1] [--modem]

Put the router at the U-Boot prompt first (C3: !hold, then restart it), or
pass --hold to have the script arm it and wait for the restart. Restart from
the web UI or with `reboot`, not by power-cycling: !hold lives in the C3's
RAM and is lost if the C3 loses power with the router.
"""
import argparse
import os
import re
import socket
import struct
import sys
import time
import zlib

PROMPT = b"IPQ5018#"
UNLOCK = b"::cb0401-magic::"   # enables the C3's ! commands
LOADADDR = 0x44000000
RAM_WINDOW = 0x6000000          # 0x44000000..0x4a000000 is free under U-Boot
CHUNK = 0x4000000               # modem_image is read in pieces that fit
BLOCK = 128 * 1024

# name, first block, block count: the stock MIBIB. The boot chain up to
# crash_syslog is identical; the CB0401V2 keeps a 147 MiB modem_image at the
# end, the CB0401 (the original) has larger rootfs/overlay and no modem_image.
_COMMON = [
    ("0:SBL1", 0, 4), ("0:MIBIB", 4, 4), ("0:BOOTCONFIG", 8, 2),
    ("0:BOOTCONFIG1", 10, 2), ("0:QSEE", 12, 8), ("0:QSEE_1", 20, 8),
    ("0:DEVCFG", 28, 2), ("0:DEVCFG_1", 30, 2), ("0:CDT", 32, 2),
    ("0:CDT_1", 34, 2), ("0:APPSBLENV", 36, 4), ("0:APPSBL", 40, 10),
    ("0:APPSBL_1", 50, 10), ("0:ART", 60, 8), ("0:TRAINING", 68, 4),
    ("bdata", 72, 4), ("crash", 76, 4), ("crash_syslog", 80, 4),
]
EXPECTED_V2 = _COMMON + [
    ("rootfs", 84, 248), ("rootfs_1", 332, 248), ("overlay", 580, 266),
    ("modem_image", 846, 1181),
]
EXPECTED_V1 = _COMMON + [
    ("rootfs", 84, 288), ("rootfs_1", 372, 288), ("overlay", 660, 344),
]
LAYOUTS = {"CB0401V2": EXPECTED_V2, "CB0401": EXPECTED_V1}


def parse_mibib(data):
    """Partition table from a MIBIB dump: [(name, first block, blocks)]."""
    t = data[0x800:]
    magic1, magic2, _ver, n = struct.unpack_from("<IIII", t, 0)
    if (magic1, magic2) != (0x55EE73AA, 0xE35EBDDB):
        return None
    out = []
    for i in range(n):
        name, off, ln, _attr = struct.unpack_from("<16sIII", t, 16 + i * 28)
        out.append((name.split(b"\0")[0].decode(), off, ln))
    return out


class Console:
    def __init__(self, host, port):
        self.s = socket.create_connection((host, port), timeout=10)
        self.s.settimeout(0.2)
        self.buf = b""

    def read(self, t):
        end = time.time() + t
        while time.time() < end:
            try:
                d = self.s.recv(65536)
                if not d:
                    raise ConnectionError("console closed")
                self.buf += d
            except socket.timeout:
                pass
        b, self.buf = self.buf, b""
        return b

    def wait(self, pattern, timeout):
        """Read until pattern (bytes) shows up; return everything read."""
        end = time.time() + timeout
        got = b""
        while time.time() < end:
            got += self.read(0.3)
            if pattern in got:
                return got
        raise TimeoutError("no %r within %ds" % (pattern, timeout))

    def send(self, data):
        self.s.sendall(data)

    def type_line(self, line, tries=4):
        """Type a command slowly, check the echo, then press Enter."""
        for _ in range(tries):
            self.read(0.2)
            echo = b""
            for i in range(0, len(line), 4):
                self.send(line[i:i + 4].encode())
                time.sleep(0.03)
                echo += self.read(0.02)
            echo += self.read(0.3)
            text = re.sub(rb"[^\x20-\x7e]", b"", echo).decode()
            if text.endswith(line):
                self.send(b"\r")
                return
            self.send(b"\x03")                  # mangled: drop the line
            self.wait(PROMPT, 5)
        raise RuntimeError("could not type %r cleanly" % line)

    def run(self, line, timeout=30):
        self.type_line(line)
        out = self.wait(PROMPT, timeout)
        return out.decode("ascii", "replace")


def dump(con, args, name, offset, size):
    """nand read + crc32 + tftpput one range, verify the uploaded file."""
    out = con.run("nand read 0x%x 0x%x 0x%x" % (LOADADDR, offset, size),
                  timeout=60 + size // 0x100000)
    if "bytes read: OK" not in out:
        raise RuntimeError("nand read %s failed:\n%s" % (name, out))
    out = con.run("crc32 0x%x 0x%x" % (LOADADDR, size), timeout=60)
    m = re.search(r"==> ([0-9a-f]{8})", out)
    if not m:
        raise RuntimeError("no crc32 for %s:\n%s" % (name, out))
    crc = int(m.group(1), 16)

    path = os.path.join(args.dir, name)
    for attempt in range(3):
        if os.path.exists(path):
            os.remove(path)
        try:
            out = con.run("tftpput 0x%x 0x%x %s" % (LOADADDR, size, name),
                          timeout=120 + size // 0x20000)
        except TimeoutError:
            con.send(b"\x03")                  # stuck transfer: abort it
            out = con.wait(PROMPT, 30).decode("ascii", "replace")
        if " Up " not in out:
            raise RuntimeError("no Ethernet link on lan1 (U-Boot eth0):\n" + out)
        time.sleep(1)
        if (os.path.exists(path) and os.path.getsize(path) == size and
                zlib.crc32(open(path, "rb").read()) == crc):
            print("  %-26s 0x%08x  crc32 %08x  ok" % (name, size, crc))
            return
        print("  %s: upload incomplete (try %d), retrying" % (name, attempt + 1))
    raise RuntimeError("upload of %s failed" % name)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--host", default="192.168.4.1")
    ap.add_argument("--port", type=int, default=2323)
    ap.add_argument("--dir", required=True,
                    help="local path of the TFTP server's upload directory")
    ap.add_argument("--hold", action="store_true",
                    help="arm !hold and wait for the router to be restarted")
    ap.add_argument("--modem", action="store_true",
                    help="also dump modem_image (147 MiB, never written)")
    args = ap.parse_args()

    con = Console(args.host, args.port)
    con.read(1)
    # the C3 forwards the unlock string to the router before it matches it:
    # cancel that line on the router side
    con.send(UNLOCK)
    con.read(1)
    con.send(b"\x03")
    con.read(1)
    if args.hold:
        con.send(b"!hold\r")
        print("C3 armed: restart the router now (web UI or reboot, no power-cycle)")
        con.wait(b"HOLD at U-Boot prompt", 300)
    con.send(b"\r")
    con.wait(PROMPT, 10)
    print("at the U-Boot prompt")
    con.run("setenv ipaddr 192.168.31.1")
    con.run("setenv serverip 192.168.31.100")
    print(con.run("smeminfo").strip())

    # 1. the partition table, checked before anything else
    dump(con, args, "mtd1_0_MIBIB.bin", 4 * BLOCK, 4 * BLOCK)
    table = parse_mibib(open(os.path.join(args.dir, "mtd1_0_MIBIB.bin"), "rb").read())
    layout = next((n for n, t in LAYOUTS.items() if table == t), None)
    if not layout:
        print("UNEXPECTED partition table, stopping:", file=sys.stderr)
        for e in table or []:
            print("  %-14s %5d %5d" % e, file=sys.stderr)
        sys.exit(1)
    expected = LAYOUTS[layout]
    print("partition table as expected (%s)" % layout)

    # 2. every partition, as nanddump names them
    for idx, (name, first, count) in enumerate(expected):
        fname = "mtd%d_%s.bin" % (idx, name.replace(":", "_"))
        size = count * BLOCK
        if name == "0:MIBIB":
            continue
        if size <= RAM_WINDOW:
            dump(con, args, fname, first * BLOCK, size)
        elif args.modem:
            for part, off in enumerate(range(0, size, CHUNK)):
                dump(con, args, "%s.part%d" % (fname, part),
                     first * BLOCK + off, min(CHUNK, size - off))
        else:
            print("  %-26s skipped (--modem to include)" % fname)
    print("done; keep these files private (ART, bdata, overlay hold MACs, serial, keys)")


if __name__ == "__main__":
    main()
