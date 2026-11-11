"""Read and write BodySlide .osd binary files.

OSD format (from BodySlide source DiffData.cpp):
  [4]  magic   = b'\x00DSO' on little-endian Windows builds. BodySlide writes
                 the multi-character integer literal ``"OSD\\0"_mci``;
                 its bytes therefore appear reversed in the file.
  [4]  version = uint32 (always 1)
  [4]  count   = uint32 (number of morph entries)
  For each entry:
    [1]  nameLen = uint8
    [N]  name    = bytes[nameLen]  (NOT null-terminated)
                   format: {shapeTargetName}{sliderName}
    [2]  nDeltas = uint16
    For each delta (14 bytes, no padding):
      [2]  index = uint16
      [4]  x     = float32
      [4]  y     = float32
      [4]  z     = float32
"""
import struct
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class MorphEntry:
    name: str
    # deltas: vertex_index -> (dx, dy, dz)
    deltas: dict[int, tuple[float, float, float]] = field(default_factory=dict)


def read_osd(path: Path) -> list[MorphEntry]:
    data = path.read_bytes()
    pos = 0

    magic = data[pos:pos+4]
    # Accept the on-disk BodySlide representation and the intuitive spelling
    # used by a few third-party writers.
    if magic not in (b'\x00DSO', b'OSD\x00'):
        raise ValueError(f"Not an OSD file (bad magic): {path}")
    pos += 4

    version = struct.unpack_from('<I', data, pos)[0]
    pos += 4
    if version != 1:
        raise ValueError(f"Unsupported OSD version {version}: {path}")

    count = struct.unpack_from('<I', data, pos)[0]
    pos += 4

    entries: list[MorphEntry] = []
    for _ in range(count):
        name_len = data[pos]
        pos += 1
        name = data[pos:pos + name_len].decode('utf-8', errors='replace')
        pos += name_len

        n_deltas = struct.unpack_from('<H', data, pos)[0]
        pos += 2

        deltas: dict[int, tuple[float, float, float]] = {}
        for _ in range(n_deltas):
            idx = struct.unpack_from('<H', data, pos)[0]
            x, y, z = struct.unpack_from('<fff', data, pos + 2)
            deltas[idx] = (x, y, z)
            pos += 14  # 2 + 4 + 4 + 4

        entries.append(MorphEntry(name=name, deltas=deltas))

    return entries


def write_osd(entries: list[MorphEntry], path: Path) -> None:
    parts: list[bytes] = []
    parts.append(b'OSD\x00')
    parts.append(struct.pack('<I', 1))       # version
    parts.append(struct.pack('<I', len(entries)))

    for entry in entries:
        name_bytes = entry.name.encode('utf-8')
        parts.append(struct.pack('<B', len(name_bytes)))
        parts.append(name_bytes)
        parts.append(struct.pack('<H', len(entry.deltas)))
        for idx, (x, y, z) in sorted(entry.deltas.items()):
            parts.append(struct.pack('<Hfff', idx, x, y, z))

    path.write_bytes(b''.join(parts))
