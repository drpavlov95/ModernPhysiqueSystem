import struct

from tools.racemenu_swf_patch.generate_binary_patch import generate_binary_patch


def _reference_apply(stock: bytes, patch: bytes) -> bytes:
    """Independent, test-only reference decoder for the MPS1 wire format
    documented in generate_binary_patch.py and src/core/SwfPatchFormat.h.
    If this and the shipped C++ decoder (tested separately in
    tests/cpp/test_swfpatchformat.cpp) agree on the same spec, the format
    is pinned correctly on both sides without a cross-language test.
    """
    assert patch[0:4] == b"MPS1"
    patched_length = struct.unpack_from("<I", patch, 4)[0]
    out = bytearray()
    pos = 8
    while True:
        opcode = patch[pos]
        pos += 1
        if opcode == 0xFF:
            break
        if opcode == 0x00:
            offset, length = struct.unpack_from("<II", patch, pos)
            pos += 8
            out += stock[offset:offset + length]
        elif opcode == 0x01:
            length = struct.unpack_from("<I", patch, pos)[0]
            pos += 4
            out += patch[pos:pos + length]
            pos += length
        else:
            raise AssertionError(f"unknown opcode {opcode!r}")
    assert len(out) == patched_length
    return bytes(out)


def test_generate_binary_patch_round_trips_through_reference_decoder():
    stock = b"AAAABBBBCCCC"
    patched = b"AAAA--BBBB--CCCC"
    patch = generate_binary_patch(stock, patched)
    assert _reference_apply(stock, patch) == patched


def test_generate_binary_patch_handles_identical_input():
    stock = b"nothing changed here"
    patch = generate_binary_patch(stock, stock)
    assert _reference_apply(stock, patch) == stock


def test_generate_binary_patch_handles_a_pure_deletion():
    stock = b"AAAABBBBCCCC"
    patched = b"AAAACCCC"  # the BBBB region is deleted, contributing no output bytes
    patch = generate_binary_patch(stock, patched)
    assert _reference_apply(stock, patch) == patched
