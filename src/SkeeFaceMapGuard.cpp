#include "SkeeFaceMapGuard.h"

#include "RaceMenuInterop.h"

#include <Windows.h>
#include <bcrypt.h>
#include <winver.h>

#include <array>
#include <filesystem>
#include <vector>

#include <spdlog/spdlog.h>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "version.lib")

namespace mps
{
    namespace
    {
        using CreateSliderListRawFn =
            void* (__fastcall*)(void*, RE::TESRace*, std::uint8_t);

        constexpr std::uintptr_t kCreateSliderListRva = 0x5F060;
        constexpr std::uintptr_t kFaceMorphObjectRva = 0x1CAB10;
        constexpr std::uintptr_t kFaceMorphVtableRva = 0x174170;
        constexpr std::uintptr_t kFaceMorphGetRva = 0x59B20;
        constexpr std::uintptr_t kFaceMorphSetRva = 0x59C20;
        constexpr std::size_t kExpectedSkeeImageSize = 0x1E4000;
        constexpr std::uint32_t kExpectedSkeeTimestamp = 0x5F799CB1;
        constexpr std::uint16_t kExpectedSkeeFixedVersionMajor = 3;
        constexpr std::uint16_t kExpectedSkeeFixedVersionMinor = 4;
        constexpr std::uint16_t kExpectedSkeeFixedVersionBuild = 5;
        constexpr std::uint16_t kExpectedSkeeFixedVersionPrivate = 0;

        constexpr std::array<std::uint8_t, 30> kCreateSliderListSignature{
            0x48, 0x89, 0x5C, 0x24, 0x20,
            0x44, 0x88, 0x44, 0x24, 0x18,
            0x48, 0x89, 0x54, 0x24, 0x10,
            0x55, 0x56, 0x57,
            0x48, 0x8D, 0x6C, 0x24, 0xB9,
            0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00
        };

        constexpr std::array<std::uint8_t, 32> kExpectedSkeeSha256{
            0x25, 0x5C, 0x0D, 0xB0, 0xBA, 0x5F, 0xF1, 0x46,
            0x40, 0xCC, 0x6D, 0x75, 0xCC, 0xDD, 0xFB, 0x24,
            0x47, 0x4B, 0x49, 0x8D, 0x35, 0xB7, 0x7F, 0x31,
            0x40, 0xCC, 0xB0, 0x5E, 0xB8, 0x0E, 0x72, 0x73
        };

        struct FixedFileVersion
        {
            std::uint16_t major = 0;
            std::uint16_t minor = 0;
            std::uint16_t build = 0;
            std::uint16_t privatePart = 0;
        };

        [[nodiscard]] bool GetModuleImageSize(
            HMODULE module,
            std::size_t& outSize,
            std::uint32_t& outTimestamp) noexcept
        {
            if (!module) {
                return false;
            }

            __try {
                const auto* dos =
                    reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
                if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
                    dos->e_lfanew <= 0) {
                    return false;
                }
                const auto* nt =
                    reinterpret_cast<const IMAGE_NT_HEADERS*>(
                        reinterpret_cast<const std::uint8_t*>(module) +
                        dos->e_lfanew);
                if (nt->Signature != IMAGE_NT_SIGNATURE ||
                    nt->OptionalHeader.SizeOfImage == 0) {
                    return false;
                }
                outSize = nt->OptionalHeader.SizeOfImage;
                outTimestamp = nt->FileHeader.TimeDateStamp;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
            return true;
        }

        [[nodiscard]] bool MemoryMatchesSEH(
            const std::uint8_t* memory,
            const std::uint8_t* expected,
            std::size_t size) noexcept
        {
            __try {
                for (std::size_t index = 0; index < size; ++index) {
                    if (memory[index] != expected[index]) {
                        return false;
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ReadObjectVtableSEH(
            void* object,
            void** outVtable) noexcept
        {
            __try {
                *outVtable = *reinterpret_cast<void**>(object);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                *outVtable = nullptr;
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ReadVtableSlotsSEH(
            void* vtable,
            std::size_t firstSlot,
            void** outFirst,
            std::size_t secondSlot,
            void** outSecond) noexcept
        {
            __try {
                auto** slots = static_cast<void**>(vtable);
                *outFirst = slots[firstSlot];
                *outSecond = slots[secondSlot];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                *outFirst = nullptr;
                *outSecond = nullptr;
                return false;
            }
            return true;
        }

        [[nodiscard]] bool InvokeCreateSliderListSEH(
            CreateSliderListRawFn function,
            void* faceMorphObject,
            RE::TESRace* race,
            std::uint8_t sex,
            void** outList,
            std::uint32_t* outExceptionCode) noexcept
        {
            *outList = nullptr;
            *outExceptionCode = 0;
            __try {
                *outList = function(faceMorphObject, race, sex);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                *outExceptionCode =
                    static_cast<std::uint32_t>(GetExceptionCode());
                return false;
            }
            return true;
        }

        [[nodiscard]] bool GetModulePath(
            HMODULE module,
            std::filesystem::path& outPath)
        {
            std::wstring buffer(32768, L'\0');
            const DWORD length = GetModuleFileNameW(
                module,
                buffer.data(),
                static_cast<DWORD>(buffer.size()));
            if (length == 0 || length >= buffer.size()) {
                return false;
            }
            buffer.resize(length);
            outPath = std::move(buffer);
            return true;
        }

        [[nodiscard]] bool TryReadFixedFileVersion(
            const std::filesystem::path& path,
            FixedFileVersion& outVersion)
        {
            DWORD ignored = 0;
            const DWORD size = GetFileVersionInfoSizeW(
                path.c_str(),
                std::addressof(ignored));
            if (size == 0) {
                return false;
            }

            std::vector<std::uint8_t> data(size);
            if (!GetFileVersionInfoW(
                    path.c_str(),
                    0,
                    size,
                    data.data())) {
                return false;
            }

            VS_FIXEDFILEINFO* info = nullptr;
            UINT infoSize = 0;
            if (!VerQueryValueW(
                    data.data(),
                    L"\\",
                    reinterpret_cast<void**>(std::addressof(info)),
                    std::addressof(infoSize)) ||
                !info ||
                infoSize < sizeof(VS_FIXEDFILEINFO) ||
                info->dwSignature != 0xFEEF04BD) {
                return false;
            }

            outVersion.major = HIWORD(info->dwFileVersionMS);
            outVersion.minor = LOWORD(info->dwFileVersionMS);
            outVersion.build = HIWORD(info->dwFileVersionLS);
            outVersion.privatePart = LOWORD(info->dwFileVersionLS);
            return true;
        }

        [[nodiscard]] bool MatchesExpectedFixedFileVersion(
            const FixedFileVersion& version) noexcept
        {
            return version.major == kExpectedSkeeFixedVersionMajor &&
                   version.minor == kExpectedSkeeFixedVersionMinor &&
                   version.build == kExpectedSkeeFixedVersionBuild &&
                   version.privatePart ==
                       kExpectedSkeeFixedVersionPrivate;
        }

        [[nodiscard]] bool ComputeSha256(
            const std::filesystem::path& path,
            std::array<std::uint8_t, 32>& outHash)
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            HANDLE file = INVALID_HANDLE_VALUE;
            std::vector<std::uint8_t> hashObject;
            bool success = false;

            do {
                if (BCryptOpenAlgorithmProvider(
                        std::addressof(algorithm),
                        BCRYPT_SHA256_ALGORITHM,
                        nullptr,
                        0) != 0) {
                    break;
                }

                DWORD objectLength = 0;
                DWORD copied = 0;
                if (BCryptGetProperty(
                        algorithm,
                        BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(
                            std::addressof(objectLength)),
                        sizeof(objectLength),
                        std::addressof(copied),
                        0) != 0 ||
                    copied != sizeof(objectLength) ||
                    objectLength == 0) {
                    break;
                }

                hashObject.resize(objectLength);
                if (BCryptCreateHash(
                        algorithm,
                        std::addressof(hash),
                        hashObject.data(),
                        static_cast<ULONG>(hashObject.size()),
                        nullptr,
                        0,
                        0) != 0) {
                    break;
                }

                file = CreateFileW(
                    path.c_str(),
                    GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                    nullptr);
                if (file == INVALID_HANDLE_VALUE) {
                    break;
                }

                std::array<std::uint8_t, 64 * 1024> buffer{};
                for (;;) {
                    DWORD bytesRead = 0;
                    if (!ReadFile(
                            file,
                            buffer.data(),
                            static_cast<DWORD>(buffer.size()),
                            std::addressof(bytesRead),
                            nullptr)) {
                        break;
                    }
                    if (bytesRead == 0) {
                        success = true;
                        break;
                    }
                    if (BCryptHashData(
                            hash,
                            buffer.data(),
                            bytesRead,
                            0) != 0) {
                        success = false;
                        break;
                    }
                }

                if (!success ||
                    BCryptFinishHash(
                        hash,
                        outHash.data(),
                        static_cast<ULONG>(outHash.size()),
                        0) != 0) {
                    success = false;
                }
            } while (false);

            if (file != INVALID_HANDLE_VALUE) {
                CloseHandle(file);
            }
            if (hash) {
                BCryptDestroyHash(hash);
            }
            if (algorithm) {
                BCryptCloseAlgorithmProvider(algorithm, 0);
            }
            return success;
        }
    }

    SkeeFaceMapGuard& SkeeFaceMapGuard::GetSingleton() noexcept
    {
        static SkeeFaceMapGuard singleton;
        return singleton;
    }

    bool SkeeFaceMapGuard::SetFaceMorphInterface(
        IFaceMorphInterface* faceMorphInterface) noexcept
    {
        if (gameThread_ && *gameThread_ != std::this_thread::get_id()) {
            return false;
        }
        if (faceMorphInterface_ == faceMorphInterface) {
            return true;
        }

        faceMorphInterface_ = faceMorphInterface;
        ResetGate();
        return true;
    }

    bool SkeeFaceMapGuard::AlignMap(
        RE::TESRace* race,
        FaceSex sex,
        std::string_view reason)
    {
        if (!CheckThread(true) ||
            !race ||
            race->GetFormID() == 0 ||
            (sex != FaceSex::Male &&
             sex != FaceSex::Female) ||
            !PrepareExactGate() ||
            !createSliderList_ ||
            !skeeModuleBase_) {
            return false;
        }

        void* result = nullptr;
        std::uint32_t exceptionCode = 0;
        if (!InvokeCreateSliderListSEH(
                createSliderList_,
                faceMorphInterface_,
                race,
                static_cast<std::uint8_t>(sex),
                std::addressof(result),
                std::addressof(exceptionCode))) {
            spdlog::error(
                "SkeeFaceMapGuard CreateSliderList failed exception=0x{:08X} reason={}",
                exceptionCode,
                reason.empty() ? "<none>" : reason);
            gateState_ = GateState::Unavailable;
            createSliderList_ = nullptr;
            return false;
        }
        if (!result) {
            // A missing SliderSet for a transient route is retryable and must
            // not poison the already verified ABI gate.
            spdlog::warn(
                "SkeeFaceMapGuard CreateSliderList returned no list sex={} race={:08X} reason={}",
                sex == FaceSex::Male ? "male" : "female",
                race->GetFormID(),
                reason.empty() ? "<none>" : reason);
            return false;
        }

        const bool changed =
            !hasLastAlignment_ ||
            lastRaceFormId_ != race->GetFormID() ||
            lastSex_ != sex;
        lastRaceFormId_ = race->GetFormID();
        lastSex_ = sex;
        hasLastAlignment_ = true;

        if (changed) {
            spdlog::info(
                "SkeeFaceMapGuard aligned list sex={} race={:08X} reason={}",
                sex == FaceSex::Male ? "male" : "female",
                race->GetFormID(),
                reason.empty() ? "<none>" : reason);
        } else {
            spdlog::debug(
                "SkeeFaceMapGuard refreshed list sex={} race={:08X} reason={}",
                sex == FaceSex::Male ? "male" : "female",
                race->GetFormID(),
                reason.empty() ? "<none>" : reason);
        }
        return true;
    }

    bool SkeeFaceMapGuard::CheckThread(bool bindIfUnset)
    {
        const auto current = std::this_thread::get_id();
        if (!gameThread_) {
            if (!bindIfUnset) {
                return false;
            }
            gameThread_ = current;
            wrongThreadLogged_ = false;
            return true;
        }
        if (*gameThread_ == current) {
            return true;
        }
        if (!wrongThreadLogged_) {
            wrongThreadLogged_ = true;
            spdlog::error(
                "SkeeFaceMapGuard rejected alignment from a non-game thread");
        }
        return false;
    }

    bool SkeeFaceMapGuard::PrepareExactGate()
    {
        if (gateState_ != GateState::Unchecked) {
            return gateState_ == GateState::Available;
        }
        if (!faceMorphInterface_) {
            spdlog::error(
                "SkeeFaceMapGuard gate rejected reason=face_interface_missing");
            return false;
        }

        HMODULE module = GetModuleHandleW(L"skee64.dll");
        std::size_t imageSize = 0;
        std::uint32_t timestamp = 0;
        if (!module ||
            !GetModuleImageSize(module, imageSize, timestamp)) {
            spdlog::error(
                "SkeeFaceMapGuard gate deferred reason=module_or_pe_unavailable");
            return false;
        }

        auto* base = reinterpret_cast<std::uint8_t*>(module);
        if (imageSize != kExpectedSkeeImageSize ||
            timestamp != kExpectedSkeeTimestamp ||
            imageSize <= kCreateSliderListRva ||
            imageSize <= kFaceMorphObjectRva ||
            imageSize <= kFaceMorphVtableRva ||
            imageSize <= kFaceMorphGetRva ||
            imageSize <= kFaceMorphSetRva) {
            gateState_ = GateState::Unavailable;
            spdlog::error(
                "SkeeFaceMapGuard gate rejected reason=pe_identity_mismatch imageSize=0x{:X} timestamp=0x{:08X}",
                imageSize,
                timestamp);
            return false;
        }
        if (faceMorphInterface_ !=
            reinterpret_cast<IFaceMorphInterface*>(
                base + kFaceMorphObjectRva)) {
            gateState_ = GateState::Unavailable;
            spdlog::error(
                "SkeeFaceMapGuard gate rejected reason=face_object_mismatch");
            return false;
        }

        void* vtable = nullptr;
        void* getSlot = nullptr;
        void* setSlot = nullptr;
        if (!ReadObjectVtableSEH(
                faceMorphInterface_,
                std::addressof(vtable)) ||
            vtable != base + kFaceMorphVtableRva ||
            !ReadVtableSlotsSEH(
                vtable,
                5,
                std::addressof(getSlot),
                6,
                std::addressof(setSlot)) ||
            getSlot != base + kFaceMorphGetRva ||
            setSlot != base + kFaceMorphSetRva) {
            gateState_ = GateState::Unavailable;
            spdlog::error(
                "SkeeFaceMapGuard gate rejected reason=face_abi_mismatch");
            return false;
        }

        std::filesystem::path modulePath;
        if (!GetModulePath(module, modulePath)) {
            spdlog::error(
                "SkeeFaceMapGuard gate deferred reason=module_path_unavailable");
            return false;
        }

        FixedFileVersion actualVersion{};
        if (!TryReadFixedFileVersion(modulePath, actualVersion)) {
            spdlog::error(
                "SkeeFaceMapGuard gate deferred reason=fixed_file_version_unreadable");
            return false;
        }
        if (!MatchesExpectedFixedFileVersion(actualVersion)) {
            gateState_ = GateState::Unavailable;
            spdlog::error(
                "SkeeFaceMapGuard gate rejected reason=fixed_file_version_mismatch actual={}.{}.{}.{} expected={}.{}.{}.{}",
                actualVersion.major,
                actualVersion.minor,
                actualVersion.build,
                actualVersion.privatePart,
                kExpectedSkeeFixedVersionMajor,
                kExpectedSkeeFixedVersionMinor,
                kExpectedSkeeFixedVersionBuild,
                kExpectedSkeeFixedVersionPrivate);
            return false;
        }

        std::array<std::uint8_t, 32> actualHash{};
        if (!ComputeSha256(modulePath, actualHash)) {
            spdlog::error(
                "SkeeFaceMapGuard gate deferred reason=sha256_unreadable");
            return false;
        }
        if (actualHash != kExpectedSkeeSha256 ||
            !MemoryMatchesSEH(
                base + kCreateSliderListRva,
                kCreateSliderListSignature.data(),
                kCreateSliderListSignature.size())) {
            gateState_ = GateState::Unavailable;
            spdlog::error(
                "SkeeFaceMapGuard gate rejected reason=hash_or_signature_mismatch");
            return false;
        }

        skeeModuleBase_ = module;
        createSliderList_ =
            reinterpret_cast<CreateSliderListFn>(
                base + kCreateSliderListRva);
        gateState_ = GateState::Available;
        spdlog::info(
            "SkeeFaceMapGuard exact SKEE CreateSliderList shim enabled");
        return true;
    }

    void SkeeFaceMapGuard::ResetGate() noexcept
    {
        gateState_ = GateState::Unchecked;
        createSliderList_ = nullptr;
        skeeModuleBase_ = nullptr;
        hasLastAlignment_ = false;
    }
}
