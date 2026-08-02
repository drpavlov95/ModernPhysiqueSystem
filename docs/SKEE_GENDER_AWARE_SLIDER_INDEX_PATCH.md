# SKEE patch — gender-aware slider index resolution

**Date:** 2026-08-02
**Target:** RaceMenu / `skee64` (`expired6978/SKSE64_Plugins`)
**Source tree used for line numbers:** `E:\SkyrimDev\RaceMenu GitSource\SKSE64Plugins\skee64`

---

## 1. The defect

`FaceMorphInterface` resolves extended face sliders two different ways, and only one
of them is gender-aware:

```cpp
// FaceMorphInterface.h
SliderInternalPtr GetSlider(TESRace * race, UInt8 gender, SKEEFixedString name);  // line 323 — gender-aware
SliderInternalPtr GetSliderByIndex(TESRace * race, UInt32 index);                 // line 324 — gender-BLIND
```

The backing store keeps exactly one list per race:

```cpp
// FaceMorphInterface.h line 114
typedef std::unordered_map<TESRace*, SliderList> RaceSliders;
```

`CreateSliderList(race, gender)` *replaces* that single list with the list for one
gender. Whichever gender was built last wins for the whole race.

`DoubleMorphCallback_Hook` (`SKEEHooks.cpp:824`) then resolves the incoming slider
by index through the gender-blind accessor:

```cpp
UInt32 sliderIndex = slider->index - SLIDER_OFFSET;
SliderInternalPtr sliderInternal = g_morphInterface.GetSliderByIndex(player->race, sliderIndex);
...
SetRelativeMorph(actorBase, faceNode, bound, relative);
g_morphInterface.SetMorphValue(actorBase, sliderInternal->name, newValue);
```

`slider->index` was assigned when the *visible* list for a given sex was built
(`LoadSliders`, which itself calls `CreateSliderList(race, currentSex)`). When the
stored list belongs to the other sex, the index addresses a different slider, and
the value is written to the wrong morph.

### Why the consequence is severe, not cosmetic

Male and female lists differ in length and content (measured on a COtR/Trixter
install: **295 male entries vs 259 female**), so a given index means different
things per sex. Two failure classes follow:

1. **Wrong morph written.** A value lands on an unrelated slider.
2. **Type confusion.** `SliderInternal::kTypePreset` sliders store a *list index*
   as their value (e.g. `37`). If that value is written onto a `kTypeSlider`
   entry, `ApplyMorphs` later amplifies it (`FaceMorphInterface.cpp:1234`):

```cpp
float relative = abs(it->second);          // 37.0
if (relative > 1.0) {
    UInt32 count = (UInt32)relative;       // 37
    for (UInt32 i = 0; i < count; i++)
        SetMorph(npc, faceNode, morphName, 1.0);   // full-strength morph, 37 times
}
```

Because `ApplyMorphs` runs on **every** face rebuild, the corrupted value is
re-applied forever after it is written once. Observed in the field as a face
slider (`Nose_NoseWingWidth = 37.000`) permanently distorting the head after
repeated sex switches.

---

## 2. The fix

Make the index accessor gender-aware and store one list per (race, gender).
`GetSlider` (by name) already takes `gender`; this brings the index path in line
with it. No behavioral change for callers that were already correct.

### 2.1 `FaceMorphInterface.h`

**Line 114** — one list per gender instead of one per race:

```cpp
// before
typedef std::unordered_map<TESRace*, SliderList> RaceSliders;

// after
typedef std::unordered_map<TESRace*, std::array<SliderList, 2>> RaceSliders;
```

Ensure `<array>` is included by this header (add `#include <array>` if it is not
already reachable).

**Line 324** — gender-aware index lookup:

```cpp
// before
SliderInternalPtr GetSliderByIndex(TESRace * race, UInt32 index);

// after
SliderInternalPtr GetSliderByIndex(TESRace * race, UInt8 gender, UInt32 index);
```

**Line 327** — the internal appender needs the gender it is appending to:

```cpp
// before
void AddSlider(TESRace * race, SliderInternalPtr & slider);

// after
void AddSlider(TESRace * race, UInt8 gender, SliderInternalPtr & slider);
```

### 2.2 `FaceMorphInterface.cpp`

**`GetSliderByIndex` (line 418)**

```cpp
// after
SliderInternalPtr FaceMorphInterface::GetSliderByIndex(TESRace * race, UInt8 gender, UInt32 index)
{
	if (gender > 1)
		return NULL;

	RaceSliders::iterator it = m_internalMap.find(race);
	if(it != m_internalMap.end()) {
		SliderList & list = it->second[gender];
		if(index < list.size())
			return list.at(index);
	}
	return NULL;
}
```

**`CreateSliderList` (line 1059)** — clear and rebuild only the requested gender,
so building one sex no longer destroys the other:

```cpp
// after
SliderList * FaceMorphInterface::CreateSliderList(TESRace * race, UInt8 gender)
{
	if (gender > 1)
		return NULL;

	// Clear only this gender's list before rebuilding it.
	RaceSliders::iterator it = m_internalMap.find(race);
	if(it != m_internalMap.end()) {
		it->second[gender].clear();
	}

	SliderSetPtr sliderMaps = m_raceMap.GetSliderSet(race);
	if(sliderMaps)
	{
		sliderMaps->for_each_slider([&](SliderGenderPtr genders) {
			SliderInternalPtr sliderInternal = genders->slider[gender];
			if (sliderInternal)
				AddSlider(race, gender, sliderInternal);
			return false;
		});
	}

	it = m_internalMap.find(race);
	if(it != m_internalMap.end()) {
		SliderList & list = it->second[gender];
		std::sort(list.begin(), list.end(), sortFixedStrings);
		list.resize(list.size());
		return &list;
	}

	return NULL;
}
```

**`AddSlider` (line 1089)**

```cpp
// after
void FaceMorphInterface::AddSlider(TESRace * race, UInt8 gender, SliderInternalPtr & slider)
{
	if (gender > 1)
		return;

	RaceSliders::iterator it = m_internalMap.find(race);
	if(it != m_internalMap.end()) {
		it->second[gender].push_back(slider);
	} else {
		std::array<SliderList, 2> newLists;
		newLists[gender].push_back(slider);
		m_internalMap.emplace(race, newLists);
	}
}
```

### 2.3 Call sites

**`SKEEHooks.cpp:824`** — `gender` is already in scope (assigned at line 810):

```cpp
// before
SliderInternalPtr sliderInternal = g_morphInterface.GetSliderByIndex(player->race, sliderIndex);

// after
SliderInternalPtr sliderInternal = g_morphInterface.GetSliderByIndex(player->race, gender, sliderIndex);
```

**`ScaleformCharGenFunctions.cpp:503` and `:588`** — both read the player's slider
data. Derive the gender from the player's base form at the top of each enclosing
`Invoke`, then pass it:

```cpp
// add near the start of the enclosing function
PlayerCharacter * player = (*g_thePlayer);
TESNPC * playerBase = DYNAMIC_CAST(player->baseForm, TESForm, TESNPC);
UInt8 playerGender = playerBase ? CALL_MEMBER_FN(playerBase, GetSex)() : 0;

// before
SliderInternalPtr sliderInternal = g_morphInterface.GetSliderByIndex((*g_thePlayer)->race, sliderIndex);

// after
SliderInternalPtr sliderInternal = g_morphInterface.GetSliderByIndex(player->race, playerGender, sliderIndex);
```

`FaceMorphInterface.cpp:69` (`m_internalMap.clear()`) and `:1280`
(`CreateSliderList(player->race, GetSex())`) need no change.

---

## 3. Why this is a root fix and not a mitigation

It removes the precondition rather than blocking a consequence. After the patch,
an index is only ever resolved against the list it was assigned from, so:

- no wrong-morph writes,
- no Preset-index-into-Slider type confusion,
- therefore no amplified replay in `ApplyMorphs`.

It fixes the defect for every consumer — RaceMenu's own sliders, COtR's, and any
mod that adds extended face sliders — not just for one mod's axes.

## 4. Verification

1. Build `skee64.dll` from the patched source.
2. In-game: open RaceMenu, switch sex repeatedly (M→F→M→F, at least four
   transitions), moving face sliders in each state.
3. Expected: no slider value outside `[-1, 1]` for `kTypeSlider` entries, and no
   progressive facial distortion across transitions.
4. Regression check: extended sliders still respond normally for both sexes, and
   preset-type sliders (head parts, hair) still cycle correctly.

## 5. Distribution note

A locally built `skee64.dll` is suitable for validating the fix, but it must not
be redistributed with a mod: that would replace RaceMenu. The distribution path
is upstream (`expired6978/SKSE64_Plugins`).

Independent of upstream acceptance, a mod can make **its own** sliders immune by
naming them so they sort to identical indices in both the male and female lists
(e.g. an `A0_` prefix pinning them to internal indices 0/1/2). That removes the
divergence for those sliders without touching SKEE, but it does nothing for
sliders the mod does not own — which is exactly why the patch above is needed.
