Scriptname NiOverride Hidden

bool Function HasSkinOverride(ObjectReference ref, bool isFemale, bool firstPerson, int slotMask, int key, int index) native global
Function AddSkinOverrideTextureSet(ObjectReference ref, bool isFemale, bool firstPerson, int slotMask, int key, int index, TextureSet value, bool persist) native global
Function AddSkinOverrideString(ObjectReference ref, bool isFemale, bool firstPerson, int slotMask, int key, int index, String value, bool persist) native global
String Function GetSkinOverrideString(ObjectReference ref, bool isFemale, bool firstPerson, int slotMask, int key, int index) native global
TextureSet Function GetSkinOverrideTextureSet(ObjectReference ref, bool isFemale, bool firstPerson, int slotMask, int key, int index) native global
String Function GetSkinPropertyString(ObjectReference ref, bool firstPerson, int slotMask, int key, int index) native global
Function ApplySkinOverrides(ObjectReference ref) native global
Function RemoveAllSkinOverrides(ObjectReference ref, bool isFemale, bool firstPerson, int slotMask) native global
Function RemoveSkinOverride(ObjectReference ref, bool isFemale, bool firstPerson, int slotMask, int key, int index) native global
