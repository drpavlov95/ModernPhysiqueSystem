import struct
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class MorphEntry:
    name: str
    deltas: dict[int, tuple[float, float, float]] = field(default_factory=dict)


def read_osd(path: Path) -> list[MorphEntry]:
    data = path.read_bytes()
    pos = 0
    magic = data[pos:pos+4]
    if magic != b'OSD\x00':
        raise ValueError(f"Not an OSD file: {path}")
    pos += 4
    version = struct.unpack_from('<I', data, pos)[0]
    pos += 4
    if version != 1:
        raise ValueError(f"Unsupported OSD version {version}")
    count = struct.unpack_from('<I', data, pos)[0]
    pos += 4
    entries = []
    for _ in range(count):
        name_len = data[pos]
        pos += 1
        name = data[pos:pos+name_len].decode('utf-8', errors='replace')
        pos += name_len
        n_deltas = struct.unpack_from('<H', data, pos)[0]
        pos += 2
        deltas = {}
        for _ in range(n_deltas):
            idx = struct.unpack_from('<H', data, pos)[0]
            x, y, z = struct.unpack_from('<fff', data, pos+2)
            deltas[idx] = (x, y, z)
            pos += 14
        entries.append(MorphEntry(name=name, deltas=deltas))
    return entries


def write_osd(entries: list[MorphEntry], path: Path) -> None:
    parts = []
    parts.append(b'OSD\x00')
    parts.append(struct.pack('<I', 1))
    parts.append(struct.pack('<I', len(entries)))
    for entry in entries:
        name_bytes = entry.name.encode('utf-8')
        parts.append(struct.pack('<B', len(name_bytes)))
        parts.append(name_bytes)
        parts.append(struct.pack('<H', len(entry.deltas)))
        for idx, (x, y, z) in sorted(entry.deltas.items()):
            parts.append(struct.pack('<H', idx))
            parts.append(struct.pack('<fff', x, y, z))
    path.write_bytes(b''.join(parts))
