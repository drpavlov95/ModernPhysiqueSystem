# Third-party notices

Physique Wheel (`PhysiqueWheel.dll`) is licensed under the GNU General Public
License version 3; its complete text is retained in the root `LICENSE` file.

## RaceMenu/SKEE interfaces

`src/RaceMenuInterop.h` contains the public RaceMenu/SKEE ABI declarations —
`SKEEFixedString`, `IPluginInterface`, `IInterfaceMap`, `InterfaceExchangeMessage` and
`IBodyMorphInterface` — adapted from the upstream RaceMenu interface header:

https://github.com/expired6978/SKSE64Plugins/blob/master/skee64/IPluginInterface.h

`src/SkeeFaceMapGuard.h` declares `IFaceMorphInterface` from the same upstream header.

Unlike the minimal subset used by [OBody NG](https://github.com/Aietos/OBody-NG/blob/main/include/SKEE.h),
this adaptation retains `SKEEFixedString`'s inline implementation, so it carries
upstream code rather than declarations alone.

The upstream repository is licensed under the GNU General Public License version 3.
That is why this project is GPL-3.0, and the sibling projects that use the same
interface agree: [Modern Physique Framework](https://github.com/drpavlov95/ModernPhysiqueFramework),
[OBody NG](https://github.com/Aietos/OBody-NG) and
[RaceMenu Atelier](https://github.com/rustyoldsword/RaceMenuAtelier) are all GPL-3.0.

## Prisma UI API

`src/PrismaUI_API.h` is StarkMP's public Prisma UI modder header, copied under the
grant stated in the file itself: *"For modders: Copy this file into your own project if
you wish to use this API."*

## Mu's Dynamic Normal Maps API

`src/MuDynamicNormalMapAPI.h` declares the vtable prefix of Mu's public
`IDynamicNormalMap` interface in order to call it dynamically. No upstream code is
copied, and the integration is optional — without Mu the plugin leaves normal-map
ownership untouched.

## Permissive dependencies

All remaining dependencies are permissive and GPL-compatible, so SKEE is the sole
copyleft vector:

| Dependency | Licence |
| --- | --- |
| [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) (orig. Ryan-rsm-McKenzie) | MIT |
| [SimpleIni](https://github.com/brofield/simpleini) (Brodie Thiesfield) | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) (Niels Lohmann) | MIT |
| [doctest](https://github.com/doctest/doctest) (Viktor Kirilov) — tests only, not linked into the shipped DLL | MIT |

## Assets are not covered by the GPL

The licence covers **code**: the SKSE plugin, the Prisma UI frontend under
and `PrismaUI/views/`.

It does **not** cover morph, mesh or texture assets. The `RLY_*` face morphs and `RM_*`
body morphs this plugin reads are hand-made and ship with **Physiques of Skyrim**,
under that mod's own permissions. Loading a data file does not make it a derivative of
the program that loads it.

## Not redistributable: `interface/racesex_menu.swf`

> [!IMPORTANT]
> This file is tracked in the repository and **must not be shipped in a public release
> as-is.**

`tools/BuildRaceMenuSwf.ps1` produces it by using FFDec to replace the `RaceMenu` and
`SliderListEntry` ActionScript packages *inside RaceMenu's own compiled SWF*.

The GPL grant does not reach this file: expired6978's GPL repository contains no `.as`,
`.swf` or `.fla` sources at all, so the SWF is a Nexus-distributed asset outside the
source licence, and RaceMenu's Nexus permissions do not allow redistributing its files.
Comparable projects (RaceMenu VR 2, Racemenu - Prisma UI) ship no RaceMenu SWF for this
reason. Bundling it would also hard-overwrite the user's RaceMenu, breaking any other
mod that touches `racesex_menu.swf`.

Before release, pick one:

1. **Written permission from expired6978** to redistribute the patched SWF; or
2. **Do not bundle it** — ship `BuildRaceMenuSwf.ps1` as an install-time step that
   patches the user's own RaceMenu copy, leaving the original asset undistributed.

## Source

Corresponding source: https://github.com/drpavlov95/ModernPhysiqueSystem
(the `origin` remote of this working tree).

> [!WARNING]
> As of 2026-11-02 that repository returns **404 — it is private or not yet created**,
> while the sibling `ModernPhysiqueFramework` is public.
>
> The GPL requires that corresponding source be obtainable by everyone who receives the
> binary. Publishing a GPL-3.0 release while the repository stays private puts the
> release in violation of its own licence. **Make the repository public before release**,
> or ship the source alongside the download.
