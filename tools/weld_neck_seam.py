#!/usr/bin/env python3
"""Weld the neck ring in the RLY_* head morphs.

Every native COtR/vanilla head morph holds the neck-ring vertices to within
1e-5 game units, because that ring is where the head mesh meets the body mesh.
A morph that moves it opens a visible neck seam, since the body does not follow.

The RLY_* morphs violate that invariant (RLY_Heavy by ~2.7 units), so this
rescales their deltas by a smoothstep weight that is exactly 0 at the ring and
reaches 1 at `--band` units above it. The ring is pinned, the transition has no
crease, and the sculpt above the band is untouched.

Only the delta blocks of the named morphs are rewritten; every other byte of the
file, including the header, base vertices, topology and native morphs, is copied
through unchanged.
"""

import argparse
import shutil
import struct
from datetime import datetime
from pathlib import Path

MAGIC = b"FRTRI003"


class Tri:
    """FRTRI003 reader that records where each morph's delta block lives."""

    def __init__(self, data: bytes):
        if data[0:8] != MAGIC:
            raise ValueError(f"not FRTRI003: {data[0:8]!r}")

        self.data = data
        off = 8

        (self.vertex_count,) = struct.unpack_from("<I", data, off)
        off += 4

        (polytris, _polyquads, _u2, _u3, uvverts, _flags,
         num_morphs, _num_mods, _mod_verts,
         _u7, _u8, _u9, _u10) = struct.unpack_from("<13I", data, off)
        off += 13 * 4

        self.base = struct.unpack_from(f"<{self.vertex_count * 3}f", data, off)
        off += self.vertex_count * 3 * 4

        off += polytris * 3 * 4
        if uvverts > 0:
            off += uvverts * 2 * 4
        off += polytris * 3 * 4

        # name -> (multiplier, delta block offset)
        self.morphs: dict[str, tuple[float, int]] = {}

        for _ in range(num_morphs):
            (str_len,) = struct.unpack_from("<I", data, off)
            off += 4
            name = data[off:off + str_len].decode("utf-8", "replace").rstrip("\x00")
            off += str_len

            (mult,) = struct.unpack_from("<f", data, off)
            off += 4

            self.morphs[name] = (mult, off)
            off += self.vertex_count * 6

    def deltas(self, name: str) -> tuple[int, ...]:
        _mult, off = self.morphs[name]
        return struct.unpack_from(f"<{self.vertex_count * 3}h", self.data, off)

    def z(self, vertex: int) -> float:
        return self.base[vertex * 3 + 2]

    def displacement(self, name: str, vertex: int) -> float:
        mult, _off = self.morphs[name]
        d = self.deltas(name)
        x, y, z = d[vertex * 3], d[vertex * 3 + 1], d[vertex * 3 + 2]
        return ((x * x + y * y + z * z) ** 0.5) * mult


def smoothstep(t: float) -> float:
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)


def neck_ring(tri: Tri, fraction: float) -> list[int]:
    """Vertices in the bottom `fraction` of the head's height."""
    zs = [tri.z(i) for i in range(tri.vertex_count)]
    zmin, zmax = min(zs), max(zs)
    cutoff = zmin + (zmax - zmin) * fraction
    return [i for i in range(tri.vertex_count) if zs[i] <= cutoff]


def weld(tri: Tri, names: list[str], band: float,
         ring_fraction: float) -> tuple[bytearray, dict[str, float]]:
    out = bytearray(tri.data)

    zs = [tri.z(i) for i in range(tri.vertex_count)]
    zmin, zmax = min(zs), max(zs)

    # The ring is a contoured edge loop spanning ~0.8 units, not a flat plane,
    # so the falloff starts at the top of the ring band. Everything at or below
    # it is pinned to exactly zero; above it the morph fades back in.
    ring_top = zmin + (zmax - zmin) * ring_fraction

    weights = [smoothstep((z - ring_top) / band) for z in zs]
    retained: dict[str, float] = {}

    for name in names:
        if name not in tri.morphs:
            continue

        _mult, off = tri.morphs[name]
        d = tri.deltas(name)

        before = sum(abs(v) for v in d)
        scaled = []
        for i in range(tri.vertex_count):
            w = weights[i]
            for axis in range(3):
                scaled.append(int(round(d[i * 3 + axis] * w)))

        struct.pack_into(f"<{tri.vertex_count * 3}h", out, off, *scaled)

        after = sum(abs(v) for v in scaled)
        retained[name] = (after / before) if before else 1.0

    return out, retained


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--band", type=float, default=6.0,
                        help="units above the ring over which the morph fades back in")
    parser.add_argument("--ring-fraction", type=float, default=0.02,
                        help="bottom fraction of head height treated as the ring")
    parser.add_argument("--prefix", default="RLY_",
                        help="only morphs with this prefix are rewritten")
    parser.add_argument("--apply", action="store_true",
                        help="write the files; without it this is a dry run")
    args = parser.parse_args()

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    for path in args.files:
        data = path.read_bytes()
        tri = Tri(data)
        targets = [n for n in tri.morphs if n.startswith(args.prefix)]
        ring = neck_ring(tri, args.ring_fraction)

        print(f"\n=== {path.name} ===")
        print(f"  {tri.vertex_count} vertices, {len(tri.morphs)} morphs, "
              f"ring = {len(ring)} vertices, band = {args.band}")

        native_worst = max(
            (max(tri.displacement(n, i) for i in ring)
             for n in tri.morphs if not n.startswith(args.prefix)),
            default=0.0)
        print(f"  pior nativo no anel: {native_worst:.5f}  (o alvo)")

        for name in targets:
            print(f"  {name:<16} anel antes = "
                  f"{max(tri.displacement(name, i) for i in ring):.5f}")

        out, retained = weld(tri, targets, args.band, args.ring_fraction)
        after = Tri(bytes(out))

        print("  --- depois ---")
        for name in targets:
            worst = max(after.displacement(name, i) for i in ring)
            print(f"  {name:<16} anel = {worst:.5f}   "
                  f"escultura preservada = {retained[name] * 100:.1f}%")

        # Nothing outside the rewritten morphs may change.
        assert len(out) == len(data), "file size changed"
        for name, (_m, off) in tri.morphs.items():
            if name.startswith(args.prefix):
                continue
            span = tri.vertex_count * 6
            assert out[off:off + span] == data[off:off + span], \
                f"native morph {name} was modified"
        first = min(off for _m, off in tri.morphs.values())
        assert out[:first] == data[:first], "header/geometry changed"

        if args.apply:
            backup = path.with_suffix(path.suffix + f".{stamp}.bak")
            shutil.copy2(path, backup)
            path.write_bytes(bytes(out))
            print(f"  ESCRITO. backup -> {backup.name}")
        else:
            print("  (dry run; use --apply para escrever)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
