# Deep audit — face slider pipeline, SKEE internals, and the two open symptoms

**Date:** 2026-08-03
**Trigger:** two symptoms reported together — MPS face morphs never applying, and
the `Nose_NoseWingWidth` corruption still occurring *while they never applied*.
**Safety action first:** the tick-frequency native slider watcher was removed from
the source (not merely disabled) and a clean Release DLL was deployed. The
shipped INI is back to production values. Nothing diagnostic remains active.

---

## 1. Why the watcher crashed — and a real SKEE finding

`GetMorphValueByName` is **not side-effect free**. It reaches
`ValueSet::GetValue` → `g_stringTable.GetString(name)`, and in `StringTable.cpp:29`:

```cpp
StringTableItem StringTable::GetString(const SKEEFixedString & str)
{
    IScopedCriticalSection locker(&m_lock);
    auto it = m_table.find(str);
    if (it != m_table.end()) {
        return it->second.lock();
    }
    else {
        StringTableItem item = std::shared_ptr<SKEEFixedString>(new SKEEFixedString(str), DeleteStringEntry);
        m_table.emplace(str, item);        // mutates a process-global map
        m_tableVector.push_back(item);     // and grows a process-global vector
        return item;
    }
}
```

When the name is absent it **allocates and inserts into a global, lock-protected
table**. The returned `shared_ptr`'s deleter then runs `RemoveString`, which does
a **linear scan** of `m_tableVector`, calling `.lock()` on each element, before
erasing.

The watcher performed four such reads per tick (~60 Hz) on names that were not in
the table, i.e. ~240 allocate/insert/O(n)-scan/erase cycles per second under a
global lock, on the game thread, during rebuilds. That is the crash.

**This also applies to our own code.** `TryReadAxisValues` reads three names on
the dispatch path. Whenever those names are not yet in the string table, each read
pays the same allocate/insert/scan/erase cost under the global lock. It is bounded
(three names, only on dispatch) and has not been observed to be harmful, but it is
worth knowing that "reading a morph value" is not free and is not read-only.

**Rule going forward:** never poll SKEE value reads on a timer. Diagnostics must
be event-driven, and must never be able to take the game down.

## 2. How slider lists are actually assembled

`FaceMorphInterface::LoadMods` iterates every active plugin and reads
`Meshes\actors\character\facegenmorphs\<plugin>\races.ini`, then `morphs.ini`.
Each `races.ini` maps a race to a slider file, and `RaceMap::AddSliderMap` inserts
into a `std::set`, so **multiple namespaces accumulate on the same race** and
`for_each_slider` iterates all of them.

Installed contributors:

| Mod | Namespace | Covers COtR races (`00DZ*`)? |
| --- | --- | --- |
| COtR - HQ Characters creation addon for RM | `racemenu.esp` | yes |
| ModernPhysiquePicker (this mod) | `racemenu.esp` | yes |
| Expressive Facegen Morphs SE (+ Racemenu) | `Expressive Facegen Morphs.esl` | **no** (0 entries) |

Two consequences that were not previously accounted for:

- MPS and COtR share the **same namespace**, so their `races.ini`,
  `morphs.ini` and `sliders/Trixter.ini` occupy identical virtual paths and only
  one copy of each wins by mod-manager priority.
- Expressive Facegen Morphs uses its own namespace and therefore **adds** 83
  sliders per sex on the races it covers. It does not cover COtR races, so it
  does not affect a COtR character's indices — but it does affect vanilla races.

This invalidates any index model derived from `Trixter.ini` alone on a
vanilla-race character, and is the likely explanation for the unresolved
"off by 2" noted in the earlier audit.

## 3. Conflict resolution — verified correct

Active profile is `PHYSIQUEWHEEL DEBUG`. In `modlist.txt`, MPS is at line 84 and
COtR at line 575; the earlier entry has the higher priority, so **MPS wins every
shared path**. Verified per file:

| Path | COtR | MPS | Winner |
| --- | --- | --- | --- |
| `sliders/Trixter.ini` | 610 lines, **0** MPS sliders | 616 lines, **6** `A0_MPS_*` | MPS |
| `morphs/Genesis/FACE_MaleHead.tri` | 1 558 434 B | 1 595 982 B | MPS |
| `morphs/Trixter/FACE_FemaleHead.tri` | 1 370 479 B | 1 408 028 B | MPS |
| `races.ini` | — | byte-identical to COtR's | either |

The larger TRI files are MPS's, which carry the `RLY_*` morph data. Empirical
confirmation that MPS wins: the working session logged the `A0_MPS_*` names, and
those exist **only** in MPS's `Trixter.ini`.

So the file layer is correct and is not the cause of either symptom.

## 4. Race coverage — the likely cause of "face morphs never apply"

`races.ini` (identical in both mods) maps:

- COtR races (`00DZ*`) → `sliders\Trixter.ini` → **exists**, contains our sliders
- every vanilla race (`NordRace`, `BretonRace`, …) → `sliders\human.ini`

and **no installed mod provides `racemenu.esp/sliders/human.ini`**. `ReadSliders`
fails silently on a missing file, so a vanilla-race character gets **no sliders at
all from the `racemenu.esp` namespace** — including ours.

**MPS face morphs only exist for COtR races.** On any vanilla race they cannot
apply, by configuration, not by defect. If the recent test used a newly created
character, this fully explains the symptom.

*To confirm:* the router logs `race={:08X}` at `info`. The COtR race seen in the
working session was `1005A184`. A different form ID in the failing session
confirms this diagnosis.

## 5. What this leaves for the nose corruption

The decisive observation from testing is that the corruption occurred **while MPS
face morphs never applied**. Combined with the earlier audit, that closes the
question of MPS involvement:

- MPS issues no `SetMorphValue` anywhere.
- MPS dispatches `ChangeDoubleMorph` only with its own certified slider ids —
  and on the failing character it dispatches nothing at all, because its sliders
  do not exist.
- The corruption still happens.

A cause cannot act while absent. **The write is RaceMenu/COtR's own.** Every
MPS-side hypothesis raised across this investigation has now been either audited
away or disproved by experiment:

| Hypothesis | Outcome |
| --- | --- |
| Cross-sex index in the settled list | disproved by measurement (male 456 / female 426, both `subType=0`) |
| Tick-time repair against a stale list | disproved — `preparedMapKey_ != key` returns first |
| 50 ms weight-sync poll + `requestUpdate` | disproved by experiment (`WeightSyncPoll=0`, corruption persisted) |
| MPS face dispatch | disproved — corruption occurs with no MPS sliders present |

What remains true and unexplained is on the SKEE side: `GetSliderByIndex(race,
index)` is gender-blind over a race-keyed map, `LoadSliders` assigns indices from
the current sex's list regardless of which gender array it fills, and
`ApplyMorphs` amplifies any stored magnitude above 1.0 into that many
full-strength morph applications on **every** rebuild — which is why one bad write
is permanent and why the damage appears to worsen across a session.

## 6. Recommended next steps

1. **Confirm the race.** Run once at `Level=debug` on the character that fails and
   read `race=`. If it is not a `00DZ*` form, "face morphs never apply" is
   explained and is not a defect.
2. **Decide on vanilla-race support.** If MPS should work outside COtR, it needs
   its own `sliders/human.ini` (and `beast.ini`) carrying the `A0_MPS_*` entries,
   or a `races.ini` that maps vanilla races to a file it actually ships. This is a
   scope decision, not a bug fix.
3. **Treat the nose corruption as upstream.** The remaining lever is the SKEE
   patch (see `SKEE_GENDER_AWARE_SLIDER_INDEX_PATCH.md`, which is marked
   incomplete: it fixes resolution but not index assignment), or a guard that
   rejects an out-of-range magnitude for a `kTypeSlider` entry at the point of
   write. Both require touching RaceMenu's code or hooking it.
4. **Do not reintroduce polled diagnostics.** See section 1.

## 7. State of the deployed mod

Clean Release build, 5/5 tests passing, no diagnostic instrumentation, INI at
production values, git tree clean. The two fixes established earlier in this
investigation remain in place and committed: the Debug/Release CRT ABI correction
and the `A0_MPS_*` index-stable slider naming.
