ScriptName MuDynamicNormalMap Hidden

; Compile-time interface for Mu Dynamic NormalMap. The installed Mu mod owns
; the runtime PEX and native implementations; ModernPhysiqueSystem deploys
; neither from this declaration-only source.
Int Function GetVersion() Native Global
Function QUpdateNormalmap(Actor a_actor, Int armorSlotBit) Native Global
