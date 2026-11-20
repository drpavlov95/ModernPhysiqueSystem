#include "EmbeddedSwfPatch.h"

#include <Windows.h>

#include "../resources/resource.h"

namespace mps
{
    std::span<const std::byte> GetEmbeddedRaceMenuSwfPatch(RaceMenuSwfVariant variant) noexcept
    {
        HMODULE module = nullptr;
        const auto address = reinterpret_cast<LPCWSTR>(&GetEmbeddedRaceMenuSwfPatch);
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                address,
                &module)) {
            return {};
        }

        const auto resourceId = variant == RaceMenuSwfVariant::AE ? IDR_RACEMENU_AE_SWF_PATCH :
            variant == RaceMenuSwfVariant::SEInstalled ? IDR_RACEMENU_INSTALLED_SWF_PATCH : IDR_RACEMENU_SWF_PATCH;
        const HRSRC resource = FindResourceW(
            module,
            MAKEINTRESOURCEW(resourceId),
            RT_RCDATA);
        if (!resource) {
            return {};
        }

        const DWORD size = SizeofResource(module, resource);
        const HGLOBAL loaded = LoadResource(module, resource);
        const void* bytes = loaded ? LockResource(loaded) : nullptr;
        if (!bytes || size == 0) {
            return {};
        }

        return {
            static_cast<const std::byte*>(bytes),
            static_cast<std::size_t>(size)
        };
    }
}
