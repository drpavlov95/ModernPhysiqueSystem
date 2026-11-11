#!/usr/bin/env python3
"""Report current wrist, ankle, and neck normal discontinuities."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from align_skin_seam_normals import (
    Mesh,
    angle_stats,
    geometric_normals,
    matched_pairs,
    normalize,
    read_head_tri,
    weld_duplicate_normals,
)


parser = argparse.ArgumentParser()
parser.add_argument("--body", type=Path, required=True)
parser.add_argument("--hands", type=Path, required=True)
parser.add_argument("--feet", type=Path, required=True)
parser.add_argument("--head-tri", type=Path, required=True)
parser.add_argument("--tolerance", type=float, default=0.001)
args = parser.parse_args()

body = Mesh(args.body, "Softbody")
hands = Mesh(args.hands, "Hands")
feet = Mesh(args.feet, "Feet")
head_vertices, head_triangles = read_head_tri(args.head_tri)
head_normals = geometric_normals(head_vertices, head_triangles)
weld_duplicate_normals(head_vertices, head_normals, args.tolerance)

wrist = matched_pairs(body.vertices, hands.vertices, args.tolerance)
ankle = matched_pairs(body.vertices, feet.vertices, args.tolerance)
neck = matched_pairs(body.vertices, head_vertices, args.tolerance)
print(json.dumps({
    "wrist": angle_stats(normalize(body.original_normals), normalize(hands.original_normals), wrist),
    "ankle": angle_stats(normalize(body.original_normals), normalize(feet.original_normals), ankle),
    "neck": angle_stats(body.original_normals, head_normals, neck),
}, indent=2))
