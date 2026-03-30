#pragma once

#include <cstdint>

namespace RE
{
	class TESObjectREFR;
	class NiAVObject;
}

using skee_u64 = std::uint64_t;
using skee_u32 = std::uint32_t;
using skee_i32 = std::int32_t;
using skee_u16 = std::uint16_t;
using skee_u8 = std::uint8_t;

class IPluginInterface
{
public:
	IPluginInterface() = default;
	virtual ~IPluginInterface() = default;

	virtual skee_u32 GetVersion() = 0;
	virtual void Revert() = 0;
};

class IInterfaceMap
{
public:
	virtual IPluginInterface* QueryInterface(const char* name) = 0;
	virtual bool AddInterface(const char* name, IPluginInterface* pluginInterface) = 0;
	virtual IPluginInterface* RemoveInterface(const char* name) = 0;
};

struct InterfaceExchangeMessage
{
	enum : std::uint32_t
	{
		kMessage_ExchangeInterface = 0x9E3779B9
	};

	IInterfaceMap* interfaceMap = nullptr;
};

class IBodyMorphInterface : public IPluginInterface
{
public:
	enum
	{
		kPluginVersion1 = 1,
		kPluginVersion2,
		kPluginVersion3,
		kPluginVersion4,
		kCurrentPluginVersion = kPluginVersion4
	};

	class MorphKeyVisitor
	{
	public:
		virtual void Visit(const char*, float) = 0;
	};

	class StringVisitor
	{
	public:
		virtual void Visit(const char*) = 0;
	};

	class ActorVisitor
	{
	public:
		virtual void Visit(RE::TESObjectREFR*) = 0;
	};

	class MorphValueVisitor
	{
	public:
		virtual void Visit(RE::TESObjectREFR*, const char*, const char*, float) = 0;
	};

	class MorphVisitor
	{
	public:
		virtual void Visit(RE::TESObjectREFR*, const char*) = 0;
	};

	virtual void SetMorph(RE::TESObjectREFR* actor, const char* morphName, const char* morphKey, float relative) = 0;
	virtual float GetMorph(RE::TESObjectREFR* actor, const char* morphName, const char* morphKey) = 0;
	virtual void ClearMorph(RE::TESObjectREFR* actor, const char* morphName, const char* morphKey) = 0;

	virtual float GetBodyMorphs(RE::TESObjectREFR* actor, const char* morphName) = 0;
	virtual void ClearBodyMorphNames(RE::TESObjectREFR* actor, const char* morphName) = 0;

	virtual void VisitMorphs(RE::TESObjectREFR* actor, MorphVisitor& visitor) = 0;
	virtual void VisitKeys(RE::TESObjectREFR* actor, const char* name, MorphKeyVisitor& visitor) = 0;
	virtual void VisitMorphValues(RE::TESObjectREFR* actor, MorphValueVisitor& visitor) = 0;

	virtual void ClearMorphs(RE::TESObjectREFR* actor) = 0;

	virtual void ApplyVertexDiff(RE::TESObjectREFR* refr, RE::NiAVObject* rootNode, bool erase = false) = 0;
	virtual void ApplyBodyMorphs(RE::TESObjectREFR* refr, bool deferUpdate = true) = 0;
	virtual void UpdateModelWeight(RE::TESObjectREFR* refr, bool immediate = false) = 0;

	virtual void SetCacheLimit(skee_u64 limit) = 0;
	virtual bool HasMorphs(RE::TESObjectREFR* actor) = 0;
	virtual skee_u32 EvaluateBodyMorphs(RE::TESObjectREFR* actor) = 0;

	virtual bool HasBodyMorph(RE::TESObjectREFR* actor, const char* morphName, const char* morphKey) = 0;
	virtual bool HasBodyMorphName(RE::TESObjectREFR* actor, const char* morphName) = 0;
	virtual bool HasBodyMorphKey(RE::TESObjectREFR* actor, const char* morphKey) = 0;
	virtual void ClearBodyMorphKeys(RE::TESObjectREFR* actor, const char* morphKey) = 0;
	virtual void VisitStrings(StringVisitor& visitor) = 0;
	virtual void VisitActors(ActorVisitor& visitor) = 0;
	virtual skee_u64 ClearMorphCache() = 0;
};

inline constexpr const char* kRaceMenuBodyMorphInterfaceName = "BodyMorph";

inline IBodyMorphInterface* GetBodyMorphInterfaceFromMap(IInterfaceMap* interfaceMap)
{
	if (!interfaceMap) {
		return nullptr;
	}

	auto* pluginInterface = interfaceMap->QueryInterface(kRaceMenuBodyMorphInterfaceName);
	if (!pluginInterface) {
		return nullptr;
	}

	return static_cast<IBodyMorphInterface*>(pluginInterface);
}