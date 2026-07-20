#pragma once

#include "FormData.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace Manager {
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
    bool PopulateFormFromGameTemplate(DynamicForms::DynamicForm& form, const DynamicForms::FormRef& templateRef);
    const char* GetListTypeName(DynamicForms::FormKind kind);
    bool UpdateForm(std::size_t index, const DynamicForms::DynamicForm& form);
    bool DeleteForm(std::size_t index);
    bool AssignFormToPackage(std::string_view editorId, std::string_view packageName, bool save = true);
    bool AddPatchLayer(std::string_view editorId, std::string_view packageName, bool save = true);
    bool AddFormToPlayerInventory(std::size_t index);
    bool SpawnFormAtPlayer(std::size_t index);
    bool SpawnLydiaForDebug();
    bool HasEditorId(std::string_view editorId);
    bool IsDirty(std::size_t index);
    bool HasDirtyForms();
    void ApplyAllForms();
}
