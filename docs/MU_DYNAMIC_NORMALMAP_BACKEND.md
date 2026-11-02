# Mu Dynamic NormalMap backend

## Runtime contract

`ModernPhysiqueSystem.ini` selects the normal-map owner:

```ini
[NormalMaps]
Backend=auto
RealtimePreview=1
```

- `auto` uses Mu Dynamic NormalMap's `QUpdateNormalmap` API when the optional
  `MuDynamicNormalMap.dll` is loaded; otherwise MPS leaves normal-map
  ownership untouched.
- `mu` explicitly enables the same optional integration and emits a warning if
  Mu is missing; it still does not make Mu a load-time dependency.
- `none` disables MPS normal-map integration even when Mu is installed.

Mu is never a load-time dependency of MPS. If it is absent, the plugin still
loads and morphs normally; only the Mu update requests become no-ops.

## Update cadence

MPS requests skin slots `0x18D` after physique morph work has run. The active
meshes do not use the same body slot: HIMBO is slot 2, while the COtR/CBBE
female body is split across slots 7 and 8. Slots 0 and 3 are included for the
head and hands so Mu can weld the neck, wrist, and ankle boundaries.

Commits request an update immediately. Preview updates run at a 32 ms UI
cadence and call Mu's native C++ `QUpdateNormalmap` interface immediately after
RaceMenu's vertex diff is applied. MPS does not use a Papyrus fallback or a
`SetNormalMap` layer. A RaceMenu sex/race rebuild temporarily holds the request
until `RSM_Reinitialized` and two stable game-thread observations of the rebuilt
actor.

For the real-time profile, Mu may use uncompressed output and no disk cache.
Those are Mu configuration choices, not MPS requirements.

## Opt-out and performance profile

When MPS owns live preview, Mu's independent actor-wide detector should remain
disabled so it cannot race the serialized MPS requests. Foreground queueing
keeps wheel updates immediate; forced GPU synchronization stays off because Mu
documents it as substantially slower:

```ini
[RealtimeDetect]
RealtimeDetect=false

[Performance]
AutoTaskQ=0
GPUForceSync=false
```

Set `NormalMaps.RealtimePreview=0` to keep live body morphing but queue Mu only
when the wheel interaction is committed. The default is `1`.

MPS queues all observed skin slots while dragging, then forces another update
after a commit and on load/RaceMenu lifecycle events.

The current RaceMenu test profile is intentionally aggressive: `TaskQTickMS=0`,
`UpdateDelayTick=0`, no texture compression, no disk cache, and error-only Mu
logging. It minimizes latency at the cost of foreground frame time and VRAM.

An installation without Mu is valid. When Mu is installed, deployment may
provide a tested Mu profile, but MPS must not require that profile or overwrite
the user's Mu installation merely to load.

## Cleanup of old MPS overrides

The first Mu update after loading a game performs one cleanup for both sex keys.
The cleanup covers the slot, node, and COtR armor-addon paths previously used by
`RleyTextureBridge`, applies the override removals once, and queues one node
rebuild. Mu reconciliation waits for that rebuild to settle; ordinary detail
updates never rebuild the actor.

NiOverride does not expose ownership metadata. The cleanup therefore removes
whatever currently occupies the exact old MPS keys and nodes. Do the first
migration test without another mod writing normal paths to those same targets;
Mu should be the sole normal-map owner while its integration is active.

## Initial runtime test

1. Enable Mu Dynamic NormalMap in the active MO2 profile.
2. Keep MPS on `Backend=auto` and start without custom MPS detail/overlay assets.
3. Test neutral, Thin, Muscular, Heavy, and mixed values while naked and in a
   BodyMorph-enabled outfit.
4. Test male/female switching, first/third person, RaceMenu close/reopen, and
   save/load.
5. Confirm the MPS log contains Mu dispatches when Mu is installed, and no
   normal-map error when it is not.
6. Confirm the physique UI has no details toggle and the MPS log dispatches
   `slots=397` (`0x18D`) for both sexes.
