#!/usr/bin/env python3
"""Weld a body OBJ and a head OBJ together at the neck ring.

The head and the body are separate meshes that meet at the neck. The seam is
invisible only while the body's neck-ring vertices sit exactly on the head's.
Sculpting either mesh breaks that, and neither follows the other on its own.

A high-poly head carries a once-subdivided ring: half its boundary vertices are
the body's ring, the other half are the midpoints between them. So the match has
to be a bijection onto the "corner" half -- this asserts that rather than
trusting nearest-neighbour to land there on its own.

`--meet` says how much of the gap the BODY closes: 1.0 moves the body alone
(the head is then a fixed reference and its coordinate text is copied verbatim,
so no float round-trip can touch it), 0.0 moves the head alone, 0.5 meets in the
middle. Splitting halves each mesh's displacement, but the head ring's edges are
half the body's length, so the same displacement creases it roughly twice as
hard -- the split that balances the two is not 0.5. Measure, don't assume.

Moving the head's ring is not free in the way moving the body's is: the head is
one mesh worn against every body in the game, and every armour carries a body
with the stock ring. A head welded to one custom body seams against all of them.
Only pass `--meet` below 1.0 when the head is knowingly paired with that body.

With `--band` the correction fades into each mesh instead of stopping dead at
the ring. Snapping a ring alone moves it up to twice the local edge length while
its immediate neighbour stays put, which creases. The band carries the ring's
own displacement inward, smoothstepped to zero at `--band` units of geodesic
distance. The field is an offset, not a smoothing pass: existing sculpt detail
rides along rather than being averaged away.

Geodesic distance, not row count, because the neck is unevenly tessellated; and
not Z, because this ring spans 0.37 in Z, so no flat cutoff describes it. (The
sibling weld_neck_seam.py does key off Z -- a .tri carries no handy topology.)

Measured on heavy3.obj, by added surface gradient |dD|/|edge| -- edges between
two ring vertices excluded, since the ring conforming to the head is the point.
Band 0.4 took the seam edge from 3.23 to 1.03. Two plausible refinements were
tried and both lost, so do not re-litigate them without new measurements:

    variant                 ring->row1 max   interior max   band edge max
    no band                           3.23             --              --
    IDW p=2  + smoothstep             1.03           1.04            0.24
    IDW p=8  + smoothstep             1.48           2.55            0.37
    harmonic (uniform Laplace)        1.42           0.78            0.41

A sharper IDW falloff localises the delta but tears it laterally; the harmonic
field spends its whole decay in the dense rows next to the ring, which is the
one place the seam cannot afford it.
"""

import argparse
import heapq
import math
import shutil
from collections import defaultdict
from datetime import datetime
from pathlib import Path

Vec = tuple[float, float, float]


def smoothstep(t: float) -> float:
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)


def fmt(v: float) -> str:
    """Match OutfitStudio's %g style, but never in exponent form.

    %.6f would round a near-zero coordinate down to two significant figures, so
    the fallback keeps enough decimals to preserve six either way.
    """
    s = f"{v:.6g}"
    if "e" in s or "E" in s:
        s = f"{v:.12f}".rstrip("0").rstrip(".")
        if s in ("", "-"):
            s = "0"
    return s


class Obj:
    """OBJ reader that remembers the source line and source text of every vertex."""

    def __init__(self, path: Path):
        self.path = path
        # newline="" keeps CRLF intact; universal-newline mode would erase it.
        with open(path, "r", errors="replace", newline="") as fh:
            self.lines = fh.read().splitlines(keepends=True)

        self.verts: list[Vec] = []
        self.vert_line: list[int] = []
        self.vert_text: list[tuple[str, str, str]] = []
        self.faces: list[list[int]] = []

        for n, line in enumerate(self.lines):
            if line.startswith("v "):
                p = line.split()
                self.vert_line.append(n)
                self.vert_text.append((p[1], p[2], p[3]))
                self.verts.append((float(p[1]), float(p[2]), float(p[3])))
            elif line.startswith("f "):
                self.faces.append([int(t.split("/")[0]) - 1 for t in line.split()[1:]])

        self.canon = self._canonical()
        self.adj = self._adjacency()

    def _canonical(self, eps: float = 1e-5) -> list[int]:
        """Merge vertices sharing a position, so UV seams do not read as holes."""
        seen: dict[tuple[int, int, int], int] = {}
        canon = [0] * len(self.verts)
        for i, p in enumerate(self.verts):
            key = (round(p[0] / eps), round(p[1] / eps), round(p[2] / eps))
            canon[i] = seen.setdefault(key, i)
        return canon

    def _adjacency(self) -> dict[int, set[int]]:
        adj: dict[int, set[int]] = defaultdict(set)
        for f in self.faces:
            for i in range(len(f)):
                a, b = self.canon[f[i]], self.canon[f[(i + 1) % len(f)]]
                if a != b:
                    adj[a].add(b)
                    adj[b].add(a)
        return adj

    def boundary_loops(self) -> list[list[int]]:
        """Holes in the mesh, each returned in cyclic order around the hole."""
        use: dict[tuple[int, int], int] = defaultdict(int)
        for f in self.faces:
            for i in range(len(f)):
                a, b = self.canon[f[i]], self.canon[f[(i + 1) % len(f)]]
                if a != b:
                    use[(min(a, b), max(a, b))] += 1

        badj: dict[int, list[int]] = defaultdict(list)
        for (a, b), count in use.items():
            if count == 1:
                badj[a].append(b)
                badj[b].append(a)

        loops, seen = [], set()
        for start in badj:
            if start in seen:
                continue
            # Walk the cycle so midpoints can be told from corners by position.
            loop, prev, cur = [start], None, start
            seen.add(start)
            while True:
                nxt = next((w for w in badj[cur] if w != prev and w not in seen), None)
                if nxt is None:
                    break
                loop.append(nxt)
                seen.add(nxt)
                prev, cur = cur, nxt
            loops.append(loop)
        return loops

    def centroid(self, loop: list[int]) -> Vec:
        n = len(loop)
        return tuple(sum(self.verts[i][a] for i in loop) / n for a in range(3))

    def geodesic(self, sources: list[int], limit: float) -> dict[int, float]:
        """Dijkstra out from the ring, stopping once past `limit`."""
        dist = {v: 0.0 for v in sources}
        pq = [(0.0, v) for v in sources]
        heapq.heapify(pq)
        while pq:
            d, u = heapq.heappop(pq)
            if d > dist.get(u, math.inf) or d > limit:
                continue
            for w in self.adj[u]:
                nd = d + math.dist(self.verts[u], self.verts[w])
                if nd <= limit and nd < dist.get(w, math.inf):
                    dist[w] = nd
                    heapq.heappush(pq, (nd, w))
        return dist

    def propagate(self, anchors: dict[int, Vec], band: float) -> dict[int, Vec]:
        """Anchor displacements, faded into the mesh over `band` geodesic units.

        Returns canonical index -> new absolute position, anchors included.
        """
        out = {i: tuple(self.verts[i][a] + anchors[i][a] for a in range(3))
               for i in anchors}
        if band <= 0:
            return out

        keys = list(anchors)
        for v, g in self.geodesic(keys, band).items():
            if v in anchors:
                continue
            w = smoothstep(1.0 - g / band)
            if w <= 0.0:
                continue
            # Inverse-distance blend: smooth everywhere, and it collapses to the
            # nearest anchor's delta as you approach it. A sharper power or a
            # harmonic solve both measured worse -- see the module docstring.
            acc, wsum = [0.0, 0.0, 0.0], 0.0
            for i in keys:
                om = 1.0 / max(math.dist(self.verts[v], self.verts[i]) ** 2, 1e-12)
                wsum += om
                for a in range(3):
                    acc[a] += om * anchors[i][a]
            out[v] = tuple(self.verts[v][a] + w * acc[a] / wsum for a in range(3))
        return out


def rewrite(obj: Obj, moves: dict[int, Vec],
            text: dict[int, tuple[str, str, str]]) -> tuple[list[str], set[int], int]:
    """New line list with every raw vertex at a moved position updated."""
    out = list(obj.lines)
    touched_lines, raw = set(), 0
    for vi in range(len(obj.verts)):
        c = obj.canon[vi]
        if c not in moves:
            continue
        raw += 1
        n = obj.vert_line[vi]
        touched_lines.add(n)
        x, y, z = text[c] if c in text else tuple(fmt(t) for t in moves[c])
        end = ("\r\n" if out[n].endswith("\r\n")
               else "\n" if out[n].endswith("\n") else "")
        out[n] = f"v {x} {y} {z}{end}"

    assert len(out) == len(obj.lines), "contagem de linhas mudou"
    for n, (a, b) in enumerate(zip(obj.lines, out)):
        if n not in touched_lines:
            assert a == b, f"linha {n} mudou fora da regiao: {a!r} -> {b!r}"
    return out, touched_lines, raw


def describe(obj: Obj, loops: list[list[int]], chosen: list[int], label: str) -> None:
    print(f"  {label}: {len(obj.verts)} vertices, {len(obj.faces)} faces, "
          f"{len(loops)} furos")
    for loop in sorted(loops, key=len, reverse=True):
        z = [obj.verts[i][2] for i in loop]
        mark = "   <- anel do pescoco" if loop is chosen else ""
        print(f"    furo n={len(loop):4d}  Zc={sum(z) / len(z):8.3f}  "
              f"Z[{min(z):8.3f},{max(z):8.3f}]{mark}")


def match_rings(body: Obj, brng: list[int], head: Obj, hrng: list[int]) -> dict[int, int]:
    """Body ring vertex -> head ring vertex, required to be one-to-one."""
    pairs = {i: min(hrng, key=lambda j: math.dist(body.verts[i], head.verts[j]))
             for i in brng}
    targets = list(pairs.values())
    if len(set(targets)) != len(targets):
        clashes = [t for t in set(targets) if targets.count(t) > 1]
        raise SystemExit(
            f"ERRO: o casamento nao e bijetivo -- {len(clashes)} vertice(s) da "
            f"cabeca reivindicados por mais de um do corpo. Os aneis divergiram "
            f"demais para casar por proximidade; alinhe-os na OutfitStudio antes.")
    return pairs


def check_cyclic(brng: list[int], hrng: list[int],
                 pairs: dict[int, int]) -> tuple[str, list[int]]:
    """Reject a matching that does not walk the head ring in order, exactly once.

    Bijectivity alone is too weak. The gap here is around four head-ring edges
    wide, so nearest-neighbour can skip along the ring and still collide with
    nobody; the result would slide vertices around the neck instead of across
    it, twisting the ring. Walking the body ring in cyclic order must therefore
    advance monotonically around the head ring and close exactly one lap.

    The two loops are walked from arbitrary ends, so opposite orientation is
    normal and not an error.
    """
    n = len(hrng)
    at = {v: k for k, v in enumerate(hrng)}
    seq = [at[pairs[v]] for v in brng]
    for name, steps in (
            ("mesma", [(seq[(k + 1) % len(seq)] - seq[k]) % n for k in range(len(seq))]),
            ("invertida", [(seq[k] - seq[(k + 1) % len(seq)]) % n for k in range(len(seq))])):
        if sum(steps) == n and all(s >= 1 for s in steps):
            return name, steps
    raise SystemExit(
        "ERRO: o casamento nao percorre o anel da cabeca em ordem. Os aneis "
        "divergiram a ponto do vizinho-mais-proximo pular posicoes, o que "
        "torceria o anel. Alinhe-os na OutfitStudio antes.")


def head_ring_moves(head: Obj, hrng: list[int], corner_delta: dict[int, Vec]) -> dict[int, Vec]:
    """Corners take their own delta; the midpoints between them take the average.

    `hrng` is in cyclic order, so a vertex that is not a corner sits between two
    that are. Leaving the midpoints behind would turn the ring into a zigzag.
    """
    moves = dict(corner_delta)
    n = len(hrng)
    for k, v in enumerate(hrng):
        if v in corner_delta:
            continue
        prev = next((hrng[(k - s) % n] for s in range(1, n)
                     if hrng[(k - s) % n] in corner_delta), None)
        nxt = next((hrng[(k + s) % n] for s in range(1, n)
                    if hrng[(k + s) % n] in corner_delta), None)
        if prev is None or nxt is None:
            continue
        moves[v] = tuple((corner_delta[prev][a] + corner_delta[nxt][a]) / 2
                         for a in range(3))
    return moves


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("body", type=Path)
    ap.add_argument("head", type=Path)
    ap.add_argument("--band", type=float, default=0.0,
                    help="unidades geodesicas em que a correcao decai a zero; "
                         "0 = so o anel (padrao)")
    ap.add_argument("--head-band", type=float, default=None,
                    help="banda da cabeca (padrao: metade de --band, porque as "
                         "arestas do anel da cabeca sao metade das do corpo)")
    ap.add_argument("--meet", type=float, default=1.0,
                    help="fracao do vao que o CORPO percorre: 1.0 so o corpo se "
                         "move (padrao), 0.5 meio a meio, 0.0 so a cabeca")
    ap.add_argument("--out", type=Path, help="destino do corpo")
    ap.add_argument("--head-out", type=Path, help="destino da cabeca")
    ap.add_argument("--apply", action="store_true",
                    help="escreve os arquivos; sem isso e dry run")
    args = ap.parse_args()

    if not 0.0 <= args.meet <= 1.0:
        raise SystemExit("ERRO: --meet precisa estar entre 0.0 e 1.0")
    head_band = args.head_band if args.head_band is not None else args.band / 2

    body, head = Obj(args.body), Obj(args.head)
    bloops, hloops = body.boundary_loops(), head.boundary_loops()
    if not bloops or not hloops:
        raise SystemExit("ERRO: um dos meshes nao tem furo de borda algum.")

    # The body's neck is its topmost hole; the head's is its bottommost.
    brng = max(bloops, key=lambda L: body.centroid(L)[2])
    hrng = min(hloops, key=lambda L: head.centroid(L)[2])

    print(f"\n=== {args.body.name} <-> {args.head.name} ===")
    print(f"  meet={args.meet}  banda corpo={args.band}  banda cabeca={head_band}")
    describe(body, bloops, brng, "corpo ")
    describe(head, hloops, hrng, "cabeca")

    gap = math.dist(body.centroid(brng), head.centroid(hrng))
    print(f"  distancia entre centroides dos aneis: {gap:.4f}")
    if gap > 1.0:
        raise SystemExit("ERRO: os aneis estao longe demais para serem o mesmo "
                         "pescoco. Os meshes estao no mesmo espaco?")

    pairs = match_rings(body, brng, head, hrng)
    orient, steps = check_cyclic(brng, hrng, pairs)
    span = sorted(math.dist(body.verts[i], head.verts[j]) for i, j in pairs.items())
    print(f"\n  anel do corpo = {len(brng)}, anel da cabeca = {len(hrng)}; "
          f"orientacao {orient}, passo {min(steps)}-{max(steps)} "
          f"(media {sum(steps) / len(steps):.2f})")
    print(f"  vao a fechar: max={span[-1]:.5f} min={span[0]:.5f} "
          f"media={sum(span) / len(span):.5f}")

    # One meeting point per pair, so both meshes land on the identical text.
    meet: dict[int, Vec] = {}
    for i, j in pairs.items():
        b, h = body.verts[i], head.verts[j]
        meet[i] = tuple(b[a] + args.meet * (h[a] - b[a]) for a in range(3))

    body_moves = body.propagate(
        {i: tuple(meet[i][a] - body.verts[i][a] for a in range(3)) for i in pairs},
        args.band)
    # At meet=1.0 the head is untouched, so the body can take its text verbatim.
    body_text = ({i: head.vert_text[pairs[i]] for i in pairs} if args.meet == 1.0
                 else {i: tuple(fmt(t) for t in meet[i]) for i in pairs})

    outputs = [(body, body_moves, body_text, "corpo ",
                args.out or args.body.with_name(args.body.stem + "_welded.obj"))]

    if args.meet < 1.0:
        corner_delta = {pairs[i]: tuple(meet[i][a] - head.verts[pairs[i]][a]
                                        for a in range(3)) for i in pairs}
        head_moves = head.propagate(head_ring_moves(head, hrng, corner_delta),
                                    head_band)
        head_text = {pairs[i]: tuple(fmt(t) for t in meet[i]) for i in pairs}
        outputs.append((head, head_moves, head_text, "cabeca",
                        args.head_out
                        or args.head.with_name(args.head.stem + "_welded.obj")))

    written = []
    for obj, moves, text, label, dest in outputs:
        lines, touched, raw = rewrite(obj, moves, text)
        ring_n = len(pairs) if label.startswith("corpo") else len(hrng)
        zs = [obj.verts[v][2] for v in moves]
        print(f"\n  {label}: {len(moves)} vertices movidos ({ring_n} no anel), "
              f"{raw} linhas 'v', alcanca Z={min(zs):.3f}")
        if not args.apply:
            print(f"    (dry run; escreveria {dest.name})")
            continue
        if dest.exists():
            stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            backup = dest.with_suffix(f".{stamp}.bak")
            shutil.copy2(dest, backup)
            print(f"    sobrescrevendo existente; backup -> {backup.name}")
        with open(dest, "w", newline="") as fh:
            fh.write("".join(lines))
        print(f"    ESCRITO -> {dest}")
        written.append((obj, dest, label))

    if not args.apply:
        return 0

    # Read both back and confirm the seam actually closed.
    print("\n  --- verificacao nos arquivos gravados ---")
    check = {label: (Obj(dest), obj) for obj, dest, label in written}
    for label, (new, old) in check.items():
        assert len(new.verts) == len(old.verts), f"{label}: vertices mudaram"
        assert len(new.faces) == len(old.faces), f"{label}: faces mudaram"
        print(f"  {label}: vertices={len(new.verts)} faces={len(new.faces)} (ok)")

    nb = check["corpo "][0]
    nh = check["cabeca"][0] if "cabeca" in check else head
    nbr = max(nb.boundary_loops(), key=lambda L: nb.centroid(L)[2])
    nhr = min(nh.boundary_loops(), key=lambda L: nh.centroid(L)[2])
    assert len(nbr) == len(brng), "o anel do corpo mudou de tamanho"
    assert len(nhr) == len(hrng), "o anel da cabeca mudou de tamanho"
    seam = max(min(math.dist(nb.verts[i], nh.verts[j]) for j in nhr) for i in nbr)
    print(f"  anel do corpo -> anel da cabeca, pior distancia = {seam:.7f}")
    assert seam < 1e-6, f"anel nao soldou: {seam}"
    print("  OK: anel soldado, topologia intacta nos dois meshes.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
