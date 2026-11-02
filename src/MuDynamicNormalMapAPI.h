#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <cstdint>

namespace mps::mu
{
	// ABI prefix of Mu Dynamic NormalMap's public IDynamicNormalMap interface.
	// Later virtual methods are intentionally omitted because MPS only queues
	// geometry updates and must not own Mu detail/overlay state.
	class Interface
	{
	public:
		virtual ~Interface() = default;
		virtual std::uint32_t GetVersion() = 0;
	};

	class DynamicNormalMapInterface : public Interface
	{
	public:
		virtual void QUpdateNormalmap(
			RE::Actor* actor,
			std::uint32_t updateBipedSlots) = 0;
	};

	struct InterfaceExchangeMessage
	{
		static constexpr std::uint32_t kType = 0x4D444E4D;
		Interface* api = nullptr;
	};
}
