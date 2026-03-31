Scriptname RleyRuntimeMorphController extends Quest

String Property ModKey = "RleyRuntimeMorphTest" Auto Hidden

String Property MorphThin = "RleyThin" Auto
String Property MorphMuscular = "RleyMuscular" Auto
String Property MorphFat = "RleyFat" Auto

Float Property CurrentThin = 0.0 Auto
Float Property CurrentMuscular = 0.0 Auto
Float Property CurrentFat = 0.0 Auto

Float Property DebugThin = 0.0 Auto
Float Property DebugMuscular = 0.0 Auto
Float Property DebugFat = 0.0 Auto
Int Property DebugApply = 0 Auto
Int Property DebugReset = 0 Auto

Event OnInit()
    ; Intentionally inert.
    ; Real-time morph preview now happens in the SKSE DLL through RaceMenu's native body morph interface.
EndEvent

Event OnPlayerLoadGame()
    ; Intentionally inert.
EndEvent

Event OnSetMorphs(String eventName, String strArg, Float numArg, Form sender)
    ; Intentionally inert.
EndEvent

Event OnUpdate()
    ; Intentionally inert.
EndEvent

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
    ; Intentionally inert.
EndFunction

Float Function ClampMorph(Float aValue)
    If aValue < 0.0
        Return 0.0
    ElseIf aValue > 1.0
        Return 1.0
    EndIf

    Return aValue
EndFunction
