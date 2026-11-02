#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <memory>

namespace RE
{
	class PlayerCharacter;
}

namespace mps
{
	struct SharedFaceMorphTarget
	{
		float thin{ 0.0F };
		float muscular{ 0.0F };
		float heavy{ 0.0F };

		[[nodiscard]] bool operator==(const SharedFaceMorphTarget&) const = default;
	};

	// Reads the published MPF/POS asset library without loading or calling MPF.
	// The library is deliberately a PW-owned backend: it consumes the same TRI
	// and TOML files, but applies only to the player through the live FOD buffers.
	class SharedFaceMorphLibrary
	{
	public:
		SharedFaceMorphLibrary();
		~SharedFaceMorphLibrary();

		SharedFaceMorphLibrary(const SharedFaceMorphLibrary&) = delete;
		SharedFaceMorphLibrary& operator=(const SharedFaceMorphLibrary&) = delete;

		bool Load(const std::filesystem::path& explicitRoot = {});
		void Reset();
		// Discards ownership without restoring the old snapshot. Use this when
		// FaceGen/RaceMenu rebuilt the FOD: the old snapshot may belong to a
		// different geometry even if the allocation address was reused.
		void Discard();
		// Clears transient buffer ownership and all remembered male/female
		// geometry. Use across save/player or explicit race boundaries, where
		// history from the previous character must never influence a new face.
		void ForgetHistory();
		// Starts a new face-generation observation without erasing the
		// per-sex morph history.  RaceMenu can rebuild the same sex more than
		// once during a single ChangeSex operation.
		void BeginFaceTransition();
		// Returns true only after the same live face/FOD scene has been observed
		// for the requested number of consecutive samples.
		[[nodiscard]] bool ObserveStableFaceScene(
			RE::PlayerCharacter* player,
			std::uint8_t expectedSex,
			std::size_t requiredSamples);

		[[nodiscard]] bool IsLoaded() const noexcept;
		[[nodiscard]] std::size_t DeclaredSetCount() const noexcept;

		// Must run on the game thread. Returns false only when no usable player
		// face node is available yet; that is a retryable state, not a hard error.
		bool Apply(RE::PlayerCharacter* player, SharedFaceMorphTarget target);

	private:
		struct Impl;
		std::unique_ptr<Impl> _impl;
	};
}
