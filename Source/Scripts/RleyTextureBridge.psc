Scriptname RleyTextureBridge Hidden

Function ApplyBodyNormalPath(String normalTexturePath) global
	String prefix = "[ModernPhysiqueSystem] [RleyTextureBridge] "
	Debug.Trace(prefix + "ApplyBodyNormalPath called for " + normalTexturePath)

	Actor playerRef = Game.GetPlayer()
	If playerRef == None
		Debug.Trace(prefix + "Abort: player is None")
		Return
	EndIf

	If normalTexturePath == ""
		Debug.Trace(prefix + "Abort: normal texture path is empty")
		Return
	EndIf

	Int bodySlotMask = 4
	Int textureSetOverrideKey = 6
	Int stringOverrideKey = 9
	Int textureSetIndex = -1
	Int normalTextureIndex = 1

	; Keep runtime deterministic: clear stale body skin overrides from previous experiments.
	NiOverride.RemoveSkinOverride(playerRef, True, False, bodySlotMask, textureSetOverrideKey, textureSetIndex)
	NiOverride.RemoveSkinOverride(playerRef, True, True, bodySlotMask, textureSetOverrideKey, textureSetIndex)
	NiOverride.RemoveSkinOverride(playerRef, True, False, bodySlotMask, stringOverrideKey, normalTextureIndex)
	NiOverride.RemoveSkinOverride(playerRef, True, True, bodySlotMask, stringOverrideKey, normalTextureIndex)

	NiOverride.AddSkinOverrideString(playerRef, True, False, bodySlotMask, stringOverrideKey, normalTextureIndex, normalTexturePath, True)
	NiOverride.AddSkinOverrideString(playerRef, True, True, bodySlotMask, stringOverrideKey, normalTextureIndex, normalTexturePath, True)

	NiOverride.ApplySkinOverrides(playerRef)
	playerRef.QueueNiNodeUpdate()

	Bool thirdPersonOverride = NiOverride.HasSkinOverride(playerRef, True, False, bodySlotMask, stringOverrideKey, normalTextureIndex)
	Bool firstPersonOverride = NiOverride.HasSkinOverride(playerRef, True, True, bodySlotMask, stringOverrideKey, normalTextureIndex)
	Debug.Trace(prefix + "Applied NiOverride normal path " + normalTexturePath + " (body3p=" + thirdPersonOverride + ", body1p=" + firstPersonOverride + ")")
EndFunction

Function ApplyHandNormalPath(String normalTexturePath) global
	String prefix = "[ModernPhysiqueSystem] [RleyTextureBridge] "
	Debug.Trace(prefix + "ApplyHandNormalPath called for " + normalTexturePath)

	Actor playerRef = Game.GetPlayer()
	If playerRef == None
		Debug.Trace(prefix + "Abort hand apply: player is None")
		Return
	EndIf

	If normalTexturePath == ""
		Debug.Trace(prefix + "Abort hand apply: normal texture path is empty")
		Return
	EndIf

	Int handSlotMask = 8
	Int textureSetOverrideKey = 6
	Int stringOverrideKey = 9
	Int textureSetIndex = -1
	Int normalTextureIndex = 1

	NiOverride.RemoveSkinOverride(playerRef, True, False, handSlotMask, textureSetOverrideKey, textureSetIndex)
	NiOverride.RemoveSkinOverride(playerRef, True, True, handSlotMask, textureSetOverrideKey, textureSetIndex)
	NiOverride.RemoveSkinOverride(playerRef, True, False, handSlotMask, stringOverrideKey, normalTextureIndex)
	NiOverride.RemoveSkinOverride(playerRef, True, True, handSlotMask, stringOverrideKey, normalTextureIndex)

	NiOverride.AddSkinOverrideString(playerRef, True, False, handSlotMask, stringOverrideKey, normalTextureIndex, normalTexturePath, True)
	NiOverride.AddSkinOverrideString(playerRef, True, True, handSlotMask, stringOverrideKey, normalTextureIndex, normalTexturePath, True)

	NiOverride.ApplySkinOverrides(playerRef)
	playerRef.QueueNiNodeUpdate()

	Bool thirdPersonOverride = NiOverride.HasSkinOverride(playerRef, True, False, handSlotMask, stringOverrideKey, normalTextureIndex)
	Bool firstPersonOverride = NiOverride.HasSkinOverride(playerRef, True, True, handSlotMask, stringOverrideKey, normalTextureIndex)
	Debug.Trace(prefix + "Applied hand NiOverride normal path " + normalTexturePath + " (hand3p=" + thirdPersonOverride + ", hand1p=" + firstPersonOverride + ")")
EndFunction

Function ApplyBodyTextureSet(String textureSetEditorID) global
	String prefix = "[ModernPhysiqueSystem] [RleyTextureBridge] "
	Debug.Trace(prefix + "ApplyBodyTextureSet called for " + textureSetEditorID)

	Actor playerRef = Game.GetPlayer()
	TextureSet textureSetForm = PO3_SKSEFunctions.GetFormFromEditorID(textureSetEditorID) as TextureSet

	If playerRef == None || textureSetForm == None
		If playerRef == None
			Debug.Trace(prefix + "Abort: player is None")
		EndIf
		If textureSetForm == None
			Debug.Trace(prefix + "Abort: failed to resolve TextureSet " + textureSetEditorID)
		EndIf
		Return
	EndIf

	Bool applied = False
	Bool hasCotrSkin = False

	; COtR uses a custom skin armor chain. Prefer replacing that source body TXST directly.
	Armor cotrSkinArmor = PO3_SKSEFunctions.GetFormFromEditorID("00DZB_SkinNaked") as Armor
	If cotrSkinArmor == None
		cotrSkinArmor = PO3_SKSEFunctions.GetFormFromEditorID("DZB_SkinNaked") as Armor
	EndIf

	TextureSet cotrSourceBodyTextureSet = PO3_SKSEFunctions.GetFormFromEditorID("00DZB_SkinBodyFemale") as TextureSet
	If cotrSourceBodyTextureSet == None
		cotrSourceBodyTextureSet = PO3_SKSEFunctions.GetFormFromEditorID("DZB_SkinBodyFemale") as TextureSet
	EndIf

	If cotrSkinArmor != None && cotrSourceBodyTextureSet != None
		hasCotrSkin = PO3_SKSEFunctions.HasSkin(playerRef, cotrSkinArmor)
		Debug.Trace(prefix + "COtR skin present on player = " + hasCotrSkin)
	Else
		Debug.Trace(prefix + "COtR path unresolved (missing skin armor or source TXST)")
	EndIf

	If hasCotrSkin
		PO3_SKSEFunctions.ReplaceArmorTextureSet(playerRef, cotrSkinArmor, cotrSourceBodyTextureSet, textureSetForm)
		Debug.Trace(prefix + "Applied COtR armor TextureSet swap to " + textureSetEditorID)
		applied = True
	Else
		Int bodySlotMask = 4
		PO3_SKSEFunctions.ReplaceSkinTextureSet(playerRef, None, textureSetForm, bodySlotMask)
		Debug.Trace(prefix + "Applied fallback skin TextureSet swap to " + textureSetEditorID + " on slot mask " + bodySlotMask)
		applied = True
	EndIf

	playerRef.QueueNiNodeUpdate()
	Debug.Trace(prefix + "Queued NiNode update for " + textureSetEditorID + " (applied=" + applied + ")")

EndFunction
