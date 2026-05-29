"""
TRI Audit Tool for RleyBodyMorphs / ModernPhysiquePicker
Parses FRTRI003 Skyrim face morph TRI files and RaceMenu config files,
cross-checks slider routing, and reports missing/misrouted morphs.

Usage:
    python tri_audit.py [--mods-root <path>] [--output-dir <path>]

Defaults:
    --mods-root   E:/Wabbajack/InstallPacks/mods
    --output-dir  docs/tri-audit
"""

import argparse
import hashlib
import json
import os
import struct
import sys
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# TRI binary parser (FRTRI003 format, from SKEE FaceMorphInterface.cpp)
# ---------------------------------------------------------------------------

@dataclass
class TriMorph:
    name: str
    multiplier: float
    vertex_count: int
    max_abs_delta: int


@dataclass
class TriFile:
    path: str
    provider_mod: Optional[str]
    file_size: int
    sha256: str
    parse_ok: bool
    parse_error: Optional[str]
    vertex_count: int = 0
    morph_count: int = 0
    morph_names: list[str] = field(default_factory=list)
    morphs: list[TriMorph] = field(default_factory=list)

    # Convenience flags
    has_RLY_Thin: bool = False
    has_RLY_Muscular: bool = False
    has_RLY_Heavy: bool = False
    has_lip_morphs: bool = False
    has_nose_morphs: bool = False
    has_brow_morphs: bool = False
    has_eye_morphs: bool = False


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_tri(path: Path, provider_mod: Optional[str] = None) -> TriFile:
    size = path.stat().st_size
    sha = _sha256(path)
    result = TriFile(
        path=str(path),
        provider_mod=provider_mod,
        file_size=size,
        sha256=sha,
        parse_ok=False,
        parse_error=None,
    )

    try:
        data = path.read_bytes()
        offset = 0

        # Magic
        if data[0:8] != b"FRTRI003":
            result.parse_error = f"bad magic: {data[0:8]!r}"
            return result

        offset = 8

        # vertexCount
        (vertex_count,) = struct.unpack_from("<I", data, offset)
        offset += 4
        result.vertex_count = vertex_count

        # 13 header fields
        (polytris, polyquads, unk2, unk3,
         uvverts, flags, num_morphs, num_mods,
         mod_verts, unk7, unk8, unk9, unk10) = struct.unpack_from("<13I", data, offset)
        offset += 13 * 4

        # Skip reference verts: vertexCount * 3 floats
        offset += vertex_count * 3 * 4

        # Skip polytris: polytris * 3 uint32
        offset += polytris * 3 * 4

        # Skip UV verts: uvverts * 2 floats (only if uvverts > 0)
        if uvverts > 0:
            offset += uvverts * 2 * 4

        # Skip text coords: polytris * 3 uint32
        offset += polytris * 3 * 4

        morphs: list[TriMorph] = []
        names: list[str] = []

        for _ in range(num_morphs):
            # strLen + name
            (str_len,) = struct.unpack_from("<I", data, offset)
            offset += 4
            name = data[offset:offset + str_len].decode("utf-8", errors="replace").rstrip("\x00")
            offset += str_len

            # multiplier
            (mult,) = struct.unpack_from("<f", data, offset)
            offset += 4

            # vertex deltas: vertexCount * (int16 x, y, z) = vertexCount * 6 bytes
            delta_block = struct.unpack_from(f"<{vertex_count * 3}h", data, offset)
            offset += vertex_count * 6

            max_abs = max(abs(v) for v in delta_block) if delta_block else 0

            morphs.append(TriMorph(name=name, multiplier=mult,
                                   vertex_count=vertex_count, max_abs_delta=max_abs))
            names.append(name)

        result.morphs = morphs
        result.morph_names = names
        result.morph_count = len(morphs)
        result.parse_ok = True

        name_lower = {n.lower() for n in names}
        result.has_RLY_Thin = "rly_thin" in name_lower
        result.has_RLY_Muscular = "rly_muscular" in name_lower
        result.has_RLY_Heavy = "rly_heavy" in name_lower
        result.has_lip_morphs = any(n.startswith("lip") or "lip" in n.lower() for n in names)
        result.has_nose_morphs = any("nose" in n.lower() for n in names)
        result.has_brow_morphs = any("brow" in n.lower() for n in names)
        result.has_eye_morphs = any("eye" in n.lower() or "blink" in n.lower() for n in names)

    except Exception as exc:
        result.parse_error = str(exc)

    return result


# ---------------------------------------------------------------------------
# RaceMenu config parsers
# ---------------------------------------------------------------------------

@dataclass
class MorphsIniExtension:
    base_mesh: str
    extensions: list[str]


@dataclass
class SliderDef:
    name: str
    category: str
    slider_type: str
    lower_bound_morph: str
    upper_bound_morph: str


@dataclass
class RaceEntry:
    race_editor_id: str
    sliders_file: str


def parse_morphs_ini(path: Path) -> list[MorphsIniExtension]:
    entries = []
    if not path.exists():
        return entries
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith(";") or line.startswith("#"):
            continue
        if "=" not in line:
            continue
        key, _, val = line.partition("=")
        key = key.strip().lower()
        if key == "extension":
            parts = [p.strip() for p in val.split(",")]
            if parts:
                entries.append(MorphsIniExtension(base_mesh=parts[0], extensions=parts[1:]))
    return entries


def parse_races_ini(path: Path) -> list[RaceEntry]:
    entries = []
    if not path.exists():
        return entries
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith(";") or line.startswith("#"):
            continue
        if "=" not in line:
            continue
        key, _, val = line.partition("=")
        entries.append(RaceEntry(race_editor_id=key.strip(), sliders_file=val.strip()))
    return entries


def parse_sliders_ini(path: Path) -> dict[str, list[SliderDef]]:
    sections: dict[str, list[SliderDef]] = {}
    if not path.exists():
        return sections
    current_section = "Unknown"
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith(";") or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            current_section = line[1:-1]
            if current_section not in sections:
                sections[current_section] = []
            continue
        if "=" not in line:
            continue
        name, _, val = line.partition("=")
        name = name.strip()
        parts = [p.strip() for p in val.split(",")]
        if len(parts) < 4:
            continue
        category, slider_type, lower, upper = parts[0], parts[1], parts[2], parts[3]
        if current_section not in sections:
            sections[current_section] = []
        sections[current_section].append(SliderDef(
            name=name,
            category=category,
            slider_type=slider_type,
            lower_bound_morph=lower,
            upper_bound_morph=upper,
        ))
    return sections


# ---------------------------------------------------------------------------
# Discovery helpers
# ---------------------------------------------------------------------------

FACEGEN_MORPHS_REL = Path("meshes/actors/character/facegenmorphs")


def find_mod_namespaces(mods_root: Path) -> list[Path]:
    """Return all facegenmorphs plugin-namespace directories across all mods."""
    namespaces = []
    for mod_dir in sorted(mods_root.iterdir()):
        if not mod_dir.is_dir():
            continue
        fg = mod_dir / FACEGEN_MORPHS_REL
        if not fg.exists():
            continue
        for ns in fg.iterdir():
            if ns.is_dir():
                namespaces.append(ns)
    return namespaces


def find_tri_files(mods_root: Path, subdir: str) -> list[tuple[Path, str]]:
    """Find all TRI files under morphs/<subdir>/ across all mods.
    Returns list of (path, provider_mod_name).
    """
    results = []
    for mod_dir in sorted(mods_root.iterdir()):
        if not mod_dir.is_dir():
            continue
        morphs_dir = mod_dir / FACEGEN_MORPHS_REL / "morphs" / subdir
        if not morphs_dir.exists():
            continue
        for tri in sorted(morphs_dir.rglob("*.tri")):
            results.append((tri, mod_dir.name))
    return results


def collect_config_files(mods_root: Path) -> dict[str, list[Path]]:
    """Collect morphs.ini, races.ini, and sliders/*.ini from all namespaces."""
    config: dict[str, list[Path]] = {
        "morphs_ini": [],
        "races_ini": [],
        "sliders_ini": [],
    }
    for mod_dir in sorted(mods_root.iterdir()):
        if not mod_dir.is_dir():
            continue
        fg = mod_dir / FACEGEN_MORPHS_REL
        if not fg.exists():
            continue
        for ns in sorted(fg.iterdir()):
            if not ns.is_dir():
                continue
            m = ns / "morphs.ini"
            if m.exists():
                config["morphs_ini"].append(m)
            r = ns / "races.ini"
            if r.exists():
                config["races_ini"].append(r)
            sliders_dir = ns / "sliders"
            if sliders_dir.exists():
                for s in sorted(sliders_dir.glob("*.ini")):
                    config["sliders_ini"].append(s)
    return config


# ---------------------------------------------------------------------------
# Cross-check: slider upperBound morphs vs TRI morph tables
# ---------------------------------------------------------------------------

@dataclass
class RoutingIssue:
    severity: str  # ERROR / WARNING / INFO
    slider_name: str
    morph_name: str
    expected_tri: str
    actual_tri: Optional[str]
    detail: str


def cross_check(
    tri_map: dict[str, TriFile],
    slider_sections: dict[str, list[SliderDef]],
    extension_routes: list[MorphsIniExtension],
) -> list[RoutingIssue]:
    """Check that every slider morph name appears in at least one loaded TRI."""
    issues = []

    # Build set of all morph names across all parsed TRIs
    all_morphs_by_tri: dict[str, set[str]] = {}
    for tri_path, tri in tri_map.items():
        all_morphs_by_tri[tri_path] = {n.lower() for n in tri.morph_names}

    all_morphs: set[str] = set()
    for s in all_morphs_by_tri.values():
        all_morphs |= s

    # Build extension route map: base_mesh stem -> list of extension TRI base names
    ext_map: dict[str, list[str]] = {}
    for ext in extension_routes:
        base_stem = Path(ext.base_mesh).stem.lower()
        ext_map[base_stem] = [Path(e).stem.lower() for e in ext.extensions]

    for section, sliders in slider_sections.items():
        for s in sliders:
            for morph_name in [s.upper_bound_morph, s.lower_bound_morph]:
                if morph_name == "None" or not morph_name:
                    continue
                mn_lower = morph_name.lower()
                if mn_lower in all_morphs:
                    # Found — check which TRIs have it
                    found_in = [p for p, names in all_morphs_by_tri.items()
                                if mn_lower in names]
                    issues.append(RoutingIssue(
                        severity="INFO",
                        slider_name=s.name,
                        morph_name=morph_name,
                        expected_tri="(any)",
                        actual_tri="; ".join(Path(p).name for p in found_in),
                        detail=f"OK — found in {len(found_in)} TRI(s)",
                    ))
                else:
                    issues.append(RoutingIssue(
                        severity="ERROR",
                        slider_name=s.name,
                        morph_name=morph_name,
                        expected_tri="(required by slider)",
                        actual_tri=None,
                        detail="MISSING — morph name not found in any parsed TRI file",
                    ))

    return issues


# ---------------------------------------------------------------------------
# Markdown report helpers
# ---------------------------------------------------------------------------

def md_table(headers: list[str], rows: list[list[str]]) -> str:
    sep = ["---"] * len(headers)
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(sep) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(str(c) for c in row) + " |")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="TRI Audit Tool for RleyBodyMorphs")
    parser.add_argument("--mods-root", default=r"E:\Wabbajack\InstallPacks\mods",
                        help="MO2 mods directory root")
    parser.add_argument("--output-dir", default="docs/tri-audit",
                        help="Output directory for reports")
    parser.add_argument("--subdirs", default="Genesis,Trixter",
                        help="Comma-separated list of morphs subdirectories to scan")
    args = parser.parse_args()

    mods_root = Path(args.mods_root)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    subdirs = [s.strip() for s in args.subdirs.split(",")]

    print(f"[1/6] Scanning TRI files in {mods_root} ...")
    tri_files: list[TriFile] = []
    tri_map: dict[str, TriFile] = {}
    for subdir in subdirs:
        found = find_tri_files(mods_root, subdir)
        print(f"      {subdir}: {len(found)} TRI file(s) found")
        for tri_path, provider in found:
            print(f"      Parsing {tri_path.name} ({provider}) ...", end=" ", flush=True)
            tf = parse_tri(tri_path, provider)
            print("OK" if tf.parse_ok else f"FAIL: {tf.parse_error}")
            tri_files.append(tf)
            tri_map[str(tri_path)] = tf

    print(f"\n[2/6] Collecting RaceMenu config files ...")
    config_files = collect_config_files(mods_root)
    print(f"      morphs.ini: {len(config_files['morphs_ini'])}")
    print(f"      races.ini:  {len(config_files['races_ini'])}")
    print(f"      sliders.ini:{len(config_files['sliders_ini'])}")

    all_extensions: list[MorphsIniExtension] = []
    for p in config_files["morphs_ini"]:
        all_extensions.extend(parse_morphs_ini(p))

    all_races: list[RaceEntry] = []
    for p in config_files["races_ini"]:
        all_races.extend(parse_races_ini(p))

    all_sliders: dict[str, list[SliderDef]] = {}
    for p in config_files["sliders_ini"]:
        secs = parse_sliders_ini(p)
        slider_file_key = f"{p.parent.parent.parent.parent.name}/{p.parent.parent.name}/{p.parent.name}/{p.name}"
        for sec, defs in secs.items():
            full_key = f"{slider_file_key}[{sec}]"
            all_sliders[full_key] = defs

    print(f"\n[3/6] Cross-checking morph routing ...")
    issues = cross_check(tri_map, all_sliders, all_extensions)
    errors = [i for i in issues if i.severity == "ERROR"]
    warnings = [i for i in issues if i.severity == "WARNING"]
    infos = [i for i in issues if i.severity == "INFO"]
    print(f"      ERRORs:   {len(errors)}")
    print(f"      WARNINGs: {len(warnings)}")
    print(f"      OK:       {len(infos)}")

    # -----------------------------------------------------------------------
    # Phase 2 output: TRI inventory
    # -----------------------------------------------------------------------
    print(f"\n[4/6] Writing TRI inventory ...")

    tri_inventory_records = []
    for tf in tri_files:
        rec = {
            "path": tf.path,
            "provider_mod": tf.provider_mod,
            "file_size": tf.file_size,
            "sha256": tf.sha256,
            "parse_ok": tf.parse_ok,
            "parse_error": tf.parse_error,
            "vertex_count": tf.vertex_count,
            "morph_count": tf.morph_count,
            "morph_names": tf.morph_names,
            "has_RLY_Thin": tf.has_RLY_Thin,
            "has_RLY_Muscular": tf.has_RLY_Muscular,
            "has_RLY_Heavy": tf.has_RLY_Heavy,
            "has_lip_morphs": tf.has_lip_morphs,
            "has_brow_morphs": tf.has_brow_morphs,
            "has_eye_morphs": tf.has_eye_morphs,
        }
        tri_inventory_records.append(rec)

    with open(output_dir / "tri_inventory.json", "w", encoding="utf-8") as f:
        json.dump(tri_inventory_records, f, indent=2)

    # Markdown
    md_rows = []
    for tf in tri_files:
        flags = []
        if tf.has_RLY_Thin: flags.append("RLY_Thin")
        if tf.has_RLY_Muscular: flags.append("RLY_Muscular")
        if tf.has_RLY_Heavy: flags.append("RLY_Heavy")
        if tf.has_lip_morphs: flags.append("lip")
        if tf.has_brow_morphs: flags.append("brow")
        if tf.has_eye_morphs: flags.append("eye")
        md_rows.append([
            Path(tf.path).name,
            tf.provider_mod or "",
            str(tf.vertex_count),
            str(tf.morph_count),
            ", ".join(flags) if flags else "(none)",
            "✓" if tf.parse_ok else f"FAIL: {tf.parse_error}",
        ])

    md = "# TRI File Inventory\n\n"
    md += md_table(
        ["File", "Provider Mod", "Vertices", "Morphs", "Key Morphs", "Status"],
        md_rows
    )
    md += "\n\n## All Morph Names Per File\n\n"
    for tf in tri_files:
        md += f"### {Path(tf.path).name} ({tf.provider_mod})\n\n"
        if tf.parse_ok:
            md += f"Vertices: {tf.vertex_count}, Morphs: {tf.morph_count}\n\n"
            md += "```\n"
            for m in tf.morphs:
                md += f"  {m.name:<40}  mult={m.multiplier:.4f}  max_delta={m.max_abs_delta}\n"
            md += "```\n\n"
        else:
            md += f"**PARSE ERROR:** {tf.parse_error}\n\n"

    with open(output_dir / "tri_inventory.md", "w", encoding="utf-8") as f:
        f.write(md)

    # -----------------------------------------------------------------------
    # Phase 3 output: RaceMenu config inventory
    # -----------------------------------------------------------------------
    print(f"[5/6] Writing RaceMenu config inventory ...")

    config_record = {
        "morphs_ini_files": [str(p) for p in config_files["morphs_ini"]],
        "races_ini_files": [str(p) for p in config_files["races_ini"]],
        "sliders_ini_files": [str(p) for p in config_files["sliders_ini"]],
        "extension_routes": [asdict(e) for e in all_extensions],
        "race_entries": [asdict(r) for r in all_races],
        "slider_sections": {k: [asdict(s) for s in v] for k, v in all_sliders.items()},
    }

    with open(output_dir / "racemenu_config_inventory.json", "w", encoding="utf-8") as f:
        json.dump(config_record, f, indent=2)

    md = "# RaceMenu Config Inventory\n\n"
    md += "## Extension Routes (morphs.ini)\n\n"
    for e in all_extensions:
        md += f"- **base:** `{e.base_mesh}`\n"
        for ext in e.extensions:
            md += f"  - `{ext}`\n"
    md += "\n## Race Entries (races.ini)\n\n"
    md += md_table(["Race EditorID", "Sliders File"], [[r.race_editor_id, r.sliders_file] for r in all_races])
    md += "\n\n## Slider Definitions\n\n"
    for key, sliders in all_sliders.items():
        md += f"### {key}\n\n"
        if sliders:
            md += md_table(
                ["Slider Name", "Category", "Type", "Lower Morph", "Upper Morph"],
                [[s.name, s.category, s.slider_type, s.lower_bound_morph, s.upper_bound_morph]
                 for s in sliders]
            )
            md += "\n\n"

    with open(output_dir / "racemenu_config_inventory.md", "w", encoding="utf-8") as f:
        f.write(md)

    # -----------------------------------------------------------------------
    # Phase 4 output: routing audit
    # -----------------------------------------------------------------------
    print(f"[6/6] Writing routing audit ...")

    routing_record = {
        "errors": [asdict(i) for i in errors],
        "warnings": [asdict(i) for i in warnings],
        "ok": [asdict(i) for i in infos],
    }

    with open(output_dir / "routing_audit.json", "w", encoding="utf-8") as f:
        json.dump(routing_record, f, indent=2)

    md = "# Routing Audit\n\n"
    md += f"**Errors:** {len(errors)}  **Warnings:** {len(warnings)}  **OK:** {len(infos)}\n\n"

    if errors:
        md += "## ERRORS — Slider morphs not found in any TRI\n\n"
        md += md_table(
            ["Slider", "Morph Name", "Detail"],
            [[i.slider_name, i.morph_name, i.detail] for i in errors]
        )
        md += "\n\n"

    if infos:
        md += "## OK — Slider morphs found in TRI\n\n"
        md += md_table(
            ["Slider", "Morph Name", "Found In TRI"],
            [[i.slider_name, i.morph_name, i.actual_tri or ""] for i in infos]
        )
        md += "\n\n"

    # FACE_RLY specific table
    all_morphs_by_tri = {k: {n.lower() for n in v.morph_names} for k, v in tri_map.items()}
    rly_sliders = [s for defs in all_sliders.values() for s in defs
                   if "RLY" in s.name.upper() or "rly" in s.upper_bound_morph.lower()]
    if rly_sliders:
        md += "## FACE_RLY Slider Details\n\n"
        md += md_table(
            ["Slider", "Category", "Upper Morph", "Upper Morph in TRI?", "Lower Morph"],
            [
                [
                    s.name, s.category, s.upper_bound_morph,
                    "YES" if any(s.upper_bound_morph.lower() in names
                                 for names in all_morphs_by_tri.values())
                    else "MISSING",
                    s.lower_bound_morph,
                ]
                for s in rly_sliders
            ]
        )
        md += "\n\n"

    with open(output_dir / "routing_audit.md", "w", encoding="utf-8") as f:
        f.write(md)

    # -----------------------------------------------------------------------
    # Summary to stdout
    # -----------------------------------------------------------------------
    print("\n" + "=" * 70)
    print("SUMMARY")
    print("=" * 70)
    print(f"TRI files parsed:  {len(tri_files)}")
    print(f"  OK:              {sum(1 for t in tri_files if t.parse_ok)}")
    print(f"  FAILED:          {sum(1 for t in tri_files if not t.parse_ok)}")
    print()
    print("RLY morph presence:")
    for tf in tri_files:
        if tf.parse_ok:
            rly = []
            if tf.has_RLY_Thin: rly.append("RLY_Thin")
            if tf.has_RLY_Muscular: rly.append("RLY_Muscular")
            if tf.has_RLY_Heavy: rly.append("RLY_Heavy")
            print(f"  {Path(tf.path).name:<40} {', '.join(rly) if rly else '(none)'}")
    print()
    print(f"Routing errors:    {len(errors)}")
    for e in errors:
        print(f"  ERROR: slider={e.slider_name} morph={e.morph_name}")
    print()
    print(f"Output written to: {output_dir}/")
    for fname in ["tri_inventory.json", "tri_inventory.md",
                  "racemenu_config_inventory.json", "racemenu_config_inventory.md",
                  "routing_audit.json", "routing_audit.md"]:
        p = output_dir / fname
        if p.exists():
            print(f"  {fname} ({p.stat().st_size} bytes)")

    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main())
