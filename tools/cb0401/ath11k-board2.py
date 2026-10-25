#!/usr/bin/env python3
# Minimal ath11k board-2.bin encoder (same layout as qca-swiss-army-knife
# ath11k-bdencoder): magic, then one BOARD IE per board name holding a
# NAME IE and a DATA IE, everything 4-byte aligned.
#
#   ath11k-board2.py OUT BDF NAME [NAME ...]
import struct
import sys

MAGIC = b"QCA-ATH11K-BOARD\0"
IE_BOARD, IE_BOARD_NAME, IE_BOARD_DATA = 0, 0, 1


def pad4(b):
    return b + b"\0" * (-len(b) % 4)


def ie(ie_id, payload):
    return struct.pack("<II", ie_id, len(payload)) + pad4(payload)


out, bdf, names = sys.argv[1], sys.argv[2], sys.argv[3:]
data = open(bdf, "rb").read()
blob = pad4(MAGIC)
for name in names:
    blob += ie(IE_BOARD, ie(IE_BOARD_NAME, name.encode()) + ie(IE_BOARD_DATA, data))
open(out, "wb").write(blob)
