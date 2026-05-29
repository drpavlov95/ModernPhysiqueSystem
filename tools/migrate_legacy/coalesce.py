from osd_parser import MorphEntry


def coalesce_entries(a: MorphEntry, b: MorphEntry, new_name: str) -> MorphEntry:
    """Sum vertex deltas from two morph entries."""
    all_indices = set(a.deltas.keys()) | set(b.deltas.keys())
    combined = {}
    for idx in all_indices:
        ax, ay, az = a.deltas.get(idx, (0.0, 0.0, 0.0))
        bx, by, bz = b.deltas.get(idx, (0.0, 0.0, 0.0))
        combined[idx] = (ax + bx, ay + by, az + bz)
    return MorphEntry(name=new_name, deltas=combined)
