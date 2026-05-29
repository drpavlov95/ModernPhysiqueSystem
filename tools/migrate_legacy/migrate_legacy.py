#!/usr/bin/env python3
import argparse
import csv
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from osp_reader import read_osp
from osd_parser import read_osd, write_osd, MorphEntry
from coalesce import coalesce_entries

LEGACY_SLIDERS = {
    "Chubby Refit",
    "Rley's CBBE Outfit",
    "Rley CBBE Shoulder",
    "Rley CBBE Outfit",
    "Fat Morph",
    "Fat Morphs",
}
COALESCE_PAIR = ("Rley CBBE Shoulder", "Rley CBBE Outfit")
RM_SLIDERS = {"RM_Heavy", "RM_Muscular", "RM_Thin"}
SKIP_OSPS = {"FOS CBBE Body.osp", "Rley COR HIMBO.osp"}
NEW_SLIDER = "RM_Heavy"


def find_osp_files(source_dir: Path) -> list[Path]:
    return [p for p in source_dir.rglob("*.osp") if p.name not in SKIP_OSPS]


def scan(source_dir: Path, output_csv: Path | None) -> None:
    rows = []
    header = ["file", "slider_set", "group", "legacy_sliders", "rm_sliders_present", "action", "risk"]

    for osp_path in find_osp_files(source_dir):
        sets = read_osp(osp_path)
        for ss in sets:
            found_legacy = ss.slider_names() & LEGACY_SLIDERS
            has_rm = bool(ss.slider_names() & RM_SLIDERS)
            has_coalesce = (COALESCE_PAIR[0] in found_legacy and COALESCE_PAIR[1] in found_legacy)

            if has_rm:
                action = "skip — already patched"
                risk = ""
            elif not found_legacy:
                action = "skip — no legacy slider"
                risk = ""
            elif has_coalesce:
                action = "coalesce+migrate"
                risk = "coalesce: verify vertex overlap"
            else:
                action = "migrate"
                risk = ""

            rows.append({
                "file": str(osp_path.relative_to(source_dir)),
                "slider_set": ss.name,
                "group": ss.group,
                "legacy_sliders": "|".join(sorted(found_legacy)),
                "rm_sliders_present": str(has_rm),
                "action": action,
                "risk": risk,
            })

    if output_csv:
        with open(output_csv, "w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=header)
            writer.writeheader()
            writer.writerows(rows)
        print(f"Manifest written to {output_csv}")
    else:
        writer = csv.DictWriter(sys.stdout, fieldnames=header)
        writer.writeheader()
        writer.writerows(rows)

    n_migrate = sum(1 for r in rows if "migrate" in r["action"])
    n_skip = sum(1 for r in rows if "skip" in r["action"])
    n_risk = sum(1 for r in rows if r["risk"])
    print(f"\nTotal: {len(rows)} | To migrate: {n_migrate} | Skip: {n_skip} | Risk flags: {n_risk}")


def pre(source_dir: Path, legacy_backup_dir: Path) -> None:
    legacy_backup_dir.mkdir(parents=True, exist_ok=True)

    for osp_path in find_osp_files(source_dir):
        sets = read_osp(osp_path)
        for ss in sets:
            found_legacy = ss.slider_names() & LEGACY_SLIDERS
            has_rm = bool(ss.slider_names() & RM_SLIDERS)
            if has_rm or not found_legacy:
                continue

            osp_dir = osp_path.parent
            shape_data_root = osp_dir.parent.parent / "ShapeData"
            osd_path = shape_data_root / ss.data_folder / ss.osd_filename()

            if not osd_path.exists():
                print(f"[WARN] OSD not found: {osd_path}")
                continue

            entries = read_osd(osd_path)
            entry_map = {e.name: e for e in entries}

            has_coalesce = (COALESCE_PAIR[0] in found_legacy and COALESCE_PAIR[1] in found_legacy)

            new_entries = []
            for entry in entries:
                legacy_suffix = None
                for ls in found_legacy:
                    if entry.name.endswith(ls):
                        legacy_suffix = ls
                        break

                if legacy_suffix is None:
                    new_entries.append(entry)
                    continue

                if has_coalesce and legacy_suffix == COALESCE_PAIR[0]:
                    prefix = entry.name[:-len(COALESCE_PAIR[0])]
                    outfit_name = prefix + COALESCE_PAIR[1]
                    outfit_entry = entry_map.get(outfit_name)
                    if outfit_entry:
                        coalesced = coalesce_entries(entry, outfit_entry, prefix + NEW_SLIDER)
                        print(f"  [COALESCE] {entry.name} + {outfit_name} → {coalesced.name}")
                        overlap = set(entry.deltas) & set(outfit_entry.deltas)
                        if overlap:
                            print(f"  [WARN] {len(overlap)} overlapping vertices")
                        new_entries.append(coalesced)
                elif has_coalesce and legacy_suffix == COALESCE_PAIR[1]:
                    continue
                else:
                    renamed = MorphEntry(
                        name=entry.name[:-len(legacy_suffix)] + NEW_SLIDER,
                        deltas=entry.deltas,
                    )
                    print(f"  [RENAME] {entry.name} → {renamed.name}")
                    new_entries.append(renamed)

            rel = osd_path.relative_to(shape_data_root)
            out_path = legacy_backup_dir / rel
            out_path.parent.mkdir(parents=True, exist_ok=True)
            write_osd(new_entries, out_path)
            print(f"[OK] {osd_path.name}")


def gen_xml(legacy_backup_dir: Path, automations_folder: Path) -> None:
    automations_folder.mkdir(parents=True, exist_ok=True)

    def write_migration_xml(out_path: Path, ref_osp: str, ref_set: str):
        root = ET.Element("AutomationScript")
        batch = ET.SubElement(root, "Batch")
        batch.set("mode", "SliderSets")
        ET.SubElement(batch, "Folder").text = "CalienteTools/BodySlide/SliderSets"
        ET.SubElement(root, "Step").set("type", "ClearReference")
        load_ref = ET.SubElement(root, "Step")
        load_ref.set("type", "LoadReference")
        ET.SubElement(load_ref, "SourceFile").text = ref_osp
        ET.SubElement(load_ref, "Set").text = ref_set
        ET.SubElement(load_ref, "MergeSliders").text = "true"
        conform = ET.SubElement(root, "Step")
        conform.set("type", "ConformSliders")
        ET.SubElement(conform, "SliderNames").text = "RM_Heavy,RM_Muscular,RM_Thin"
        ET.SubElement(conform, "ProximityRadius").text = "10.0"
        ET.SubElement(conform, "MaxResults").text = "10"
        import_osd = ET.SubElement(root, "Step")
        import_osd.set("type", "ImportSliderData")
        ET.SubElement(import_osd, "SliderDataFile").text = str(legacy_backup_dir)
        ET.SubElement(import_osd, "FromFolder").text = "true"
        del_step = ET.SubElement(root, "Step")
        del_step.set("type", "DeleteSlider")
        ET.SubElement(del_step, "SliderNames").text = (
            "Chubby Refit|Rley's CBBE Outfit|Rley CBBE Shoulder|Rley CBBE Outfit|Fat Morph|Fat Morphs"
        )
        ET.SubElement(del_step, "SliderRegex").text = "true"
        ET.SubElement(root, "Step").set("type", "SaveProject")
        tree = ET.ElementTree(root)
        ET.indent(tree, space="    ")
        tree.write(str(out_path), encoding="UTF-8", xml_declaration=True)
        print(f"Written: {out_path}")

    write_migration_xml(
        automations_folder / "mppatcher_migrate_cbbe.xml",
        "CalienteTools/BodySlide/SliderSets/FOS CBBE Body.osp",
        "Rley COR - CBBE Body",
    )
    write_migration_xml(
        automations_folder / "mppatcher_migrate_himbo.xml",
        "CalienteTools/BodySlide/SliderSets/Rley COR HIMBO.osp",
        "Rley HIMBO BODY 2",
    )
    print(f"Migration XML files written to {automations_folder}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scan", action="store_true")
    parser.add_argument("--pre", action="store_true")
    parser.add_argument("--gen-xml", action="store_true")
    parser.add_argument("--source", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--legacy-backup", type=Path)
    parser.add_argument("--automations-folder", type=Path)
    args = parser.parse_args()

    if args.scan:
        if not args.source:
            parser.error("--scan requires --source")
        scan(args.source, args.output)
    elif args.pre:
        if not args.source or not args.legacy_backup:
            parser.error("--pre requires --source and --legacy-backup")
        pre(args.source, args.legacy_backup)
    elif args.gen_xml:
        if not args.legacy_backup or not args.automations_folder:
            parser.error("--gen-xml requires --legacy-backup and --automations-folder")
        gen_xml(args.legacy_backup, args.automations_folder)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
