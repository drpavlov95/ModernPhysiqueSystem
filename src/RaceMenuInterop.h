#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include "RE/B/BSFixedString.h"

namespace RE
{
	class TESObjectREFR;
	class TESNPC;
	class NiAVObject;
}

using skee_u64 = std::uint64_t;
using skee_u32 = std::uint32_t;
using skee_i32 = std::int32_t;
using skee_u16 = std::uint16_t;
using skee_u8 = std::uint8_t;

class SKEEFixedString
{
public:
	SKEEFixedString() :
		m_hash(HashLower(m_internal.c_str(), m_internal.size()))
	{}

	SKEEFixedString(const char* str) :
		m_internal(str ? str : ""),
		m_hash(HashLower(m_internal.c_str(), m_internal.size()))
	{}

	SKEEFixedString(const std::string& str) :
		m_internal(str),
		m_hash(HashLower(m_internal.c_str(), m_internal.size()))
	{}

	SKEEFixedString(const RE::BSFixedString& str) :
		m_internal(str.c_str() ? str.c_str() : ""),
		m_hash(HashLower(m_internal.c_str(), m_internal.size()))
	{}

	const char* c_str() const { return m_internal.c_str(); }
	std::size_t GetHash() const { return m_hash; }

private:
	static std::size_t HashLower(const char* str, std::size_t count)
	{
		constexpr std::size_t kFnvOffsetBasis = 14695981039346656037ULL;
		constexpr std::size_t kFnvPrime = 1099511628211ULL;

		std::size_t value = kFnvOffsetBasis;
		for (std::size_t index = 0; index < count; ++index) {
			value ^= static_cast<std::size_t>(std::tolower(static_cast<unsigned char>(str[index])));
			value *= kFnvPrime;
		}
		return value;
	}

	std::string m_internal;
	std::size_t m_hash;
};

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

class IFaceMorphInterface : public IPluginInterface
{
public:
	enum
	{
		kCurrentPluginVersion = 1
	};

	// Keep the reserved slots before SetMorphValue so a ValueMap write cannot
	// accidentally land on SKEE's save/serialize path.
	virtual float GetMorphValueByName(RE::TESNPC* npc, const SKEEFixedString& name) = 0;
	virtual void SKEEReserved_Serialize(void*, void*, unsigned int) = 0;
	virtual unsigned int SKEEReserved_GetByHash(void*, unsigned long long, char*) = 0;
	virtual void SetMorphValue(RE::TESNPC* npc, const SKEEFixedString& name, float value) = 0;
};

class IAddonAttachmentInterface;

class IActorUpdateManager : public IPluginInterface
{
public:
	enum
	{
		kPluginVersion1 = 1,
		kPluginVersion2,
		kCurrentPluginVersion = kPluginVersion2
	};

	using FlushCallback = void (*)(skee_u32* formId, skee_u32 length);

	virtual void AddBodyUpdate(skee_u32 formId) = 0;
	virtual void AddTransformUpdate(skee_u32 formId) = 0;
	virtual void AddOverlayUpdate(skee_u32 formId) = 0;
	virtual void AddNodeOverrideUpdate(skee_u32 formId) = 0;
	virtual void AddWeaponOverrideUpdate(skee_u32 formId) = 0;
	virtual void AddAddonOverrideUpdate(skee_u32 formId) = 0;
	virtual void AddSkinOverrideUpdate(skee_u32 formId) = 0;
	virtual void Flush() = 0;
	virtual void AddInterface(IAddonAttachmentInterface* observer) = 0;
	virtual void RemoveInterface(IAddonAttachmentInterface* observer) = 0;
	virtual bool RegisterFlushCallback(const char* key, FlushCallback cb) = 0;
	virtual bool UnregisterFlushCallback(const char* key) = 0;
};

inline constexpr const char* kRaceMenuBodyMorphInterfaceName = "BodyMorph";
inline constexpr const char* kRaceMenuFaceMorphInterfaceName = "FaceMorph";
inline constexpr const char* kRaceMenuActorUpdateManagerInterfaceName = "ActorUpdateManager";

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

inline IFaceMorphInterface* GetFaceMorphInterfaceFromMap(IInterfaceMap* interfaceMap)
{
	if (!interfaceMap) {
		return nullptr;
	}

	auto* pluginInterface = interfaceMap->QueryInterface(kRaceMenuFaceMorphInterfaceName);
	if (!pluginInterface) {
		return nullptr;
	}

	return static_cast<IFaceMorphInterface*>(pluginInterface);
}

inline IActorUpdateManager* GetActorUpdateManagerInterfaceFromMap(IInterfaceMap* interfaceMap)
{
	if (!interfaceMap) {
		return nullptr;
	}

	auto* pluginInterface = interfaceMap->QueryInterface(kRaceMenuActorUpdateManagerInterfaceName);
	if (!pluginInterface) {
		return nullptr;
	}

	return static_cast<IActorUpdateManager*>(pluginInterface);
}
