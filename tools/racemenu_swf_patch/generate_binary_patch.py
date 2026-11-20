"""One-time authoring tool: given a stock and a patched byte buffer (the
decompressed SWF tag streams, in this project's actual use), produce a
binary patch in this project's own MPS1 format -- never VCDIFF/xdelta3,
no compatibility requirement with any third-party tool.

Run once per supported RaceMenu version by a maintainer who has both a
genuine stock SWF and this project's patched SWF available locally. Never
run by an end user; the shipped decoder is the C++ ApplyBinaryPatch in
src/core/SwfPatchFormat.cpp, not this script.

Wire format (also documented in src/core/SwfPatchFormat.h -- keep both in
sync):
    bytes 0-3  : magic "MPS1"
    bytes 4-7  : patchedLength, u32 little-endian
    bytes 8... : operations, each starting with a 1-byte opcode:
      0x00 COPY   : u32 LE sourceOffset, u32 LE length
      0x01 INSERT : u32 LE length, then `length` literal bytes
      0xFF END    : ends the operation sequence
"""

import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"MPS1"
OP_COPY = 0x00
OP_INSERT = 0x01
OP_END = 0xFF


def generate_binary_patch(stock: bytes, patched: bytes) -> bytes:
    min_match = 16
    max_candidates = 16

    # Index short exact anchors from the stock stream, then extend matching
    # anchors byte-for-byte. This is linear for the mostly-identical SWFs we
    # author and avoids SequenceMatcher's pathological behaviour on the many
    # repeated zero/opcode bytes in compiled ActionScript.
    anchors: dict[bytes, list[int]] = {}
    for offset in range(0, max(0, len(stock) - min_match + 1)):
        key = stock[offset : offset + min_match]
        positions = anchors.setdefault(key, [])
        if len(positions) < max_candidates:
            positions.append(offset)

    ops = bytearray()
    insert_start = 0
    patched_offset = 0

    while patched_offset < len(patched):
        best_stock_offset = -1
        best_length = 0

        if patched_offset + min_match <= len(patched):
            key = patched[patched_offset : patched_offset + min_match]
            for stock_offset in anchors.get(key, ()):
                match_length = min_match
                limit = min(len(stock) - stock_offset, len(patched) - patched_offset)
                while (
                    match_length < limit
                    and stock[stock_offset + match_length]
                    == patched[patched_offset + match_length]
                ):
                    match_length += 1
                if match_length > best_length:
                    best_stock_offset = stock_offset
                    best_length = match_length

        if best_length >= min_match:
            if insert_start < patched_offset:
                inserted = patched[insert_start:patched_offset]
                ops += bytes([OP_INSERT])
                ops += struct.pack("<I", len(inserted))
                ops += inserted

            ops += bytes([OP_COPY])
            ops += struct.pack("<II", best_stock_offset, best_length)
            patched_offset += best_length
            insert_start = patched_offset
        else:
            patched_offset += 1

    if insert_start < len(patched):
        inserted = patched[insert_start:]
        ops += bytes([OP_INSERT])
        ops += struct.pack("<I", len(inserted))
        ops += inserted
    ops += bytes([OP_END])

    header = MAGIC + struct.pack("<I", len(patched))
    return bytes(header + ops)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(
            "usage: generate_binary_patch.py <stock.swf> <patched.swf> <output.patch>",
            file=sys.stderr,
        )
        return 2

    stock_swf_path, patched_swf_path, output_path = (Path(a) for a in argv)

    def inflated_tag_stream(swf_path: Path) -> bytes:
        data = swf_path.read_bytes()
        if data[0:3] == b"FWS":
            return data[8:]
        if data[0:3] == b"CWS":
            return zlib.decompress(data[8:])
        raise ValueError(f"unrecognized SWF signature in {swf_path}: {data[0:3]!r}")

    stock_tag_stream = inflated_tag_stream(stock_swf_path)
    patched_tag_stream = inflated_tag_stream(patched_swf_path)

    patch = generate_binary_patch(stock_tag_stream, patched_tag_stream)
    output_path.write_bytes(patch)
    print(f"wrote {len(patch)} bytes to {output_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
