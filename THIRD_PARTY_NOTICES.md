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

## Source

Corresponding source: https://github.com/drpavlov95/ModernPhysiqueSystem
