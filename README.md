## RleyBodyMorphs

Skyrim SE 1.5.97 SKSE plugin + Prisma UI prototype for a Starfield-style physique triangle inside RaceMenu.

### Current state

- Native morph backend is implemented in [`src/plugin.cpp`](src/plugin.cpp).
- Morphs are applied through RaceMenu/SKEE `IBodyMorphInterface`.
- Prisma UI is served from `PrismaUI/views/RleyBodyMorphs/`.
- RaceMenu extension scripts live in `Source/Scripts/`.
- Current working opener path is the custom RaceMenu category + helper slider from `RleyPhysiqueRM.psc`.

### Morph channels

- `RleyThin`
- `RleyMuscular`
- `RleyFat`

### Known gap

Automatic Prisma visibility based on the active RaceMenu category is not finished yet. The stable baseline is:

- custom `rley_physique` category
- helper `"Open Physique"` slider
- native preview/commit through the DLL

### Repo layout

- `src/` - SKSE plugin source
- `PrismaUI/views/RleyBodyMorphs/` - Prisma frontend
- `Source/Scripts/` - Papyrus source scripts

### Notes

- `RleyRuntimeMorphController.psc` is intentionally inert now. Real-time morph preview moved into the DLL.
- `hook.cpp`, `hook.h`, and `settings.h` are placeholders from the original template and are not part of the current feature path.
