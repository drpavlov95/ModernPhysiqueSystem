Scriptname RleyTextureBridge Hidden

Function ReportMuLegacyCleanupResult(Int cleanupGeneration, Bool succeeded) Native Global

Bool Function IsVerboseLoggingEnabled() global
	Return False
EndFunction

Function TraceVerbose(String logText) global
	If IsVerboseLoggingEnabled()
		Debug.Trace(logText)
	EndIf
EndFunction

Function ClearMpsLegacyNormalOverrides(Actor playerRef, Bool isFemale) global
	Int bodySlotMask = 4
	Int handSlotMask = 8
	Int textureSetOverrideKey = 6
	Int stringOverrideKey = 9
	Int textureSetIndex = -1
	Int normalTextureIndex = 1

	NiOverride.RemoveSkinOverride(playerRef, isFemale, False, bodySlotMask, textureSetOverrideKey, textureSetIndex)
	NiOverride.RemoveSkinOverride(playerRef, isFemale, True, bodySlotMask, textureSetOverrideKey, textureSetIndex)
	NiOverride.RemoveSkinOverride(playerRef, isFemale, False, bodySlotMask, stringOverrideKey, normalTextureIndex)
	NiOverride.RemoveSkinOverride(playerRef, isFemale, True, bodySlotMask, stringOverrideKey, normalTextureIndex)
	NiOverride.RemoveSkinOverride(playerRef, isFemale, False, handSlotMask, textureSetOverrideKey, textureSetIndex)
	NiOverride.RemoveSkinOverride(playerRef, isFemale, True, handSlotMask, textureSetOverrideKey, textureSetIndex)
	NiOverride.RemoveSkinOverride(playerRef, isFemale, False, handSlotMask, stringOverrideKey, normalTextureIndex)
	NiOverride.RemoveSkinOverride(playerRef, isFemale, True, handSlotMask, stringOverrideKey, normalTextureIndex)

	NiOverride.RemoveNodeOverride(playerRef, isFemale, "Body", textureSetOverrideKey, textureSetIndex)
	NiOverride.RemoveNodeOverride(playerRef, isFemale, "Body", stringOverrideKey, normalTextureIndex)
	NiOverride.RemoveNodeOverride(playerRef, isFemale, "CBBE", textureSetOverrideKey, textureSetIndex)
	NiOverride.RemoveNodeOverride(playerRef, isFemale, "CBBE", stringOverrideKey, normalTextureIndex)
	NiOverride.RemoveNodeOverride(playerRef, isFemale, "Hands", textureSetOverrideKey, textureSetIndex)
	NiOverride.RemoveNodeOverride(playerRef, isFemale, "Hands", stringOverrideKey, normalTextureIndex)

	Armor cotrSkinArmor = PO3_SKSEFunctions.GetFormFromEditorID("00DZB_SkinNaked") as Armor
	If cotrSkinArmor == None
		cotrSkinArmor = PO3_SKSEFunctions.GetFormFromEditorID("DZB_SkinNaked") as Armor
	EndIf
	ArmorAddon cotrBodyAddon = PO3_SKSEFunctions.GetFormFromEditorID("00DZB_NakedTorso") as ArmorAddon
	If cotrBodyAddon == None
		cotrBodyAddon = PO3_SKSEFunctions.GetFormFromEditorID("DZB_NakedTorso") as ArmorAddon
	EndIf
	ArmorAddon cotrHandAddon = PO3_SKSEFunctions.GetFormFromEditorID("00DZB_NakedHands") as ArmorAddon
	If cotrHandAddon == None
		cotrHandAddon = PO3_SKSEFunctions.GetFormFromEditorID("DZB_NakedHands") as ArmorAddon
	EndIf

	If cotrSkinArmor != None && cotrBodyAddon != None
		NiOverride.RemoveOverride(playerRef, isFemale, cotrSkinArmor, cotrBodyAddon, "", stringOverrideKey, normalTextureIndex)
		NiOverride.RemoveOverride(playerRef, isFemale, cotrSkinArmor, cotrBodyAddon, "Body", stringOverrideKey, normalTextureIndex)
		NiOverride.RemoveOverride(playerRef, isFemale, cotrSkinArmor, cotrBodyAddon, "CBBE", stringOverrideKey, normalTextureIndex)
	EndIf
	If cotrSkinArmor != None && cotrHandAddon != None
		NiOverride.RemoveOverride(playerRef, isFemale, cotrSkinArmor, cotrHandAddon, "", stringOverrideKey, normalTextureIndex)
		NiOverride.RemoveOverride(playerRef, isFemale, cotrSkinArmor, cotrHandAddon, "Hands", stringOverrideKey, normalTextureIndex)
	EndIf

EndFunction

Function ApplyMpsLegacyNormalCleanup(Actor playerRef) global
	NiOverride.ApplySkinOverrides(playerRef)
	NiOverride.ApplyNodeOverrides(playerRef)
	NiOverride.ApplyOverrides(playerRef)
	playerRef.QueueNiNodeUpdate()
EndFunction

Function PrepareMuBackend(Int cleanupGeneration, String applySource = "unspecified") global
	String prefix = "[ModernPhysiqueSystem] [RleyTextureBridge] "
	Actor playerRef = Game.GetPlayer()
	If playerRef == None
		ReportMuLegacyCleanupResult(cleanupGeneration, False)
		TraceVerbose(prefix + "Mu cleanup aborted: player is None (source=" + applySource + ")")
		Return
	EndIf

	If MuDynamicNormalMap.GetVersion() <= 0
		ReportMuLegacyCleanupResult(cleanupGeneration, False)
		TraceVerbose(prefix + "Mu cleanup aborted: native API is unavailable (source=" + applySource + ")")
		Return
	EndIf

	ClearMpsLegacyNormalOverrides(playerRef, False)
	ClearMpsLegacyNormalOverrides(playerRef, True)
	ApplyMpsLegacyNormalCleanup(playerRef)
	ReportMuLegacyCleanupResult(cleanupGeneration, True)
	TraceVerbose(prefix + "Queued one consolidated NiNode rebuild after clearing both legacy sex keys (source=" + applySource + ")")
EndFunction

Function QueueMuNormalUpdate(Int slotBits = 0x0000018D, Bool clearLegacyOverrides = False, String applySource = "unspecified") global
	String prefix = "[ModernPhysiqueSystem] [RleyTextureBridge] "
	Actor playerRef = Game.GetPlayer()
	If playerRef == None
		TraceVerbose(prefix + "Mu update aborted: player is None (source=" + applySource + ")")
		Return
	EndIf

	If slotBits <= 0
		TraceVerbose(prefix + "Mu update aborted: slot bits are empty (source=" + applySource + ")")
		Return
	EndIf

	If clearLegacyOverrides
		ActorBase playerBase = playerRef.GetLeveledActorBase()
		If playerBase == None
			TraceVerbose(prefix + "Mu update aborted: player base is None (source=" + applySource + ")")
			Return
		EndIf
		ClearMpsLegacyNormalOverrides(playerRef, False)
		ClearMpsLegacyNormalOverrides(playerRef, True)
		ApplyMpsLegacyNormalCleanup(playerRef)
		TraceVerbose(prefix + "Legacy compatibility cleanup queued a NiNode rebuild; Mu update deferred (source=" + applySource + ")")
		Return
	EndIf

	Int muVersion = MuDynamicNormalMap.GetVersion()
	If muVersion <= 0
		TraceVerbose(prefix + "Mu update aborted: native API is unavailable (source=" + applySource + ")")
		Return
	EndIf

	MuDynamicNormalMap.QUpdateNormalmap(playerRef, slotBits)
	TraceVerbose(prefix + "Queued Mu normal-map update slots=" + slotBits + " version=" + muVersion + " source=" + applySource)
EndFunction

Function ApplySelectedNormalPaths(String bodyNormalTexturePath, String handNormalTexturePath, Bool persist, String applySource = "unspecified") global
	String prefix = "[ModernPhysiqueSystem] [RleyTextureBridge] "
	Actor playerRef = Game.GetPlayer()
	If playerRef == None
		TraceVerbose(prefix + "Legacy ApplySelectedNormalPaths aborted: player is None")
		Return
	EndIf

	ActorBase playerBase = playerRef.GetLeveledActorBase()
	If playerBase == None
		TraceVerbose(prefix + "Legacy ApplySelectedNormalPaths aborted: player base is None")
		Return
	EndIf

	ApplySelectedNormalPathsForSex(bodyNormalTexturePath, handNormalTexturePath, playerBase.GetSex() == 1, persist, applySource)
EndFunction

Function ApplySelectedNormalPathsForSex(String bodyNormalTexturePath, String handNormalTexturePath, Bool isFemale, Bool persist, String applySource = "unspecified") global
	String prefix = "[ModernPhysiqueSystem] [RleyTextureBridge] "
	TraceVerbose(prefix + "ApplySelectedNormalPathsForSex called for body=" + bodyNormalTexturePath + " hand=" + handNormalTexturePath + " isFemale=" + isFemale + " persist=" + persist + " source=" + applySource)

	Actor playerRef = Game.GetPlayer()
	If playerRef == None
		TraceVerbose(prefix + "Abort: player is None")
		Return
	EndIf

	If bodyNormalTexturePath == "" && handNormalTexturePath == ""
		TraceVerbose(prefix + "Abort: body and hand normal texture paths are empty")
		Return
	EndIf

	Int bodySlotMask = 4
	Int handSlotMask = 8
	Int textureSetOverrideKey = 6
	Int stringOverrideKey = 9
	Int textureSetIndex = -1
	Int normalTextureIndex = 1
	; Keep the legacy body override scoped to the explicit body geometry node.
	; An empty node name is a wildcard in NiOverride and can bleed into hands.
	String bodyNodeName = "CBBE"
	String bodyNamedNodeName = "CBBE"
	; Likewise, never apply body/detail paths through the empty hand node.
	String handNodeName = "Hands"
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
			bodyAddonOverrideBefore = NiOverride.HasOverride(playerRef, isFemale, cotrSkinArmor, cotrBodyAddon, bodyNodeName, stringOverrideKey, normalTextureIndex)
			bodyAddonNamedOverrideBefore = NiOverride.HasOverride(playerRef, isFemale, cotrSkinArmor, cotrBodyAddon, bodyNamedNodeName, stringOverrideKey, normalTextureIndex)
			usingCotrBodyAddonPath = True
		Else
			TraceVerbose(prefix + "Body path falling back to skin override because COtR body addon path is unavailable (skin=" + cotrSkinArmor + ", addon=" + cotrBodyAddon + ", hasSkin=" + hasCotrSkin + ", source=" + applySource + ")")
		EndIf
	EndIf

	If handNormalTexturePath != ""
		cotrHandAddon = PO3_SKSEFunctions.GetFormFromEditorID("00DZB_NakedHands") as ArmorAddon
		If cotrHandAddon == None
			cotrHandAddon = PO3_SKSEFunctions.GetFormFromEditorID("DZB_NakedHands") as ArmorAddon
		EndIf

		If cotrSkinArmor == None || cotrHandAddon == None
			TraceVerbose(prefix + "Hand path unresolved: missing COtR skin armor or hand addon (source=" + applySource + ")")
		ElseIf hasCotrSkin == False
			TraceVerbose(prefix + "Hand path skipped: COtR skin armor is not present on the player (source=" + applySource + ")")
		Else
			; Armor-addon overrides match by geometry name; for single-geometry addons the key is the empty string.
			handAddonNode3p = NiOverride.HasArmorAddonNode(playerRef, False, cotrSkinArmor, cotrHandAddon, handNodeName, True)
			handAddonNode1p = NiOverride.HasArmorAddonNode(playerRef, True, cotrSkinArmor, cotrHandAddon, handNodeName, True)
			handAddonOverrideBefore = NiOverride.HasOverride(playerRef, isFemale, cotrSkinArmor, cotrHandAddon, handNodeName, stringOverrideKey, normalTextureIndex)
		EndIf
	EndIf

	If bodyNormalTexturePath != ""
		bodyThirdPersonOverrideBefore = NiOverride.HasSkinOverride(playerRef, isFemale, False, bodySlotMask, stringOverrideKey, normalTextureIndex)
		bodyFirstPersonOverrideBefore = NiOverride.HasSkinOverride(playerRef, isFemale, True, bodySlotMask, stringOverrideKey, normalTextureIndex)

		; Clear old slot-based body state before selecting the exact runtime path.
		NiOverride.RemoveSkinOverride(playerRef, isFemale, False, bodySlotMask, textureSetOverrideKey, textureSetIndex)
		NiOverride.RemoveSkinOverride(playerRef, isFemale, True, bodySlotMask, textureSetOverrideKey, textureSetIndex)
		NiOverride.RemoveSkinOverride(playerRef, isFemale, False, bodySlotMask, stringOverrideKey, normalTextureIndex)
		NiOverride.RemoveSkinOverride(playerRef, isFemale, True, bodySlotMask, stringOverrideKey, normalTextureIndex)

		If usingCotrBodyAddonPath
			NiOverride.RemoveNodeOverride(playerRef, isFemale, legacyBodyNodeName, textureSetOverrideKey, textureSetIndex)
			NiOverride.RemoveNodeOverride(playerRef, isFemale, legacyBodyNodeName, stringOverrideKey, normalTextureIndex)
			NiOverride.RemoveNodeOverride(playerRef, isFemale, bodyNodeName, textureSetOverrideKey, textureSetIndex)
			NiOverride.RemoveNodeOverride(playerRef, isFemale, bodyNodeName, stringOverrideKey, normalTextureIndex)
			NiOverride.RemoveNodeOverride(playerRef, isFemale, bodyNamedNodeName, textureSetOverrideKey, textureSetIndex)
			NiOverride.RemoveNodeOverride(playerRef, isFemale, bodyNamedNodeName, stringOverrideKey, normalTextureIndex)
			If persist
				NiOverride.RemoveOverride(playerRef, isFemale, cotrSkinArmor, cotrBodyAddon, legacyBodyNodeName, stringOverrideKey, normalTextureIndex)
				NiOverride.RemoveOverride(playerRef, isFemale, cotrSkinArmor, cotrBodyAddon, bodyNodeName, stringOverrideKey, normalTextureIndex)
				NiOverride.RemoveOverride(playerRef, isFemale, cotrSkinArmor, cotrBodyAddon, bodyNamedNodeName, stringOverrideKey, normalTextureIndex)
			EndIf
			NiOverride.AddOverrideString(playerRef, isFemale, cotrSkinArmor, cotrBodyAddon, bodyNodeName, stringOverrideKey, normalTextureIndex, bodyNormalTexturePath, persist)
			NiOverride.AddOverrideString(playerRef, isFemale, cotrSkinArmor, cotrBodyAddon, bodyNamedNodeName, stringOverrideKey, normalTextureIndex, bodyNormalTexturePath, persist)
			bodyAddonOverride = NiOverride.HasOverride(playerRef, isFemale, cotrSkinArmor, cotrBodyAddon, bodyNodeName, stringOverrideKey, normalTextureIndex)
			bodyAddonNamedOverride = NiOverride.HasOverride(playerRef, isFemale, cotrSkinArmor, cotrBodyAddon, bodyNamedNodeName, stringOverrideKey, normalTextureIndex)
		Else
			NiOverride.AddSkinOverrideString(playerRef, isFemale, False, bodySlotMask, stringOverrideKey, normalTextureIndex, bodyNormalTexturePath, persist)
			NiOverride.AddSkinOverrideString(playerRef, isFemale, True, bodySlotMask, stringOverrideKey, normalTextureIndex, bodyNormalTexturePath, persist)
		EndIf
	EndIf

	If handNormalTexturePath != "" && cotrSkinArmor != None && cotrHandAddon != None && hasCotrSkin
		resolvedHandNormalTexturePath = handNormalTexturePath
		handMode = "custom_selected_path"

		; Migrate away from the legacy hand experiments before applying the exact COtR hand addon target.
		NiOverride.RemoveSkinOverride(playerRef, isFemale, False, handSlotMask, textureSetOverrideKey, textureSetIndex)
		NiOverride.RemoveSkinOverride(playerRef, isFemale, True, handSlotMask, textureSetOverrideKey, textureSetIndex)
		NiOverride.RemoveSkinOverride(playerRef, isFemale, False, handSlotMask, stringOverrideKey, normalTextureIndex)
		NiOverride.RemoveSkinOverride(playerRef, isFemale, True, handSlotMask, stringOverrideKey, normalTextureIndex)
		NiOverride.RemoveNodeOverride(playerRef, isFemale, legacyHandNodeName, textureSetOverrideKey, textureSetIndex)
		NiOverride.RemoveNodeOverride(playerRef, isFemale, legacyHandNodeName, stringOverrideKey, normalTextureIndex)
		NiOverride.RemoveNodeOverride(playerRef, isFemale, handNodeName, textureSetOverrideKey, textureSetIndex)
		NiOverride.RemoveNodeOverride(playerRef, isFemale, handNodeName, stringOverrideKey, normalTextureIndex)
		If persist
			NiOverride.RemoveOverride(playerRef, isFemale, cotrSkinArmor, cotrHandAddon, legacyHandNodeName, stringOverrideKey, normalTextureIndex)
			NiOverride.RemoveOverride(playerRef, isFemale, cotrSkinArmor, cotrHandAddon, handNodeName, stringOverrideKey, normalTextureIndex)
		EndIf

		NiOverride.AddOverrideString(playerRef, isFemale, cotrSkinArmor, cotrHandAddon, handNodeName, stringOverrideKey, normalTextureIndex, resolvedHandNormalTexturePath, persist)
		handAddonOverride = NiOverride.HasOverride(playerRef, isFemale, cotrSkinArmor, cotrHandAddon, handNodeName, stringOverrideKey, normalTextureIndex)
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
			bodyThirdPersonOverride = NiOverride.HasSkinOverride(playerRef, isFemale, False, bodySlotMask, stringOverrideKey, normalTextureIndex)
			bodyFirstPersonOverride = NiOverride.HasSkinOverride(playerRef, isFemale, True, bodySlotMask, stringOverrideKey, normalTextureIndex)
		EndIf
	EndIf

	String bodyMode = "skin_slot_fallback"
	If usingCotrBodyAddonPath
		bodyMode = "cotr_body_addon"
	EndIf

	TraceVerbose(prefix + "Applied selected normal paths body=" + bodyNormalTexturePath + " handRequest=" + handNormalTexturePath + " handApplied=" + resolvedHandNormalTexturePath + " persist=" + persist + " source=" + applySource + " (bodyMode=" + bodyMode + ", handMode=" + handMode + ", bodyNode3p=" + bodyAddonNode3p + ", bodyNode1p=" + bodyAddonNode1p + ", bodyNamedNode3p=" + bodyAddonNamedNode3p + ", bodyNamedNode1p=" + bodyAddonNamedNode1p + ", bodyAddonOverrideBefore=" + bodyAddonOverrideBefore + ", bodyNamedOverrideBefore=" + bodyAddonNamedOverrideBefore + ", body3pBefore=" + bodyThirdPersonOverrideBefore + ", body1pBefore=" + bodyFirstPersonOverrideBefore + ", body3p=" + bodyThirdPersonOverride + ", body1p=" + bodyFirstPersonOverride + ", cotrSkin=" + hasCotrSkin + ", handNode3p=" + handAddonNode3p + ", handNode1p=" + handAddonNode1p + ", handOverrideBefore=" + handAddonOverrideBefore + ", handOverride=" + handAddonOverride + ")")
EndFunction

Function ApplyBodyNormalPath(String normalTexturePath) global
	ApplySelectedNormalPaths(normalTexturePath, "", True, "legacy_body_only")
EndFunction

Function ApplyHandNormalPath(String normalTexturePath) global
	ApplySelectedNormalPaths("", normalTexturePath, True, "legacy_hand_only")
EndFunction

Function ApplyBodyTextureSet(String textureSetEditorID) global
	String prefix = "[ModernPhysiqueSystem] [RleyTextureBridge] "
	TraceVerbose(prefix + "ApplyBodyTextureSet called for " + textureSetEditorID)

	Actor playerRef = Game.GetPlayer()
	TextureSet textureSetForm = PO3_SKSEFunctions.GetFormFromEditorID(textureSetEditorID) as TextureSet

	If playerRef == None || textureSetForm == None
		If playerRef == None
			TraceVerbose(prefix + "Abort: player is None")
		EndIf
		If textureSetForm == None
			TraceVerbose(prefix + "Abort: failed to resolve TextureSet " + textureSetEditorID)
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
		TraceVerbose(prefix + "COtR skin present on player = " + hasCotrSkin)
	Else
		TraceVerbose(prefix + "COtR path unresolved (missing skin armor or source TXST)")
	EndIf

	If hasCotrSkin
		PO3_SKSEFunctions.ReplaceArmorTextureSet(playerRef, cotrSkinArmor, cotrSourceBodyTextureSet, textureSetForm)
		TraceVerbose(prefix + "Applied COtR armor TextureSet swap to " + textureSetEditorID)
		applied = True
	Else
		Int bodySlotMask = 4
		PO3_SKSEFunctions.ReplaceSkinTextureSet(playerRef, None, textureSetForm, bodySlotMask)
		TraceVerbose(prefix + "Applied fallback skin TextureSet swap to " + textureSetEditorID + " on slot mask " + bodySlotMask)
		applied = True
	EndIf

	playerRef.QueueNiNodeUpdate()
	TraceVerbose(prefix + "Queued NiNode update for " + textureSetEditorID + " (applied=" + applied + ")")

EndFunction
