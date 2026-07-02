#include "Configuration.h"

#include "ListManager.h"
#include "Manager.h"
#include "logger.h"

#include <SKSEMCP/SKSEMenuFramework.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <format>
#include <fstream>
#include <vector>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <string>
#include <unordered_map>

namespace {
    namespace ImGui = ImGuiMCP;

    std::unordered_map<std::string, std::string> language;
    std::array<char, 128> editorIdBuffer{};
    std::array<char, 128> filterEditorIdBuffer{};
    int selectedFormKind = 0;
    int selectedFilterKind = 0;
    int selectedGlobalType = 2;
    int defaultIntValue = 0;
    float defaultFloatValue = 0.0F;
    std::array<char, 256> createNameBuffer{};
    std::array<char, 256> createModelBuffer{};
    std::array<char, 256> createRaceMorphBuffer{};
    std::array<char, 256> createDefaultMorphBuffer{};
    std::array<char, 256> createChargenMorphBuffer{};
    int selectedArtType = 0;
    int selectedHeadPartType = 0;
    int createColor[4]{ 255, 255, 255, 0 };
    bool createPlayable = true;
    bool createHeadPartMale = false;
    bool createHeadPartFemale = false;
    bool createHeadPartIsExtraPart = false;
    bool createHeadPartUseSolidTint = false;
    int pendingDeleteIndex = -1;
    std::unordered_map<std::string, std::string> outfitPieceFilters;
    std::unordered_map<std::string, std::string> formPickerFilters;
    std::string createError;
    std::string deleteError;
    bool requestDeletePopup = false;

    constexpr auto DELETE_POPUP_ID = "Delete Form##dynamic_forms_delete_popup";
    constexpr std::array FORM_KIND_ITEMS{ "Global", "Keyword", "Outfit", "Color", "Art Object", "Perk", "Head Part", "Sound Description", "Light", "Explosion", "Activator" };
    constexpr std::array FILTER_KIND_ITEMS{ "All", "Global", "Keyword", "Outfit", "Color", "Art Object", "Perk", "Head Part", "Sound Description", "Light", "Explosion", "Activator" };
    constexpr std::array GLOBAL_TYPE_ITEMS{ "short", "long", "float" };
    constexpr std::array ART_TYPE_ITEMS{ "MagicCasting", "MagicHitEffect", "MagicEnchantEffect" };
    constexpr std::array HEAD_PART_TYPE_ITEMS{ "Misc", "Face", "Eyes", "Hair", "FacialHair", "Scar", "Eyebrows" };
    constexpr std::array CONDITION_KIND_ITEMS{ "Raw", "GetGlobalValue", "GetActorValue", "GetBaseActorValue", "HasPerk", "GetQuestCompleted", "HasSpell" };
    constexpr std::array CONDITION_OP_ITEMS{ "==", "!=", ">", ">=", "<", "<=" };

    void AddLocValue(const std::string& prefix, const rapidjson::Value& value) {
        if (value.IsString()) {
            language[prefix] = value.GetString();
            return;
        }

        if (!value.IsObject()) {
            return;
        }

        for (auto itr = value.MemberBegin(); itr != value.MemberEnd(); ++itr) {
            const auto key = prefix.empty() ? itr->name.GetString() : prefix + "." + itr->name.GetString();
            AddLocValue(key, itr->value);
        }
    }

    bool IsValidEditorId(const std::string_view editorId) {
        if (editorId.empty()) {
            return false;
        }

        return std::ranges::all_of(editorId, [](const unsigned char c) {
            return std::isalnum(c) != 0 || c == '_';
        });
    }

    DynamicForms::GlobalType SelectedGlobalType() {
        switch (selectedGlobalType) {
        case 0:
            return DynamicForms::GlobalType::Short;
        case 1:
            return DynamicForms::GlobalType::Long;
        case 2:
        default:
            return DynamicForms::GlobalType::Float;
        }
    }

    DynamicForms::FormKind SelectedFormKind() {
        switch (selectedFormKind) {
        case 1:
            return DynamicForms::FormKind::Keyword;
        case 2:
            return DynamicForms::FormKind::Outfit;
        case 3:
            return DynamicForms::FormKind::Color;
        case 4:
            return DynamicForms::FormKind::ArtObject;
        case 5:
            return DynamicForms::FormKind::Perk;
        case 6:
            return DynamicForms::FormKind::HeadPart;
        case 7:
            return DynamicForms::FormKind::SoundDescriptor;
        case 8:
            return DynamicForms::FormKind::Light;
        case 9:
            return DynamicForms::FormKind::Explosion;
        case 10:
            return DynamicForms::FormKind::Activator;
        case 0:
        default:
            return DynamicForms::FormKind::Global;
        }
    }

    DynamicForms::ArtObjectType SelectedArtType() {
        switch (selectedArtType) {
        case 1:
            return DynamicForms::ArtObjectType::MagicHitEffect;
        case 2:
            return DynamicForms::ArtObjectType::MagicEnchantEffect;
        case 0:
        default:
            return DynamicForms::ArtObjectType::MagicCasting;
        }
    }

    DynamicForms::HeadPartType SelectedHeadPartType() {
        switch (selectedHeadPartType) {
        case 1:
            return DynamicForms::HeadPartType::Face;
        case 2:
            return DynamicForms::HeadPartType::Eyes;
        case 3:
            return DynamicForms::HeadPartType::Hair;
        case 4:
            return DynamicForms::HeadPartType::FacialHair;
        case 5:
            return DynamicForms::HeadPartType::Scar;
        case 6:
            return DynamicForms::HeadPartType::Eyebrows;
        case 0:
        default:
            return DynamicForms::HeadPartType::Misc;
        }
    }

    float SelectedDefaultValue() {
        if (SelectedGlobalType() == DynamicForms::GlobalType::Float) {
            return defaultFloatValue;
        }
        return static_cast<float>(defaultIntValue);
    }

    int GlobalTypeIndex(const DynamicForms::GlobalType type) {
        switch (type) {
        case DynamicForms::GlobalType::Short:
            return 0;
        case DynamicForms::GlobalType::Long:
            return 1;
        case DynamicForms::GlobalType::Float:
        default:
            return 2;
        }
    }

    DynamicForms::GlobalType GlobalTypeFromIndex(const int index) {
        switch (index) {
        case 0:
            return DynamicForms::GlobalType::Short;
        case 1:
            return DynamicForms::GlobalType::Long;
        case 2:
        default:
            return DynamicForms::GlobalType::Float;
        }
    }

    const char* FormKindLabel(const DynamicForms::FormKind kind) {
        switch (kind) {
        case DynamicForms::FormKind::Keyword:
            return "Keyword";
        case DynamicForms::FormKind::Outfit:
            return "Outfit";
        case DynamicForms::FormKind::Color:
            return "Color";
        case DynamicForms::FormKind::ArtObject:
            return "Art Object";
        case DynamicForms::FormKind::Perk:
            return "Perk";
        case DynamicForms::FormKind::HeadPart:
            return "Head Part";
        case DynamicForms::FormKind::SoundDescriptor:
            return "Sound Description";
        case DynamicForms::FormKind::Light:
            return "Light";
        case DynamicForms::FormKind::Explosion:
            return "Explosion";
        case DynamicForms::FormKind::Activator:
            return "Activator";
        case DynamicForms::FormKind::Global:
        default:
            return "Global";
        }
    }

    int ArtTypeIndex(const DynamicForms::ArtObjectType type) {
        switch (type) {
        case DynamicForms::ArtObjectType::MagicHitEffect:
            return 1;
        case DynamicForms::ArtObjectType::MagicEnchantEffect:
            return 2;
        case DynamicForms::ArtObjectType::MagicCasting:
        default:
            return 0;
        }
    }

    DynamicForms::ArtObjectType ArtTypeFromIndex(const int index) {
        switch (index) {
        case 1:
            return DynamicForms::ArtObjectType::MagicHitEffect;
        case 2:
            return DynamicForms::ArtObjectType::MagicEnchantEffect;
        case 0:
        default:
            return DynamicForms::ArtObjectType::MagicCasting;
        }
    }

    int HeadPartTypeIndex(const DynamicForms::HeadPartType type) {
        return static_cast<int>(type);
    }

    DynamicForms::HeadPartType HeadPartTypeFromIndex(const int index) {
        if (index >= 0 && index < static_cast<int>(HEAD_PART_TYPE_ITEMS.size())) {
            return static_cast<DynamicForms::HeadPartType>(index);
        }
        return DynamicForms::HeadPartType::Misc;
    }

    int ConditionKindIndex(const DynamicForms::PerkConditionKind kind) {
        const auto index = static_cast<int>(kind);
        return index >= 0 && index < static_cast<int>(CONDITION_KIND_ITEMS.size()) ? index : 0;
    }

    DynamicForms::PerkConditionKind ConditionKindFromIndex(const int index) {
        if (index >= 0 && index < static_cast<int>(CONDITION_KIND_ITEMS.size())) {
            return static_cast<DynamicForms::PerkConditionKind>(index);
        }
        return DynamicForms::PerkConditionKind::Raw;
    }

    std::string ToLower(std::string value) {
        std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    bool MatchesFilters(const DynamicForms::DynamicForm& form) {
        if (selectedFilterKind == 1 && form.kind != DynamicForms::FormKind::Global) {
            return false;
        }
        if (selectedFilterKind == 2 && form.kind != DynamicForms::FormKind::Keyword) {
            return false;
        }
        if (selectedFilterKind == 3 && form.kind != DynamicForms::FormKind::Outfit) {
            return false;
        }
        if (selectedFilterKind == 4 && form.kind != DynamicForms::FormKind::Color) {
            return false;
        }
        if (selectedFilterKind == 5 && form.kind != DynamicForms::FormKind::ArtObject) {
            return false;
        }
        if (selectedFilterKind == 6 && form.kind != DynamicForms::FormKind::Perk) {
            return false;
        }
        if (selectedFilterKind == 7 && form.kind != DynamicForms::FormKind::HeadPart) {
            return false;
        }
        if (selectedFilterKind == 8 && form.kind != DynamicForms::FormKind::SoundDescriptor) {
            return false;
        }
        if (selectedFilterKind == 9 && form.kind != DynamicForms::FormKind::Light) {
            return false;
        }
        if (selectedFilterKind == 10 && form.kind != DynamicForms::FormKind::Explosion) {
            return false;
        }
        if (selectedFilterKind == 11 && form.kind != DynamicForms::FormKind::Activator) {
            return false;
        }

        const std::string filter = filterEditorIdBuffer.data();
        if (filter.empty()) {
            return true;
        }

        return ToLower(form.editorId).find(ToLower(filter)) != std::string::npos;
    }

    void ResetCreateState() {
        editorIdBuffer.fill('\0');
        selectedFormKind = 0;
        selectedGlobalType = 2;
        defaultIntValue = 0;
        defaultFloatValue = 0.0F;
        createNameBuffer.fill('\0');
        createModelBuffer.fill('\0');
        createRaceMorphBuffer.fill('\0');
        createDefaultMorphBuffer.fill('\0');
        createChargenMorphBuffer.fill('\0');
        selectedArtType = 0;
        selectedHeadPartType = 0;
        createColor[0] = 255;
        createColor[1] = 255;
        createColor[2] = 255;
        createColor[3] = 0;
        createPlayable = true;
        createHeadPartMale = false;
        createHeadPartFemale = false;
        createHeadPartIsExtraPart = false;
        createHeadPartUseSolidTint = false;
        createError.clear();
    }

    bool InputString(const char* label, std::string& value, const float width = 320.0F) {
        std::array<char, 512> buffer{};
        strcpy_s(buffer.data(), buffer.size(), value.c_str());
        ImGui::SetNextItemWidth(width);
        if (ImGui::InputText(label, buffer.data(), buffer.size())) {
            value = buffer.data();
            return true;
        }
        return false;
    }

    bool DrawRGBColorEditor(const char* label, std::uint8_t& red, std::uint8_t& green, std::uint8_t& blue) {
        float color[3]{
            static_cast<float>(red) / 255.0F,
            static_cast<float>(green) / 255.0F,
            static_cast<float>(blue) / 255.0F
        };
        ImGui::SetNextItemWidth(260.0F);
        if (ImGui::ColorEdit3(label, color, ImGui::ImGuiColorEditFlags_DisplayRGB | ImGui::ImGuiColorEditFlags_Uint8 | ImGui::ImGuiColorEditFlags_InputRGB)) {
            red = static_cast<std::uint8_t>(std::clamp(color[0], 0.0F, 1.0F) * 255.0F);
            green = static_cast<std::uint8_t>(std::clamp(color[1], 0.0F, 1.0F) * 255.0F);
            blue = static_cast<std::uint8_t>(std::clamp(color[2], 0.0F, 1.0F) * 255.0F);
            return true;
        }
        return false;
    }

    bool DrawRGBAColorEditor(const char* label, std::uint8_t& red, std::uint8_t& green, std::uint8_t& blue, std::uint8_t& alpha) {
        float color[4]{
            static_cast<float>(red) / 255.0F,
            static_cast<float>(green) / 255.0F,
            static_cast<float>(blue) / 255.0F,
            static_cast<float>(alpha) / 255.0F
        };
        ImGui::SetNextItemWidth(260.0F);
        if (ImGui::ColorEdit4(label, color, ImGui::ImGuiColorEditFlags_DisplayRGB | ImGui::ImGuiColorEditFlags_Uint8 | ImGui::ImGuiColorEditFlags_InputRGB | ImGui::ImGuiColorEditFlags_AlphaBar)) {
            red = static_cast<std::uint8_t>(std::clamp(color[0], 0.0F, 1.0F) * 255.0F);
            green = static_cast<std::uint8_t>(std::clamp(color[1], 0.0F, 1.0F) * 255.0F);
            blue = static_cast<std::uint8_t>(std::clamp(color[2], 0.0F, 1.0F) * 255.0F);
            alpha = static_cast<std::uint8_t>(std::clamp(color[3], 0.0F, 1.0F) * 255.0F);
            return true;
        }
        return false;
    }

    bool RenderCreatePopup(const std::string& editorId) {
        bool created = false;
        bool open = true;
        if (!ImGui::BeginPopupModal(Configuration::GetLoc("menu.create_popup", "Create Form"), &open)) {
            return false;
        }

        ImGui::Text("%s", Configuration::GetLoc("menu.form_type", "Form type"));
        ImGui::SetNextItemWidth(220.0F);
        ImGui::Combo("##formType", &selectedFormKind, FORM_KIND_ITEMS.data(), static_cast<int>(FORM_KIND_ITEMS.size()));

        if (SelectedFormKind() == DynamicForms::FormKind::Global) {
            ImGui::Separator();
            ImGui::Text("%s", Configuration::GetLoc("menu.global_settings", "Global settings"));
            ImGui::SetNextItemWidth(220.0F);
            ImGui::Combo(Configuration::GetLoc("menu.global_type", "Global type"), &selectedGlobalType, GLOBAL_TYPE_ITEMS.data(), static_cast<int>(GLOBAL_TYPE_ITEMS.size()));

            ImGui::SetNextItemWidth(220.0F);
            if (SelectedGlobalType() == DynamicForms::GlobalType::Float) {
                ImGui::InputFloat(Configuration::GetLoc("menu.default_value", "Default value"), &defaultFloatValue);
            } else {
                ImGui::InputInt(Configuration::GetLoc("menu.default_value", "Default value"), &defaultIntValue);
            }
        }
        if (SelectedFormKind() == DynamicForms::FormKind::Color) {
            ImGui::Separator();
            ImGui::Text("%s", Configuration::GetLoc("menu.color_settings", "Color settings"));
            ImGui::SetNextItemWidth(260.0F);
            ImGui::InputText(Configuration::GetLoc("menu.full_name", "Name"), createNameBuffer.data(), createNameBuffer.size());
            auto red = static_cast<std::uint8_t>(std::clamp(createColor[0], 0, 255));
            auto green = static_cast<std::uint8_t>(std::clamp(createColor[1], 0, 255));
            auto blue = static_cast<std::uint8_t>(std::clamp(createColor[2], 0, 255));
            auto alpha = static_cast<std::uint8_t>(std::clamp(createColor[3], 0, 255));
            if (DrawRGBAColorEditor("RGBA", red, green, blue, alpha)) {
                createColor[0] = red;
                createColor[1] = green;
                createColor[2] = blue;
                createColor[3] = alpha;
            }
            ImGui::Checkbox(Configuration::GetLoc("menu.playable", "Playable"), &createPlayable);
        }
        if (SelectedFormKind() == DynamicForms::FormKind::ArtObject) {
            ImGui::Separator();
            ImGui::Text("%s", Configuration::GetLoc("menu.art_object_settings", "Art Object settings"));
            ImGui::SetNextItemWidth(360.0F);
            ImGui::InputText(Configuration::GetLoc("menu.model_path", "Model path"), createModelBuffer.data(), createModelBuffer.size());
            ImGui::SetNextItemWidth(260.0F);
            ImGui::Combo(Configuration::GetLoc("menu.art_type", "Art type"), &selectedArtType, ART_TYPE_ITEMS.data(), static_cast<int>(ART_TYPE_ITEMS.size()));
        }
        if (SelectedFormKind() == DynamicForms::FormKind::Perk) {
            ImGui::Separator();
            ImGui::Text("%s", Configuration::GetLoc("menu.perk_settings", "Perk settings"));
            ImGui::SetNextItemWidth(260.0F);
            ImGui::InputText(Configuration::GetLoc("menu.full_name", "Name"), createNameBuffer.data(), createNameBuffer.size());
            ImGui::Checkbox(Configuration::GetLoc("menu.playable", "Playable"), &createPlayable);
        }
        if (SelectedFormKind() == DynamicForms::FormKind::HeadPart) {
            ImGui::Separator();
            ImGui::Text("%s", Configuration::GetLoc("menu.headpart_settings", "HeadPart settings"));
            ImGui::SetNextItemWidth(260.0F);
            ImGui::InputText(Configuration::GetLoc("menu.full_name", "Name"), createNameBuffer.data(), createNameBuffer.size());
            ImGui::SetNextItemWidth(360.0F);
            ImGui::InputText(Configuration::GetLoc("menu.model_path", "Model path"), createModelBuffer.data(), createModelBuffer.size());
            ImGui::SetNextItemWidth(220.0F);
            ImGui::Combo("Head part type", &selectedHeadPartType, HEAD_PART_TYPE_ITEMS.data(), static_cast<int>(HEAD_PART_TYPE_ITEMS.size()));
            ImGui::Checkbox(Configuration::GetLoc("menu.playable", "Playable"), &createPlayable);
            ImGui::SameLine();
            ImGui::Checkbox("Male", &createHeadPartMale);
            ImGui::SameLine();
            ImGui::Checkbox("Female", &createHeadPartFemale);
            ImGui::Checkbox("Is extra part", &createHeadPartIsExtraPart);
            ImGui::SameLine();
            ImGui::Checkbox("Use solid tint", &createHeadPartUseSolidTint);
            ImGui::SetNextItemWidth(360.0F);
            ImGui::InputText("Race Morph", createRaceMorphBuffer.data(), createRaceMorphBuffer.size());
            ImGui::SetNextItemWidth(360.0F);
            ImGui::InputText("Tri", createDefaultMorphBuffer.data(), createDefaultMorphBuffer.size());
            ImGui::SetNextItemWidth(360.0F);
            ImGui::InputText("Chargen Morph", createChargenMorphBuffer.data(), createChargenMorphBuffer.size());
            ImGui::TextDisabled("%s", "Meshes paths are relative to Data/Meshes.");
        }
        if (SelectedFormKind() == DynamicForms::FormKind::SoundDescriptor) {
            ImGui::Separator();
            ImGui::Text("%s", "Sound Description settings");
            ImGui::TextDisabled("%s", "Add sound files after creation.");
        }
        if (SelectedFormKind() == DynamicForms::FormKind::Light ||
            SelectedFormKind() == DynamicForms::FormKind::Explosion ||
            SelectedFormKind() == DynamicForms::FormKind::Activator) {
            ImGui::Separator();
            ImGui::Text("%s", "Base settings");
            ImGui::SetNextItemWidth(260.0F);
            ImGui::InputText(Configuration::GetLoc("menu.full_name", "Name"), createNameBuffer.data(), createNameBuffer.size());
            ImGui::SetNextItemWidth(360.0F);
            ImGui::InputText(Configuration::GetLoc("menu.model_path", "Model path"), createModelBuffer.data(), createModelBuffer.size());
            if (SelectedFormKind() == DynamicForms::FormKind::Light) {
                auto red = static_cast<std::uint8_t>(std::clamp(createColor[0], 0, 255));
                auto green = static_cast<std::uint8_t>(std::clamp(createColor[1], 0, 255));
                auto blue = static_cast<std::uint8_t>(std::clamp(createColor[2], 0, 255));
                if (DrawRGBColorEditor("RGB", red, green, blue)) {
                    createColor[0] = red;
                    createColor[1] = green;
                    createColor[2] = blue;
                }
            }
        }

        ImGui::Separator();
        if (ImGui::Button(Configuration::GetLoc("menu.confirm", "Confirm"))) {
            DynamicForms::DynamicForm form;
            form.editorId = editorId;
            form.kind = SelectedFormKind();
            form.globalType = SelectedGlobalType();
            form.defaultValue = SelectedDefaultValue();
            form.fullName = createNameBuffer.data();
            form.playable = createPlayable;
            form.red = static_cast<std::uint8_t>(createColor[0]);
            form.green = static_cast<std::uint8_t>(createColor[1]);
            form.blue = static_cast<std::uint8_t>(createColor[2]);
            form.alpha = static_cast<std::uint8_t>(createColor[3]);
            form.modelPath = createModelBuffer.data();
            form.artType = SelectedArtType();
            form.headPartType = SelectedHeadPartType();
            form.male = createHeadPartMale;
            form.female = createHeadPartFemale;
            form.isExtraPart = createHeadPartIsExtraPart;
            form.useSolidTint = createHeadPartUseSolidTint;
            form.raceMorphPath = createRaceMorphBuffer.data();
            form.defaultMorphPath = createDefaultMorphBuffer.data();
            form.chargenMorphPath = createChargenMorphBuffer.data();
            if (form.kind == DynamicForms::FormKind::Perk) {
                form.numRanks = 1;
            }
            if (form.kind == DynamicForms::FormKind::Light) {
                form.lightRadius = 128;
                form.red = 255;
                form.green = 255;
                form.blue = 255;
                form.fade = 1.0F;
            }
            if (form.kind == DynamicForms::FormKind::Explosion) {
                form.radius = 128.0F;
                form.verticalOffsetMult = 100.0F;
            }
            if (Manager::AddForm(form)) {
                ResetCreateState();
                ImGui::CloseCurrentPopup();
                created = true;
            } else {
                createError = Configuration::GetLoc("menu.create_failed", "Could not create form. Check if DPF is available.");
            }
        }

        ImGui::SameLine();
        if (ImGui::Button(Configuration::GetLoc("menu.cancel", "Cancel"))) {
            ImGui::CloseCurrentPopup();
        }

        if (!createError.empty()) {
            ImGui::TextColored({ 1.0F, 0.35F, 0.35F, 1.0F }, "%s", createError.c_str());
        }

        ImGui::EndPopup();
        return created;
    }

    bool RenderGlobalEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;

        int typeIndex = GlobalTypeIndex(edited.globalType);
        ImGui::SetNextItemWidth(220.0F);
        if (ImGui::Combo(Configuration::GetLoc("menu.global_type", "Global type"), &typeIndex, GLOBAL_TYPE_ITEMS.data(), static_cast<int>(GLOBAL_TYPE_ITEMS.size()))) {
            edited.globalType = GlobalTypeFromIndex(typeIndex);
            changed = true;
        }

        ImGui::SetNextItemWidth(220.0F);
        if (edited.globalType == DynamicForms::GlobalType::Float) {
            if (ImGui::InputFloat(Configuration::GetLoc("menu.default_value", "Default value"), &edited.defaultValue)) {
                changed = true;
            }
        } else {
            int intValue = static_cast<int>(edited.defaultValue);
            if (ImGui::InputInt(Configuration::GetLoc("menu.default_value", "Default value"), &intValue)) {
                edited.defaultValue = static_cast<float>(intValue);
                changed = true;
            }
        }

        if (changed && Manager::UpdateForm(index, edited)) {
            form = edited;
            return true;
        }

        return false;
    }

    std::string PieceLabel(const InternalFormInfo& info) {
        auto label = info.GetDisplayName();
        if (!info.editorID.empty() && label != info.editorID) {
            label += " [" + info.editorID + "]";
        }
        label += " (" + info.pluginName + ")";
        return label;
    }

    DynamicForms::FormRef MakeFormRef(const InternalFormInfo& info) {
        DynamicForms::FormRef ref;
        ref.editorID = info.editorID;
        ref.formID = FormUtil::NormalizeFormID(RE::TESForm::LookupByID(info.formID));
        return ref;
    }

    bool SameFormRef(const DynamicForms::FormRef& lhs, const DynamicForms::FormRef& rhs) {
        if (!lhs.editorID.empty() && !rhs.editorID.empty()) {
            return lhs.editorID == rhs.editorID;
        }
        if (!lhs.formID.empty() && !rhs.formID.empty()) {
            return lhs.formID == rhs.formID;
        }
        return false;
    }

    bool HasReference(const std::vector<DynamicForms::FormRef>& refs, const DynamicForms::FormRef& ref) {
        return std::ranges::any_of(refs, [&ref](const DynamicForms::FormRef& existing) {
            return SameFormRef(existing, ref);
        });
    }

    bool HasPiece(const DynamicForms::DynamicForm& form, const DynamicForms::FormRef& pieceId) {
        return HasReference(form.outfitPieces, pieceId);
    }

    std::vector<const InternalFormInfo*> BuildPieceRows(const char* typeName, const DynamicForms::DynamicForm& edited, const std::string& search) {
        std::vector<const InternalFormInfo*> rows;
        for (const auto& info : ListManager::GetSingleton()->GetList(typeName)) {
            const auto pieceId = MakeFormRef(info);
            if (pieceId.empty() || HasPiece(edited, pieceId)) {
                continue;
            }

            const auto labelText = PieceLabel(info);
            if (!search.empty() && ToLower(labelText).find(search) == std::string::npos && ToLower(pieceId.Display()).find(search) == std::string::npos) {
                continue;
            }

            rows.push_back(&info);
        }
        return rows;
    }

    bool DrawPieceTab(const char* typeName, const char* tabLabel, DynamicForms::DynamicForm& edited, const std::string& search) {
        bool changed = false;
        if (!ImGui::BeginTabItem(tabLabel)) {
            return false;
        }

        auto rows = BuildPieceRows(typeName, edited, search);
        ImGui::Text("%s: %zu", Configuration::GetLoc("menu.available", "Available"), rows.size());
        ImGui::Separator();

        if (rows.empty()) {
            ImGui::TextDisabled("%s", Configuration::GetLoc("menu.no_pieces_found", "No pieces found."));
            ImGui::EndTabItem();
            return false;
        }

        ImGui::PushID(typeName);
        ImGui::BeginChild("##pieceScroll", { 0.0F, 220.0F }, false);
        auto* clipper = ImGui::ImGuiListClipperManager::Create();
        ImGui::ImGuiListClipperManager::Begin(clipper, static_cast<int>(rows.size()), 0.0F);
        while (ImGui::ImGuiListClipperManager::Step(clipper)) {
            for (int rowIndex = clipper->DisplayStart; rowIndex < clipper->DisplayEnd; ++rowIndex) {
                const auto& info = *rows[static_cast<std::size_t>(rowIndex)];
                const auto pieceId = MakeFormRef(info);
                const auto labelText = PieceLabel(info);
                if (ImGui::Selectable(labelText.c_str(), false)) {
                    edited.outfitPieces.push_back(pieceId);
                    changed = true;
                }
            }
        }
        ImGui::ImGuiListClipperManager::End(clipper);
        ImGui::ImGuiListClipperManager::Destroy(clipper);
        ImGui::EndChild();
        ImGui::PopID();

        ImGui::EndTabItem();
        return changed;
    }

    bool DrawPiecePicker(const char* label, DynamicForms::DynamicForm& edited) {
        bool changed = false;
        auto& filter = outfitPieceFilters[label];
        const auto preview = Configuration::GetLoc("menu.add_piece", "Add piece");

        const bool listsReady = ListManager::GetSingleton()->PopulateAllLists(false);
        ImGui::SetNextItemWidth(360.0F);
        if (ImGui::BeginCombo(label, preview)) {
            char searchBuf[256]{};
            strcpy_s(searchBuf, filter.c_str());
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::InputText("##filter", searchBuf, sizeof(searchBuf))) {
                filter = searchBuf;
            }
            ImGui::Separator();

            const auto search = ToLower(filter);

            if (!listsReady) {
                ImGui::TextDisabled("%s", Configuration::GetLoc("menu.dpf_lists_unavailable", "DPF is not available yet."));
            } else if (ImGui::BeginTabBar("##pieceTabs")) {
                if (DrawPieceTab("Armor", Configuration::GetLoc("menu.armor_tab", "Armor"), edited, search)) {
                    filter.clear();
                    changed = true;
                }
                if (DrawPieceTab("LeveledItem", Configuration::GetLoc("menu.leveled_item_tab", "LeveledItem"), edited, search)) {
                    filter.clear();
                    changed = true;
                }
                ImGui::EndTabBar();
            }
            ImGui::EndCombo();
        }

        return changed;
    }

    bool DrawFormReferencePicker(const char* label, const char* typeName, DynamicForms::FormRef& value) {
        bool changed = false;
        auto& filter = formPickerFilters[std::string(label) + ":" + typeName];
        const bool listsReady = ListManager::GetSingleton()->PopulateAllLists(false);
        const auto previewText = value.empty() ? std::string(Configuration::GetLoc("common.select", "Select")) : value.Display();

        ImGui::SetNextItemWidth(360.0F);
        if (ImGui::BeginCombo(label, previewText.c_str())) {
            char searchBuf[256]{};
            strcpy_s(searchBuf, filter.c_str());
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::InputText("##filter", searchBuf, sizeof(searchBuf))) {
                filter = searchBuf;
            }
            ImGui::Separator();

            if (!listsReady) {
                ImGui::TextDisabled("%s", Configuration::GetLoc("menu.dpf_lists_unavailable", "DPF is not available yet."));
            } else {
                std::vector<const InternalFormInfo*> rows;
                const auto search = ToLower(filter);
                for (const auto& info : ListManager::GetSingleton()->GetList(typeName)) {
                    const auto id = MakeFormRef(info);
                    if (id.empty()) {
                        continue;
                    }
                    const auto labelText = PieceLabel(info);
                    if (!search.empty() && ToLower(labelText).find(search) == std::string::npos && ToLower(id.Display()).find(search) == std::string::npos) {
                        continue;
                    }
                    rows.push_back(&info);
                }

                if (ImGui::Selectable(Configuration::GetLoc("common.none", "None"), value.empty())) {
                    value = {};
                    filter.clear();
                    changed = true;
                }

                auto* clipper = ImGui::ImGuiListClipperManager::Create();
                ImGui::ImGuiListClipperManager::Begin(clipper, static_cast<int>(rows.size()), 0.0F);
                while (ImGui::ImGuiListClipperManager::Step(clipper)) {
                    for (int rowIndex = clipper->DisplayStart; rowIndex < clipper->DisplayEnd; ++rowIndex) {
                        const auto& info = *rows[static_cast<std::size_t>(rowIndex)];
                        const auto id = MakeFormRef(info);
                        const auto labelText = PieceLabel(info);
                        if (ImGui::Selectable(labelText.c_str(), SameFormRef(value, id))) {
                            value = id;
                            filter.clear();
                            changed = true;
                        }
                    }
                }
                ImGui::ImGuiListClipperManager::End(clipper);
                ImGui::ImGuiListClipperManager::Destroy(clipper);
            }
            ImGui::EndCombo();
        }

        return changed;
    }

    bool DrawFormReferencePicker(const char* label, const char* typeName, std::string& value) {
        DynamicForms::FormRef ref;
        if (!value.empty()) {
            ref.editorID = value;
            ref.formID = value;
        }
        const bool changed = DrawFormReferencePicker(label, typeName, ref);
        if (changed) {
            value = !ref.editorID.empty() ? ref.editorID : ref.formID;
        }
        return changed;
    }

    bool RenderOutfitEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;

        ImGui::Text("%s: %zu", Configuration::GetLoc("menu.outfit_piece_count", "Pieces"), edited.outfitPieces.size());
        for (std::size_t i = 0; i < edited.outfitPieces.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const auto display = edited.outfitPieces[i].Display();
            ImGui::Text("%s", display.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(Configuration::GetLoc("menu.remove", "Remove"))) {
                edited.outfitPieces.erase(edited.outfitPieces.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }

        if (DrawPiecePicker(Configuration::GetLoc("menu.add_piece", "Add piece"), edited)) {
            changed = true;
        }

        if (changed && Manager::UpdateForm(index, edited)) {
            form = edited;
            return true;
        }

        return false;
    }

    bool RenderColorEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;
        changed |= InputString(Configuration::GetLoc("menu.full_name", "Name"), edited.fullName);
        changed |= DrawRGBAColorEditor("RGBA", edited.red, edited.green, edited.blue, edited.alpha);
        if (ImGui::Checkbox(Configuration::GetLoc("menu.playable", "Playable"), &edited.playable)) {
            changed = true;
        }

        if (changed && Manager::UpdateForm(index, edited)) {
            form = edited;
            return true;
        }
        return false;
    }

    bool RenderArtObjectEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;
        changed |= InputString(Configuration::GetLoc("menu.model_path", "Model path"), edited.modelPath, 420.0F);

        int artType = ArtTypeIndex(edited.artType);
        ImGui::SetNextItemWidth(260.0F);
        if (ImGui::Combo(Configuration::GetLoc("menu.art_type", "Art type"), &artType, ART_TYPE_ITEMS.data(), static_cast<int>(ART_TYPE_ITEMS.size()))) {
            edited.artType = ArtTypeFromIndex(artType);
            changed = true;
        }

        int minBounds[3]{ edited.boundX1, edited.boundY1, edited.boundZ1 };
        int maxBounds[3]{ edited.boundX2, edited.boundY2, edited.boundZ2 };
        ImGui::SetNextItemWidth(260.0F);
        if (ImGui::InputInt3("Min bounds", minBounds)) {
            edited.boundX1 = static_cast<std::int16_t>(std::clamp(minBounds[0], -32768, 32767));
            edited.boundY1 = static_cast<std::int16_t>(std::clamp(minBounds[1], -32768, 32767));
            edited.boundZ1 = static_cast<std::int16_t>(std::clamp(minBounds[2], -32768, 32767));
            changed = true;
        }
        ImGui::SetNextItemWidth(260.0F);
        if (ImGui::InputInt3("Max bounds", maxBounds)) {
            edited.boundX2 = static_cast<std::int16_t>(std::clamp(maxBounds[0], -32768, 32767));
            edited.boundY2 = static_cast<std::int16_t>(std::clamp(maxBounds[1], -32768, 32767));
            edited.boundZ2 = static_cast<std::int16_t>(std::clamp(maxBounds[2], -32768, 32767));
            changed = true;
        }

        if (changed && Manager::UpdateForm(index, edited)) {
            form = edited;
            return true;
        }
        return false;
    }

    bool DrawStringListEditor(const char* label, std::vector<std::string>& values, const char* addLabel, const char* inputLabel) {
        bool changed = false;
        ImGui::Text("%s: %zu", label, values.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            changed |= InputString(inputLabel, values[i], 460.0F);
            ImGui::SameLine();
            if (ImGui::SmallButton(Configuration::GetLoc("menu.remove", "Remove"))) {
                values.erase(values.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ImGui::Button(addLabel)) {
            values.emplace_back();
            changed = true;
        }
        return changed;
    }

    bool FlagCheckbox(const char* label, std::uint32_t& flags, const std::uint32_t bit) {
        bool value = (flags & bit) != 0;
        if (ImGui::Checkbox(label, &value)) {
            if (value) {
                flags |= bit;
            } else {
                flags &= ~bit;
            }
            return true;
        }
        return false;
    }

    bool RenderSoundDescriptorEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;
        changed |= DrawStringListEditor("Sound files", edited.soundFiles, "Add sound file", "File");
        changed |= DrawFormReferencePicker("Category", "SoundCategory", edited.category);
        changed |= DrawFormReferencePicker("Alternate sound", "SoundDescriptor", edited.alternateSound);
        changed |= DrawFormReferencePicker("Output model", "SoundOutput", edited.outputModel);

        int frequencyShift = edited.frequencyShift;
        int frequencyVariance = edited.frequencyVariance;
        int priority = edited.priority;
        int dbVariance = edited.dbVariance;
        int looping = edited.looping;
        int rumble = edited.rumbleSendValue;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputInt("% Frequency Shift", &frequencyShift)) {
            edited.frequencyShift = static_cast<std::uint8_t>(std::clamp(frequencyShift, 0, 255));
            changed = true;
        }
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputInt("% Frequency Variance", &frequencyVariance)) {
            edited.frequencyVariance = static_cast<std::uint8_t>(std::clamp(frequencyVariance, 0, 255));
            changed = true;
        }
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputInt("Priority", &priority)) {
            edited.priority = static_cast<std::uint8_t>(std::clamp(priority, 0, 255));
            changed = true;
        }
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputInt("db Variance", &dbVariance)) {
            edited.dbVariance = static_cast<std::uint8_t>(std::clamp(dbVariance, 0, 255));
            changed = true;
        }
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat("Static Attenuation (db)", &edited.staticAttenuation)) {
            changed = true;
        }
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputInt("Looping raw", &looping)) {
            edited.looping = static_cast<std::uint8_t>(std::clamp(looping, 0, 255));
            changed = true;
        }
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputInt("Rumble Send Value", &rumble)) {
            edited.rumbleSendValue = static_cast<std::uint8_t>(std::clamp(rumble, 0, 255));
            changed = true;
        }

        if (changed && Manager::UpdateForm(index, edited)) {
            form = edited;
            return true;
        }
        return false;
    }

    bool RenderLightEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;
        changed |= InputString(Configuration::GetLoc("menu.full_name", "Name"), edited.fullName);
        changed |= InputString(Configuration::GetLoc("menu.model_path", "Model path"), edited.modelPath, 420.0F);

        int time = edited.lightTime;
        int radius = static_cast<int>(edited.lightRadius);
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputInt("Time", &time)) {
            edited.lightTime = time;
            changed = true;
        }
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputInt("Radius", &radius)) {
            edited.lightRadius = static_cast<std::uint32_t>(std::max(radius, 0));
            changed = true;
        }
        changed |= DrawRGBColorEditor("RGB", edited.red, edited.green, edited.blue);
        ImGui::TextDisabled("%s", "Light DATA flags");
        changed |= FlagCheckbox("Dynamic", edited.flags, 1u << 0);
        changed |= FlagCheckbox("Can Carry", edited.flags, 1u << 1);
        changed |= FlagCheckbox("Negative", edited.flags, 1u << 2);
        changed |= FlagCheckbox("Flicker", edited.flags, 1u << 3);
        changed |= FlagCheckbox("Deep Copy", edited.flags, 1u << 4);
        changed |= FlagCheckbox("Off By Default", edited.flags, 1u << 5);
        changed |= FlagCheckbox("Flicker Slow", edited.flags, 1u << 6);
        changed |= FlagCheckbox("Pulse", edited.flags, 1u << 7);
        changed |= FlagCheckbox("Pulse Slow", edited.flags, 1u << 8);
        changed |= FlagCheckbox("Spotlight", edited.flags, 1u << 9);
        changed |= FlagCheckbox("Spot Shadow", edited.flags, 1u << 10);
        changed |= FlagCheckbox("Hemi Shadow", edited.flags, 1u << 11);
        changed |= FlagCheckbox("Omni Shadow", edited.flags, 1u << 12);
        changed |= FlagCheckbox("Portal-strict", edited.flags, 1u << 13);
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat("Falloff Exponent", &edited.falloffExponent)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat("FOV", &edited.fov)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat("Near Clip", &edited.nearClip)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat("Flicker Period", &edited.flickerPeriod)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat("Intensity Amplitude", &edited.flickerIntensityAmplitude)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat("Movement Amplitude", &edited.flickerMovementAmplitude)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat("Fade", &edited.fade)) changed = true;
        changed |= DrawFormReferencePicker("Sound", "SoundDescriptor", edited.sound);
        changed |= DrawFormReferencePicker("Lens", "LensFlare", edited.lensFlare);

        if (changed && Manager::UpdateForm(index, edited)) {
            form = edited;
            return true;
        }
        return false;
    }

    bool RenderExplosionEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;
        changed |= InputString(Configuration::GetLoc("menu.full_name", "Name"), edited.fullName);
        changed |= InputString(Configuration::GetLoc("menu.model_path", "Model path"), edited.modelPath, 420.0F);
        changed |= DrawFormReferencePicker("Light", "Light", edited.light);
        changed |= DrawFormReferencePicker("Sound 1", "SoundDescriptor", edited.sound1);
        changed |= DrawFormReferencePicker("Sound 2", "SoundDescriptor", edited.sound2);
        changed |= DrawFormReferencePicker("Impact Data Set", "ImpactDataSet", edited.impactDataSet);
        changed |= DrawFormReferencePicker("Spawn Projectile", "Projectile", edited.spawnProjectile);
        changed |= DrawFormReferencePicker("Object Effect", "Enchantment", edited.objectEffect);
        changed |= DrawFormReferencePicker("Image Space Modifier", "ImageSpaceModifier", edited.imageSpaceModifier);
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat("Force", &edited.force)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat("Damage", &edited.damage)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat("Radius", &edited.radius)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat("IS Radius", &edited.imageSpaceRadius)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat("Vertical Offset Mult", &edited.verticalOffsetMult)) changed = true;
        ImGui::TextDisabled("%s", "Explosion DATA flags");
        changed |= FlagCheckbox("Always Uses World Orientation", edited.flags, 1u << 1);
        changed |= FlagCheckbox("Knock Down - Always", edited.flags, 1u << 2);
        changed |= FlagCheckbox("Knock Down - By Formula", edited.flags, 1u << 3);
        changed |= FlagCheckbox("Ignore LOS Check", edited.flags, 1u << 4);
        changed |= FlagCheckbox("Push Explosion Source Ref Only", edited.flags, 1u << 5);
        changed |= FlagCheckbox("Ignore Image Space Swap", edited.flags, 1u << 6);
        changed |= FlagCheckbox("Chain", edited.flags, 1u << 7);
        changed |= FlagCheckbox("No Controller Vibration", edited.flags, 1u << 8);
        int soundLevel = static_cast<int>(edited.soundLevel);
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputInt("Sound Level raw", &soundLevel)) {
            edited.soundLevel = static_cast<std::uint32_t>(std::max(soundLevel, 0));
            changed = true;
        }

        if (changed && Manager::UpdateForm(index, edited)) {
            form = edited;
            return true;
        }
        return false;
    }

    bool RenderActivatorEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;
        changed |= InputString(Configuration::GetLoc("menu.full_name", "Name"), edited.fullName);
        changed |= InputString(Configuration::GetLoc("menu.model_path", "Model path"), edited.modelPath, 420.0F);
        changed |= DrawFormReferencePicker("Sound - Looping", "SoundDescriptor", edited.soundLoop);
        changed |= DrawFormReferencePicker("Sound - Activation", "SoundDescriptor", edited.soundActivate);
        changed |= DrawFormReferencePicker("Water Type", "Water", edited.waterType);
        ImGui::TextDisabled("%s", "Activator FNAM flags");
        changed |= FlagCheckbox("No Displacement", edited.flags, 1u << 0);
        changed |= FlagCheckbox("Ignored By Sandbox", edited.flags, 1u << 1);
        changed |= FlagCheckbox("Is Procedural Water", edited.flags, 1u << 2);
        changed |= FlagCheckbox("Is LOD Water", edited.flags, 1u << 3);

        if (changed && Manager::UpdateForm(index, edited)) {
            form = edited;
            return true;
        }
        return false;
    }

    void DrawMoveButtons(auto& list, const std::size_t index, bool& changed) {
        if (index > 0) {
            if (ImGui::SmallButton("^")) {
                std::swap(list[index], list[index - 1]);
                changed = true;
            }
            ImGui::SameLine();
        }
        if (index + 1 < list.size()) {
            if (ImGui::SmallButton("v")) {
                std::swap(list[index], list[index + 1]);
                changed = true;
            }
            ImGui::SameLine();
        }
    }

    bool DrawConditionEditor(DynamicForms::PerkCondition& condition, const char* idPrefix) {
        bool changed = false;
        ImGui::PushID(idPrefix);
        int kind = ConditionKindIndex(condition.kind);
        ImGui::SetNextItemWidth(220.0F);
        if (ImGui::Combo("Kind", &kind, CONDITION_KIND_ITEMS.data(), static_cast<int>(CONDITION_KIND_ITEMS.size()))) {
            condition.kind = ConditionKindFromIndex(kind);
            if (condition.kind == DynamicForms::PerkConditionKind::GetBaseActorValue ||
                condition.kind == DynamicForms::PerkConditionKind::GetActorValue) {
                condition.param1 = "25";
            }
            changed = true;
        }

        if (condition.kind == DynamicForms::PerkConditionKind::Raw) {
            int functionId = static_cast<int>(condition.functionId);
            ImGui::SetNextItemWidth(160.0F);
            if (ImGui::InputInt("Function ID", &functionId)) {
                condition.functionId = static_cast<std::uint32_t>(std::max(functionId, 0));
                changed = true;
            }
        }

        int opCode = static_cast<int>(condition.opCode);
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::Combo("Operator", &opCode, CONDITION_OP_ITEMS.data(), static_cast<int>(CONDITION_OP_ITEMS.size()))) {
            condition.opCode = static_cast<std::uint32_t>(opCode);
            changed = true;
        }

        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat("Comparison", &condition.comparisonValue)) {
            changed = true;
        }
        if (ImGui::Checkbox("OR", &condition.isOr)) {
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Global comparison", &condition.useGlobalComparison)) {
            changed = true;
        }
        if (condition.useGlobalComparison) {
            changed |= DrawFormReferencePicker("Comparison global", "Global", condition.comparisonGlobal);
        }
        if (condition.kind == DynamicForms::PerkConditionKind::GetGlobalValue) {
            changed |= DrawFormReferencePicker("Param 1 Global", "Global", condition.param1);
        } else if (condition.kind == DynamicForms::PerkConditionKind::HasPerk) {
            changed |= DrawFormReferencePicker("Param 1 Perk", "Perk", condition.param1);
        } else if (condition.kind == DynamicForms::PerkConditionKind::GetQuestCompleted) {
            changed |= DrawFormReferencePicker("Param 1 Quest", "Quest", condition.param1);
        } else if (condition.kind == DynamicForms::PerkConditionKind::HasSpell) {
            changed |= DrawFormReferencePicker("Param 1 Spell", "Spell", condition.param1);
        } else {
            changed |= InputString("Param 1", condition.param1, 360.0F);
        }
        changed |= InputString("Param 2", condition.param2, 360.0F);
        ImGui::PopID();
        return changed;
    }

    bool DrawPerkConditions(std::vector<DynamicForms::PerkCondition>& conditions) {
        bool changed = false;
        if (ImGui::Button("Add condition")) {
            DynamicForms::PerkCondition condition;
            condition.kind = DynamicForms::PerkConditionKind::GetBaseActorValue;
            condition.param1 = "25";
            condition.opCode = 3;
            condition.comparisonValue = 20.0F;
            conditions.push_back(std::move(condition));
            changed = true;
        }

        for (std::size_t i = 0; i < conditions.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const auto header = std::format("Condition {}##condition", i);
            if (ImGui::CollapsingHeader(header.c_str())) {
                DrawMoveButtons(conditions, i, changed);
                if (ImGui::SmallButton(Configuration::GetLoc("menu.remove", "Remove"))) {
                    conditions.erase(conditions.begin() + static_cast<std::ptrdiff_t>(i));
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                changed |= DrawConditionEditor(conditions[i], "conditionEditor");
            }
            ImGui::PopID();
        }
        return changed;
    }

    bool DrawPerkEntries(std::vector<DynamicForms::PerkEntry>& entries) {
        bool changed = false;
        if (ImGui::Button("Add entry")) {
            DynamicForms::PerkEntry entry;
            entry.entryPoint = 75;
            entry.function = 1;
            entry.value = 2.0F;
            entries.push_back(std::move(entry));
            changed = true;
        }

        for (std::size_t i = 0; i < entries.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const auto header = std::format("Entry {}##entry", i);
            if (ImGui::CollapsingHeader(header.c_str())) {
                auto& entry = entries[i];
                DrawMoveButtons(entries, i, changed);
                if (ImGui::SmallButton(Configuration::GetLoc("menu.remove", "Remove"))) {
                    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
                    changed = true;
                    ImGui::PopID();
                    break;
                }

                int rank = static_cast<int>(entry.rank);
                int priority = static_cast<int>(entry.priority);
                int entryPoint = static_cast<int>(entry.entryPoint);
                int function = static_cast<int>(entry.function);
                int numArgs = static_cast<int>(entry.numArgs);
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt("Rank", &rank)) {
                    entry.rank = static_cast<std::uint32_t>(std::max(rank, 0));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt("Priority", &priority)) {
                    entry.priority = static_cast<std::uint32_t>(std::max(priority, 0));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt("Entry point", &entryPoint)) {
                    entry.entryPoint = static_cast<std::uint32_t>(std::clamp(entryPoint, 0, 91));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt("Function", &function)) {
                    entry.function = static_cast<std::uint32_t>(std::clamp(function, 1, 15));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt("Num args", &numArgs)) {
                    entry.numArgs = static_cast<std::uint32_t>(std::max(numArgs, 0));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputFloat("Value", &entry.value)) {
                    changed = true;
                }
                ImGui::Separator();
                ImGui::Text("%s", "Entry conditions");
                changed |= DrawPerkConditions(entry.conditions);
            }
            ImGui::PopID();
        }
        return changed;
    }

    bool RenderPerkEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;

        if (ImGui::BeginTabBar("##perkTabs")) {
            if (ImGui::BeginTabItem("Data")) {
                changed |= InputString(Configuration::GetLoc("menu.full_name", "Name"), edited.fullName);
                changed |= InputString("Description", edited.description, 460.0F);
                int level = edited.level;
                int numRanks = edited.numRanks;
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt("Level", &level)) {
                    edited.level = static_cast<std::int8_t>(std::clamp(level, -128, 127));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt("Num ranks", &numRanks)) {
                    edited.numRanks = static_cast<std::int8_t>(std::clamp(numRanks, 0, 127));
                    changed = true;
                }
                if (ImGui::Checkbox("Trait", &edited.trait)) {
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox(Configuration::GetLoc("menu.playable", "Playable"), &edited.playable)) {
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Hidden", &edited.hidden)) {
                    changed = true;
                }
                changed |= DrawFormReferencePicker("Next perk", "Perk", edited.nextPerk);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Conditions")) {
                changed |= DrawPerkConditions(edited.conditions);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Effects")) {
                changed |= DrawPerkEntries(edited.entries);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Debug")) {
                ImGui::Text("conditions: %zu", edited.conditions.size());
                ImGui::Text("entries: %zu", edited.entries.size());
                for (std::size_t i = 0; i < edited.conditions.size(); ++i) {
                    const auto& condition = edited.conditions[i];
                    ImGui::Text("condition[%zu]: kind=%d function=%u op=%u cmp=%.3f p1=%s p2=%s",
                        i,
                        static_cast<int>(condition.kind),
                        condition.functionId,
                        condition.opCode,
                        condition.comparisonValue,
                        condition.param1.c_str(),
                        condition.param2.c_str());
                }
                for (std::size_t i = 0; i < edited.entries.size(); ++i) {
                    const auto& entry = edited.entries[i];
                    ImGui::Text("entry[%zu]: ep=%u function=%u rank=%u priority=%u value=%.3f conditions=%zu",
                        i,
                        entry.entryPoint,
                        entry.function,
                        entry.rank,
                        entry.priority,
                        entry.value,
                        entry.conditions.size());
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (changed && Manager::UpdateForm(index, edited)) {
            form = edited;
            return true;
        }
        return false;
    }

    bool DrawHeadPartExtraPicker(DynamicForms::DynamicForm& edited) {
        bool changed = false;
        auto& filter = formPickerFilters["headpart_extra_parts"];
        const bool listsReady = ListManager::GetSingleton()->PopulateAllLists(false);

        ImGui::SetNextItemWidth(360.0F);
        if (ImGui::BeginCombo("Add extra part", Configuration::GetLoc("common.select", "Select"))) {
            char searchBuf[256]{};
            strcpy_s(searchBuf, filter.c_str());
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::InputText("##filter", searchBuf, sizeof(searchBuf))) {
                filter = searchBuf;
            }
            ImGui::Separator();

            if (!listsReady) {
                ImGui::TextDisabled("%s", Configuration::GetLoc("menu.dpf_lists_unavailable", "DPF is not available yet."));
            } else {
                const auto search = ToLower(filter);
                std::vector<const InternalFormInfo*> rows;
                for (const auto& info : ListManager::GetSingleton()->GetList("HeadPart")) {
                    const auto id = MakeFormRef(info);
                    if (id.empty() || HasReference(edited.extraParts, id)) {
                        continue;
                    }
                    const auto labelText = PieceLabel(info);
                    if (!search.empty() && ToLower(labelText).find(search) == std::string::npos && ToLower(id.Display()).find(search) == std::string::npos) {
                        continue;
                    }
                    rows.push_back(&info);
                }

                auto* clipper = ImGui::ImGuiListClipperManager::Create();
                ImGui::ImGuiListClipperManager::Begin(clipper, static_cast<int>(rows.size()), 0.0F);
                while (ImGui::ImGuiListClipperManager::Step(clipper)) {
                    for (int rowIndex = clipper->DisplayStart; rowIndex < clipper->DisplayEnd; ++rowIndex) {
                        const auto& info = *rows[static_cast<std::size_t>(rowIndex)];
                        const auto id = MakeFormRef(info);
                        const auto labelText = PieceLabel(info);
                        if (ImGui::Selectable(labelText.c_str(), false)) {
                            edited.extraParts.push_back(id);
                            filter.clear();
                            changed = true;
                        }
                    }
                }
                ImGui::ImGuiListClipperManager::End(clipper);
                ImGui::ImGuiListClipperManager::Destroy(clipper);
            }
            ImGui::EndCombo();
        }

        return changed;
    }

    bool RenderHeadPartEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;

        if (ImGui::BeginTabBar("##headPartTabs")) {
            if (ImGui::BeginTabItem("Data")) {
                changed |= InputString(Configuration::GetLoc("menu.full_name", "Name"), edited.fullName);
                int typeIndex = HeadPartTypeIndex(edited.headPartType);
                ImGui::SetNextItemWidth(220.0F);
                if (ImGui::Combo("Type", &typeIndex, HEAD_PART_TYPE_ITEMS.data(), static_cast<int>(HEAD_PART_TYPE_ITEMS.size()))) {
                    edited.headPartType = HeadPartTypeFromIndex(typeIndex);
                    changed = true;
                }
                if (ImGui::Checkbox(Configuration::GetLoc("menu.playable", "Playable"), &edited.playable)) {
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Male", &edited.male)) {
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Female", &edited.female)) {
                    changed = true;
                }
                if (ImGui::Checkbox("Is extra part", &edited.isExtraPart)) {
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Use solid tint", &edited.useSolidTint)) {
                    changed = true;
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Files")) {
                changed |= InputString("NIF model", edited.modelPath, 460.0F);
                changed |= InputString("Race Morph", edited.raceMorphPath, 460.0F);
                changed |= InputString("Tri", edited.defaultMorphPath, 460.0F);
                changed |= InputString("Chargen Morph", edited.chargenMorphPath, 460.0F);
                ImGui::TextDisabled("%s", "Meshes paths are relative to Data/Meshes. Texture paths in texture sets are relative to Data/Textures.");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("References")) {
                changed |= DrawFormReferencePicker("Texture set", "TextureSet", edited.textureSet);
                changed |= DrawFormReferencePicker("Color", "Color", edited.colorForm);
                changed |= DrawFormReferencePicker("Valid races", "FormList", edited.validRaces);
                ImGui::Separator();
                ImGui::Text("Extra parts: %zu", edited.extraParts.size());
                for (std::size_t i = 0; i < edited.extraParts.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    const auto display = edited.extraParts[i].Display();
                    ImGui::Text("%s", display.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton(Configuration::GetLoc("menu.remove", "Remove"))) {
                        edited.extraParts.erase(edited.extraParts.begin() + static_cast<std::ptrdiff_t>(i));
                        changed = true;
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                changed |= DrawHeadPartExtraPicker(edited);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Debug")) {
                ImGui::Text("type=%d", static_cast<int>(edited.headPartType));
                ImGui::Text("flags playable=%d male=%d female=%d extra=%d solidTint=%d",
                    edited.playable,
                    edited.male,
                    edited.female,
                    edited.isExtraPart,
                    edited.useSolidTint);
                ImGui::Text("model=%s", edited.modelPath.c_str());
                ImGui::Text("raceMorph=%s", edited.raceMorphPath.c_str());
                ImGui::Text("defaultMorph=%s", edited.defaultMorphPath.c_str());
                ImGui::Text("chargenMorph=%s", edited.chargenMorphPath.c_str());
                ImGui::Text("textureSet=%s", edited.textureSet.Display().c_str());
                ImGui::Text("color=%s", edited.colorForm.Display().c_str());
                ImGui::Text("validRaces=%s", edited.validRaces.Display().c_str());
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (changed && Manager::UpdateForm(index, edited)) {
            form = edited;
            return true;
        }
        return false;
    }

    bool RenderDeletePopup() {
        bool deleted = false;
        bool open = true;
        if (!ImGui::BeginPopupModal(DELETE_POPUP_ID, &open)) {
            return false;
        }

        auto& forms = Manager::GetForms();
        if (pendingDeleteIndex < 0 || static_cast<std::size_t>(pendingDeleteIndex) >= forms.size()) {
            ImGui::Text("%s", Configuration::GetLoc("menu.delete_missing", "The selected form no longer exists."));
        } else {
            const auto& form = forms[static_cast<std::size_t>(pendingDeleteIndex)];
            ImGui::Text("%s", Configuration::GetLoc("menu.delete_confirm", "Are you sure you want to delete this form?"));
            ImGui::Text("%s", form.editorId.c_str());
            ImGui::Text("%s: %u", Configuration::GetLoc("menu.local_id", "Local ID"), form.localId);

            if (ImGui::Button(Configuration::GetLoc("menu.delete", "Delete"))) {
                if (Manager::DeleteForm(static_cast<std::size_t>(pendingDeleteIndex))) {
                    pendingDeleteIndex = -1;
                    requestDeletePopup = false;
                    deleteError.clear();
                    ImGui::CloseCurrentPopup();
                    deleted = true;
                } else {
                    deleteError = Configuration::GetLoc("menu.delete_failed", "Could not delete form. Check if DPF is available.");
                }
            }
            ImGui::SameLine();
        }

        if (ImGui::Button(Configuration::GetLoc("menu.cancel", "Cancel"))) {
            pendingDeleteIndex = -1;
            requestDeletePopup = false;
            deleteError.clear();
            ImGui::CloseCurrentPopup();
        }

        if (!deleteError.empty()) {
            ImGui::TextColored({ 1.0F, 0.35F, 0.35F, 1.0F }, "%s", deleteError.c_str());
        }

        ImGui::EndPopup();
        return deleted;
    }
}

namespace Configuration {
    void Register() {
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::warn("SKSE Menu Framework is not installed.");
            return;
        }

        LoadLanguage();
        LoadForms();

        SKSEMenuFramework::SetSection("Dynamic Forms Generator");
        SKSEMenuFramework::AddSectionItem(GetLoc("menu.forms", "Forms"), RenderFormsMenu);
    }

    void LoadLanguage() {
        language.clear();

        std::ifstream stream(Manager::LANG_PATH);
        if (!stream.is_open()) {
            return;
        }

        rapidjson::IStreamWrapper wrapper(stream);
        rapidjson::Document doc;
        doc.ParseStream(wrapper);
        if (doc.HasParseError() || !doc.IsObject()) {
            logger::warn("Invalid language JSON: {}", Manager::LANG_PATH);
            return;
        }

        for (auto itr = doc.MemberBegin(); itr != doc.MemberEnd(); ++itr) {
            AddLocValue(itr->name.GetString(), itr->value);
        }
    }

    const char* GetLoc(const char* key, const char* fallback) {
        const auto found = language.find(key);
        return found != language.end() ? found->second.c_str() : fallback;
    }

    void LoadForms() {
        Manager::LoadForms();
    }

    void SaveForms() {
        Manager::SaveAllForms();
    }

    void RenderFormsMenu() {
        ListManager::GetSingleton()->PopulateAllLists(ImGui::IsWindowAppearing());

        std::string editorId = editorIdBuffer.data();
        const bool validEditorId = IsValidEditorId(editorId);
        const bool duplicateEditorId = validEditorId && Manager::HasEditorId(editorId);

        ImGui::SetNextItemWidth(280.0F);
        ImGui::InputText(GetLoc("menu.editor_id", "EditorID"), editorIdBuffer.data(), editorIdBuffer.size());
        editorId = editorIdBuffer.data();

        if (editorId.empty()) {
            ImGui::TextColored({ 1.0F, 0.75F, 0.35F, 1.0F }, "%s", GetLoc("menu.editor_id_required", "EditorID is required."));
        } else if (!validEditorId) {
            ImGui::TextColored({ 1.0F, 0.35F, 0.35F, 1.0F }, "%s", GetLoc("menu.editor_id_invalid", "Use only letters, numbers and underscore."));
        } else if (duplicateEditorId) {
            ImGui::TextColored({ 1.0F, 0.35F, 0.35F, 1.0F }, "%s", GetLoc("menu.editor_id_duplicate", "A form with this EditorID already exists."));
        }

        if (!validEditorId || duplicateEditorId) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Create")) {
            ImGui::OpenPopup(GetLoc("menu.create_popup", "Create Form"));
        }

        if (!validEditorId || duplicateEditorId) {
            ImGui::EndDisabled();
        }

        RenderCreatePopup(editorId);

        ImGui::Separator();
        ImGui::TextColored({ 0.6F, 0.8F, 1.0F, 1.0F }, "%s", GetLoc("menu.saved_forms", "Saved forms"));

        ImGui::SetNextItemWidth(220.0F);
        ImGui::Combo(GetLoc("menu.filter_type", "Filter by type"), &selectedFilterKind, FILTER_KIND_ITEMS.data(), static_cast<int>(FILTER_KIND_ITEMS.size()));
        ImGui::SetNextItemWidth(280.0F);
        ImGui::InputText(GetLoc("menu.filter_editor_id", "Filter EditorID"), filterEditorIdBuffer.data(), filterEditorIdBuffer.size());

        auto& forms = Manager::GetForms();
        for (std::size_t i = 0; i < forms.size(); ++i) {
            auto& form = forms[i];
            if (!MatchesFilters(form)) {
                continue;
            }

            ImGui::PushID(form.editorId.c_str());
            if (ImGui::CollapsingHeader(form.editorId.c_str())) {
                ImGui::Indent();
                ImGui::Text("%s: %s", GetLoc("menu.form_type", "Form type"), FormKindLabel(form.kind));
                ImGui::Text("%s: %u", GetLoc("menu.local_id", "Local ID"), form.localId);
                if (form.kind == DynamicForms::FormKind::Global) {
                    RenderGlobalEditor(i, form);
                } else if (form.kind == DynamicForms::FormKind::Outfit) {
                    RenderOutfitEditor(i, form);
                } else if (form.kind == DynamicForms::FormKind::Color) {
                    RenderColorEditor(i, form);
                } else if (form.kind == DynamicForms::FormKind::ArtObject) {
                    RenderArtObjectEditor(i, form);
                } else if (form.kind == DynamicForms::FormKind::Perk) {
                    RenderPerkEditor(i, form);
                } else if (form.kind == DynamicForms::FormKind::HeadPart) {
                    RenderHeadPartEditor(i, form);
                } else if (form.kind == DynamicForms::FormKind::SoundDescriptor) {
                    RenderSoundDescriptorEditor(i, form);
                } else if (form.kind == DynamicForms::FormKind::Light) {
                    RenderLightEditor(i, form);
                } else if (form.kind == DynamicForms::FormKind::Explosion) {
                    RenderExplosionEditor(i, form);
                } else if (form.kind == DynamicForms::FormKind::Activator) {
                    RenderActivatorEditor(i, form);
                } else {
                    ImGui::Text("%s", GetLoc("menu.no_editable_fields", "No editable fields for this form type yet."));
                }
                if (ImGui::Button(GetLoc("menu.delete", "Delete"))) {
                    pendingDeleteIndex = static_cast<int>(i);
                    deleteError.clear();
                    requestDeletePopup = true;
                }
                ImGui::Unindent();
            }
            ImGui::PopID();
        }

        if (requestDeletePopup) {
            requestDeletePopup = false;
            ImGui::OpenPopup(DELETE_POPUP_ID);
        }

        RenderDeletePopup();
    }
}
