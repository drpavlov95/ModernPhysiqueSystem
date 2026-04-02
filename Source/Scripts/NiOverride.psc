Scriptname NiOverride Hidden

bool Function HasOverride(ObjectReference ref, bool isFemale, Armor arm, ArmorAddon addon, String node, int key, int index) native global
bool Function HasSkinOverride(ObjectReference ref, bool isFemale, bool firstPerson, int slotMask, int key, int index) native global
bool Function HasNodeOverride(ObjectReference ref, bool isFemale, String node, int key, int index) native global
bool Function HasArmorAddonNode(ObjectReference ref, bool firstPerson, Armor arm, ArmorAddon addon, String node, bool debug = false) native global
Function AddOverrideString(ObjectReference ref, bool isFemale, Armor arm, ArmorAddon addon, String node, int key, int index, String value, bool persist) native global
Function AddSkinOverrideTextureSet(ObjectReference ref, bool isFemale, bool firstPerson, int slotMask, int key, int index, TextureSet value, bool persist) native global
Function AddSkinOverrideString(ObjectReference ref, bool isFemale, bool firstPerson, int slotMask, int key, int index, String value, bool persist) native global
Function AddNodeOverrideString(ObjectReference ref, bool isFemale, String node, int key, int index, String value, bool persist) native global
String Function GetSkinOverrideString(ObjectReference ref, bool isFemale, bool firstPerson, int slotMask, int key, int index) native global
TextureSet Function GetSkinOverrideTextureSet(ObjectReference ref, bool isFemale, bool firstPerson, int slotMask, int key, int index) native global
String Function GetSkinPropertyString(ObjectReference ref, bool firstPerson, int slotMask, int key, int index) native global
Function ApplyOverrides(ObjectReference ref) native global
Function ApplySkinOverrides(ObjectReference ref) native global
Function ApplyNodeOverrides(ObjectReference ref) native global
Function RemoveAllSkinOverrides(ObjectReference ref, bool isFemale, bool firstPerson, int slotMask) native global
Function RemoveOverride(ObjectReference ref, bool isFemale, Armor arm, ArmorAddon addon, String node, int key, int index) native global
Function RemoveSkinOverride(ObjectReference ref, bool isFemale, bool firstPerson, int slotMask, int key, int index) native global
Function RemoveNodeOverride(ObjectReference ref, bool isFemale, String node, int key, int index) native global
