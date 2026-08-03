# Root cause — `Nose_NoseWingWidth = 37` and the face distortion

**Date:** 2026-08-03
**Status:** cause identified end to end, including the specific value. A targeted,
asset-level fix is verified by simulation and described in section 5. Not applied:
it changes one COtR slider's saved-state key (section 6).

---

## 1. The chain, complete

```
RaceMenu shows the FEMALE slider list
  └─ RM_FacePart is the last entry: female internal index 258 → slider->index = 458
       └─ its value is a HEAD PART index (e.g. 37), not a morph magnitude
            └─ it is dispatched while SKEE's race-keyed map holds the MALE list
                 └─ GetSliderByIndex(race, 258) → male[258] = Nose_NoseWingWidth  [type Slider]
                      └─ the hook takes the *Slider* branch and stores the raw value:
                         SetMorphValue(actorBase, "Nose_NoseWingWidth", 37.0)
                           └─ ApplyMorphs replays |37.0| as 37 full-strength morphs
                              on EVERY face rebuild, permanently
```

Every link is verified against the SKEE source and the installed slider
configuration. The value `37` is not a corrupted float — it is a head part index
being interpreted as a morph magnitude.

## 2. The structural defect

The male and female sections of `sliders/Trixter.ini` have **different membership**
(295 vs 259 entries), so identical indices mean different sliders per sex. SKEE
resolves dispatches by index through `GetSliderByIndex(race, index)`, which is
gender-blind over a race-keyed map, so whenever the loaded list belongs to the
other sex, an index lands on the wrong slider.

Most such mismatches are Slider↔Slider: the wrong morph moves, but the value stays
in `[-1, 1]` and the damage is bounded. The destructive cases are where the
**types** differ, because Preset and HeadPart sliders carry a *list index* as their
value. There are exactly three, across the whole file:

| Internal index | Male | Female |
| ---: | --- | --- |
| 18 (`ext 218`) | `EAR_EarRelax` [Slider] | `EAR_EarShapeElf` [**Preset**, 16 options] |
| 20 (`ext 220`) | `EAR_EarShapeElf` [**Preset**, 20 options] | `EAR_EarUpDown` [Slider] |
| 258 (`ext 458`) | `Nose_NoseWingWidth` [Slider] | `RM_FacePart` [**HeadPart**] |

Row 3 is the reported bug. Rows 1 and 2 are the same defect with a smaller
magnitude (a preset index up to 20 written as a morph value).

## 3. This is pre-existing, not caused by the `A0_MPS_*` rename

The same three collisions are present in the pre-rename backup
(`__rollback_backups/20260802_165339_a0_index_stable/sliders/Trixter.ini`), with
`RM_FacePart` at male 294 / female 258 and `Nose_NoseWingWidth` at male 258 /
female 228 — identical values.

The rename moved MPS's three axes from male 177-179 / female 156-158 to 0/1/2,
which shifted only entries that previously sorted *before* them. `Nose_*` and
`RM_*` sort after, so their indices were untouched. The ear collisions did shift
(15/17 → 18/20) because those entries sort before.

The defect ships in COtR's `Trixter.ini`; this mod's copy inherits it.

## 4. Why every MPS-side hypothesis failed

Because MPS was never involved. The corrupting dispatch is `RM_FacePart`, a COtR
slider, driven by RaceMenu's own UI. This is consistent with the decisive
observation that the corruption occurred while MPS face morphs never applied, and
with all four earlier disproofs (see `NOSEWINGWIDTH_PIPELINE_AUDIT_2026-08-02.md`).

## 5. The fix — pin the special-type sliders to identical indices

Same principle that made MPS's own axes safe: if a slider occupies the **same
internal index in both lists**, a wrong-sex resolution still lands on it, so
neither the slider nor its type can be confused.

Only the special-type sliders need this. Renaming them so they sort immediately
after the `A0_MPS_*` block pins them to indices 3 and 4 in both lists:

| Current name | New name | Resulting index (both sexes) |
| --- | --- | --- |
| `RM_FacePart` | `A1_RM_FacePart` | 3 |
| `EAR_EarShapeElf` | `A2_EAR_EarShapeElf` | 4 |

Simulated against the installed file: **type collisions drop from 3 to 0.** Sorted
head of both lists becomes identical:

```
A0_MPS_RLYHeavy, A0_MPS_RLYMuscular, A0_MPS_RLYThin, A1_RM_FacePart, A2_EAR_EarShapeElf, Brow_BrowAdjust, ...
```

Edits required (both `[Male]` and `[Female]` sections, 4 lines total):

```ini
; sliders/Trixter.ini
A1_RM_FacePart              = 8, HeadPart, 1
A2_EAR_EarShapeElf          = 8, Preset, EAR_ElfEarType, 20   ; male   (16 in [Female])
```

plus the matching `$A1_RM_FacePart` / `$A2_EAR_EarShapeElf` tokens in
`interface/translations/racemenu_english.txt` (UTF-16 LE with BOM in the repo).

No C++ change: MPS never references either slider.

## 6. Cost, and why it is not applied yet

Renaming a slider changes its **saved-state key**:

- `EAR_EarShapeElf` is a Preset, and the hook does store Preset values via
  `SetMorphValue`. Existing characters lose that value — `LoadSliders` erases keys
  that are no longer in the list — so **elf ear shape resets to default** on
  existing COtR characters that used it.
- `RM_FacePart` is a HeadPart, and the HeadPart branch returns *before*
  `SetMorphValue`; the chosen part lives on the actor (`ChangeHeadPart`), not in
  the value map. The actual head part should therefore survive; only the slider's
  displayed position resets.

Nothing else references either name: no `.jslot` preset found referencing them,
and no script. The only other occurrence is COtR's own `Trixter.ini`, which this
mod's copy already outranks.

That is a small, well-understood cost for removing the catastrophic case — but it
alters a COtR asset's semantics, so it is a decision to take deliberately rather
than apply silently.

## 7. Alternatives considered

- **Make both sections have identical membership.** Correct in principle, but the
  lists differ by 36 entries that COtR deliberately excludes per sex; adding them
  changes far more behaviour than the rename.
- **Fix SKEE upstream.** Removes the whole class for everyone, but requires
  RaceMenu (see `SKEE_GENDER_AWARE_SLIDER_INDEX_PATCH.md`, still incomplete on the
  index-assignment side).
- **Guard the write.** Rejecting a magnitude above 1.0 for a `kTypeSlider` entry
  stops the damage but leaves the mis-dispatch, and needs a hook.

The rename is the only option that is asset-level, verifiable offline, reversible,
and independent of RaceMenu's release cycle.
