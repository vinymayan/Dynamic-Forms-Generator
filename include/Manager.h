#pragma once

#include "FormData.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace Manager {
    constexpr const char* MOD_DIR = "Data/Viny Mods/Dynamic Forms Generator";
    constexpr const char* FORMS_DIR = "Data/Viny Mods/Dynamic Forms Generator/Forms";
    constexpr const char* LANG_PATH = "Data/Viny Mods/Dynamic Forms Generator/Language.json";
    constexpr const char* DPF_OWNER = "DynamicFormsGenerator";

    std::vector<DynamicForms::DynamicForm>& GetForms();
    void LoadForms();
    bool SaveForm(const DynamicForms::DynamicForm& form);
    bool SaveAllForms();
    bool AddForm(const DynamicForms::DynamicForm& form);
    bool UpdateForm(std::size_t index, const DynamicForms::DynamicForm& form);
    bool DeleteForm(std::size_t index);
    bool HasEditorId(std::string_view editorId);
    void ApplyAllForms();
}
