# Rley Body Morphs

Bring a Starfield-style physique triangle to Skyrim.

Rley Body Morphs adds a RaceMenu-driven physique control that lets you blend between **Thin**, **Muscular**, and **Heavy** in real time instead of juggling a pile of disconnected sliders. Under the hood, it uses a native SKSE plugin to apply morphs directly through RaceMenu/SKEE, preview changes live, and keep supporting body and hand normal-map swaps in sync with the shape you choose.

This is not just another preset. It is a runtime physique system built to make body shaping feel faster, clearer, and more visual.

---

## What This Mod Does

- Adds a triangle-based physique control for blending between thin, muscular, and heavy body states
- Applies body morphs natively through RaceMenu/SKEE for responsive real-time preview
- Syncs the selected physique state with body weight-style intensity handling
- Supports runtime body and hand normal-map selection to match the chosen physique
- Includes optional face morph support for supported RaceMenu extended-morph setups
- Focuses on a cleaner, more modern character-sculpting workflow inside RaceMenu

---

## Key Features

### Starfield-Style Physique Triangle

Instead of pushing separate sliders and guessing how they combine, you drag a single selector inside a three-point triangle:

- **Thin**
- **Muscular**
- **Heavy**

Move toward any corner for a stronger result, or blend between edges for mixed physiques like:

- thin + muscular
- muscular + heavy
- heavy + thin

The center returns you to a neutral state.

### Native Real-Time Preview

The mod does not fake the effect through delayed scripts. The SKSE plugin talks directly to RaceMenu's morph interfaces, so changes preview immediately while you move the control.

### Weight Sync Support

The physique direction and overall body intensity stay coordinated, which makes it easier to preserve the shape style you chose while adjusting how strong the result should be.

### Matching Normal-Map Support

Rley Body Morphs can also select matching body and hand normal-map variants at runtime so the final look stays visually consistent with the morph state, especially when pushing more muscular or heavier silhouettes.

### Optional Face Morph Integration

For supported setups, the same thin / muscular / heavy values can also drive RaceMenu face morph values. If your setup does not support that path, the mod falls back gracefully and body morphing still remains the core stable feature.

---

## Who This Is For

This mod is for players who want:

- a more intuitive body-shaping workflow
- a visual physique picker instead of raw slider micromanagement
- live preview while sculpting
- a modern overlay-style UI inside RaceMenu
- optional support for matching face and normal-map presentation

If you like building characters that sit somewhere between archetypes instead of landing on a single preset, this mod was made for that.

---

## Current Scope

The current release is centered on the **player character** and the **Thin / Muscular / Heavy** morph set.

The most stable path is:

- body morph application
- live preview and commit
- RaceMenu/SKEE native morph handling
- runtime normal-map selection when matching assets are present

Face morph support is included as an enhancement layer, but it depends on RaceMenu extended morphs and compatible race mappings.

---

## Requirements

### Hard Requirements

- **Skyrim Special Edition 1.5.97**
- **SKSE64** for Skyrim SE 1.5.97
- **RaceMenu** / SKEE
- **Prisma UI**
- **powerofthree's Papyrus Extender**

### Setup Requirements

- A compatible body setup with morph targets for:
  - `RM_Thin`
  - `RM_Muscular`
  - `RM_Heavy`
- If you want face morph support:
  - RaceMenu extended morphs must be enabled
  - your race must be mapped for the included face morph configuration
  - the included mappings currently target supported COtR female race setups

### Important Note

This mod is a **runtime controller** for compatible morphs. It does not magically generate body morph targets on its own. Your body setup still needs to provide the morphs this system is meant to drive.

---

## Installation

1. Install all requirements first.
2. Install this mod with your mod manager.
3. Make sure the SKSE plugin, scripts, and interface files from this mod are enabled.
4. Make sure your body setup includes the supported morph names.
5. Launch the game through SKSE.
6. Open RaceMenu and access the Physique control included with the mod's setup.

If you are using the face morph portion and it does not respond:

- confirm RaceMenu extended morphs are enabled in `skee64.ini` or `skee64_custom.ini`
- confirm your race is covered by the included face morph mapping

---

## How To Use

1. Open RaceMenu.
2. Open the mod's Physique control.
3. Drag the selector toward **Thin**, **Muscular**, or **Heavy**.
4. Stop between points to create blended physiques.
5. Use **Reset** to return to neutral.
6. Fine-tune your overall character setup as usual.

The mod previews changes while you move the selector, then commits the final values through the native morph pipeline.

---

## Compatibility

### General Compatibility

Rley Body Morphs is designed to work with RaceMenu by using its exposed morph interfaces instead of replacing the entire character system.

### Body Compatibility

It should work best with body setups that already support the target morph names. If your installed body, morph project, or preset does not include the expected morphs, the control will not have valid data to drive.

### Texture / Normal Compatibility

The normal-map side is runtime-driven. If you install matching normal assets, the mod can use them to reinforce the selected physique. If those assets are missing, the morph side can still work, but you should not expect the same texture response.

The texture bridge also includes COtR-aware handling for body and hand application, with a more generic fallback path for standard setups.

### Face Morph Compatibility

Face morph support is more selective than body support.

- It depends on RaceMenu extended morphs
- it depends on valid race mappings
- it is currently aimed at supported COtR female race setups

If the face path is unavailable, the mod continues using the stable body-first behavior.

### Mods That May Need Attention

Expect possible conflicts or last-write-wins behavior with mods that:

- aggressively replace body or hand texture sets at runtime
- overwrite NiOverride-based skin or node overrides
- replace the same morph targets with different naming conventions
- depend on unsupported game versions

---

## Known Limitations

- Built for **Skyrim SE 1.5.97**
- Focused on the **player character**
- Core workflow is centered on **Thin / Muscular / Heavy**
- Face morph support is setup-dependent and not guaranteed for every race
- Matching normal-map behavior depends on compatible installed assets
- This mod does not replace your body mod, preset framework, or morph project

---

## FAQ

### Is this a body preset?

No. This is a runtime physique control system. It drives compatible morph targets through RaceMenu/SKEE.

### Does this replace CBBE, 3BA, BHUNP, or my body mod?

No. It sits on top of your existing body setup. You still need a compatible body and morph data.

### Does it support NPCs?

The current focus is the player character.

### Do I need the face morph part for the mod to work?

No. The stable core feature is body morphing. Face morph support is an extra layer for compatible setups.

### What if the face does not change?

Check that RaceMenu extended morphs are enabled and that your race is supported by the included mapping.

### What if the body changes but the textures do not?

That usually means the matching normal-map assets are missing, being overwritten, or being replaced by another runtime texture system.

### Is this for Anniversary Edition?

This build is intended for **Skyrim SE 1.5.97**.

---

## Recommended Mod Page Notes

If you are uploading this to Nexus, I would strongly recommend adding:

- 1 screenshot of the triangle UI in RaceMenu
- 1 before / after comparison for each corner state
- 1 blended example showing a non-corner physique
- a short GIF or video of live dragging and preview

This mod will sell much better visually than through text alone.

---

## Credits

- Expired / RaceMenu for the morph interfaces and NiOverride ecosystem
- SKSE and CommonLibSSE contributors
- Prisma UI for the in-game webview layer
- powerofthree for Papyrus Extender

---

## Permissions

You can adjust this section to match how you want to release the mod, but a clean starter version would be:

> Please do not reupload this mod or its assets without permission.  
> Patches are welcome. If you want to build compatibility support for other bodies, races, or texture packs, feel free to reach out.

---

## Short Description Version

If you also want a shorter summary for the Nexus summary field:

> Adds a Starfield-style physique triangle to RaceMenu for real-time Thin / Muscular / Heavy blending, powered by a native SKSE backend with optional face morph and matching normal-map support.
