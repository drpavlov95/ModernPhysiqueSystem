# Face Slider Index-Stable Workaround - 2026-05-28

## Native finding

SKEE sorts RaceMenu face sliders by `SliderInternal::name`, assigns
`sliderIndex = SLIDER_OFFSET + sortedIndex`, then later resolves slider movement
through `GetSliderByIndex(player->race, sliderIndex)`.

The reverse-engineered/source-confirmed problem is that `GetSliderByIndex` is
race-keyed, not race+gender-keyed. After male > female > male, a stale opposite
sex sorted list can interpret a valid male slider index as a different female
slider.

## Live data proof before this change

In the injected COtR `Trixter.ini`, the old MPS keys sorted to different male
and female indices:

| Male slider | Male zero index | Female key at same index |
| --- | ---: | --- |
| `FACE_RLYHeavy` | 177 | `LIP_LipUpperFuller` |
| `FACE_RLYMuscular` | 178 | `LIP_LipUpperHeight` |
| `FACE_RLYThin` | 179 | `LIP_LipUpperPikHeight` |
| `LIP_LipsHeight` | 194 | `Nose_AroundNoseBackForward` |

This matches the in-game observation that RLY sliders move the upper lip and
`Lips Height` bends the nose after returning to male.

## Change

The MPS face slider saved keys were renamed so they sort to the same first three
positions in both sex lists:

| Old key | New key | Geometry morph |
| --- | --- | --- |
| `FACE_RLYHeavy` | `A0_MPS_RLYHeavy` | `RLY_Heavy` |
| `FACE_RLYMuscular` | `A0_MPS_RLYMuscular` | `RLY_Muscular` |
| `FACE_RLYThin` | `A0_MPS_RLYThin` | `RLY_Thin` |

Post-change sorted indices:

| Key | Male zero index | Female zero index |
| --- | ---: | ---: |
| `A0_MPS_RLYHeavy` | 0 | 0 |
| `A0_MPS_RLYMuscular` | 1 | 1 |
| `A0_MPS_RLYThin` | 2 | 2 |

## Scope

This is a reversible MPS workaround. It only stabilizes the MPS RLY sliders. It
does not fix RaceMenu/COtR's own stale-index behavior for unrelated sliders.

## Rollback

External mod backup:

`E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker\__rollback_backups\20260528_index_stable_mps_keys`

Copy its backed-up files over the live files to restore the previous injected
`FACE_RLY*` state.
