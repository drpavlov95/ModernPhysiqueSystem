Scriptname RleyTextureBridge Hidden

Function ApplySelectedNormalPaths(String bodyNormalTexturePath, String handNormalTexturePath, Bool persist, String applySource = "unspecified") global
	String prefix = "[ModernPhysiqueSystem] [RleyTextureBridge] "
	Debug.Trace(prefix + "ApplySelectedNormalPaths called for body=" + bodyNormalTexturePath + " hand=" + handNormalTexturePath + " persist=" + persist + " source=" + applySource)

	Actor playerRef = Game.GetPlayer()
	If playerRef == None
		Debug.Trace(prefix + "Abort: player is None")
		Return
	EndIf

	If bodyNormalTexturePath == "" && handNormalTexturePath == ""
		Debug.Trace(prefix + "Abort: body and hand normal texture paths are empty")
		Return
	EndIf

	Int bodySlotMask = 4
	Int handSlotMask = 8
	Int textureSetOverrideKey = 6
	Int stringOverrideKey = 9
	Int textureSetIndex = -1
	Int normalTextureIndex = 1
	String bodyNodeName = ""
	String bodyNamedNodeName = "CBBE"
	String handNodeName = ""
	String legacyBodyNodeName = "Body"
	String legacyHandNodeName = "Hands"
	String resolvedHandNormalTexturePath = handNormalTexturePath
	String handMode = "skipped"

	Armor cotrSkinArmor = None
	ArmorAddon cotrBodyAddon = None
	ArmorAddon cotrHandAddon = None
	Bool hasCotrSkin = False
	Bool bodyAddonNode3p = False
	Bool bodyAddonNode1p = False
	Bool bodyAddonNamedNode3p = False
	Bool bodyAddonNamedNode1p = False
	Bool bodyAddonOverrideBefore = False
	Bool bodyAddonNamedOverrideBefore = False
	Bool bodyAddonOverride = False
	Bool bodyAddonNamedOverride = False
	Bool handAddonNode3p = False
	Bool handAddonNode1p = False
	Bool handAddonOverrideBefore = False
	Bool handAddonOverride = False
	Bool bodyThirdPersonOverrideBefore = False
	Bool bodyFirstPersonOverrideBefore = False
	Bool usingCotrBodyAddonPath = False

	If bodyNormalTexturePath != "" || handNormalTexturePath != ""
		cotrSkinArmor = PO3_SKSEFunctions.GetFormFromEditorID("00DZB_SkinNaked") as Armor
		If cotrSkinArmor == None
			cotrSkinArmor = PO3_SKSEFunctions.GetFormFromEditorID("DZB_SkinNaked") as Armor
		EndIf

		If cotrSkinArmor != None
			hasCotrSkin = PO3_SKSEFunctions.HasSkin(playerRef, cotrSkinArmor)
		EndIf
	EndIf

	If bodyNormalTexturePath != ""
		cotrBodyAddon = PO3_SKSEFunctions.GetFormFromEditorID("00DZB_NakedTorso") as ArmorAddon
		If cotrBodyAddon == None
			cotrBodyAddon = PO3_SKSEFunctions.GetFormFromEditorID("DZB_NakedTorso") as ArmorAddon
		EndIf

		If cotrSkinArmor != None && cotrBodyAddon != None && hasCotrSkin
			bodyAddonNode3p = NiOverride.HasArmorAddonNode(playerRef, False, cotrSkinArmor, cotrBodyAddon, bodyNodeName, True)
			bodyAddonNode1p = NiOverride.HasArmorAddonNode(playerRef, True, cotrSkinArmor, cotrBodyAddon, bodyNodeName, True)
			bodyAddonNamedNode3p = NiOverride.HasArmorAddonNode(playerRef, False, cotrSkinArmor, cotrBodyAddon, bodyNamedNodeName, True)
			bodyAddonNamedNode1p = NiOverride.HasArmorAddonNode(playerRef, True, cotrSkinArmor, cotrBodyAddon, bodyNamedNodeName, True)
			bodyAddonOverrideBefore = NiOverride.HasOverride(playerRef, True, cotrSkinArmor, cotrBodyAddon, bodyNodeName, stringOverrideKey, normalTextureIndex)
			bodyAddonNamedOverrideBefore = NiOverride.HasOverride(playerRef, True, cotrSkinArmor, cotrBodyAddon, bodyNamedNodeName, stringOverrideKey, normalTextureIndex)
			usingCotrBodyAddonPath = True
		Else
			Debug.Trace(prefix + "Body path falling back to skin override because COtR body addon path is unavailable (skin=" + cotrSkinArmor + ", addon=" + cotrBodyAddon + ", hasSkin=" + hasCotrSkin + ", source=" + applySource + ")")
		EndIf
	EndIf

	If handNormalTexturePath != ""
		cotrHandAddon = PO3_SKSEFunctions.GetFormFromEditorID("00DZB_NakedHands") as ArmorAddon
		If cotrHandAddon == None
			cotrHandAddon = PO3_SKSEFunctions.GetFormFromEditorID("DZB_NakedHands") as ArmorAddon
		EndIf

		If cotrSkinArmor == None || cotrHandAddon == None
			Debug.Trace(prefix + "Hand path unresolved: missing COtR skin armor or hand addon (source=" + applySource + ")")
		ElseIf hasCotrSkin == False
			Debug.Trace(prefix + "Hand path skipped: COtR skin armor is not present on the player (source=" + applySource + ")")
		Else
			; Armor-addon overrides match by geometry name; for single-geometry addons the key is the empty string.
			handAddonNode3p = NiOverride.HasArmorAddonNode(playerRef, False, cotrSkinArmor, cotrHandAddon, handNodeName, True)
			handAddonNode1p = NiOverride.HasArmorAddonNode(playerRef, True, cotrSkinArmor, cotrHandAddon, handNodeName, True)
			handAddonOverrideBefore = NiOverride.HasOverride(playerRef, True, cotrSkinArmor, cotrHandAddon, handNodeName, stringOverrideKey, normalTextureIndex)
		EndIf
	EndIf

	If bodyNormalTexturePath != ""
		bodyThirdPersonOverrideBefore = NiOverride.HasSkinOverride(playerRef, True, False, bodySlotMask, stringOverrideKey, normalTextureIndex)
		bodyFirstPersonOverrideBefore = NiOverride.HasSkinOverride(playerRef, True, True, bodySlotMask, stringOverrideKey, normalTextureIndex)

		; Clear old slot-based body state before selecting the exact runtime path.
		NiOverride.RemoveSkinOverride(playerRef, True, False, bodySlotMask, textureSetOverrideKey, textureSetIndex)
		NiOverride.RemoveSkinOverride(playerRef, True, True, bodySlotMask, textureSetOverrideKey, textureSetIndex)
		NiOverride.RemoveSkinOverride(playerRef, True, False, bodySlotMask, stringOverrideKey, normalTextureIndex)
		NiOverride.RemoveSkinOverride(playerRef, True, True, bodySlotMask, stringOverrideKey, normalTextureIndex)

		If usingCotrBodyAddonPath
			NiOverride.RemoveNodeOverride(playerRef, True, legacyBodyNodeName, textureSetOverrideKey, textureSetIndex)
			NiOverride.RemoveNodeOverride(playerRef, True, legacyBodyNodeName, stringOverrideKey, normalTextureIndex)
			NiOverride.RemoveNodeOverride(playerRef, True, bodyNodeName, textureSetOverrideKey, textureSetIndex)
			NiOverride.RemoveNodeOverride(playerRef, True, bodyNodeName, stringOverrideKey, normalTextureIndex)
			NiOverride.RemoveNodeOverride(playerRef, True, bodyNamedNodeName, textureSetOverrideKey, textureSetIndex)
			NiOverride.RemoveNodeOverride(playerRef, True, bodyNamedNodeName, stringOverrideKey, normalTextureIndex)
			If persist
				NiOverride.RemoveOverride(playerRef, True, cotrSkinArmor, cotrBodyAddon, legacyBodyNodeName, stringOverrideKey, normalTextureIndex)
				NiOverride.RemoveOverride(playerRef, True, cotrSkinArmor, cotrBodyAddon, bodyNodeName, stringOverrideKey, normalTextureIndex)
				NiOverride.RemoveOverride(playerRef, True, cotrSkinArmor, cotrBodyAddon, bodyNamedNodeName, stringOverrideKey, normalTextureIndex)
			EndIf
			NiOverride.AddOverrideString(playerRef, True, cotrSkinArmor, cotrBodyAddon, bodyNodeName, stringOverrideKey, normalTextureIndex, bodyNormalTexturePath, persist)
			NiOverride.AddOverrideString(playerRef, True, cotrSkinArmor, cotrBodyAddon, bodyNamedNodeName, stringOverrideKey, normalTextureIndex, bodyNormalTexturePath, persist)
			bodyAddonOverride = NiOverride.HasOverride(playerRef, True, cotrSkinArmor, cotrBodyAddon, bodyNodeName, stringOverrideKey, normalTextureIndex)
			bodyAddonNamedOverride = NiOverride.HasOverride(playerRef, True, cotrSkinArmor, cotrBodyAddon, bodyNamedNodeName, stringOverrideKey, normalTextureIndex)
		Else
			NiOverride.AddSkinOverrideString(playerRef, True, False, bodySlotMask, stringOverrideKey, normalTextureIndex, bodyNormalTexturePath, persist)
			NiOverride.AddSkinOverrideString(playerRef, True, True, bodySlotMask, stringOverrideKey, normalTextureIndex, bodyNormalTexturePath, persist)
		EndIf
	EndIf

	If handNormalTexturePath != "" && cotrSkinArmor != None && cotrHandAddon != None && hasCotrSkin
		resolvedHandNormalTexturePath = handNormalTexturePath
		handMode = "custom_selected_path"

		; Migrate away from the legacy hand experiments before applying the exact COtR hand addon target.
		NiOverride.RemoveSkinOverride(playerRef, True, False, handSlotMask, textureSetOverrideKey, textureSetIndex)
		NiOverride.RemoveSkinOverride(playerRef, True, True, handSlotMask, textureSetOverrideKey, textureSetIndex)
		NiOverride.RemoveSkinOverride(playerRef, True, False, handSlotMask, stringOverrideKey, normalTextureIndex)
		NiOverride.RemoveSkinOverride(playerRef, True, True, handSlotMask, stringOverrideKey, normalTextureIndex)
		NiOverride.RemoveNodeOverride(playerRef, True, legacyHandNodeName, textureSetOverrideKey, textureSetIndex)
		NiOverride.RemoveNodeOverride(playerRef, True, legacyHandNodeName, stringOverrideKey, normalTextureIndex)
		NiOverride.RemoveNodeOverride(playerRef, True, handNodeName, textureSetOverrideKey, textureSetIndex)
		NiOverride.RemoveNodeOverride(playerRef, True, handNodeName, stringOverrideKey, normalTextureIndex)
		If persist
			NiOverride.RemoveOverride(playerRef, True, cotrSkinArmor, cotrHandAddon, legacyHandNodeName, stringOverrideKey, normalTextureIndex)
			NiOverride.RemoveOverride(playerRef, True, cotrSkinArmor, cotrHandAddon, handNodeName, stringOverrideKey, normalTextureIndex)
		EndIf

		NiOverride.AddOverrideString(playerRef, True, cotrSkinArmor, cotrHandAddon, handNodeName, stringOverrideKey, normalTextureIndex, resolvedHandNormalTexturePath, persist)
		handAddonOverride = NiOverride.HasOverride(playerRef, True, cotrSkinArmor, cotrHandAddon, handNodeName, stringOverrideKey, normalTextureIndex)
	EndIf

	If bodyNormalTexturePath != ""
		If usingCotrBodyAddonPath
			NiOverride.ApplyOverrides(playerRef)
		Else
			NiOverride.ApplySkinOverrides(playerRef)
		EndIf
	EndIf

	If handNormalTexturePath != "" && cotrSkinArmor != None && cotrHandAddon != None && hasCotrSkin
		NiOverride.ApplyOverrides(playerRef)
	EndIf

	playerRef.QueueNiNodeUpdate()

	Bool bodyThirdPersonOverride = False
	Bool bodyFirstPersonOverride = False
	If bodyNormalTexturePath != ""
		If usingCotrBodyAddonPath
			bodyThirdPersonOverride = bodyAddonOverride || bodyAddonNamedOverride
			bodyFirstPersonOverride = bodyAddonOverride || bodyAddonNamedOverride
		Else
			bodyThirdPersonOverride = NiOverride.HasSkinOverride(playerRef, True, False, bodySlotMask, stringOverrideKey, normalTextureIndex)
			bodyFirstPersonOverride = NiOverride.HasSkinOverride(playerRef, True, True, bodySlotMask, stringOverrideKey, normalTextureIndex)
		EndIf
	EndIf

	String bodyMode = "skin_slot_fallback"
	If usingCotrBodyAddonPath
		bodyMode = "cotr_body_addon"
	EndIf

	Debug.Trace(prefix + "Applied selected normal paths body=" + bodyNormalTexturePath + " handRequest=" + handNormalTexturePath + " handApplied=" + resolvedHandNormalTexturePath + " persist=" + persist + " source=" + applySource + " (bodyMode=" + bodyMode + ", handMode=" + handMode + ", bodyNode3p=" + bodyAddonNode3p + ", bodyNode1p=" + bodyAddonNode1p + ", bodyNamedNode3p=" + bodyAddonNamedNode3p + ", bodyNamedNode1p=" + bodyAddonNamedNode1p + ", bodyAddonOverrideBefore=" + bodyAddonOverrideBefore + ", bodyNamedOverrideBefore=" + bodyAddonNamedOverrideBefore + ", body3pBefore=" + bodyThirdPersonOverrideBefore + ", body1pBefore=" + bodyFirstPersonOverrideBefore + ", body3p=" + bodyThirdPersonOverride + ", body1p=" + bodyFirstPersonOverride + ", cotrSkin=" + hasCotrSkin + ", handNode3p=" + handAddonNode3p + ", handNode1p=" + handAddonNode1p + ", handOverrideBefore=" + handAddonOverrideBefore + ", handOverride=" + handAddonOverride + ")")
EndFunction

Function ApplyBodyNormalPath(String normalTexturePath) global
	ApplySelectedNormalPaths(normalTexturePath, "", True, "legacy_body_only")
EndFunction

Function ApplyHandNormalPath(String normalTexturePath) global
	ApplySelectedNormalPaths("", normalTexturePath, True, "legacy_hand_only")
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
