# SKEE FaceGenMorphs File Format Specification

**Source:** `_tmp_skee_source/skee64/FaceMorphInterface.cpp` (ReadMorphs, ReadRaces, ReadSliders,
LoadSliders), `SKEEHooks.cpp` (DoubleMorphCallback, AddSlider_Hook, InvokeCategoryList_Hook),
`FileUtils.cpp` (ForEachMod, GetRaceByName), `FaceMorphInterface.h` (category/type constants).  
**Verified against:** skee64.dll loaded in Ghidra (build 180000000).

---

## 1. Discovery — Which Plugins SKEE Iterates

```cpp
// FileUtils.cpp:255
void ForEachMod(std::function<void(ModInfo*)> functor)
{
    (*g_dataHandler)->modList.modInfoList.Visit(visitor);
}
```

**`modInfoList` contains only regular ESP/ESM plugins.**  
ESL-flagged plugins (header flag `0x200`) live in `lightMods` — **ForEachMod never visits them.**

For each regular plugin `plugin.esp`, SKEE constructs:
```
Meshes\actors\character\FaceGenMorphs\<plugin.esp>\races.ini
Meshes\actors\character\FaceGenMorphs\<plugin.esp>\morphs.ini   ← only if g_extendedMorphs = true
Meshes\actors\character\FaceGenMorphs\<plugin.esp>\replacements.ini
```

File resolution is through the **MO2 VFS** (BSResourceNiBinaryStream). If the file is absent,
the call silently returns — no error is logged.

**Rule:** The namespace folder name must exactly match the plugin's `.esp`/`.esm`/`.esl` filename
as reported by `modInfo->name`. The plugin must be a **non-ESL** plugin in the active load order.

---

## 2. races.ini

**Path:** `Meshes\actors\character\FaceGenMorphs\<plugin.esp>\races.ini`

### Parser behavior (ReadRaces, lines 762–828)

```
# comment line (# as first character after trim — skipped)
<blank line>                     → skipped
<EditorID> = <sliderfile>[, <sliderfile2>, ...]
```

- Lines are trimmed of leading/trailing whitespace before parsing.
- `#` as the first character after trim → line skipped (comment).
- Separator is `=`; left side is trimmed → race EditorID; right side is split on `,`.
- Each comma-separated value is a **path to a sliders ini file**, relative to the same namespace folder, unless prefixed with `:` (colon prefix strips the namespace, making the path resolve relative to `FaceGenMorphs\` directly — rarely used).
- **Race lookup:** `GetRaceByName(editorID)` iterates `DataHandler::races` comparing `race->editorId` (case-sensitive string compare via `BSFixedString`). The EditorID must match exactly what the game loaded from the ESP records.
- If the same slider file appears for multiple races, SKEE caches the SliderMap by filename within one `ReadRaces` call — the file is only parsed once per namespace.
- Each (race, SliderMap) pair is added via `RaceMap::AddSliderMap` which inserts into a `std::set<SliderMapPtr>` (pointer identity). Multiple namespaces for the same race **accumulate** — their SliderMaps are all kept and iterated in insertion order.

### Valid example (our file)

```ini
# RleyBodyMorphs — COtR race RLY slider routing
00DZN_NordRace          = sliders\RLY.ini
00DZN_NordRaceVampire   = sliders\RLY.ini
00DZH_HighElfRace       = sliders\RLY.ini
```

### Error logged when

- Line has no `=` → `ReadRaces Error - Line (N) loading a race from <path> has insufficient parameters.`
- Referenced slider file cannot be opened → `ReadRaces Error - Line (N) failed to load slider map for <race> from <path>.`

---

## 3. morphs.ini

**Path:** `Meshes\actors\character\FaceGenMorphs\<plugin.esp>\morphs.ini`

**Only loaded when `g_extendedMorphs = true` (default: true; overridable in skse.ini `[FaceGen] bExtendedMorphs`).**

### Parser behavior (ReadMorphs, lines 709–760)

```
# comment
extension = <base_tri_path>, <morph_tri_path1>[, <morph_tri_path2>, ...]
```

- Left side must be exactly the keyword `extension` (case-insensitive, first 9 chars compared).  
  Any other keyword on the left side → error logged, line skipped.
- Right side is split on `,`; first value = base TRI path (`key`); remaining = morph TRI paths.
- Paths are all trimmed of whitespace.
- Added via `MorphMap::AddMorph(key, value)` — deduplicates: if the exact (key, value) pair is already in the map, it is not added again. Allows multiple mods to extend the same base TRI without doubling routes.
- **TRI paths are relative to `SLIDER_DIRECTORY = "actors\character\FaceGenMorphs\morphs\"`.** The full VFS path is `Meshes\actors\character\FaceGenMorphs\morphs\<morph_tri_path>`.

### Valid example (COtR's file, abridged)

```ini
# COtR morphs.ini
extension = !COR_Male\Head\MaleHeadCharGen.tri, Genesis\FACE_MaleHead.tri
extension = !COR\Head\FemaleHeadCharGen.tri,    Trixter\FACE_FemaleHead.tri
```

This tells SKEE: when applying morphs for actors using `MaleHeadCharGen.tri` as their base,
also apply deltas from `Genesis\FACE_MaleHead.tri` (resolved as
`Meshes\actors\character\FaceGenMorphs\morphs\Genesis\FACE_MaleHead.tri`).

### Error logged when

- Line has no `=` → `ReadMorphs Error - Line (N) loading a morph from <path> has no left-hand side.`
- Left side is not `extension` → `ReadMorphs Error - Line (N) loading a morph from <path> invalid left-hand side.`
- Right side has fewer than 2 comma-separated values → `ReadMorphs Error - ...has less than 2 parameters.`

---

## 4. sliders/*.ini

**Path:** `Meshes\actors\character\FaceGenMorphs\<plugin.esp>\<any path referenced in races.ini>`

Typically `sliders\human.ini`, `sliders\Trixter.ini`, `sliders\RLY.ini`, etc.
The path is relative to the namespace folder unless prefixed with `:` in races.ini.

### Parser behavior (ReadSliders, lines 844–985)

```
# comment
[Male]          ← sets gender context to 0 (male) — case-insensitive, only first 4/6 chars checked
[Female]        ← sets gender context to 1 (female)
<KEY> = <category>, <type>, <param3>[, <param4>]
```

- Default gender context at file start is **0 (Male)**.
- Section headers: `[` as first char → strip `[`, compare first 4 chars case-insensitively against `"Male"` or first 6 against `"Female"`. Any other `[...]` label is silently ignored (gender unchanged).
- `#` as first char after trim → comment, skipped.
- Blank lines skipped.
- Separator `=`; left side trimmed → **slider key** (becomes the internal name and the GFx `callbackName`).

### Field table

```
KEY = category, type, param3, [param4]
```

| Field | Notes |
|---|---|
| `KEY` | Identifier string. Stored as `SKEEFixedString`. Used as the ValueMap key and GFx display name prefix (SKEE prepends `$` → `$KEY` for localization). Must be unique per gender within the race's combined SliderSet. |
| `category` | Integer. `-1` maps to `kCategoryExtra` (512). See table below. |
| `type` | String: `Slider`, `Preset`, or `HeadPart` (case-insensitive first 6/8 chars). |
| `param3` | Depends on type — see below. |
| `param4` | Depends on type — see below. |

### Category numbers

| Value | Constant | RaceMenu tab |
|---|---|---|
| 4 | `kCategoryBody` | Body |
| 8 | `kCategoryHead` | Head |
| 16 | `kCategoryFace` | Face |
| 32 | `kCategoryEyes` | Eyes |
| 64 | `kCategoryBrow` | Brow |
| 128 | `kCategoryMouth` | Mouth |
| 256 | `kCategoryHair` | Hair |
| 512 | `kCategoryExtra` | Extra (also triggered by `-1`) |
| 1024 | `kCategoryExpressions` | Expressions |

Any other integer → error logged, line skipped.

### Type: `Slider`

```
KEY = <category>, Slider, <lowerMorph>, <upperMorph>
```

- **Requires `g_extendedMorphs = true`** — line is skipped (not an error) when false.
- `param3` = lower-bound morph name (the TRI morph applied when value < 0). String `None` → stored as empty string → lower bound is 0.0.
- `param4` = upper-bound morph name (the TRI morph applied when value > 0). String `None` → stored as empty string → upper bound is 0.0.
- Both bounds empty → slider range is [0.0, 0.0] (zero-range, effectively inert).
- One-sided slider: `None` for one side → that side is 0.0, other side active.
- Minimum 4 parameters required.
- **This is the type we use for FACE_RLY sliders.**

**Our sliders:**
```ini
FACE_RLYThin     = 8, Slider, None, RLY_Thin
```
→ category=Head, no lower morph, upper morph=`RLY_Thin`, range [0.0, 1.0] in UI.

### Type: `Preset`

```
KEY = <category>, Preset, <morphPrefix>, <presetCount>
```

- **Requires `g_extendedMorphs = true`** — skipped when false.
- `param3` = morph name prefix (e.g., `FacePreset`). SKEE builds morph name as `<prefix><int_value>` at runtime.
- `param4` = number of presets (integer, clamped to 255). UI shows this many discrete steps.

### Type: `HeadPart`

```
KEY = <category>, HeadPart, <partType>
```

- **NOT gated by `g_extendedMorphs`** — loaded regardless.
- `param3` = head part type index (integer stored in `sliderInternal.presetCount`).
- Used to swap `BGSHeadPart` assets (e.g., hair, brows). No param4.

### Slider creation in GFx (LoadSliders, lines 1272–1358)

When SKEE builds the RaceMenu slider list, for each `SliderInternal`:

```cpp
std::string sliderName = "$";
sliderName.append(slider->name.c_str());  // → "$FACE_RLYThin"

RaceMenuSlider newSlider(
    slider->category,       // e.g. 8 (Head)
    morphName.c_str(),      // "$FACE_RLYThin"  ← display name / localization key
    "ChangeDoubleMorph",    // ← callbackName (ALWAYS this string for all extended morphs)
    sliderId++,
    sliderIndex,
    ...
);
```

The slider `name` field in the GFx object is `"$FACE_RLYThin"` (with `$`).  
The `callbackName` is always `"ChangeDoubleMorph"`.  
The `sliderIndex` is `SLIDER_OFFSET + i` where `i` is the position in `currentList` after sorting.

**Slider ordering:** `CreateSliderList` calls `std::sort` on the accumulated SliderList using
`sortFixedStrings` (alphabetical by `SliderInternal::name`). Sliders from ALL SliderMaps for a
race are merged into one list, then sorted alphabetically. The final GFx slider order is
alphabetical by key name across all namespaces.

---

## 5. Translation / Localization (interface/translations/)

SKEE sends the slider display name to GFx as `"$<KEY>"` (e.g. `"$FACE_RLYThin"`).  
Skyrim's ActionScript localization layer resolves `$TOKEN` strings using the active translation file.

### Translation file location

```
Data\Interface\Translations\<plugin_name>_<language>.txt
```

e.g. `Data\Interface\Translations\RleyBodyMorphs_english.txt`

SKEE itself does not load these files — the **Skyrim engine / RaceMenu SWF** reads them.
RaceMenu loads the translation file named after the currently active language (from the game's
`sLanguage` setting).

### Format

Tab-delimited, UTF-16 LE with BOM:
```
$TOKEN<TAB>Display Name
$FACE_RLYThin<TAB>RLY Thin
$FACE_RLYMuscular<TAB>RLY Muscular
$FACE_RLYHeavy<TAB>RLY Heavy
```

- Lines beginning with `;` are comments (ignored).
- `$TOKEN` is case-insensitive in lookup.
- If a token is **not found**, RaceMenu displays the raw `$FACE_RLYThin` string in the UI — not an error.

### Category name localization

Category tab names are also localization keys. SKEE sends `"$EXTRA"` and `"$EXPRESSIONS"`
for the two SKEE-added categories (SKEEHooks.cpp:775, 781). Standard category names
(`$HEAD`, `$FACE`, etc.) come from RaceMenu's own translation file.

### Is a translation file required?

**No.** The sliders will appear and function with raw token strings (`FACE_RLYThin`) displayed
if no translation file is provided. A translation file is cosmetic only.

---

## 6. Complete Load Chain Summary

```
Game launch
└── SKEE ForEachMod (modInfoList — regular ESPs only, NO ESL)
    └── For each plugin.esp:
        ├── ReadRaces( FaceGenMorphs\plugin.esp\races.ini )
        │   └── For each "EditorID = sliders\file.ini":
        │       ├── GetRaceByName(EditorID)  ← linear scan of DataHandler::races by editorId
        │       └── ReadSliders( FaceGenMorphs\plugin.esp\sliders\file.ini )
        │           └── Parse [Male]/[Female] sections, KEY = cat, type, bounds
        │               → AddSlider(race, SliderInternal) → RaceMap accumulates
        │
        ├── ReadMorphs( FaceGenMorphs\plugin.esp\morphs.ini )  [if g_extendedMorphs]
        │   └── "extension = baseTRI, morphTRI1, morphTRI2"
        │       → MorphMap.AddMorph(baseTRI, morphTRI)  ← deduplicates
        │
        └── ReadPartReplacements( FaceGenMorphs\plugin.esp\replacements.ini )

RaceMenu opened
└── AddSlider_Hook → FaceMorphInterface::LoadSliders(player->race, gender)
    ├── CreateSliderList: gathers all SliderMaps for race, sorts alphabetically
    └── For each SliderInternal:
        ├── name → "$KEY" (GFx display / localization token)
        ├── callbackName → "ChangeDoubleMorph" (always)
        └── Added to tArray<RaceMenuSlider>

Slider moved in UI
└── DoubleMorphCallback_Hook(newValue, sliderId)
    ├── slider->index >= SLIDER_OFFSET → extended morph
    └── GetSliderByIndex → SliderInternal
        ├── kTypeSlider   → ApplyMorph(lowerBound or upperBound morph name, relative)
        ├── kTypePreset   → ApplyMorph("<prefix><int>", 1.0) + SetMorphValue
        └── kTypeHeadPart → ChangeHeadPart
```

---

## 7. Our RLY Configuration — Correctness Check

| Requirement | Our value | Status |
|---|---|---|
| Namespace plugin is regular ESP (not ESL) | `RleyBodyMorphs.esp` flags=0x00000000 | ✓ |
| Namespace folder name matches plugin filename | `FaceGenMorphs\RleyBodyMorphs.esp\` | ✓ |
| races.ini EditorIDs match game-loaded race editorId | `00DZN_NordRace` etc. (copied from COtR's working file) | ✓ |
| sliders\RLY.ini path in races.ini is relative to namespace | `sliders\RLY.ini` | ✓ |
| Slider type for face morphs | `Slider` | ✓ |
| Category for Head sliders | `8` (kCategoryHead) | ✓ |
| Lower bound for one-sided slider | `None` (becomes empty → lowerBound = 0.0) | ✓ |
| Upper bound morph name exists in TRI | `RLY_Thin`, `RLY_Muscular`, `RLY_Heavy` in `Genesis\FACE_MaleHead.tri` | ✓ (confirmed by tri_audit.py) |
| `g_extendedMorphs` must be true | Default true, no skse.ini override | ✓ |
| morphs.ini not needed in our namespace | COtR's `racemenu.esp\morphs.ini` already routes `Genesis\FACE_MaleHead.tri` | ✓ |
| Translation file | Not provided — sliders display as `FACE_RLYThin` etc. | Optional |

---

## 8. Known Pitfalls

| Pitfall | Consequence | Guard |
|---|---|---|
| ESL-flagged plugin used as namespace | Silently skipped by ForEachMod | Use regular ESP |
| Wrong EditorID in races.ini | `GetRaceByName` returns NULL → silently dropped | Verify from CK or COtR's own races.ini |
| Slider type `Slider` when `g_extendedMorphs=false` | Slider entry silently skipped | Ensure skse.ini doesn't set `bExtendedMorphs=0` |
| Prepending RLY sliders to an existing ini (e.g. Trixter.ini) | All COtR slider indices shift +N → "lip sliders affect nose" after sex switch | Use separate namespace for RLY (separate SliderMap) |
| Same slider KEY in multiple namespaces for same race | `SliderMap::AddSlider` overwrites (last-inserted wins per gender) — not accumulated | Unique KEY names across all mods |
| `lowerBound == upperBound == ""` | Slider range [0,0] — visible but has no effect | Always set at least one bound |
| MO2 priority: two mods providing same `.ini` path | Only the higher-priority mod's file is read (VFS single winner) | Don't share ini paths — use distinct namespaces |
