#pragma once

#include "RE/Skyrim.h"
#include <Windows.h>
#include <cstdint>

namespace DPF {
    constexpr auto InterfaceName = "DynamicPersistentForms";
    constexpr uint32_t InterfaceVersion = 1;

    class IDynamicPersistentForms {
    public:
        virtual ~IDynamicPersistentForms() = default;

        virtual uint32_t GetVersion() const = 0;

        virtual RE::TESForm* Create(RE::TESForm* baseItem) = 0;
        virtual RE::TESForm* CreateByType(uint32_t formType) = 0;

        virtual RE::TESForm* GetOrCreateByLocalId(uint32_t localId, uint32_t formType) = 0;
        virtual RE::TESForm* GetOrCreateByFormId(RE::FormID formId, uint32_t formType) = 0;

        virtual RE::TESForm* GetOrCreateByOwnerKey(const char* owner, const char* key, uint32_t formType,
            uint32_t* localId, bool* existed) = 0;
        virtual bool ReleaseByOwnerKey(const char* owner, const char* key) = 0;
        virtual bool ReleaseByLocalId(uint32_t localId, const char* owner) = 0;
        virtual uint32_t ReleaseOwner(const char* owner) = 0;
    };

    using GetDPFAPI = void* (*)();

    inline IDynamicPersistentForms* GetAPI() {
        const auto module = GetModuleHandleA("DPF.dll");
        if (!module) {
            return nullptr;
        }

        const auto getter = reinterpret_cast<GetDPFAPI>(GetProcAddress(module, "GetDPFAPI"));
        if (!getter) {
            return nullptr;
        }

        auto* api = static_cast<IDynamicPersistentForms*>(getter());
        if (!api || api->GetVersion() != InterfaceVersion) {
            return nullptr;
        }

        return api;
    }
}
