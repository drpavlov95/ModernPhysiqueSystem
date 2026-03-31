Scriptname RleyPhysiqueRM extends RaceMenuBase

String Property CATEGORY_KEY = "rley_physique" AutoReadOnly
String Property OPEN_CALLBACK = "RleyPhysiqueOpen" AutoReadOnly

Event OnCategoryRequest()
	AddCategory(CATEGORY_KEY, "$PHYSIQUE", 0)
EndEvent

Event OnSliderRequest(Actor player, ActorBase playerBase, Race actorRace, bool isFemale)
	AddSliderEx("Open Physique", CATEGORY_KEY, OPEN_CALLBACK, 0.0, 1.0, 1.0, 0.0, 0, 0)
EndEvent

Event OnSliderChanged(string callback, float value)
	If callback == OPEN_CALLBACK && value >= 1.0
		Game.GetPlayer().SendModEvent("Rley_OpenPhysique")
		SetSliderParameters(OPEN_CALLBACK, 0.0, 1.0, 1.0, 0.0)
	EndIf
EndEvent
