Scriptname RleyRuntimeMorphController extends Quest

String Property ModKey = "RleyRuntimeMorphTest" Auto Hidden

String Property MorphThin = "RM_Thin" Auto
String Property MorphMuscular = "RM_Muscular" Auto
String Property MorphFat = "RM_Heavy" Auto

Float Property CurrentThin = 0.0 Auto
Float Property CurrentMuscular = 0.0 Auto
Float Property CurrentFat = 0.0 Auto

Float Property DebugThin = 0.0 Auto
Float Property DebugMuscular = 0.0 Auto
Float Property DebugFat = 0.0 Auto
Int Property DebugApply = 0 Auto
Int Property DebugReset = 0 Auto

TextureSet Property CurrentBodyTextureSet Auto Hidden
String Property DebugBodyTextureSetName = "" Auto Hidden

String Property BodyTextureEventName = "Rley_SetBodyTextureSet" Auto Hidden
Int Property BodySlotMask = 4 Auto Hidden

Event OnInit()
	RegisterEvents()
EndEvent

Event OnPlayerLoadGame()
	RegisterEvents()
	ApplyBodyTextureSet()
EndEvent

Function RegisterEvents()
	UnregisterForAllModEvents()
	RegisterForModEvent(BodyTextureEventName, "OnSetBodyTextureSet")
EndFunction

Event OnSetBodyTextureSet(String eventName, String strArg, Float numArg, Form sender)
	TextureSet textureSetForm = sender as TextureSet
	If textureSetForm == None
		Return
	EndIf

	CurrentBodyTextureSet = textureSetForm
	DebugBodyTextureSetName = textureSetForm.GetName()
	ApplyBodyTextureSet()
EndEvent

Function ApplyBodyTextureSet()
	Actor playerRef = Game.GetPlayer()
	If playerRef == None || CurrentBodyTextureSet == None
		Return
	EndIf

	PO3_SKSEFunctions.ReplaceSkinTextureSet(playerRef, None, CurrentBodyTextureSet, BodySlotMask)
	playerRef.QueueNiNodeUpdate()
EndFunction

Function ResetMorphs()
	CurrentThin = 0.0
	CurrentMuscular = 0.0
	CurrentFat = 0.0

	DebugThin = 0.0
	DebugMuscular = 0.0
	DebugFat = 0.0
	DebugApply = 0
	DebugReset = 0
EndFunction

Function ApplyMorphs()
	; Real-time morph preview happens in the SKSE DLL.
EndFunction

Float Function ClampMorph(Float aValue)
	If aValue < 0.0
		Return 0.0
	ElseIf aValue > 1.0
		Return 1.0
	EndIf

	Return aValue
EndFunction
