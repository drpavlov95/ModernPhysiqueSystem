#include "SharedFaceMorphLibrary.h"

#include "core/FaceGeometrySafety.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "RE/B/BSVisit.h"

namespace
{
	constexpr std::string_view kTriMagic = "FRTRI003";
	constexpr std::size_t kMaxGeometryHistoryPerSex = 32;
	constexpr std::size_t kMaxFaceGenerationsPerGeometry = 8;
	constexpr std::uint32_t kMaxTriVertices = 1'000'000;
	constexpr std::uint32_t kMaxTriMorphs = 20'000;
	constexpr std::size_t kMaxManifestBytes = 4u * 1024u * 1024u;

	struct Vec3
	{
		float x{ 0.0F };
		float y{ 0.0F };
		float z{ 0.0F };
	};

	using Deltas = std::vector<Vec3>;

	class Cursor
	{
	public:
		explicit Cursor(const std::vector<std::uint8_t>& bytes) : _bytes(bytes) {}

		bool Take(std::size_t count, const std::uint8_t*& out)
		{
			if (count > _bytes.size() - std::min(_offset, _bytes.size())) {
				return false;
			}
			out = _bytes.data() + _offset;
			_offset += count;
			return true;
		}

		bool Skip(std::size_t count)
		{
			const std::uint8_t* ignored = nullptr;
			return Take(count, ignored);
		}

		bool U32(std::uint32_t& out)
		{
			const std::uint8_t* raw = nullptr;
			if (!Take(sizeof(out), raw)) {
				return false;
			}
			std::memcpy(&out, raw, sizeof(out));
			return true;
		}

		bool F32(float& out)
		{
			const std::uint8_t* raw = nullptr;
			if (!Take(sizeof(out), raw)) {
				return false;
			}
			std::memcpy(&out, raw, sizeof(out));
			return std::isfinite(out);
		}

	private:
		const std::vector<std::uint8_t>& _bytes;
		std::size_t _offset{ 0 };
	};

	struct TriFile
	{
		std::uint32_t vertexCount{ 0 };
		std::vector<Vec3> base;
		std::unordered_map<std::string, Deltas> morphs;

		[[nodiscard]] const Deltas* Find(std::string_view name) const
		{
			const auto it = morphs.find(std::string(name));
			return it == morphs.end() ? nullptr : &it->second;
		}
	};

	bool CheckedProduct(std::size_t left, std::size_t right, std::size_t& out)
	{
		if (right != 0 && left > (std::numeric_limits<std::size_t>::max)() / right) {
			return false;
		}
		out = left * right;
		return true;
	}

	bool LoadTri(const std::filesystem::path& path, TriFile& out, std::string& error)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file) {
			error = "cannot open file";
			return false;
		}
		const auto end = file.tellg();
		if (end < 0 || static_cast<std::uintmax_t>(end) > 512u * 1024u * 1024u) {
			error = "file is missing or unreasonably large";
			return false;
		}
		const auto size = static_cast<std::size_t>(end);
		file.seekg(0);
		std::vector<std::uint8_t> bytes(size);
		if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
			error = "cannot read file";
			return false;
		}

		Cursor cursor(bytes);
		const std::uint8_t* rawMagic = nullptr;
		if (!cursor.Take(kTriMagic.size(), rawMagic) ||
			std::memcmp(rawMagic, kTriMagic.data(), kTriMagic.size()) != 0) {
			error = "not an FRTRI003 file";
			return false;
		}

		std::uint32_t header[14]{};
		for (auto& value : header) {
			if (!cursor.U32(value)) {
				error = "truncated header";
				return false;
			}
		}
		const std::uint32_t vertexCount = header[0];
		const std::uint32_t polyTris = header[1];
		const std::uint32_t polyQuads = header[2];
		const std::uint32_t uvVerts = header[5];
		const std::uint32_t numMorphs = header[7];
		const std::uint32_t modVerts = header[9];
		if (vertexCount == 0 || vertexCount > kMaxTriVertices || numMorphs > kMaxTriMorphs) {
			error = "invalid TRI dimensions";
			return false;
		}

		std::size_t blockBytes = 0;
		if (!CheckedProduct(vertexCount, 12u, blockBytes)) {
			error = "vertex block overflow";
			return false;
		}
		const std::uint8_t* baseBytes = nullptr;
		if (!cursor.Take(blockBytes, baseBytes)) {
			error = "truncated base vertex block";
			return false;
		}
		out.base.resize(vertexCount);
		for (std::uint32_t i = 0; i < vertexCount; ++i) {
			float xyz[3]{};
			std::memcpy(xyz, baseBytes + static_cast<std::size_t>(i) * 12u, sizeof(xyz));
			if (!std::isfinite(xyz[0]) || !std::isfinite(xyz[1]) || !std::isfinite(xyz[2])) {
				error = "non-finite base vertex";
				return false;
			}
			out.base[i] = { xyz[0], xyz[1], xyz[2] };
		}

		const auto skipBlock = [&cursor](std::uint32_t count, std::size_t stride) {
			std::size_t bytesToSkip = 0;
			return CheckedProduct(count, stride, bytesToSkip) && cursor.Skip(bytesToSkip);
		};
		if (!skipBlock(modVerts, 12u) ||
			!skipBlock(polyTris, 12u) ||
			!skipBlock(polyQuads, 16u) ||
			!skipBlock(uvVerts, 8u) ||
			!skipBlock(polyTris, 12u) ||
			!skipBlock(polyQuads, 16u)) {
			error = "truncated before morph section";
			return false;
		}

		for (std::uint32_t morphIndex = 0; morphIndex < numMorphs; ++morphIndex) {
			std::uint32_t nameLength = 0;
			if (!cursor.U32(nameLength) || nameLength > 4096u) {
				error = "invalid morph name length";
				return false;
			}
			const std::uint8_t* nameBytes = nullptr;
			if (!cursor.Take(nameLength, nameBytes)) {
				error = "truncated morph name";
				return false;
			}
			std::string name(reinterpret_cast<const char*>(nameBytes), nameLength);
			while (!name.empty() && name.back() == '\0') {
				name.pop_back();
			}
			float multiplier = 0.0F;
			if (!cursor.F32(multiplier)) {
				error = "invalid morph multiplier";
				return false;
			}

			if (name != "RLY_Thin" && name != "RLY_Muscular" && name != "RLY_Heavy" &&
				name != "RLY_NoThin" && name != "RLY_NoMuscular" && name != "RLY_NoFat" &&
				name != "RLY_DoubleChin" && name != "RLY_HeavyFuller" && name != "RLY_HeavyFully" &&
				name != "RLY_Standalone_Thin" && name != "RLY_Standalone_Muscular" &&
				name != "RLY_Standalone_Heavy") {
				if (!skipBlock(vertexCount, 6u)) {
					error = "truncated unused morph";
					return false;
				}
				continue;
			}

			std::size_t deltaBytes = 0;
			if (!CheckedProduct(vertexCount, 6u, deltaBytes)) {
				error = "morph block overflow";
				return false;
			}
			const std::uint8_t* rawDeltas = nullptr;
			if (!cursor.Take(deltaBytes, rawDeltas)) {
				error = "truncated morph delta block";
				return false;
			}
			Deltas deltas(vertexCount);
			for (std::uint32_t vertex = 0; vertex < vertexCount; ++vertex) {
				std::int16_t xyz[3]{};
				std::memcpy(xyz, rawDeltas + static_cast<std::size_t>(vertex) * 6u, sizeof(xyz));
				deltas[vertex] = {
					static_cast<float>(xyz[0]) * multiplier,
					static_cast<float>(xyz[1]) * multiplier,
					static_cast<float>(xyz[2]) * multiplier
				};
			}
			out.morphs.emplace(std::move(name), std::move(deltas));
		}

		out.vertexCount = vertexCount;
		return true;
	}

	std::string Trim(std::string_view text)
	{
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
			text.remove_prefix(1);
		}
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
			text.remove_suffix(1);
		}
		return std::string(text);
	}

	std::string RemoveTomlComment(std::string line)
	{
		bool quoted = false;
		bool escaped = false;
		for (std::size_t i = 0; i < line.size(); ++i) {
			const char ch = line[i];
			if (ch == '"' && !escaped) {
				quoted = !quoted;
			}
			if (ch == '#' && !quoted) {
				line.resize(i);
				break;
			}
			escaped = ch == '\\' && !escaped;
			if (ch != '\\') {
				escaped = false;
			}
		}
		return line;
	}

	bool ParseQuoted(std::string value, std::string& out)
	{
		value = Trim(value);
		if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
			return false;
		}
		value.erase(value.begin());
		value.pop_back();
		out.clear();
		out.reserve(value.size());
		bool escaped = false;
		for (const char ch : value) {
			if (escaped) {
				if (ch != '"' && ch != '\\') {
					return false;
				}
				out.push_back(ch);
				escaped = false;
			} else if (ch == '\\') {
				escaped = true;
			} else {
				out.push_back(ch);
			}
		}
		return !escaped;
	}

	bool IsSafeRelativePath(const std::filesystem::path& path)
	{
		if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
			return false;
		}
		for (const auto& part : path) {
			if (part == "..") {
				return false;
			}
		}
		return true;
	}

	std::filesystem::path GameRoot()
	{
		std::array<wchar_t, MAX_PATH> buffer{};
		const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size()) {
			return {};
		}
		return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
	}

	std::filesystem::path FindLibraryRoot()
	{
		const std::array candidates{
			std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / "MPF" / "morphs",
			GameRoot() / "Data" / "SKSE" / "Plugins" / "MPF" / "morphs",
			std::filesystem::current_path() / "SKSE" / "Plugins" / "MPF" / "morphs"
		};
		for (const auto& candidate : candidates) {
			std::error_code ec;
			if (!candidate.empty() && std::filesystem::is_directory(candidate, ec)) {
				return candidate;
			}
		}
		return {};
	}

	struct Entry
	{
		std::filesystem::path path;
		bool female{ false };
		std::uint32_t vertexCount{ 0 };
		bool loadAttempted{ false };
		TriFile tri;
	};

	std::uint32_t ReadTriVertexCount(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		std::array<char, 12> bytes{};
		if (!file.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) ||
			std::memcmp(bytes.data(), kTriMagic.data(), kTriMagic.size()) != 0) {
			return 0;
		}
		std::uint32_t count = 0;
		std::memcpy(&count, bytes.data() + kTriMagic.size(), sizeof(count));
		return count <= kMaxTriVertices ? count : 0;
	}

}

// CommonLibSSE-NG exposes the FOD RTTI but not this private Skyrim class.
// This is the measured 0x28-byte layout used by MPF and by the game's own
// FaceGen code: NiExtraData (0x18) + pointer + count + padding.
struct FOD : RE::NiExtraData
{
	RE::NiPoint3* vertexData{ nullptr };
	std::uint32_t vertexCount{ 0 };
	std::uint32_t pad24{ 0 };
};
static_assert(sizeof(FOD) == 0x28);

namespace
{
	FOD* GetFOD(RE::BSGeometry* geometry)
	{
		static const RE::BSFixedString key{ "FOD" };
		return geometry ? static_cast<FOD*>(geometry->GetExtraData(key)) : nullptr;
	}

	bool BufferLooksLikeGeometry(const FOD* fod)
	{
		if (!fod || !fod->vertexData || fod->vertexCount < 3) {
			return false;
		}
		auto lo = fod->vertexData[0];
		auto hi = lo;
		for (std::uint32_t i = 1; i < fod->vertexCount; ++i) {
			const auto& vertex = fod->vertexData[i];
			lo.x = (std::min)(lo.x, vertex.x);
			lo.y = (std::min)(lo.y, vertex.y);
			lo.z = (std::min)(lo.z, vertex.z);
			hi.x = (std::max)(hi.x, vertex.x);
			hi.y = (std::max)(hi.y, vertex.y);
			hi.z = (std::max)(hi.z, vertex.z);
		}
		return (std::max)({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z }) > 0.5F;
	}

	template <class Point>
	float CentredMeanDistance(const RE::NiPoint3* live, const std::vector<Point>& rest)
	{
		if (!live || rest.empty()) {
			return (std::numeric_limits<float>::max)();
		}
		double lx = 0.0, ly = 0.0, lz = 0.0;
		double rx = 0.0, ry = 0.0, rz = 0.0;
		for (const auto& vertex : rest) {
			rx += vertex.x;
			ry += vertex.y;
			rz += vertex.z;
		}
		for (std::size_t i = 0; i < rest.size(); ++i) {
			lx += live[i].x;
			ly += live[i].y;
			lz += live[i].z;
		}
		const double inverse = 1.0 / static_cast<double>(rest.size());
		lx *= inverse; ly *= inverse; lz *= inverse;
		rx *= inverse; ry *= inverse; rz *= inverse;
		double sum = 0.0;
		for (std::size_t i = 0; i < rest.size(); ++i) {
			const double dx = (live[i].x - lx) - (rest[i].x - rx);
			const double dy = (live[i].y - ly) - (rest[i].y - ry);
			const double dz = (live[i].z - lz) - (rest[i].z - rz);
			sum += std::sqrt(dx * dx + dy * dy + dz * dz);
		}
		return static_cast<float>(sum * inverse);
	}

	template <class Point>
	float CentredMaxDistance(const RE::NiPoint3* live, const std::vector<Point>& rest)
	{
		if (!live || rest.empty()) {
			return (std::numeric_limits<float>::max)();
		}
		double lx = 0.0, ly = 0.0, lz = 0.0;
		double rx = 0.0, ry = 0.0, rz = 0.0;
		for (std::size_t i = 0; i < rest.size(); ++i) {
			lx += live[i].x;
			ly += live[i].y;
			lz += live[i].z;
			rx += rest[i].x;
			ry += rest[i].y;
			rz += rest[i].z;
		}
		const double inverse = 1.0 / static_cast<double>(rest.size());
		lx *= inverse; ly *= inverse; lz *= inverse;
		rx *= inverse; ry *= inverse; rz *= inverse;
		double maximum = 0.0;
		for (std::size_t i = 0; i < rest.size(); ++i) {
			const double dx = (live[i].x - lx) - (rest[i].x - rx);
			const double dy = (live[i].y - ly) - (rest[i].y - ry);
			const double dz = (live[i].z - lz) - (rest[i].z - rz);
			maximum = (std::max)(maximum, std::sqrt(dx * dx + dy * dy + dz * dz));
		}
		return static_cast<float>(maximum);
	}

	bool HasMorphStrength(const mps::SharedFaceMorphTarget& target)
	{
		return target.thin != 0.0F || target.muscular != 0.0F || target.heavy != 0.0F ||
			target.noThin != 0.0F || target.noMuscular != 0.0F || target.noFat != 0.0F ||
			target.doubleChin != 0.0F || target.heavyFully != 0.0F ||
			target.standaloneThin != 0.0F || target.standaloneMuscular != 0.0F ||
			target.standaloneHeavy != 0.0F;
	}

	struct ResolvedTargetDeltas
	{
		std::array<const Deltas*, 11> deltas{};
		std::array<float, 11> strengths{};

		[[nodiscard]] Vec3 At(std::size_t index) const
		{
			Vec3 result{};
			for (std::size_t axis = 0; axis < deltas.size(); ++axis) {
				if (!deltas[axis] || index >= deltas[axis]->size() || strengths[axis] == 0.0F) {
					continue;
				}
				result.x += deltas[axis]->at(index).x * strengths[axis];
				result.y += deltas[axis]->at(index).y * strengths[axis];
				result.z += deltas[axis]->at(index).z * strengths[axis];
			}
			return result;
		}
	};

	ResolvedTargetDeltas ResolveTargetDeltas(
		const TriFile& tri,
		const mps::SharedFaceMorphTarget& target)
	{
		ResolvedTargetDeltas result{
			.deltas = { tri.Find("RLY_Thin"), tri.Find("RLY_Muscular"), tri.Find("RLY_Heavy"),
				tri.Find("RLY_NoThin"), tri.Find("RLY_NoMuscular"), tri.Find("RLY_NoFat"),
				tri.Find("RLY_DoubleChin"),
				tri.Find("RLY_HeavyFuller") ? tri.Find("RLY_HeavyFuller") : tri.Find("RLY_HeavyFully"),
				tri.Find("RLY_Standalone_Thin"), tri.Find("RLY_Standalone_Muscular"),
				tri.Find("RLY_Standalone_Heavy") },
			.strengths = { target.thin, target.muscular, target.heavy,
				target.noThin, target.noMuscular, target.noFat, target.doubleChin,
				target.heavyFully, target.standaloneThin, target.standaloneMuscular,
				target.standaloneHeavy }
		};
		for (std::size_t axis = 0; axis < result.deltas.size(); ++axis) {
			if (result.deltas[axis] && result.deltas[axis]->size() != tri.vertexCount) {
				result.deltas[axis] = nullptr;
			}
			const float strength = result.strengths[axis];
			if (strength == 0.0F) {
				result.deltas[axis] = nullptr;
			}
		}
		return result;
	}

	float CentredMeanDistanceWithoutTarget(
		const RE::NiPoint3* live,
		const TriFile& tri,
		const ResolvedTargetDeltas& targetDeltas)
	{
		if (!live || tri.base.empty() || tri.base.size() != tri.vertexCount) {
			return (std::numeric_limits<float>::max)();
		}

		double lx = 0.0, ly = 0.0, lz = 0.0;
		double rx = 0.0, ry = 0.0, rz = 0.0;
		for (std::size_t i = 0; i < tri.base.size(); ++i) {
			const auto delta = targetDeltas.At(i);
			lx += static_cast<double>(live[i].x - delta.x);
			ly += static_cast<double>(live[i].y - delta.y);
			lz += static_cast<double>(live[i].z - delta.z);
			rx += tri.base[i].x;
			ry += tri.base[i].y;
			rz += tri.base[i].z;
		}
		const double inverse = 1.0 / static_cast<double>(tri.base.size());
		lx *= inverse; ly *= inverse; lz *= inverse;
		rx *= inverse; ry *= inverse; rz *= inverse;

		double sum = 0.0;
		for (std::size_t i = 0; i < tri.base.size(); ++i) {
			const auto delta = targetDeltas.At(i);
			const double dx = (live[i].x - delta.x - lx) - (tri.base[i].x - rx);
			const double dy = (live[i].y - delta.y - ly) - (tri.base[i].y - ry);
			const double dz = (live[i].z - delta.z - lz) - (tri.base[i].z - rz);
			sum += std::sqrt(dx * dx + dy * dy + dz * dz);
		}
		return static_cast<float>(sum * inverse);
	}

	void RemoveTarget(
		std::vector<RE::NiPoint3>& vertices,
		const TriFile& tri,
		const ResolvedTargetDeltas& targetDeltas)
	{
		if (vertices.size() != tri.vertexCount) {
			return;
		}
		for (std::size_t i = 0; i < vertices.size(); ++i) {
			const auto delta = targetDeltas.At(i);
			vertices[i].x -= delta.x;
			vertices[i].y -= delta.y;
			vertices[i].z -= delta.z;
		}
	}

	void AddTarget(
		RE::NiPoint3* vertices,
		std::uint32_t count,
		const TriFile& tri,
		const ResolvedTargetDeltas& targetDeltas)
	{
		if (!vertices || count != tri.vertexCount) {
			return;
		}
		for (std::uint32_t i = 0; i < count; ++i) {
			const auto delta = targetDeltas.At(i);
			vertices[i].x += delta.x;
			vertices[i].y += delta.y;
			vertices[i].z += delta.z;
		}
	}

	constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
	constexpr std::uint64_t kFnvPrime = 1099511628211ull;

	void HashWord(std::uint64_t& hash, std::uint64_t value)
	{
		for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
			hash ^= static_cast<std::uint8_t>(value & 0xFFu);
			hash *= kFnvPrime;
			value >>= 8u;
		}
	}

	std::uint64_t Fingerprint(
		const RE::NiPoint3* vertices,
		std::uint32_t count,
		bool full)
	{
		if (!vertices || count == 0) {
			return 0;
		}
		const std::uint32_t step = full ? 1u : (count / 128u) + 1u;
		std::uint64_t hash = kFnvOffset;
		HashWord(hash, count);
		for (std::uint32_t i = 0; i < count; i += step) {
			std::uint32_t xyz[3]{};
			std::memcpy(&xyz[0], &vertices[i].x, sizeof(xyz[0]));
			std::memcpy(&xyz[1], &vertices[i].y, sizeof(xyz[1]));
			std::memcpy(&xyz[2], &vertices[i].z, sizeof(xyz[2]));
			HashWord(hash, i);
			HashWord(hash, xyz[0]);
			HashWord(hash, xyz[1]);
			HashWord(hash, xyz[2]);
		}
		return hash;
	}

	std::uint64_t Fingerprint(const std::vector<std::uintptr_t>& values)
	{
		std::uint64_t hash = kFnvOffset;
		HashWord(hash, values.size());
		for (const auto value : values) {
			HashWord(hash, static_cast<std::uint64_t>(value));
		}
		return hash;
	}

	struct FaceSceneSignature
	{
		std::uintptr_t faceNode{ 0 };
		std::uint8_t sex{ 2 };
		std::uint64_t fingerprint{ 0 };
		std::uint32_t bufferCount{ 0 };
		bool valid{ false };
	};

	FaceSceneSignature CaptureFaceScene(RE::PlayerCharacter* player)
	{
		FaceSceneSignature result;
		if (!player) {
			return result;
		}

		const auto* actorBase = player->GetActorBase();
		if (!actorBase) {
			return result;
		}
		result.sex = static_cast<std::uint8_t>(actorBase->GetSex());
		if (result.sex > 1) {
			return result;
		}

		auto* faceNode = player->GetFaceNodeSkinned();
		if (!faceNode) {
			faceNode = player->GetFaceNode();
		}
		if (!faceNode) {
			return result;
		}
		result.faceNode = reinterpret_cast<std::uintptr_t>(faceNode);

		std::vector<std::uintptr_t> sceneKeys;
		sceneKeys.push_back(result.faceNode);
		sceneKeys.push_back(result.sex);
		RE::BSVisit::TraverseScenegraphGeometries(faceNode, [&](RE::BSGeometry* geometry) {
			auto* triShape = geometry ? geometry->AsTriShape() : nullptr;
			const auto geometryVertexCount = triShape ?
				static_cast<std::uint32_t>(triShape->GetTrishapeRuntimeData().vertexCount) : 0u;
			if (auto* fod = GetFOD(geometry);
				fod && fod->vertexData && fod->vertexCount > 0 &&
				geometryVertexCount == fod->vertexCount) {
				const auto contentFingerprint = Fingerprint(
					fod->vertexData,
					fod->vertexCount,
					false);
				sceneKeys.push_back(reinterpret_cast<std::uintptr_t>(geometry));
				sceneKeys.push_back(reinterpret_cast<std::uintptr_t>(fod));
				sceneKeys.push_back(reinterpret_cast<std::uintptr_t>(fod->vertexData));
				sceneKeys.push_back(fod->vertexCount);
				sceneKeys.push_back(static_cast<std::uintptr_t>(contentFingerprint));
				++result.bufferCount;
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});

		if (result.bufferCount == 0) {
			return result;
		}
		result.fingerprint = Fingerprint(sceneKeys);
		result.valid = true;
		return result;
	}
}

namespace mps
{
	struct SharedFaceMorphLibrary::Impl
	{
		struct SexHistory
		{
			SharedFaceMorphTarget target{};
			bool hasTarget{ false };
		};

		struct FaceGeneration
		{
			SharedFaceMorphTarget target{};
			std::vector<RE::NiPoint3> base;
			std::vector<RE::NiPoint3> output;
			std::uint64_t baseFingerprint{ 0 };
			std::uint64_t outputFingerprint{ 0 };
			std::uint64_t lastTouched{ 0 };
		};

		struct GeometryHistory
		{
			std::deque<FaceGeneration> generations;
			std::uint64_t lastTouched{ 0 };
		};

		struct PickResult
		{
			Entry* entry{ nullptr };
			SharedFaceMorphTarget previousTarget{};
			float distance{ (std::numeric_limits<float>::max)() };
			float rawDistance{ (std::numeric_limits<float>::max)() };
			float rawMaxDistance{ (std::numeric_limits<float>::max)() };
			bool removePreviousTarget{ false };
			const FaceGeneration* recoveredGeneration{ nullptr };
		};

		FaceSceneSignature observedScene{};
		std::size_t observedStableSamples{ 0 };

		struct BufferState
		{
			RE::NiPoint3* data{ nullptr };
			std::uint32_t count{ 0 };
			Entry* entry{ nullptr };
			std::vector<RE::NiPoint3> base;
			std::vector<RE::NiPoint3> output;
			SharedFaceMorphTarget appliedTarget{};
			std::uint64_t lastAppliedFingerprint{ 0 };
			bool hasAppliedTarget{ false };
			bool hasAppliedFingerprint{ false };
		};

		std::filesystem::path root;
		std::vector<Entry> entries;
		std::unordered_map<std::uintptr_t, BufferState> buffers;
		std::array<SexHistory, 2> sexHistory{};
		std::array<std::unordered_map<std::string, GeometryHistory>, 2> geometryHistory{};
		std::uint64_t historyGeneration{ 0 };
		SharedFaceMorphTarget lastTarget{};
		bool hasTarget{ false };
		std::uint8_t lastTargetSex{ 2 };
		bool loaded{ false };
		bool recoverInteractiveEdit{ false };
		std::uint64_t lastSceneFingerprint{ 0 };
		bool hasSceneFingerprint{ false };

		void Restore(BufferState& state)
		{
			if (!state.data || state.base.size() != state.count) {
				return;
			}
			std::copy(state.base.begin(), state.base.end(), state.data);
		}

		void PruneGeometryHistory(std::uint8_t sexIndex)
		{
			auto& history = geometryHistory[sexIndex];
			while (history.size() > kMaxGeometryHistoryPerSex) {
				const auto oldest = std::min_element(
					history.begin(),
					history.end(),
					[](const auto& left, const auto& right) {
						return left.second.lastTouched < right.second.lastTouched;
					});
				if (oldest == history.end()) {
					break;
				}
				history.erase(oldest);
			}
		}

		PickResult Pick(
			bool female,
			std::uint8_t sexIndex,
			FOD* fod,
			const SexHistory& history)
		{
			PickResult best;
			for (auto& entry : entries) {
				if (entry.female != female || entry.vertexCount != fod->vertexCount) {
					continue;
				}
				if (!entry.loadAttempted) {
					entry.loadAttempted = true;
					std::string error;
					if (!LoadTri(entry.path, entry.tri, error)) {
						spdlog::warn("PW shared face morph: failed to load {}: {}", entry.path.string(), error);
						continue;
					}
				}
				if (entry.tri.vertexCount != fod->vertexCount || entry.tri.base.size() != fod->vertexCount) {
					continue;
				}
				const float rawDistance = CentredMeanDistance(fod->vertexData, entry.tri.base);
				const float rawMaxDistance = CentredMaxDistance(fod->vertexData, entry.tri.base);
				float candidateDistance = rawDistance;
				bool removePreviousTarget = false;
				const FaceGeneration* recoveredGeneration = nullptr;
				const auto historyKey = entry.path.generic_string();
				const auto geometryHistoryIt = geometryHistory[sexIndex].find(historyKey);
				const auto* priorGeometry = geometryHistoryIt != geometryHistory[sexIndex].end() ?
					std::addressof(geometryHistoryIt->second) : nullptr;
				SharedFaceMorphTarget previousTarget = history.target;
				bool hasPreviousTarget = history.hasTarget;
				const auto liveFingerprint = Fingerprint(
					fod->vertexData,
					fod->vertexCount,
					true);

				if (priorGeometry && !priorGeometry->generations.empty()) {
					const auto& latest = priorGeometry->generations.back();
					previousTarget = latest.target;
					hasPreviousTarget = true;
					for (const auto& generation : priorGeometry->generations) {
						if (generation.output.size() != fod->vertexCount ||
							generation.base.size() != fod->vertexCount) {
							continue;
						}

						// Exact output fingerprints are the strongest possible evidence:
						// RaceMenu may have resurrected an older PW generation, even
						// though the latest target belongs to a different generation.
						if (generation.outputFingerprint != 0 &&
							generation.outputFingerprint == liveFingerprint) {
							candidateDistance = 0.0F;
							removePreviousTarget = false;
							recoveredGeneration = std::addressof(generation);
							previousTarget = generation.target;
							break;
						}

						const float previousBaseDistance = CentredMeanDistance(
							fod->vertexData,
							generation.base);
						const float previousOutputDistance = CentredMeanDistance(
							fod->vertexData,
							generation.output);
						if (mps::IsMaterialFaceResidualImprovement(
								previousBaseDistance,
								previousOutputDistance) &&
							HasMorphStrength(generation.target)) {
							const auto generationDeltas = ResolveTargetDeltas(entry.tri, generation.target);
							const float normalizedDistance = CentredMeanDistanceWithoutTarget(
								fod->vertexData,
								entry.tri,
								generationDeltas);
							if (mps::IsMaterialFaceResidualImprovement(
									candidateDistance,
									normalizedDistance)) {
								candidateDistance = normalizedDistance;
								removePreviousTarget = true;
								previousTarget = generation.target;
								recoveredGeneration = nullptr;
							}
						}
					}
				}

				if (!recoveredGeneration && hasPreviousTarget && HasMorphStrength(previousTarget)) {
					const auto previousDeltas = ResolveTargetDeltas(entry.tri, previousTarget);
					const float normalizedDistance = CentredMeanDistanceWithoutTarget(
						fod->vertexData,
						entry.tri,
						previousDeltas);
					if (mps::IsMaterialFaceResidualImprovement(
							candidateDistance,
							normalizedDistance)) {
						candidateDistance = normalizedDistance;
						removePreviousTarget = true;
					}
				}
				if (candidateDistance < best.distance) {
					best.entry = &entry;
					best.previousTarget = previousTarget;
					best.distance = candidateDistance;
					best.rawDistance = rawDistance;
					best.rawMaxDistance = rawMaxDistance;
					best.removePreviousTarget = removePreviousTarget;
					best.recoveredGeneration = recoveredGeneration;
				}
			}
			if (!mps::IsSafeFaceGeometryCandidate({
					best.distance,
					best.rawMaxDistance,
					best.recoveredGeneration != nullptr })) {
				if (best.entry) {
					spdlog::warn(
						"PW shared face morph: rejected ambiguous TRI {} meanDistance={:.6f} maxDistance={:.6f} exactHistory={}",
						best.entry->path.string(),
						best.distance,
						best.rawMaxDistance,
						best.recoveredGeneration ? 1 : 0);
				}
				return {};
			}
			return best;
		}

		void ParseManifest(const std::filesystem::path& manifest)
		{
			std::ifstream file(manifest, std::ios::binary | std::ios::ate);
			if (!file) {
				return;
			}
			const auto end = file.tellg();
			if (end < 0 || static_cast<std::size_t>(end) > kMaxManifestBytes) {
				spdlog::warn("PW shared face morph: skipping oversized manifest {}", manifest.string());
				return;
			}
			std::string content(static_cast<std::size_t>(end), '\0');
			file.seekg(0);
			file.read(content.data(), static_cast<std::streamsize>(content.size()));

			std::string relativeFile;
			std::string sex;
			std::string part;
			bool inSet = false;
			const auto flush = [&]() {
				if (!inSet) {
					return;
				}
				inSet = false;
				if (relativeFile.empty() || (sex != "male" && sex != "female")) {
					relativeFile.clear(); sex.clear(); part.clear();
					return;
				}
				std::filesystem::path relative(relativeFile);
				if (!IsSafeRelativePath(relative) || relative.extension() != ".tri") {
					spdlog::warn("PW shared face morph: rejected unsafe TRI path '{}' in {}", relativeFile, manifest.string());
					relativeFile.clear(); sex.clear(); part.clear();
					return;
				}
				const auto path = manifest.parent_path() / relative;
				const auto vertexCount = ReadTriVertexCount(path);
				if (vertexCount == 0) {
					spdlog::warn("PW shared face morph: manifest points to missing/invalid TRI {}", path.string());
				} else {
					entries.push_back({ path, sex == "female", vertexCount });
				}
				relativeFile.clear(); sex.clear(); part.clear();
			};

			std::size_t lineStart = 0;
			while (lineStart <= content.size()) {
				const auto lineEnd = content.find('\n', lineStart);
				const auto line = content.substr(lineStart, lineEnd == std::string::npos ? std::string::npos : lineEnd - lineStart);
				const auto clean = Trim(RemoveTomlComment(line));
				if (clean == "[[sets]]") {
					flush();
					inSet = true;
				} else if (inSet) {
					const auto equals = clean.find('=');
					if (equals != std::string::npos) {
						const auto key = Trim(std::string_view(clean).substr(0, equals));
						std::string value;
						if (ParseQuoted(clean.substr(equals + 1), value)) {
							if (key == "file") relativeFile = std::move(value);
							else if (key == "sex") sex = std::move(value);
							else if (key == "part") part = std::move(value);
						}
					}
				}
				if (lineEnd == std::string::npos) break;
				lineStart = lineEnd + 1;
			}
			flush();
		}
	};

	SharedFaceMorphLibrary::SharedFaceMorphLibrary() : _impl(std::make_unique<Impl>()) {}
	SharedFaceMorphLibrary::~SharedFaceMorphLibrary() = default;

	bool SharedFaceMorphLibrary::Load(const std::filesystem::path& explicitRoot)
	{
		Reset();
		_impl->entries.clear();
		_impl->sexHistory = {};
		_impl->geometryHistory = {};
		_impl->historyGeneration = 0;
		_impl->observedScene = {};
		_impl->observedStableSamples = 0;
		_impl->loaded = false;
		_impl->root = explicitRoot.empty() ? FindLibraryRoot() : explicitRoot;
		std::error_code ec;
		if (_impl->root.empty() || !std::filesystem::is_directory(_impl->root, ec)) {
			spdlog::warn("PW shared face morph: MPF morph library not found");
			return false;
		}
		for (const auto& item : std::filesystem::recursive_directory_iterator(_impl->root, ec)) {
			if (ec) break;
			if (item.is_regular_file(ec) && item.path().extension() == ".toml") {
				_impl->ParseManifest(item.path());
			}
		}
		std::sort(_impl->entries.begin(), _impl->entries.end(), [](const Entry& left, const Entry& right) {
			return left.path.generic_string() < right.path.generic_string();
		});
		_impl->loaded = !_impl->entries.empty();
		spdlog::info("PW shared face morph: root={} manifests loaded, declaredSets={}", _impl->root.string(), _impl->entries.size());
		return _impl->loaded;
	}

	void SharedFaceMorphLibrary::Reset()
	{
		if (!_impl) return;
		std::unordered_map<std::uintptr_t, FOD*> liveBuffers;
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			auto* faceNode = player->GetFaceNodeSkinned();
			if (!faceNode) faceNode = player->GetFaceNode();
			if (faceNode) {
				RE::BSVisit::TraverseScenegraphGeometries(faceNode, [&](RE::BSGeometry* geometry) {
					auto* triShape = geometry ? geometry->AsTriShape() : nullptr;
					const auto geometryVertexCount = triShape ?
						static_cast<std::uint32_t>(triShape->GetTrishapeRuntimeData().vertexCount) : 0u;
					if (auto* fod = GetFOD(geometry);
						fod && fod->vertexData && fod->vertexCount > 0 &&
						geometryVertexCount == fod->vertexCount) {
						liveBuffers.emplace(reinterpret_cast<std::uintptr_t>(fod), fod);
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			}
		}
		for (auto& [key, state] : _impl->buffers) {
			// Restore only while the live buffer still equals the last output
			// written by PW. If FaceGen/RaceMenu has already rebuilt or mutated
			// this allocation, writing the old snapshot back would contaminate
			// the new gender/race geometry. Never dereference a cached raw pointer
			// unless the current face scene still exposes that exact FOD/data pair.
			const auto liveIt = liveBuffers.find(key);
			if (state.hasAppliedFingerprint &&
				liveIt != liveBuffers.end() &&
				liveIt->second->vertexData == state.data &&
				liveIt->second->vertexCount == state.count &&
				Fingerprint(state.data, state.count, true) == state.lastAppliedFingerprint) {
				_impl->Restore(state);
			}
		}
		Discard();
	}

	void SharedFaceMorphLibrary::Discard()
	{
		if (!_impl) return;
		_impl->buffers.clear();
		_impl->lastTarget = {};
		_impl->hasTarget = false;
		_impl->lastTargetSex = 2;
		_impl->hasSceneFingerprint = false;
		_impl->recoverInteractiveEdit = false;
		_impl->observedScene = {};
		_impl->observedStableSamples = 0;
	}

	void SharedFaceMorphLibrary::ForgetHistory()
	{
		if (!_impl) return;
		Discard();
		_impl->sexHistory = {};
		_impl->geometryHistory = {};
		_impl->historyGeneration = 0;
	}

	void SharedFaceMorphLibrary::BeginFaceTransition()
	{
		if (!_impl) {
			return;
		}
		_impl->observedScene = {};
		_impl->observedStableSamples = 0;
		_impl->recoverInteractiveEdit = false;
	}

	void SharedFaceMorphLibrary::BeginInteractiveFaceEdit()
	{
		if (!_impl) {
			return;
		}
		_impl->observedScene = {};
		_impl->observedStableSamples = 0;
		_impl->hasSceneFingerprint = false;
		_impl->recoverInteractiveEdit = true;
	}

	bool SharedFaceMorphLibrary::ObserveStableFaceScene(
		RE::PlayerCharacter* player,
		std::uint8_t expectedSex,
		std::size_t requiredSamples)
	{
		if (!_impl || !_impl->loaded || !player || expectedSex > 1 || requiredSamples == 0) {
			return false;
		}

		const auto current = CaptureFaceScene(player);
		if (!current.valid || current.sex != expectedSex) {
			_impl->observedScene = {};
			_impl->observedStableSamples = 0;
			return false;
		}

		const bool sameScene =
			_impl->observedScene.valid &&
			_impl->observedScene.faceNode == current.faceNode &&
			_impl->observedScene.sex == current.sex &&
			_impl->observedScene.fingerprint == current.fingerprint &&
			_impl->observedScene.bufferCount == current.bufferCount;
		if (sameScene) {
			++_impl->observedStableSamples;
		} else {
			_impl->observedScene = current;
			_impl->observedStableSamples = 1;
			spdlog::debug(
				"PW shared face morph: observed new face scene sex={} node={} buffers={} fingerprint={:016X}",
				static_cast<unsigned>(current.sex),
				reinterpret_cast<const void*>(current.faceNode),
				current.bufferCount,
				current.fingerprint);
		}
		return _impl->observedStableSamples >= requiredSamples;
	}

	bool SharedFaceMorphLibrary::IsLoaded() const noexcept { return _impl && _impl->loaded; }
	std::size_t SharedFaceMorphLibrary::DeclaredSetCount() const noexcept { return _impl ? _impl->entries.size() : 0; }

	bool SharedFaceMorphLibrary::Apply(RE::PlayerCharacter* player, SharedFaceMorphTarget target)
	{
		if (!_impl || !_impl->loaded || !player) return false;
		auto* faceNode = player->GetFaceNodeSkinned();
		if (!faceNode) faceNode = player->GetFaceNode();
		if (!faceNode) return false;

		const auto* actorBase = player->GetActorBase();
		const bool female = actorBase && actorBase->GetSex() == RE::SEX::kFemale;
		const std::uint8_t sexIndex = female ? 1u : 0u;
		const auto previousHistory = _impl->sexHistory[sexIndex];

		std::vector<std::uintptr_t> sceneKeys;
		RE::BSVisit::TraverseScenegraphGeometries(faceNode, [&](RE::BSGeometry* geometry) {
			auto* triShape = geometry ? geometry->AsTriShape() : nullptr;
			const auto geometryVertexCount = triShape ?
				static_cast<std::uint32_t>(triShape->GetTrishapeRuntimeData().vertexCount) : 0u;
			if (auto* fod = GetFOD(geometry);
				fod && fod->vertexData && fod->vertexCount > 0 &&
				geometryVertexCount == fod->vertexCount) {
				const auto contentFingerprint = Fingerprint(
					fod->vertexData,
					fod->vertexCount,
					false);
				sceneKeys.push_back(reinterpret_cast<std::uintptr_t>(fod));
				sceneKeys.push_back(reinterpret_cast<std::uintptr_t>(fod->vertexData));
				sceneKeys.push_back(fod->vertexCount);
				sceneKeys.push_back(static_cast<std::uintptr_t>(contentFingerprint));
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		const auto sceneFingerprint = Fingerprint(sceneKeys);
		const bool targetChanged = !_impl->hasTarget ||
			target != _impl->lastTarget ||
			_impl->lastTargetSex != sexIndex;
		if (!targetChanged && _impl->hasSceneFingerprint && sceneFingerprint == _impl->lastSceneFingerprint) {
			return true;
		}
		_impl->lastTarget = target;
		_impl->hasTarget = true;
		_impl->lastTargetSex = sexIndex;
		_impl->lastSceneFingerprint = sceneFingerprint;
		_impl->hasSceneFingerprint = true;

		std::unordered_set<std::uintptr_t> visited;
		bool appliedAny = false;
		RE::BSVisit::TraverseScenegraphGeometries(faceNode, [&](RE::BSGeometry* geometry) {
			auto* triShape = geometry ? geometry->AsTriShape() : nullptr;
			const auto geometryVertexCount = triShape ?
				static_cast<std::uint32_t>(triShape->GetTrishapeRuntimeData().vertexCount) : 0u;
			auto* fod = GetFOD(geometry);
			if (!BufferLooksLikeGeometry(fod) ||
				geometryVertexCount == 0 ||
				geometryVertexCount != fod->vertexCount) {
				return RE::BSVisit::BSVisitControl::kContinue;
			}
			const auto key = reinterpret_cast<std::uintptr_t>(fod);
			auto stateIt = _impl->buffers.find(key);
			const bool canRecoverInteractiveEdit =
				_impl->recoverInteractiveEdit &&
				stateIt != _impl->buffers.end() &&
				stateIt->second.data == fod->vertexData &&
				stateIt->second.count == fod->vertexCount &&
				stateIt->second.entry &&
				stateIt->second.entry->female == female &&
				stateIt->second.entry->vertexCount == fod->vertexCount;

			Impl::PickResult pick;
			if (canRecoverInteractiveEdit) {
				// This is stronger evidence than TRI similarity: this exact live
				// FOD/data pair was owned by PW immediately before RaceMenu edited it.
				pick.entry = stateIt->second.entry;
				pick.previousTarget = stateIt->second.appliedTarget;
			} else {
				pick = _impl->Pick(female, sexIndex, fod, previousHistory);
			}
			if (!pick.entry) {
				return RE::BSVisit::BSVisitControl::kContinue;
			}

			visited.insert(key);
			if (stateIt == _impl->buffers.end() ||
				stateIt->second.data != fod->vertexData || stateIt->second.count != fod->vertexCount) {
				Impl::BufferState state;
				state.data = fod->vertexData;
				state.count = fod->vertexCount;
				state.entry = pick.entry;
				if (pick.recoveredGeneration &&
					pick.recoveredGeneration->base.size() == fod->vertexCount) {
					state.base = pick.recoveredGeneration->base;
					spdlog::info(
						"PW shared face morph: recovered historical {} generation on recaptured TRI {} target=({:.3f},{:.3f},{:.3f})",
						female ? "female" : "male",
						pick.entry->path.string(),
						pick.recoveredGeneration->target.thin,
						pick.recoveredGeneration->target.muscular,
						pick.recoveredGeneration->target.heavy);
				} else {
					state.base.assign(fod->vertexData, fod->vertexData + fod->vertexCount);
				}
				if (!pick.recoveredGeneration && pick.removePreviousTarget) {
					const auto previousDeltas = ResolveTargetDeltas(
						pick.entry->tri,
						pick.previousTarget);
					RemoveTarget(state.base, pick.entry->tri, previousDeltas);
					spdlog::info(
						"PW shared face morph: normalized residual {} target on recaptured TRI {} rawDistance={:.6f} normalizedDistance={:.6f}",
						female ? "female" : "male",
						pick.entry->path.string(),
						pick.rawDistance,
						pick.distance);
				}
				stateIt = _impl->buffers.insert_or_assign(key, std::move(state)).first;
			} else if (stateIt->second.hasAppliedFingerprint &&
				Fingerprint(fod->vertexData, fod->vertexCount, true) !=
					stateIt->second.lastAppliedFingerprint) {
				// RaceMenu/FaceGen can rebuild the FOD in place, preserving both
				// the address and the vertex count.  The old base would then be
				// mixed with the new face and can produce extreme spikes.  Treat
				// changed contents as a new scene. It may still contain the last
				// target used by this sex, so normalize it before accepting it as
				// the new clean base.
				if (canRecoverInteractiveEdit) {
					const float previousBaseDistance = CentredMeanDistance(
						fod->vertexData,
						stateIt->second.base);
					const float previousOutputDistance = CentredMeanDistance(
						fod->vertexData,
						stateIt->second.output);
					const bool containsPriorTarget =
						stateIt->second.hasAppliedTarget &&
						HasMorphStrength(stateIt->second.appliedTarget) &&
						mps::IsMaterialFaceResidualImprovement(
							previousBaseDistance,
							previousOutputDistance);
					stateIt->second.base.assign(
						fod->vertexData,
						fod->vertexData + fod->vertexCount);
					if (containsPriorTarget) {
						const auto previousDeltas = ResolveTargetDeltas(
							pick.entry->tri,
							stateIt->second.appliedTarget);
						RemoveTarget(stateIt->second.base, pick.entry->tri, previousDeltas);
					}
					spdlog::info(
						"PW shared face morph: adopted tracked RaceMenu edit TRI {} priorTargetRemoved={} baseDistance={:.6f} outputDistance={:.6f}",
						pick.entry->path.string(),
						containsPriorTarget ? 1 : 0,
						previousBaseDistance,
						previousOutputDistance);
				} else if (pick.recoveredGeneration &&
					pick.recoveredGeneration->base.size() == fod->vertexCount) {
					stateIt->second.base = pick.recoveredGeneration->base;
					spdlog::info(
						"PW shared face morph: recovered historical {} generation after in-place rebuild TRI {} target=({:.3f},{:.3f},{:.3f})",
						female ? "female" : "male",
						pick.entry->path.string(),
						pick.recoveredGeneration->target.thin,
						pick.recoveredGeneration->target.muscular,
						pick.recoveredGeneration->target.heavy);
				} else {
					stateIt->second.base.assign(fod->vertexData, fod->vertexData + fod->vertexCount);
				}
				if (!canRecoverInteractiveEdit &&
					!pick.recoveredGeneration && pick.removePreviousTarget) {
					const auto previousDeltas = ResolveTargetDeltas(
						pick.entry->tri,
						pick.previousTarget);
					RemoveTarget(stateIt->second.base, pick.entry->tri, previousDeltas);
					spdlog::info(
						"PW shared face morph: normalized residual {} target after in-place rebuild TRI {} rawDistance={:.6f} normalizedDistance={:.6f}",
						female ? "female" : "male",
						pick.entry->path.string(),
						pick.rawDistance,
						pick.distance);
				}
				stateIt->second.hasAppliedFingerprint = false;
			} else {
				_impl->Restore(stateIt->second);
			}
			const auto targetDeltas = ResolveTargetDeltas(pick.entry->tri, target);
			AddTarget(fod->vertexData, fod->vertexCount, pick.entry->tri, targetDeltas);
			stateIt->second.entry = pick.entry;
			stateIt->second.output.assign(
				fod->vertexData,
				fod->vertexData + fod->vertexCount);
			stateIt->second.appliedTarget = target;
			stateIt->second.hasAppliedTarget = true;
			stateIt->second.lastAppliedFingerprint = Fingerprint(
				fod->vertexData,
				fod->vertexCount,
				true);
			stateIt->second.hasAppliedFingerprint = true;
			auto& geometryHistory =
				_impl->geometryHistory[sexIndex][pick.entry->path.generic_string()];
			Impl::FaceGeneration generation;
			generation.target = target;
			generation.base = stateIt->second.base;
			generation.output.assign(
				fod->vertexData,
				fod->vertexData + fod->vertexCount);
			generation.baseFingerprint = generation.base.empty() ?
				0 : Fingerprint(generation.base.data(), static_cast<std::uint32_t>(generation.base.size()), true);
			generation.outputFingerprint = stateIt->second.lastAppliedFingerprint;
			generation.lastTouched = ++_impl->historyGeneration;
			const bool sameAsLatest =
				!geometryHistory.generations.empty() &&
				geometryHistory.generations.back().target == generation.target &&
				geometryHistory.generations.back().baseFingerprint == generation.baseFingerprint &&
				geometryHistory.generations.back().outputFingerprint == generation.outputFingerprint;
			if (sameAsLatest) {
				geometryHistory.generations.back().lastTouched = generation.lastTouched;
			} else {
				geometryHistory.generations.push_back(std::move(generation));
				while (geometryHistory.generations.size() > kMaxFaceGenerationsPerGeometry) {
					geometryHistory.generations.pop_front();
				}
			}
			geometryHistory.lastTouched = _impl->historyGeneration;
			_impl->PruneGeometryHistory(sexIndex);
			appliedAny = true;
			return RE::BSVisit::BSVisitControl::kContinue;
		});

		for (auto it = _impl->buffers.begin(); it != _impl->buffers.end();) {
			if (!visited.contains(it->first)) it = _impl->buffers.erase(it);
			else ++it;
		}
		if (appliedAny) {
			_impl->sexHistory[sexIndex].target = target;
			_impl->sexHistory[sexIndex].hasTarget = true;
			_impl->recoverInteractiveEdit = false;
		}
		return appliedAny;
	}
}
