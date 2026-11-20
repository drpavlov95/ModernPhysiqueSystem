## RleyBodyMorphs

Skyrim SE 1.5.97 SKSE plugin + Prisma UI prototype for a Starfield-style physique triangle inside RaceMenu.

### Current state

- Native morph backend is implemented in [`src/plugin.cpp`](src/plugin.cpp).
- Morphs are applied through RaceMenu/SKEE `IBodyMorphInterface`.
- Prisma UI is served from `PrismaUI/views/RleyBodyMorphs/`.
- The wheel appears inside RaceMenu's Body category. `interface/racesex_menu.swf` is generated at game start from the user's own copy (see THIRD_PARTY_NOTICES.md).
- No Papyrus scripts and no ESP: everything runs in the DLL.

### Morph channels

- `RleyThin`
- `RleyMuscular`
- `RleyFat`

### Repo layout

- `src/` - SKSE plugin source
- `PrismaUI/views/RleyBodyMorphs/` - Prisma frontend
- `reference/RaceMenu/` - RaceMenu ActionScript used to author the SWF patch

### Notes

- `hook.cpp`, `hook.h`, and `settings.h` are placeholders from the original template and are not part of the current feature path.

### License

[GPL-3.0-only](LICENSE).

The RaceMenu/SKEE interface declarations in `src/RaceMenuInterop.h` are adapted from
public interface headers, and retain `SKEEFixedString`'s inline implementation. Every
other dependency is permissive, so SKEE is the sole copyleft vector. The licence covers
code, not the morph assets. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

> [!WARNING]
> Two things gate a public release:
>
> 1. `interface/racesex_menu.swf` is tracked here but **is not redistributable** — it is
>    built by patching RaceMenu's own compiled SWF, outside both expired6978's GPL grant
>    and RaceMenu's Nexus permissions. Either obtain written permission, or ship
>    `tools/BuildRaceMenuSwf.ps1` as an install-time step that patches the user's copy.
> 2. The `origin` repository is **private**, which the GPL does not allow for a released
>    binary. Make it public, or ship the source with the download.
>
> See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
