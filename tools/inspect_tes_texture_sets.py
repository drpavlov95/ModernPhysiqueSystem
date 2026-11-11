#!/usr/bin/env python3
"""Inspect ARMA texture-set references and TXST paths in Bethesda plugins."""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path


COMPRESSED = 0x00040000


def subrecords(payload: bytes):
    offset = 0
    extended_size = None
    while offset + 6 <= len(payload):
        signature = payload[offset : offset + 4].decode("ascii", errors="replace")
        size = struct.unpack_from("<H", payload, offset + 4)[0]
        offset += 6
        if signature == "XXXX":
            if size != 4 or offset + 4 > len(payload):
                return
            extended_size = struct.unpack_from("<I", payload, offset)[0]
            offset += 4
            continue
        if extended_size is not None:
            size = extended_size
            extended_size = None
        if offset + size > len(payload):
            return
        yield signature, payload[offset : offset + size]
        offset += size


def records(blob: bytes, start: int = 0, end: int | None = None):
    if end is None:
        end = len(blob)
    offset = start
    while offset + 24 <= end:
        signature = blob[offset : offset + 4]
        if signature == b"GRUP":
            size = struct.unpack_from("<I", blob, offset + 4)[0]
            if size < 24 or offset + size > end:
                return
            yield from records(blob, offset + 24, offset + size)
            offset += size
            continue

        size, flags, form_id = struct.unpack_from("<III", blob, offset + 4)
        payload_start = offset + 24
        payload_end = payload_start + size
        if payload_end > end:
            return
        payload = blob[payload_start:payload_end]
        if flags & COMPRESSED:
            if len(payload) < 4:
                return
            expected = struct.unpack_from("<I", payload, 0)[0]
            payload = zlib.decompress(payload[4:])
            if len(payload) != expected:
                raise ValueError(
                    f"decompressed record size mismatch at 0x{offset:X}: "
                    f"expected {expected}, got {len(payload)}"
                )
        yield signature.decode("ascii", errors="replace"), form_id, payload
        offset = payload_end


def cstring(value: bytes) -> str:
    return value.split(b"\0", 1)[0].decode("utf-8", errors="replace")


parser = argparse.ArgumentParser()
parser.add_argument("plugin", type=Path)
parser.add_argument("--match", action="append", default=[])
args = parser.parse_args()

all_records = list(records(args.plugin.read_bytes()))
txst = {}
selected = []
need = set()
for signature, form_id, payload in all_records:
    fields = list(subrecords(payload))
    editor_id = next((cstring(data) for name, data in fields if name == "EDID"), "")
    if signature == "TXST":
        txst[form_id & 0x00FFFFFF] = (editor_id, fields)
    if signature == "ARMA" and (
        not args.match or any(token.lower() in editor_id.lower() for token in args.match)
    ):
        refs = []
        for name, data in fields:
            if name in {"NAM0", "NAM1", "NAM2", "NAM3"} and len(data) == 4:
                ref = struct.unpack("<I", data)[0]
                refs.append((name, ref))
                need.add(ref & 0x00FFFFFF)
        selected.append((form_id, editor_id, refs))

print(args.plugin)
for form_id, editor_id, refs in selected:
    print(f"ARMA 0x{form_id:08X} {editor_id}")
    for field, ref in refs:
        entry = txst.get(ref & 0x00FFFFFF)
        print(f"  {field}=0x{ref:08X}")
        if entry is None:
            print("    TXST=<external or missing>")
            continue
        txst_editor_id, fields = entry
        print(f"    TXST={txst_editor_id}")
        for name, data in fields:
            if name.startswith("TX") and len(name) == 4:
                print(f"    {name}={cstring(data)}")
