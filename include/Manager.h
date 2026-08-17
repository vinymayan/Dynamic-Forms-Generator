#pragma once

#include "FormData.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Manager {
    struct ExternalPatchFieldView
    {
        std::string field;
        std::string inheritedValue;
        std::string resolvedValue;
        bool conflict{ false };
        bool arrayValue{ false };
        std::string operation;
    };

    constexpr const char* MOD_DIR = "Data/Viny Mods/Dynamic Forms Generator";
    constexpr const char* FORMS_DIR = "Data/Viny Mods/Dynamic Forms Generator/Forms";
    constexpr const char* PACKAGES_DIR = "Data/Viny Mods/Dynamic Forms Generator/Packages";
    constexpr const char* DEFAULT_PACKAGE_NAME = "Local Forms";
    constexpr const char* LANG_PATH = "Data/Viny Mods/Dynamic Forms Generator/Language.json";
    constexpr const char* DPF_OWNER = "DynamicFormsGenerator";

    std::vector<DynamicForms::DynamicForm>& GetForms();
    void InstallHooks();
    void LoadForms();
    bool SaveForm(const DynamicForms::DynamicForm& form);
    bool SaveForm(std::size_t index, bool dispatchUpdate = true);
    bool SaveAllForms(bool dispatchUpdate = true);
    bool AddForm(const DynamicForms::DynamicForm& form);
    bool AddExternalPatch(
        const DynamicForms::FormRef& target,
        DynamicForms::FormKind kind,
        std::string_view packageName);
    bool AddExternalPatchLayer(std::size_t index, std::string_view packageName);
    bool SupportsExternalPatch(DynamicForms::FormKind kind);
    std::vector<ExternalPatchFieldView> GetExternalPatchFieldViews(std::size_t index);
    bool SetExternalPatchArrayOperation(
        std::size_t index,
        std::string_view field,
        std::string_view operation);
    std::int32_t GetPackagePriority(std::string_view packageName);
    bool SetPackagePriority(std::string_view packageName, std::int32_t priority);
    bool PopulateFormFromGameTemplate(DynamicForms::DynamicForm& form, const DynamicForms::FormRef& templateRef);
    bool ValidateForm(const DynamicForms::DynamicForm& form, std::vector<std::string>& errors);
    const char* GetListTypeName(DynamicForms::FormKind kind);
    const char* GetStoredFormKindName(DynamicForms::FormKind kind);
    bool UpdateForm(std::size_t index, const DynamicForms::DynamicForm& form);
    bool DeleteForm(std::size_t index);
    bool AssignFormToPackage(std::string_view editorId, std::string_view packageName, bool save = true);
    bool AddPatchLayer(std::string_view editorId, std::string_view packageName, bool save = true);
    bool AddFormToPlayerInventory(std::size_t index);
    bool SpawnFormAtPlayer(std::size_t index);
    bool SpawnLydiaForDebug();
    bool PlaySoundPreview(std::size_t index);
    bool PlaySoundPreviewOnPlayer(std::size_t index);
    bool StopSoundPreview();
    bool IsSoundPreviewPlaying(std::string_view editorId);
    bool HasEditorId(std::string_view editorId);
    bool IsEditorIdReserved(std::string_view editorId);
    bool IsDirty(std::size_t index);
    bool HasDirtyForms();
    bool IsReady() noexcept;
    void ApplyAllForms();
}
