#include "Configuration.h"

#include "ConditionCatalog.h"
#include "ListManager.h"
#include "Manager.h"
#include "logger.h"

#include <SKSEMCP/SKSEMenuFramework.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <miniz.h>
#include <optional>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

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
    std::string conditionFunctionFilter;
    std::array<char, 128> exportFilterEditorIdBuffer{};
    int selectedExportFilterKind = 0;
    std::string createError;
    std::string deleteError;
    std::string saveMessage;
    bool lastSaveSucceeded = true;
    std::string testActionMessage;
    bool lastTestActionSucceeded = true;
    bool requestDeletePopup = false;
    bool deleteSelectionMode = false;
    bool requestBatchDeletePopup = false;
    std::set<std::string> selectedDeleteForms;
    std::set<std::string> selectedExportForms;
    std::array<char, 128> exportPackageName{ 'D', 'F', 'G', '_', 'E', 'x', 'p', 'o', 'r', 't', '\0' };
    std::string exportMessage;
    bool lastExportSucceeded = true;

    constexpr auto DELETE_POPUP_ID = "Delete Form##dynamic_forms_delete_popup";
    constexpr auto BATCH_DELETE_POPUP_ID = "Delete Selected Forms##dynamic_forms_batch_delete_popup";
    constexpr auto EXPORT_DIR = "Data/Viny Mods/Dynamic Forms Generator/Export";
    const ImGui::ImVec4 DIRTY_COLOR{ 1.0F, 0.72F, 0.2F, 1.0F };
    const ImGui::ImVec4 SUCCESS_COLOR{ 0.45F, 0.9F, 0.55F, 1.0F };
    const ImGui::ImVec4 ERROR_COLOR{ 1.0F, 0.35F, 0.35F, 1.0F };
    constexpr std::array FORM_KIND_ITEMS{ "Global", "Keyword", "Outfit", "Armor Type", "Armor", "Color", "Art Object", "Perk", "Head Part", "Sound Description", "Light", "Explosion", "Activator", "Effect Shader", "NPC" };
    constexpr std::array FILTER_KIND_ITEMS{ "All", "Global", "Keyword", "Outfit", "Armor Type", "Armor", "Color", "Art Object", "Perk", "Head Part", "Sound Description", "Light", "Explosion", "Activator", "Effect Shader", "NPC" };
    constexpr std::array GLOBAL_TYPE_ITEMS{ "short", "long", "float" };
    constexpr std::array ART_TYPE_ITEMS{ "MagicCasting", "MagicHitEffect", "MagicEnchantEffect" };
    constexpr std::array HEAD_PART_TYPE_ITEMS{ "Misc", "Face", "Eyes", "Hair", "FacialHair", "Scar", "Eyebrows" };
    constexpr std::array CONDITION_KIND_ITEMS{ "Raw", "GetGlobalValue", "GetActorValue", "GetBaseActorValue", "HasPerk", "GetQuestCompleted", "HasSpell" };
    constexpr std::array CONDITION_OP_ITEMS{ "==", "!=", ">", ">=", "<", "<=" };
    constexpr std::array CONDITION_RUN_ON_ITEMS{ "Subject", "Target", "Reference", "Combat Target", "Linked Ref", "Quest Alias", "Package Data", "Event Data", "Command Target" };
    constexpr std::array NPC_SKILL_NAMES{
        "One-Handed", "Two-Handed", "Archery", "Block", "Smithing", "Heavy Armor",
        "Light Armor", "Pickpocket", "Lockpicking", "Sneak", "Alchemy", "Speech",
        "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration", "Enchanting"
    };
    constexpr std::array NPC_MORPH_NAMES{
        "Nose: Long/Short", "Nose: Up/Down", "Jaw: Up/Down", "Jaw: Narrow/Wide", "Jaw: Forward/Back",
        "Cheeks: Up/Down", "Cheeks: Forward/Back", "Eyes: Up/Down", "Eyes: In/Out", "Brows: Up/Down",
        "Brows: In/Out", "Brows: Forward/Back", "Lips: Up/Down", "Lips: In/Out", "Chin: Narrow/Wide",
        "Chin: Up/Down", "Chin: Underbite/Overbite", "Eyes: Forward/Back", "Unknown"
    };
    constexpr std::array NPC_FACE_PART_NAMES{ "Nose", "Unknown", "Eyes", "Mouth" };
    constexpr std::array HEAD_PART_FILTER_ITEMS{ "All", "Hair", "Facial Hair", "Eye Brows", "Eye", "Face", "Misc", "Scar" };
    constexpr std::array ARMOR_TYPE_ITEMS{ "Light Armor", "Heavy Armor", "Clothing" };
    constexpr std::array BIPED_SLOT_ITEMS{
        "Head", "Hair", "Body", "Hands", "Forearms", "Amulet", "Ring", "Feet",
        "Calves", "Shield", "Tail", "Long Hair", "Circlet", "Ears", "Mod Mouth", "Mod Neck",
        "Mod Chest Primary", "Mod Back", "Mod Misc 1", "Mod Pelvis Primary", "Decapitate Head", "Decapitate",
        "Mod Pelvis Secondary", "Mod Leg Right", "Mod Leg Left", "Mod Face Jewelry", "Mod Chest Secondary",
        "Mod Shoulder", "Mod Arm Left", "Mod Arm Right", "Mod Misc 2", "FX01"
    };
    constexpr float NPC_NUMBER_INPUT_WIDTH = 180.0F;

    int selectedNpcHeadPartFilter = 0;

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
            return DynamicForms::FormKind::ArmorType;
        case 4:
            return DynamicForms::FormKind::Armor;
        case 5:
            return DynamicForms::FormKind::Color;
        case 6:
            return DynamicForms::FormKind::ArtObject;
        case 7:
            return DynamicForms::FormKind::Perk;
        case 8:
            return DynamicForms::FormKind::HeadPart;
        case 9:
            return DynamicForms::FormKind::SoundDescriptor;
        case 10:
            return DynamicForms::FormKind::Light;
        case 11:
            return DynamicForms::FormKind::Explosion;
        case 12:
            return DynamicForms::FormKind::Activator;
        case 13:
            return DynamicForms::FormKind::EffectShader;
        case 14:
            return DynamicForms::FormKind::NPC;
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
        case DynamicForms::FormKind::ArmorType:
            return "Armor Type";
        case DynamicForms::FormKind::Armor:
            return "Armor";
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
        case DynamicForms::FormKind::EffectShader:
            return "Effect Shader";
        case DynamicForms::FormKind::NPC:
            return "NPC";
        case DynamicForms::FormKind::Global:
        default:
            return "Global";
        }
    }

    std::string SanitizeFilename(std::string name) {
        constexpr std::string_view invalid = "<>:/\\|?*\"";
        for (char& ch : name) {
            if (invalid.find(ch) != std::string_view::npos || static_cast<unsigned char>(ch) < 32) {
                ch = '_';
            }
        }
        return name.empty() ? "DFG_Export" : name;
    }

    std::filesystem::path FormJsonPath(const std::string& editorId) {
        return std::filesystem::path(Manager::FORMS_DIR) / std::format("{}.json", editorId);
    }

    bool AddFileToZipOnce(
        mz_zip_archive& zip,
        std::set<std::string>& addedPaths,
        const std::filesystem::path& source,
        const std::string& internalPath)
    {
        if (!std::filesystem::exists(source)) {
            logger::warn("Export: missing source file '{}'.", source.string());
            return false;
        }
        if (!addedPaths.insert(internalPath).second) {
            return true;
        }
        if (!mz_zip_writer_add_file(&zip, internalPath.c_str(), source.string().c_str(), nullptr, 0, MZ_BEST_COMPRESSION)) {
            logger::error("Export: failed to add '{}' as '{}'.", source.string(), internalPath);
            return false;
        }
        return true;
    }

    std::optional<std::filesystem::path> ExportSelectedFormsAsZip(const std::string& packageName, const std::set<std::string>& editorIds) {
        if (editorIds.empty()) {
            logger::warn("Export: no forms selected.");
            return std::nullopt;
        }

        auto& forms = Manager::GetForms();
        for (std::size_t i = 0; i < forms.size(); ++i) {
            if (editorIds.contains(forms[i].editorId) && Manager::IsDirty(i) && !Manager::SaveForm(i)) {
                logger::warn("Export: could not save dirty form '{}' before exporting.", forms[i].editorId);
                return std::nullopt;
            }
        }

        namespace fs = std::filesystem;
        fs::create_directories(EXPORT_DIR);
        const auto zipPath = fs::path(EXPORT_DIR) / (SanitizeFilename(packageName.empty() ? "DFG_Export" : packageName) + ".zip");

        mz_zip_archive zip{};
        if (!mz_zip_writer_init_file(&zip, zipPath.string().c_str(), 0)) {
            logger::error("Export: failed to initialize ZIP file at '{}'.", zipPath.string());
            return std::nullopt;
        }

        bool ok = true;
        std::set<std::string> addedPaths;
        for (const auto& editorId : editorIds) {
            const auto source = FormJsonPath(editorId);
            const auto internalPath = std::format("Viny Mods/Dynamic Forms Generator/Forms/{}.json", editorId);
            ok = AddFileToZipOnce(zip, addedPaths, source, internalPath) && ok;
        }

        if (!mz_zip_writer_finalize_archive(&zip)) {
            logger::error("Export: failed to finalize ZIP '{}'.", zipPath.string());
            ok = false;
        }
        mz_zip_writer_end(&zip);

        if (!ok) {
            return std::nullopt;
        }

        logger::info("Export package successfully written to '{}'.", zipPath.string());
        return zipPath;
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

    int ArmorTypeIndex(const std::uint32_t armorType) {
        if (armorType <= 2) {
            return static_cast<int>(armorType);
        }
        return 2;
    }

    std::uint32_t ArmorTypeFromIndex(const int index) {
        if (index >= 0 && index < static_cast<int>(ARMOR_TYPE_ITEMS.size())) {
            return static_cast<std::uint32_t>(index);
        }
        return 2;
    }

    const char* SelectedNpcHeadPartListType() {
        switch (selectedNpcHeadPartFilter) {
        case 1:
            return "Hair";
        case 2:
            return "Facial Hair";
        case 3:
            return "Eye Brows";
        case 4:
            return "Eye";
        case 5:
            return "Face";
        case 6:
            return "Misc";
        case 7:
            return "Scar";
        case 0:
        default:
            return "HeadPart";
        }
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

    std::uint32_t ConditionFunctionIdForUi(const DynamicForms::PerkCondition& condition) {
        if (condition.functionId != 0 || condition.kind == DynamicForms::PerkConditionKind::Raw) {
            return condition.functionId;
        }

        const char* name = CONDITION_KIND_ITEMS[static_cast<std::size_t>(ConditionKindIndex(condition.kind))];
        for (const auto& function : ConditionCatalog::GetFunctions()) {
            if (std::string_view(function.name) == name) {
                return function.id;
            }
        }
        return 0;
    }

    std::string ToLower(std::string value) {
        std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    float TextWidth(const char* text) {
        ImGui::ImVec2 size{};
        ImGui::CalcTextSize(&size, text ? text : "", nullptr, true, -1.0F);
        return size.x;
    }

    template <class T>
    float WidestItemWidth(const T& items, const float minWidth = 160.0F) {
        float width = minWidth;
        for (const auto* item : items) {
            width = std::max(width, TextWidth(item) + 56.0F);
        }
        return width;
    }

    template <class T>
    void SetStableComboWidth(const T& items, const float minWidth = 160.0F, const float maxWidth = 520.0F) {
        const float desired = std::min(WidestItemWidth(items, minWidth), maxWidth);
        ImGui::SetNextItemWidth(std::max(minWidth, desired));
    }

    void SetAvailableComboWidth(const float minWidth = 360.0F, const float maxWidth = 720.0F) {
        static_cast<void>(maxWidth);
        ImGui::SetNextItemWidth(minWidth);
    }

    void SetFixedComboPopupWidth(const float width = 360.0F) {
        ImGui::SetNextWindowSize({ width, 0.0F }, ImGui::ImGuiCond_Always);
    }

    std::optional<DynamicForms::FormKind> FormKindFromFilterIndex(const int kindFilter) {
        switch (kindFilter) {
        case 1:
            return DynamicForms::FormKind::Global;
        case 2:
            return DynamicForms::FormKind::Keyword;
        case 3:
            return DynamicForms::FormKind::Outfit;
        case 4:
            return DynamicForms::FormKind::ArmorType;
        case 5:
            return DynamicForms::FormKind::Armor;
        case 6:
            return DynamicForms::FormKind::Color;
        case 7:
            return DynamicForms::FormKind::ArtObject;
        case 8:
            return DynamicForms::FormKind::Perk;
        case 9:
            return DynamicForms::FormKind::HeadPart;
        case 10:
            return DynamicForms::FormKind::SoundDescriptor;
        case 11:
            return DynamicForms::FormKind::Light;
        case 12:
            return DynamicForms::FormKind::Explosion;
        case 13:
            return DynamicForms::FormKind::Activator;
        case 14:
            return DynamicForms::FormKind::EffectShader;
        case 15:
            return DynamicForms::FormKind::NPC;
        default:
            return std::nullopt;
        }
    }

    bool MatchesFilterValues(const DynamicForms::DynamicForm& form, const int kindFilter, const std::string_view editorIdFilter) {
        const auto filterKind = FormKindFromFilterIndex(kindFilter);
        if (filterKind && form.kind != *filterKind) {
            return false;
        }

        const std::string filter(editorIdFilter);
        if (filter.empty()) {
            return true;
        }

        return ToLower(form.editorId).find(ToLower(filter)) != std::string::npos;
    }

    bool MatchesFilters(const DynamicForms::DynamicForm& form) {
        return MatchesFilterValues(form, selectedFilterKind, filterEditorIdBuffer.data());
    }

    bool MatchesExportFilters(const DynamicForms::DynamicForm& form) {
        return MatchesFilterValues(form, selectedExportFilterKind, exportFilterEditorIdBuffer.data());
    }

    bool CanAddToInventory(const DynamicForms::FormKind kind) {
        return kind == DynamicForms::FormKind::Armor ||
            kind == DynamicForms::FormKind::Light;
    }

    bool CanSpawnInWorld(const DynamicForms::FormKind kind) {
        return kind == DynamicForms::FormKind::Armor ||
            kind == DynamicForms::FormKind::Light ||
            kind == DynamicForms::FormKind::Explosion ||
            kind == DynamicForms::FormKind::Activator ||
            kind == DynamicForms::FormKind::NPC;
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
        SetStableComboWidth(FORM_KIND_ITEMS, 220.0F);
        ImGui::Combo("##formType", &selectedFormKind, FORM_KIND_ITEMS.data(), static_cast<int>(FORM_KIND_ITEMS.size()));

        if (SelectedFormKind() == DynamicForms::FormKind::Global) {
            ImGui::Separator();
            ImGui::Text("%s", Configuration::GetLoc("menu.global_settings", "Global settings"));
            SetStableComboWidth(GLOBAL_TYPE_ITEMS, 220.0F);
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
            SetStableComboWidth(ART_TYPE_ITEMS, 260.0F);
            ImGui::Combo(Configuration::GetLoc("menu.art_type", "Art type"), &selectedArtType, ART_TYPE_ITEMS.data(), static_cast<int>(ART_TYPE_ITEMS.size()));
        }
        if (SelectedFormKind() == DynamicForms::FormKind::ArmorType ||
            SelectedFormKind() == DynamicForms::FormKind::Armor) {
            ImGui::Separator();
            ImGui::Text("%s", Configuration::GetLoc("menu.armor_settings", "Armor settings"));
            if (SelectedFormKind() == DynamicForms::FormKind::Armor) {
                ImGui::SetNextItemWidth(260.0F);
                ImGui::InputText(Configuration::GetLoc("menu.full_name", "Name"), createNameBuffer.data(), createNameBuffer.size());
            }
            ImGui::SetNextItemWidth(360.0F);
            ImGui::InputText(Configuration::GetLoc("menu.model_path", "Male world model"), createModelBuffer.data(), createModelBuffer.size());
            ImGui::TextDisabled("%s", Configuration::GetLoc("menu.meshes_path_hint", "Meshes paths are relative to Data/Meshes."));
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
            SetStableComboWidth(HEAD_PART_TYPE_ITEMS, 220.0F);
            ImGui::Combo(Configuration::GetLoc("menu.head_part_type", "Head part type"), &selectedHeadPartType, HEAD_PART_TYPE_ITEMS.data(), static_cast<int>(HEAD_PART_TYPE_ITEMS.size()));
            ImGui::Checkbox(Configuration::GetLoc("menu.playable", "Playable"), &createPlayable);
            ImGui::SameLine();
            ImGui::Checkbox(Configuration::GetLoc("menu.male", "Male"), &createHeadPartMale);
            ImGui::SameLine();
            ImGui::Checkbox(Configuration::GetLoc("menu.female", "Female"), &createHeadPartFemale);
            ImGui::Checkbox(Configuration::GetLoc("menu.is_extra_part", "Is extra part"), &createHeadPartIsExtraPart);
            ImGui::SameLine();
            ImGui::Checkbox(Configuration::GetLoc("menu.use_solid_tint", "Use solid tint"), &createHeadPartUseSolidTint);
            ImGui::SetNextItemWidth(360.0F);
            ImGui::InputText(Configuration::GetLoc("menu.race_morph", "Race Morph"), createRaceMorphBuffer.data(), createRaceMorphBuffer.size());
            ImGui::SetNextItemWidth(360.0F);
            ImGui::InputText(Configuration::GetLoc("menu.tri", "Tri"), createDefaultMorphBuffer.data(), createDefaultMorphBuffer.size());
            ImGui::SetNextItemWidth(360.0F);
            ImGui::InputText(Configuration::GetLoc("menu.chargen_morph", "Chargen Morph"), createChargenMorphBuffer.data(), createChargenMorphBuffer.size());
            ImGui::TextDisabled("%s", Configuration::GetLoc("menu.meshes_path_hint", "Meshes paths are relative to Data/Meshes."));
        }
        if (SelectedFormKind() == DynamicForms::FormKind::SoundDescriptor) {
            ImGui::Separator();
            ImGui::Text("%s", Configuration::GetLoc("menu.sound_description_settings", "Sound Description settings"));
            ImGui::TextDisabled("%s", Configuration::GetLoc("menu.sound_files_hint", "Add sound files after creation."));
        }
        if (SelectedFormKind() == DynamicForms::FormKind::EffectShader) {
            ImGui::Separator();
            ImGui::Text("%s", Configuration::GetLoc("menu.effect_shader_settings", "Effect Shader settings"));
            ImGui::TextDisabled("%s", Configuration::GetLoc("menu.texture_paths_hint", "Texture paths are relative to Data/Textures and can be edited after creation."));
        }
        if (SelectedFormKind() == DynamicForms::FormKind::Light ||
            SelectedFormKind() == DynamicForms::FormKind::Explosion ||
            SelectedFormKind() == DynamicForms::FormKind::Activator) {
            ImGui::Separator();
            ImGui::Text("%s", Configuration::GetLoc("menu.base_settings", "Base settings"));
            ImGui::SetNextItemWidth(260.0F);
            ImGui::InputText(Configuration::GetLoc("menu.full_name", "Name"), createNameBuffer.data(), createNameBuffer.size());
            ImGui::SetNextItemWidth(360.0F);
            ImGui::InputText(Configuration::GetLoc("menu.model_path", "Model path"), createModelBuffer.data(), createModelBuffer.size());
            if (SelectedFormKind() == DynamicForms::FormKind::Light) {
                auto red = static_cast<std::uint8_t>(std::clamp(createColor[0], 0, 255));
                auto green = static_cast<std::uint8_t>(std::clamp(createColor[1], 0, 255));
                auto blue = static_cast<std::uint8_t>(std::clamp(createColor[2], 0, 255));
                if (DrawRGBColorEditor(Configuration::GetLoc("menu.rgb", "RGB"), red, green, blue)) {
                    createColor[0] = red;
                    createColor[1] = green;
                    createColor[2] = blue;
                }
            }
        }
        if (SelectedFormKind() == DynamicForms::FormKind::NPC) {
            ImGui::Separator();
            ImGui::Text("%s", Configuration::GetLoc("menu.npc_settings", "NPC settings"));
            ImGui::SetNextItemWidth(260.0F);
            ImGui::InputText(Configuration::GetLoc("menu.full_name", "Name"), createNameBuffer.data(), createNameBuffer.size());
            auto red = static_cast<std::uint8_t>(std::clamp(createColor[0], 0, 255));
            auto green = static_cast<std::uint8_t>(std::clamp(createColor[1], 0, 255));
            auto blue = static_cast<std::uint8_t>(std::clamp(createColor[2], 0, 255));
            auto alpha = static_cast<std::uint8_t>(std::clamp(createColor[3], 0, 255));
            if (DrawRGBAColorEditor(Configuration::GetLoc("menu.body_tint", "Body tint"), red, green, blue, alpha)) {
                createColor[0] = red;
                createColor[1] = green;
                createColor[2] = blue;
                createColor[3] = alpha;
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
            if (form.kind == DynamicForms::FormKind::ArmorType || form.kind == DynamicForms::FormKind::Armor) {
                form.maleWorldModel = createModelBuffer.data();
            }
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
            if (form.kind == DynamicForms::FormKind::NPC) {
                form.health = 100;
                form.magicka = 50;
                form.stamina = 50;
                form.calcMinLevel = 1;
                form.calcMaxLevel = 1;
                form.npcLevel = 1;
                form.speedMult = 100;
                form.dispositionBase = 35;
                form.height = 1.0F;
                form.weight = 50.0F;
                form.skills.fill(15);
                form.skillOffsets.fill(0);
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
        SetStableComboWidth(GLOBAL_TYPE_ITEMS, 220.0F);
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
            form = Manager::GetForms()[index];
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

    DynamicForms::FormRef ParseDisplayFormRef(const std::string& value) {
        DynamicForms::FormRef ref;
        const auto open = value.rfind(" (");
        if (open != std::string::npos && value.ends_with(')')) {
            ref.editorID = value.substr(0, open);
            ref.formID = value.substr(open + 2, value.size() - open - 3);
            return ref;
        }

        if (value.find('|') != std::string::npos) {
            ref.formID = value;
        } else {
            ref.editorID = value;
        }
        return ref;
    }

    DynamicForms::FormRef MakeFormRef(const InternalFormInfo& info) {
        DynamicForms::FormRef ref;
        ref.editorID = info.editorID;
        ref.formID = FormUtil::NormalizeFormID(RE::TESForm::LookupByID(info.formID));
        return ref;
    }

    std::string ReferenceLabel(const InternalFormInfo& info) {
        const auto ref = MakeFormRef(info);
        auto label = ref.empty() ? PieceLabel(info) : ref.Display();
        if (!info.name.empty() && info.name != info.editorID) {
            label += " - " + info.name;
        }
        return label;
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

            const auto labelText = ReferenceLabel(info);
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
                const auto labelText = ReferenceLabel(info);
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

        SetAvailableComboWidth(360.0F);
        SetFixedComboPopupWidth(360.0F);
        if (ImGui::BeginCombo(label, preview)) {
            const bool listsReady = ListManager::GetSingleton()->IsPopulated();
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
        auto previewText = value.empty() ? std::string(Configuration::GetLoc("common.select", "Select")) : value.Display();

        SetAvailableComboWidth(360.0F);
        SetFixedComboPopupWidth(360.0F);
        if (ImGui::BeginCombo(label, previewText.c_str())) {
            const bool listsReady = ListManager::GetSingleton()->IsPopulated();
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
                    const auto labelText = ReferenceLabel(info);
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
                        const auto labelText = ReferenceLabel(info);
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
        DynamicForms::FormRef ref = ParseDisplayFormRef(value);
        const bool changed = DrawFormReferencePicker(label, typeName, ref);
        if (changed) {
            value = ref.Display();
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
            form = Manager::GetForms()[index];
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
            form = Manager::GetForms()[index];
            return true;
        }
        return false;
    }

    bool RenderArtObjectEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;
        changed |= InputString(Configuration::GetLoc("menu.model_path", "Model path"), edited.modelPath, 420.0F);

        int artType = ArtTypeIndex(edited.artType);
        SetStableComboWidth(ART_TYPE_ITEMS, 260.0F);
        if (ImGui::Combo(Configuration::GetLoc("menu.art_type", "Art type"), &artType, ART_TYPE_ITEMS.data(), static_cast<int>(ART_TYPE_ITEMS.size()))) {
            edited.artType = ArtTypeFromIndex(artType);
            changed = true;
        }

        int minBounds[3]{ edited.boundX1, edited.boundY1, edited.boundZ1 };
        int maxBounds[3]{ edited.boundX2, edited.boundY2, edited.boundZ2 };
        ImGui::SetNextItemWidth(260.0F);
        if (ImGui::InputInt3(Configuration::GetLoc("menu.min_bounds", "Min bounds"), minBounds)) {
            edited.boundX1 = static_cast<std::int16_t>(std::clamp(minBounds[0], -32768, 32767));
            edited.boundY1 = static_cast<std::int16_t>(std::clamp(minBounds[1], -32768, 32767));
            edited.boundZ1 = static_cast<std::int16_t>(std::clamp(minBounds[2], -32768, 32767));
            changed = true;
        }
        ImGui::SetNextItemWidth(260.0F);
        if (ImGui::InputInt3(Configuration::GetLoc("menu.max_bounds", "Max bounds"), maxBounds)) {
            edited.boundX2 = static_cast<std::int16_t>(std::clamp(maxBounds[0], -32768, 32767));
            edited.boundY2 = static_cast<std::int16_t>(std::clamp(maxBounds[1], -32768, 32767));
            edited.boundZ2 = static_cast<std::int16_t>(std::clamp(maxBounds[2], -32768, 32767));
            changed = true;
        }

        if (changed && Manager::UpdateForm(index, edited)) {
            form = Manager::GetForms()[index];
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

    bool DrawPerkConditions(std::vector<DynamicForms::PerkCondition>& conditions);

    bool RenderSoundDescriptorEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;

        if (ImGui::BeginTabBar("##soundDescriptorTabs")) {
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.data", "Data"))) {
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
                if (ImGui::InputInt(Configuration::GetLoc("menu.frequency_shift", "% Frequency Shift"), &frequencyShift)) {
                    edited.frequencyShift = static_cast<std::uint8_t>(std::clamp(frequencyShift, 0, 255));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.frequency_variance", "% Frequency Variance"), &frequencyVariance)) {
                    edited.frequencyVariance = static_cast<std::uint8_t>(std::clamp(frequencyVariance, 0, 255));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.priority", "Priority"), &priority)) {
                    edited.priority = static_cast<std::uint8_t>(std::clamp(priority, 0, 255));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.db_variance", "db Variance"), &dbVariance)) {
                    edited.dbVariance = static_cast<std::uint8_t>(std::clamp(dbVariance, 0, 255));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputFloat(Configuration::GetLoc("menu.static_attenuation_db", "Static Attenuation (db)"), &edited.staticAttenuation)) {
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.looping_raw", "Looping raw"), &looping)) {
                    edited.looping = static_cast<std::uint8_t>(std::clamp(looping, 0, 255));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.rumble_send_value", "Rumble Send Value"), &rumble)) {
                    edited.rumbleSendValue = static_cast<std::uint8_t>(std::clamp(rumble, 0, 255));
                    changed = true;
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.conditions", "Conditions"))) {
                changed |= DrawPerkConditions(edited.conditions);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (changed && Manager::UpdateForm(index, edited)) {
            form = Manager::GetForms()[index];
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
        if (ImGui::InputInt(Configuration::GetLoc("menu.time", "Time"), &time)) {
            edited.lightTime = time;
            changed = true;
        }
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputInt(Configuration::GetLoc("menu.radius", "Radius"), &radius)) {
            edited.lightRadius = static_cast<std::uint32_t>(std::max(radius, 0));
            changed = true;
        }
        changed |= DrawRGBColorEditor(Configuration::GetLoc("menu.rgb", "RGB"), edited.red, edited.green, edited.blue);
        ImGui::TextDisabled("%s", Configuration::GetLoc("menu.light_data_flags", "Light DATA flags"));
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_dynamic", "Dynamic"), edited.flags, 1u << 0);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_can_carry", "Can Carry"), edited.flags, 1u << 1);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_negative", "Negative"), edited.flags, 1u << 2);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_flicker", "Flicker"), edited.flags, 1u << 3);
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
        if (ImGui::InputFloat(Configuration::GetLoc("menu.falloff_exponent", "Falloff Exponent"), &edited.falloffExponent)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat(Configuration::GetLoc("menu.fov", "FOV"), &edited.fov)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat(Configuration::GetLoc("menu.near_clip", "Near Clip"), &edited.nearClip)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat(Configuration::GetLoc("menu.flicker_period", "Flicker Period"), &edited.flickerPeriod)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat(Configuration::GetLoc("menu.intensity_amplitude", "Intensity Amplitude"), &edited.flickerIntensityAmplitude)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat(Configuration::GetLoc("menu.movement_amplitude", "Movement Amplitude"), &edited.flickerMovementAmplitude)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat(Configuration::GetLoc("menu.fade", "Fade"), &edited.fade)) changed = true;
        changed |= DrawFormReferencePicker("Sound", "SoundDescriptor", edited.sound);
        changed |= DrawFormReferencePicker("Lens", "LensFlare", edited.lensFlare);

        if (changed && Manager::UpdateForm(index, edited)) {
            form = Manager::GetForms()[index];
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
        if (ImGui::InputFloat(Configuration::GetLoc("menu.force", "Force"), &edited.force)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat(Configuration::GetLoc("menu.damage", "Damage"), &edited.damage)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat(Configuration::GetLoc("menu.radius", "Radius"), &edited.radius)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat(Configuration::GetLoc("menu.is_radius", "IS Radius"), &edited.imageSpaceRadius)) changed = true;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat(Configuration::GetLoc("menu.vertical_offset_mult", "Vertical Offset Mult"), &edited.verticalOffsetMult)) changed = true;
        ImGui::TextDisabled("%s", Configuration::GetLoc("menu.explosion_data_flags", "Explosion DATA flags"));
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_always_uses_world_orientation", "Always Uses World Orientation"), edited.flags, 1u << 1);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_knock_down_always", "Knock Down - Always"), edited.flags, 1u << 2);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_knock_down_by_formula", "Knock Down - By Formula"), edited.flags, 1u << 3);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_ignore_los_check", "Ignore LOS Check"), edited.flags, 1u << 4);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_push_explosion_source_ref_only", "Push Explosion Source Ref Only"), edited.flags, 1u << 5);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_ignore_image_space_swap", "Ignore Image Space Swap"), edited.flags, 1u << 6);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_chain", "Chain"), edited.flags, 1u << 7);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_no_controller_vibration", "No Controller Vibration"), edited.flags, 1u << 8);
        int soundLevel = static_cast<int>(edited.soundLevel);
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputInt(Configuration::GetLoc("menu.sound_level_raw", "Sound Level raw"), &soundLevel)) {
            edited.soundLevel = static_cast<std::uint32_t>(std::max(soundLevel, 0));
            changed = true;
        }

        if (changed && Manager::UpdateForm(index, edited)) {
            form = Manager::GetForms()[index];
            return true;
        }
        return false;
    }

    bool RenderActivatorEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;
        changed |= InputString(Configuration::GetLoc("menu.full_name", "Name"), edited.fullName);
        changed |= InputString(Configuration::GetLoc("menu.model_path", "Model path"), edited.modelPath, 420.0F);
        changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.sound_looping", "Sound - Looping"), "SoundDescriptor", edited.soundLoop);
        changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.sound_activation", "Sound - Activation"), "SoundDescriptor", edited.soundActivate);
        changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.water_type", "Water Type"), "Water", edited.waterType);
        ImGui::TextDisabled("%s", Configuration::GetLoc("menu.activator_fnam_flags", "Activator FNAM flags"));
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_no_displacement", "No Displacement"), edited.flags, 1u << 0);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_ignored_by_sandbox", "Ignored By Sandbox"), edited.flags, 1u << 1);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_is_procedural_water", "Is Procedural Water"), edited.flags, 1u << 2);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_is_lod_water", "Is LOD Water"), edited.flags, 1u << 3);

        if (changed && Manager::UpdateForm(index, edited)) {
            form = Manager::GetForms()[index];
            return true;
        }
        return false;
    }

    void DrawFloatInput(const char* label, float& value, bool& changed, const float width = 160.0F) {
        ImGui::SetNextItemWidth(width);
        if (ImGui::InputFloat(label, &value)) {
            changed = true;
        }
    }

    bool RenderEffectShaderEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;

        if (ImGui::BeginTabBar("##effectShaderTabs")) {
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.textures", "Textures"))) {
                changed |= InputString("Fill texture", edited.fillTexturePath, 460.0F);
                changed |= InputString("Particle texture", edited.particleShaderTexturePath, 460.0F);
                changed |= InputString(Configuration::GetLoc("menu.holes_texture", "Holes texture"), edited.holesTexturePath, 460.0F);
                changed |= InputString(Configuration::GetLoc("menu.membrane_palette_texture", "Membrane palette texture"), edited.membranePaletteTexturePath, 460.0F);
                changed |= InputString(Configuration::GetLoc("menu.particle_palette_texture", "Particle palette texture"), edited.particlePaletteTexturePath, 460.0F);
                ImGui::TextDisabled("%s", Configuration::GetLoc("menu.texture_paths_hint_short", "Texture paths are relative to Data/Textures."));
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.ambient_sound", "Ambient sound"), "SoundDescriptor", edited.ambientSound);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.flags", "Flags"))) {
                ImGui::Text(Configuration::GetLoc("menu.flags_0x", "Flags: 0x%08X"), edited.flags);
                changed |= FlagCheckbox("No Membrane Shader", edited.flags, 1u << 0);
                changed |= FlagCheckbox("Membrane Grayscale Color", edited.flags, 1u << 1);
                changed |= FlagCheckbox("Membrane Grayscale Alpha", edited.flags, 1u << 2);
                changed |= FlagCheckbox("No Particle Shader", edited.flags, 1u << 3);
                changed |= FlagCheckbox("Edge Effect Inverse", edited.flags, 1u << 4);
                changed |= FlagCheckbox("Affect Skin Only", edited.flags, 1u << 5);
                changed |= FlagCheckbox("Ignore Alpha", edited.flags, 1u << 6);
                changed |= FlagCheckbox("Project UVs", edited.flags, 1u << 7);
                changed |= FlagCheckbox("Ignore Base Geometry Alpha", edited.flags, 1u << 8);
                changed |= FlagCheckbox("Lighting", edited.flags, 1u << 9);
                changed |= FlagCheckbox("No Weapons", edited.flags, 1u << 10);
                changed |= FlagCheckbox("Unknown 11", edited.flags, 1u << 11);
                changed |= FlagCheckbox("Unknown 12", edited.flags, 1u << 12);
                changed |= FlagCheckbox("Unknown 13", edited.flags, 1u << 13);
                changed |= FlagCheckbox("Unknown 14", edited.flags, 1u << 14);
                changed |= FlagCheckbox("Particle Animated", edited.flags, 1u << 15);
                changed |= FlagCheckbox("Particle Grayscale Color", edited.flags, 1u << 16);
                changed |= FlagCheckbox("Particle Grayscale Alpha", edited.flags, 1u << 17);
                changed |= FlagCheckbox("Unknown 18", edited.flags, 1u << 18);
                changed |= FlagCheckbox("Unknown 19", edited.flags, 1u << 19);
                changed |= FlagCheckbox("Unknown 20", edited.flags, 1u << 20);
                changed |= FlagCheckbox("Unknown 21", edited.flags, 1u << 21);
                changed |= FlagCheckbox("Unknown 22", edited.flags, 1u << 22);
                changed |= FlagCheckbox("Unknown 23", edited.flags, 1u << 23);
                changed |= FlagCheckbox("Use Blood Geometry", edited.flags, 1u << 24);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.fill", "Fill"))) {
                changed |= DrawRGBAColorEditor("Fill color 1", edited.fillColor1Red, edited.fillColor1Green, edited.fillColor1Blue, edited.fillColor1Alpha);
                changed |= DrawRGBAColorEditor("Fill color 2", edited.fillColor2Red, edited.fillColor2Green, edited.fillColor2Blue, edited.fillColor2Alpha);
                changed |= DrawRGBAColorEditor("Fill color 3", edited.fillColor3Red, edited.fillColor3Green, edited.fillColor3Blue, edited.fillColor3Alpha);
                DrawFloatInput("Alpha fade in", edited.fillAlphaFadeIn, changed);
                DrawFloatInput("Full alpha time", edited.fillFullAlphaTime, changed);
                DrawFloatInput("Alpha fade out", edited.fillAlphaFadeOut, changed);
                DrawFloatInput("Persistent alpha ratio", edited.fillPersistentAlphaRatio, changed);
                DrawFloatInput("Alpha pulse amplitude", edited.fillAlphaPulseAmplitude, changed);
                DrawFloatInput("Alpha pulse frequency", edited.fillAlphaPulseFrequency, changed);
                DrawFloatInput("Animation speed U", edited.fillTextureAnimationSpeedU, changed);
                DrawFloatInput("Animation speed V", edited.fillTextureAnimationSpeedV, changed);
                DrawFloatInput("Texture scale U", edited.fillTextureScaleU, changed);
                DrawFloatInput("Texture scale V", edited.fillTextureScaleV, changed);
                DrawFloatInput("Full alpha ratio", edited.fillFullAlphaRatio, changed);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.edge", "Edge"))) {
                changed |= DrawRGBAColorEditor("Edge effect color", edited.edgeEffectRed, edited.edgeEffectGreen, edited.edgeEffectBlue, edited.edgeEffectAlpha);
                changed |= DrawRGBAColorEditor("Edge color", edited.edgeColorRed, edited.edgeColorGreen, edited.edgeColorBlue, edited.edgeColorAlpha);
                DrawFloatInput("Falloff", edited.edgeFalloff, changed);
                DrawFloatInput("Alpha fade in", edited.edgeAlphaFadeIn, changed);
                DrawFloatInput("Full alpha time", edited.edgeFullAlphaTime, changed);
                DrawFloatInput("Alpha fade out", edited.edgeAlphaFadeOut, changed);
                DrawFloatInput("Persistent alpha ratio", edited.edgePersistentAlphaRatio, changed);
                DrawFloatInput("Alpha pulse amplitude", edited.edgeAlphaPulseAmplitude, changed);
                DrawFloatInput("Alpha pulse frequency", edited.edgeAlphaPulseFrequency, changed);
                DrawFloatInput("Full alpha ratio", edited.edgeFullAlphaRatio, changed);
                DrawFloatInput("Width alpha units", edited.edgeWidthAlphaUnits, changed);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.particles", "Particles"))) {
                changed |= DrawRGBAColorEditor("Color 1", edited.particleColor1Red, edited.particleColor1Green, edited.particleColor1Blue, edited.particleColor1Alpha);
                changed |= DrawRGBAColorEditor("Color 2", edited.particleColor2Red, edited.particleColor2Green, edited.particleColor2Blue, edited.particleColor2Alpha);
                changed |= DrawRGBAColorEditor("Color 3", edited.particleColor3Red, edited.particleColor3Green, edited.particleColor3Blue, edited.particleColor3Alpha);
                DrawFloatInput("Birth ramp up time", edited.particleBirthRampUpTime, changed);
                DrawFloatInput("Full birth time", edited.particleFullBirthTime, changed);
                DrawFloatInput("Birth ramp down time", edited.particleBirthRampDownTime, changed);
                DrawFloatInput("Full birth ratio", edited.particleFullBirthRatio, changed);
                DrawFloatInput("Particle count", edited.particleCount, changed);
                DrawFloatInput("Lifetime", edited.particleLifetime, changed);
                DrawFloatInput("Lifetime variance", edited.particleLifetimeVariance, changed);
                DrawFloatInput("Initial speed along normal", edited.particleInitialSpeedAlongNormal, changed);
                DrawFloatInput("Acceleration along normal", edited.particleAccelerationAlongNormal, changed);
                DrawFloatInput("Scale key 1", edited.particleScaleKey1, changed);
                DrawFloatInput("Scale key 2", edited.particleScaleKey2, changed);
                DrawFloatInput("Scale key 1 time", edited.particleScaleKey1Time, changed);
                DrawFloatInput("Scale key 2 time", edited.particleScaleKey2Time, changed);
                DrawFloatInput("Color 1 alpha", edited.particleColor1AlphaValue, changed);
                DrawFloatInput("Color 2 alpha", edited.particleColor2AlphaValue, changed);
                DrawFloatInput("Color 3 alpha", edited.particleColor3AlphaValue, changed);
                DrawFloatInput("Color 1 time", edited.particleColor1Time, changed);
                DrawFloatInput("Color 2 time", edited.particleColor2Time, changed);
                DrawFloatInput("Color 3 time", edited.particleColor3Time, changed);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (changed && Manager::UpdateForm(index, edited)) {
            form = Manager::GetForms()[index];
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

    const char* ListTypeForConditionParam(const std::string_view rawType) {
        if (rawType == "ptGlobal") {
            return "Global";
        }
        if (rawType == "ptPerk") {
            return "Perk";
        }
        if (rawType == "ptQuest") {
            return "Quest";
        }
        if (rawType == "ptMagicItem") {
            return "Spell";
        }
        if (rawType == "ptKeyword") {
            return "Keyword";
        }
        if (rawType == "ptFormList") {
            return "FormList";
        }
        if (rawType == "ptObjectReference" || rawType == "ptActor" || rawType == "ptReferencableObject") {
            return "Activator";
        }
        if (rawType == "ptInventoryObject") {
            return "Armor";
        }
        if (rawType == "ptLocation") {
            return "FormList";
        }
        if (rawType == "ptActorBase") {
            return "NPC";
        }
        return nullptr;
    }

    std::string ConditionFunctionPreview(const DynamicForms::PerkCondition& condition) {
        const auto id = ConditionFunctionIdForUi(condition);
        return ConditionCatalog::GetFunctionName(id);
    }

    bool DrawConditionFunctionPicker(DynamicForms::PerkCondition& condition) {
        bool changed = false;
        const auto preview = ConditionFunctionPreview(condition);
        SetAvailableComboWidth(420.0F);
        SetFixedComboPopupWidth(420.0F);
        if (ImGui::BeginCombo(Configuration::GetLoc("menu.function", "Function"), preview.c_str())) {
            const auto functions = ConditionCatalog::GetFunctions();
            char searchBuf[256]{};
            strcpy_s(searchBuf, conditionFunctionFilter.c_str());
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::InputText("##conditionFunctionFilter", searchBuf, sizeof(searchBuf))) {
                conditionFunctionFilter = searchBuf;
            }
            ImGui::Separator();

            std::vector<const ConditionCatalog::FunctionInfo*> rows;
            rows.reserve(functions.size());
            const auto search = ToLower(conditionFunctionFilter);
            for (const auto& function : functions) {
                if (!search.empty() && ToLower(function.name).find(search) == std::string::npos) {
                    continue;
                }
                rows.push_back(&function);
            }
            std::ranges::sort(rows, [](const auto* lhs, const auto* rhs) {
                return ToLower(lhs->name) < ToLower(rhs->name);
            });

            auto* clipper = ImGui::ImGuiListClipperManager::Create();
            ImGui::ImGuiListClipperManager::Begin(clipper, static_cast<int>(rows.size()), 0.0F);
            while (ImGui::ImGuiListClipperManager::Step(clipper)) {
                for (int rowIndex = clipper->DisplayStart; rowIndex < clipper->DisplayEnd; ++rowIndex) {
                    const auto& function = *rows[static_cast<std::size_t>(rowIndex)];
                    const std::string label = function.name;
                    if (ImGui::Selectable(label.c_str(), ConditionFunctionIdForUi(condition) == function.id)) {
                        condition.kind = DynamicForms::PerkConditionKind::Raw;
                        condition.functionId = function.id;
                        condition.functionName = function.name;
                        conditionFunctionFilter.clear();
                        changed = true;
                    }
                }
            }
            ImGui::ImGuiListClipperManager::End(clipper);
            ImGui::ImGuiListClipperManager::Destroy(clipper);
            ImGui::EndCombo();
        }
        return changed;
    }

    bool DrawConditionParam(const char* label, const char* rawType, std::string& value) {
        if (!rawType || std::string_view(rawType) == "ptNone") {
            return false;
        }

        bool changed = false;
        ImGui::TextDisabled("%s: %s", label, rawType);
        if (const auto* listType = ListTypeForConditionParam(rawType)) {
            changed |= DrawFormReferencePicker(label, listType, value);
        } else {
            changed |= InputString(label, value, 360.0F);
        }
        return changed;
    }

    bool DrawConditionEditor(DynamicForms::PerkCondition& condition, const char* idPrefix) {
        bool changed = false;
        ImGui::PushID(idPrefix);

        changed |= DrawConditionFunctionPicker(condition);

        const auto* functionInfo = ConditionCatalog::FindFunction(ConditionFunctionIdForUi(condition));

        int opCode = static_cast<int>(condition.opCode);
        SetStableComboWidth(CONDITION_OP_ITEMS, 160.0F);
        if (ImGui::Combo(Configuration::GetLoc("menu.operator", "Operator"), &opCode, CONDITION_OP_ITEMS.data(), static_cast<int>(CONDITION_OP_ITEMS.size()))) {
            condition.opCode = static_cast<std::uint32_t>(opCode);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox(Configuration::GetLoc("menu.or", "OR"), &condition.isOr)) {
            changed = true;
        }

        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat(Configuration::GetLoc("menu.comparison", "Comparison"), &condition.comparisonValue)) {
            changed = true;
        }

        if (ImGui::Checkbox(Configuration::GetLoc("menu.swap_target", "Swap target"), &condition.swapTarget)) {
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox(Configuration::GetLoc("menu.use_pack_data", "Use pack data"), &condition.usePackData)) {
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox(Configuration::GetLoc("menu.use_aliases", "Use aliases"), &condition.useAliases)) {
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox(Configuration::GetLoc("menu.global_comparison", "Global comparison"), &condition.useGlobalComparison)) {
            changed = true;
        }
        if (condition.useGlobalComparison) {
            changed |= DrawFormReferencePicker("Comparison global", "Global", condition.comparisonGlobal);
        }

        int runOn = static_cast<int>(std::min<std::uint32_t>(condition.runOn, static_cast<std::uint32_t>(CONDITION_RUN_ON_ITEMS.size() - 1)));
        SetStableComboWidth(CONDITION_RUN_ON_ITEMS, 220.0F);
        if (ImGui::Combo(Configuration::GetLoc("menu.run_on", "Run on"), &runOn, CONDITION_RUN_ON_ITEMS.data(), static_cast<int>(CONDITION_RUN_ON_ITEMS.size()))) {
            condition.runOn = static_cast<std::uint32_t>(runOn);
            changed = true;
        }

        if (condition.runOn == 2) {
            changed |= InputString(Configuration::GetLoc("menu.run_on_ref", "Run on ref"), condition.runOnRef, 360.0F);
        } else {
            ImGui::TextDisabled("%s", Configuration::GetLoc("menu.run_on_ref_hint", "Run on ref is used only when Run on = Reference."));
        }

        if (condition.runOn == 5 || condition.runOn == 6 || condition.runOn == 7) {
            int dataId = static_cast<int>(condition.dataId);
            ImGui::SetNextItemWidth(160.0F);
            if (ImGui::InputInt(Configuration::GetLoc("menu.data_id", "Data ID"), &dataId)) {
                condition.dataId = static_cast<std::uint32_t>(std::max(dataId, 0));
                changed = true;
            }
        } else {
            ImGui::TextDisabled("%s", Configuration::GetLoc("menu.data_id_unused_hint", "Data ID is unused for this Run on mode."));
        }

        if (functionInfo) {
            changed |= DrawConditionParam("Param 1", functionInfo->rawParam1, condition.param1);
            changed |= DrawConditionParam("Param 2", functionInfo->rawParam2, condition.param2);
            if (std::string_view(functionInfo->rawParam3) != "ptNone") {
                ImGui::TextColored({ 1.0F, 0.75F, 0.35F, 1.0F }, Configuration::GetLoc("menu.param_3_exists_in_catalog_but_this_commonlib_condition_layou", "Param 3 exists in catalog (%s), but this CommonLib condition layout exposes params[2]."), functionInfo->rawParam3);
            }
        } else {
            changed |= InputString("Param 1", condition.param1, 360.0F);
            changed |= InputString("Param 2", condition.param2, 360.0F);
        }
        ImGui::PopID();
        return changed;
    }

    bool DrawPerkConditions(std::vector<DynamicForms::PerkCondition>& conditions) {
        bool changed = false;
        if (ImGui::Button(Configuration::GetLoc("menu.add_condition", "Add condition"))) {
            DynamicForms::PerkCondition condition;
            condition.kind = DynamicForms::PerkConditionKind::Raw;
            condition.functionId = 277;
            condition.functionName = ConditionCatalog::GetFunctionName(condition.functionId);
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
        if (ImGui::Button(Configuration::GetLoc("menu.add_entry", "Add entry"))) {
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
                if (ImGui::InputInt(Configuration::GetLoc("menu.rank", "Rank"), &rank)) {
                    entry.rank = static_cast<std::uint32_t>(std::max(rank, 0));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.priority", "Priority"), &priority)) {
                    entry.priority = static_cast<std::uint32_t>(std::max(priority, 0));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.entry_point", "Entry point"), &entryPoint)) {
                    entry.entryPoint = static_cast<std::uint32_t>(std::clamp(entryPoint, 0, 91));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.function", "Function"), &function)) {
                    entry.function = static_cast<std::uint32_t>(std::clamp(function, 1, 15));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.num_args", "Num args"), &numArgs)) {
                    entry.numArgs = static_cast<std::uint32_t>(std::max(numArgs, 0));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputFloat(Configuration::GetLoc("menu.value", "Value"), &entry.value)) {
                    changed = true;
                }
                ImGui::Separator();
                ImGui::Text("%s", Configuration::GetLoc("menu.entry_conditions", "Entry conditions"));
                changed |= DrawPerkConditions(entry.conditions);
            }
            ImGui::PopID();
        }
        return changed;
    }

    bool DrawFormRefListEditor(const char* label, const char* typeName, std::vector<DynamicForms::FormRef>& values) {
        bool changed = false;
        ImGui::Text("%s: %zu", label, values.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            DrawMoveButtons(values, i, changed);
            changed |= DrawFormReferencePicker(label, typeName, values[i]);
            ImGui::SameLine();
            if (ImGui::SmallButton(Configuration::GetLoc("menu.remove", "Remove"))) {
                values.erase(values.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        const auto button = std::format("Add {}", label);
        if (ImGui::Button(button.c_str())) {
            values.emplace_back();
            changed = true;
        }
        return changed;
    }

    bool DrawRankedFormRefListEditor(const char* label, const char* typeName, std::vector<DynamicForms::RankedFormRef>& values) {
        bool changed = false;
        ImGui::Text("%s: %zu", label, values.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            DrawMoveButtons(values, i, changed);
            changed |= DrawFormReferencePicker(label, typeName, values[i].form);
            ImGui::SameLine();
            int rank = values[i].rank;
            ImGui::SetNextItemWidth(90.0F);
            if (ImGui::InputInt(Configuration::GetLoc("menu.rank", "Rank"), &rank)) {
                values[i].rank = rank;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(Configuration::GetLoc("menu.remove", "Remove"))) {
                values.erase(values.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        const auto button = std::format("Add {}", label);
        if (ImGui::Button(button.c_str())) {
            values.emplace_back();
            changed = true;
        }
        return changed;
    }

    bool DrawBipedDataEditor(DynamicForms::DynamicForm& edited) {
        bool changed = false;
        int armorType = ArmorTypeIndex(edited.armorType);
        SetStableComboWidth(ARMOR_TYPE_ITEMS, 220.0F);
        if (ImGui::Combo(Configuration::GetLoc("menu.armor_type", "Armor type"), &armorType, ARMOR_TYPE_ITEMS.data(), static_cast<int>(ARMOR_TYPE_ITEMS.size()))) {
            edited.armorType = ArmorTypeFromIndex(armorType);
            changed = true;
        }

        if (ImGui::TreeNode(Configuration::GetLoc("menu.biped_slots", "Biped slots"))) {
            for (std::size_t i = 0; i < BIPED_SLOT_ITEMS.size(); ++i) {
                const auto mask = static_cast<std::uint32_t>(1u << i);
                bool selected = (edited.bipedSlots & mask) != 0;
                if (ImGui::Checkbox(BIPED_SLOT_ITEMS[i], &selected)) {
                    if (selected) {
                        edited.bipedSlots |= mask;
                    } else {
                        edited.bipedSlots &= ~mask;
                    }
                    changed = true;
                }
                if ((i + 1) % 4 != 0) {
                    ImGui::SameLine();
                }
            }
            ImGui::TreePop();
        }

        return changed;
    }

    bool DrawArmorModelEditor(DynamicForms::DynamicForm& edited, const bool includeIcons) {
        bool changed = false;
        changed |= InputString(Configuration::GetLoc("menu.male_world_model", "Male world model"), edited.maleWorldModel, 460.0F);
        changed |= InputString(Configuration::GetLoc("menu.female_world_model", "Female world model"), edited.femaleWorldModel, 460.0F);
        changed |= InputString(Configuration::GetLoc("menu.male_first_person_model", "Male 1st person model"), edited.maleFirstPersonModel, 460.0F);
        changed |= InputString(Configuration::GetLoc("menu.female_first_person_model", "Female 1st person model"), edited.femaleFirstPersonModel, 460.0F);
        if (includeIcons) {
            ImGui::Separator();
            changed |= InputString(Configuration::GetLoc("menu.male_inventory_icon", "Male inventory icon"), edited.maleInventoryIcon, 460.0F);
            changed |= InputString(Configuration::GetLoc("menu.female_inventory_icon", "Female inventory icon"), edited.femaleInventoryIcon, 460.0F);
            changed |= InputString(Configuration::GetLoc("menu.male_message_icon", "Male message icon"), edited.maleMessageIcon, 460.0F);
            changed |= InputString(Configuration::GetLoc("menu.female_message_icon", "Female message icon"), edited.femaleMessageIcon, 460.0F);
        }
        ImGui::TextDisabled("%s", Configuration::GetLoc("menu.meshes_and_textures_hint", "Meshes paths are relative to Data/Meshes. Texture paths in texture sets are relative to Data/Textures."));
        return changed;
    }

    bool RenderArmorTypeEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;

        if (ImGui::BeginTabBar("##armorTypeTabs")) {
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.data", "Data"))) {
                changed |= DrawBipedDataEditor(edited);
                changed |= DrawFormReferencePicker("Race", "Race", edited.race);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.models", "Models"))) {
                changed |= DrawArmorModelEditor(edited, false);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.references", "References"))) {
                changed |= DrawFormReferencePicker("Male skin texture", "TextureSet", edited.maleSkinTexture);
                changed |= DrawFormReferencePicker("Female skin texture", "TextureSet", edited.femaleSkinTexture);
                changed |= DrawFormReferencePicker("Male skin texture swap list", "FormList", edited.maleSkinTextureSwapList);
                changed |= DrawFormReferencePicker("Female skin texture swap list", "FormList", edited.femaleSkinTextureSwapList);
                changed |= DrawFormReferencePicker("Footstep set", "FootstepSet", edited.footstepSet);
                changed |= DrawFormReferencePicker("Art object", "ArtObject", edited.armorArtObject);
                ImGui::Separator();
                changed |= DrawFormRefListEditor("Additional race", "Race", edited.additionalRaces);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (changed && Manager::UpdateForm(index, edited)) {
            form = Manager::GetForms()[index];
            return true;
        }
        return false;
    }

    bool RenderArmorEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;

        if (ImGui::BeginTabBar("##armorTabs")) {
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.data", "Data"))) {
                changed |= InputString(Configuration::GetLoc("menu.full_name", "Name"), edited.fullName);
                changed |= DrawBipedDataEditor(edited);
                int value = edited.armorValue;
                int enchantmentAmount = edited.enchantmentAmount;
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.value", "Value"), &value)) {
                    edited.armorValue = value;
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputFloat(Configuration::GetLoc("menu.weight", "Weight"), &edited.armorWeight)) {
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputFloat(Configuration::GetLoc("menu.armor_rating", "Armor rating"), &edited.armorRating)) {
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.enchantment_amount", "Enchantment amount"), &enchantmentAmount)) {
                    edited.enchantmentAmount = static_cast<std::uint16_t>(std::clamp(enchantmentAmount, 0, 65535));
                    changed = true;
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.models", "Models"))) {
                changed |= DrawArmorModelEditor(edited, true);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.references", "References"))) {
                changed |= DrawFormReferencePicker("Race", "Race", edited.race);
                changed |= DrawFormReferencePicker("Equip slot", "EquipSlot", edited.equipSlot);
                changed |= DrawFormReferencePicker("Enchantment", "Enchantment", edited.enchantment);
                changed |= DrawFormReferencePicker("Template armor", "Armor", edited.templateArmor);
                changed |= DrawFormReferencePicker("Pickup sound", "SoundDescriptor", edited.pickupSound);
                changed |= DrawFormReferencePicker("Putdown sound", "SoundDescriptor", edited.putdownSound);
                changed |= DrawFormReferencePicker("Block bash impact data set", "ImpactDataSet", edited.blockBashImpactDataSet);
                changed |= DrawFormReferencePicker("Alt block material type", "MaterialType", edited.altBlockMaterialType);
                ImGui::Separator();
                changed |= DrawFormRefListEditor("Armor type", "ArmorType", edited.armorAddons);
                changed |= DrawFormRefListEditor("Keyword", "Keyword", edited.keywords);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (changed && Manager::UpdateForm(index, edited)) {
            form = Manager::GetForms()[index];
            return true;
        }
        return false;
    }

    bool DrawTintLayerEditor(std::vector<DynamicForms::TintLayer>& values) {
        bool changed = false;
        ImGui::Text(Configuration::GetLoc("menu.tint_layers_u", "Tint layers: %zu"), values.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            auto& layer = values[i];
            const auto header = std::format("Tint layer {}##tint", i);
            if (ImGui::CollapsingHeader(header.c_str())) {
                DrawMoveButtons(values, i, changed);
                if (ImGui::SmallButton(Configuration::GetLoc("menu.remove", "Remove"))) {
                    values.erase(values.begin() + static_cast<std::ptrdiff_t>(i));
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                int index = layer.index;
                int preset = layer.preset;
                ImGui::SetNextItemWidth(140.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.tint_index", "Tint index"), &index)) {
                    layer.index = static_cast<std::uint16_t>(std::clamp(index, 0, 65535));
                    changed = true;
                }
                ImGui::SetNextItemWidth(140.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.preset", "Preset"), &preset)) {
                    layer.preset = static_cast<std::uint16_t>(std::clamp(preset, 0, 65535));
                    changed = true;
                }
                ImGui::SetNextItemWidth(140.0F);
                if (ImGui::InputFloat(Configuration::GetLoc("menu.interpolation", "Interpolation"), &layer.interpolation)) {
                    changed = true;
                }
                changed |= DrawRGBAColorEditor("Color", layer.red, layer.green, layer.blue, layer.alpha);
            }
            ImGui::PopID();
        }
        if (ImGui::Button(Configuration::GetLoc("menu.add_tint_layer", "Add tint layer"))) {
            values.emplace_back();
            changed = true;
        }
        return changed;
    }

    bool RenderPerkEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;

        if (ImGui::BeginTabBar("##perkTabs")) {
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.data", "Data"))) {
                changed |= InputString(Configuration::GetLoc("menu.full_name", "Name"), edited.fullName);
                changed |= InputString("Description", edited.description, 460.0F);
                int level = edited.level;
                int numRanks = edited.numRanks;
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.level", "Level"), &level)) {
                    edited.level = static_cast<std::int8_t>(std::clamp(level, -128, 127));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.num_ranks", "Num ranks"), &numRanks)) {
                    edited.numRanks = static_cast<std::int8_t>(std::clamp(numRanks, 0, 127));
                    changed = true;
                }
                if (ImGui::Checkbox(Configuration::GetLoc("menu.trait", "Trait"), &edited.trait)) {
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox(Configuration::GetLoc("menu.playable", "Playable"), &edited.playable)) {
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox(Configuration::GetLoc("menu.hidden", "Hidden"), &edited.hidden)) {
                    changed = true;
                }
                changed |= DrawFormReferencePicker("Next perk", "Perk", edited.nextPerk);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.conditions", "Conditions"))) {
                changed |= DrawPerkConditions(edited.conditions);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.effects", "Effects"))) {
                changed |= DrawPerkEntries(edited.entries);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.debug", "Debug"))) {
                ImGui::Text(Configuration::GetLoc("menu.conditions_u", "conditions: %zu"), edited.conditions.size());
                ImGui::Text(Configuration::GetLoc("menu.entries_u", "entries: %zu"), edited.entries.size());
                for (std::size_t i = 0; i < edited.conditions.size(); ++i) {
                    const auto& condition = edited.conditions[i];
                    ImGui::Text(Configuration::GetLoc("menu.condition_u_kind_function_op_cmp_p1_p2", "condition[%zu]: kind=%d function=%u op=%u cmp=%.3f p1=%s p2=%s"),
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
                    ImGui::Text(Configuration::GetLoc("menu.entry_u_ep_function_rank_priority_value_conditions_u", "entry[%zu]: ep=%u function=%u rank=%u priority=%u value=%.3f conditions=%zu"),
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
            form = Manager::GetForms()[index];
            return true;
        }
        return false;
    }

    bool DrawHeadPartExtraPicker(DynamicForms::DynamicForm& edited) {
        bool changed = false;
        auto& filter = formPickerFilters["headpart_extra_parts"];

        SetAvailableComboWidth(360.0F);
        SetFixedComboPopupWidth(360.0F);
        if (ImGui::BeginCombo(Configuration::GetLoc("menu.add_extra_part", "Add extra part"), Configuration::GetLoc("common.select", "Select"))) {
            const bool listsReady = ListManager::GetSingleton()->IsPopulated();
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
                    const auto labelText = ReferenceLabel(info);
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
                        const auto labelText = ReferenceLabel(info);
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
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.data", "Data"))) {
                changed |= InputString(Configuration::GetLoc("menu.full_name", "Name"), edited.fullName);
                int typeIndex = HeadPartTypeIndex(edited.headPartType);
                SetStableComboWidth(HEAD_PART_TYPE_ITEMS, 220.0F);
                if (ImGui::Combo(Configuration::GetLoc("menu.type", "Type"), &typeIndex, HEAD_PART_TYPE_ITEMS.data(), static_cast<int>(HEAD_PART_TYPE_ITEMS.size()))) {
                    edited.headPartType = HeadPartTypeFromIndex(typeIndex);
                    changed = true;
                }
                if (ImGui::Checkbox(Configuration::GetLoc("menu.playable", "Playable"), &edited.playable)) {
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox(Configuration::GetLoc("menu.male", "Male"), &edited.male)) {
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox(Configuration::GetLoc("menu.female", "Female"), &edited.female)) {
                    changed = true;
                }
                if (ImGui::Checkbox(Configuration::GetLoc("menu.is_extra_part", "Is extra part"), &edited.isExtraPart)) {
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox(Configuration::GetLoc("menu.use_solid_tint", "Use solid tint"), &edited.useSolidTint)) {
                    changed = true;
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.files", "Files"))) {
                changed |= InputString(Configuration::GetLoc("menu.nif_model", "NIF model"), edited.modelPath, 460.0F);
                changed |= InputString(Configuration::GetLoc("menu.race_morph", "Race Morph"), edited.raceMorphPath, 460.0F);
                changed |= InputString(Configuration::GetLoc("menu.tri", "Tri"), edited.defaultMorphPath, 460.0F);
                changed |= InputString(Configuration::GetLoc("menu.chargen_morph", "Chargen Morph"), edited.chargenMorphPath, 460.0F);
                ImGui::TextDisabled("%s", Configuration::GetLoc("menu.meshes_and_textures_hint", "Meshes paths are relative to Data/Meshes. Texture paths in texture sets are relative to Data/Textures."));
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.references", "References"))) {
                changed |= DrawFormReferencePicker("Texture set", "TextureSet", edited.textureSet);
                changed |= DrawFormReferencePicker("Color", "Color", edited.colorForm);
                changed |= DrawFormReferencePicker("Valid races", "FormList", edited.validRaces);
                ImGui::Separator();
                ImGui::Text(Configuration::GetLoc("menu.extra_parts_u", "Extra parts: %zu"), edited.extraParts.size());
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

            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.debug", "Debug"))) {
                ImGui::Text(Configuration::GetLoc("menu.type", "type=%d"), static_cast<int>(edited.headPartType));
                ImGui::Text(Configuration::GetLoc("menu.flags_playable_male_female_extra_solidtint", "flags playable=%d male=%d female=%d extra=%d solidTint=%d"),
                    edited.playable,
                    edited.male,
                    edited.female,
                    edited.isExtraPart,
                    edited.useSolidTint);
                ImGui::Text(Configuration::GetLoc("menu.model", "model=%s"), edited.modelPath.c_str());
                ImGui::Text(Configuration::GetLoc("menu.racemorph", "raceMorph=%s"), edited.raceMorphPath.c_str());
                ImGui::Text(Configuration::GetLoc("menu.defaultmorph", "defaultMorph=%s"), edited.defaultMorphPath.c_str());
                ImGui::Text(Configuration::GetLoc("menu.chargenmorph", "chargenMorph=%s"), edited.chargenMorphPath.c_str());
                ImGui::Text(Configuration::GetLoc("menu.textureset", "textureSet=%s"), edited.textureSet.Display().c_str());
                ImGui::Text(Configuration::GetLoc("menu.color", "color=%s"), edited.colorForm.Display().c_str());
                ImGui::Text(Configuration::GetLoc("menu.validraces", "validRaces=%s"), edited.validRaces.Display().c_str());
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (changed && Manager::UpdateForm(index, edited)) {
            form = Manager::GetForms()[index];
            return true;
        }
        return false;
    }

    bool RenderNPCEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;

        if (ImGui::BeginTabBar("##npcTabs")) {
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.data", "Data"))) {
                changed |= InputString(Configuration::GetLoc("menu.full_name", "Name"), edited.fullName);
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputFloat(Configuration::GetLoc("menu.height", "Height"), &edited.height)) changed = true;
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputFloat(Configuration::GetLoc("menu.weight", "Weight"), &edited.weight)) changed = true;
                changed |= DrawRGBAColorEditor("Body tint", edited.red, edited.green, edited.blue, edited.alpha);

                if (ImGui::Checkbox(Configuration::GetLoc("menu.female", "Female"), &edited.femaleNpc)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox(Configuration::GetLoc("menu.opposite_gender_anim", "Opposite gender anim"), &edited.oppositeGenderAnim)) changed = true;
                if (ImGui::Checkbox(Configuration::GetLoc("menu.essential", "Essential"), &edited.essential)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox(Configuration::GetLoc("menu.protected", "Protected"), &edited.protectedNpc)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox(Configuration::GetLoc("menu.unique", "Unique"), &edited.unique)) changed = true;
                if (ImGui::Checkbox(Configuration::GetLoc("menu.calc_stats", "Calc stats"), &edited.calcStats)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox(Configuration::GetLoc("menu.respawn", "Respawn"), &edited.respawn)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox(Configuration::GetLoc("menu.no_stealth_meter", "No stealth meter"), &edited.doesntAffectStealthMeter)) changed = true;
                if (ImGui::Checkbox(Configuration::GetLoc("menu.doesn_t_bleed", "Doesn't bleed"), &edited.doesntBleed)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox(Configuration::GetLoc("menu.bleedout_override_flag", "Bleedout override flag"), &edited.bleedoutOverrideFlag)) changed = true;
                if (ImGui::Checkbox(Configuration::GetLoc("menu.simple_actor", "Simple actor"), &edited.simpleActor)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox(Configuration::GetLoc("menu.no_activation", "No activation"), &edited.noActivation)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox(Configuration::GetLoc("menu.ghost", "Ghost"), &edited.ghost)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox(Configuration::GetLoc("menu.invulnerable", "Invulnerable"), &edited.invulnerable)) changed = true;
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.refs", "Refs"))) {
                changed |= DrawFormReferencePicker("Race", "Race", edited.race);
                changed |= DrawFormReferencePicker("Skin", "Armor", edited.skin);
                changed |= DrawFormReferencePicker("Default outfit", "Outfit", edited.defaultOutfit);
                changed |= DrawFormReferencePicker("Sleep outfit", "Outfit", edited.sleepOutfit);
                changed |= DrawFormReferencePicker("Voice", "Voice", edited.voice);
                changed |= DrawFormReferencePicker("Hair color", "Color", edited.hairColor);
                changed |= DrawFormReferencePicker("Face texture", "TextureSet", edited.faceTexture);
                changed |= DrawFormReferencePicker("Class", "Class", edited.npcClass);
                changed |= DrawFormReferencePicker("Combat style", "CombatStyle", edited.combatStyle);
                changed |= DrawFormReferencePicker("Gift filter", "FormList", edited.giftFilter);
                changed |= DrawFormReferencePicker("Death item", "LeveledItem", edited.deathItem);
                changed |= DrawFormReferencePicker("Default package list", "FormList", edited.defaultPackageList);
                changed |= DrawFormReferencePicker("Crime faction", "Faction", edited.crimeFaction);
                int soundLevel = static_cast<int>(edited.soundLevel);
                ImGui::SetNextItemWidth(140.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.sound_level_raw", "Sound level raw"), &soundLevel)) {
                    edited.soundLevel = static_cast<std::uint32_t>(std::max(soundLevel, 0));
                    changed = true;
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.ai", "AI"))) {
                int aggression = edited.aiAggression;
                int confidence = edited.aiConfidence;
                int energy = edited.aiEnergyLevel;
                int morality = edited.aiMorality;
                int mood = edited.aiMood;
                int assistance = edited.aiAssistance;
                int warn = edited.aiAggroRadiusWarn;
                int warnAndAttack = edited.aiAggroRadiusWarnAndAttack;
                int attack = edited.aiAggroRadiusAttack;

                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.ai_aggression", "Aggression"), &aggression)) { edited.aiAggression = std::clamp(aggression, 0, 3); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.ai_confidence", "Confidence"), &confidence)) { edited.aiConfidence = std::clamp(confidence, 0, 4); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.ai_energy", "Energy"), &energy)) { edited.aiEnergyLevel = static_cast<std::uint8_t>(std::clamp(energy, 0, 100)); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.ai_morality", "Morality"), &morality)) { edited.aiMorality = std::clamp(morality, 0, 3); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.ai_mood", "Mood"), &mood)) { edited.aiMood = std::clamp(mood, 0, 7); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.ai_assistance", "Assistance"), &assistance)) { edited.aiAssistance = std::clamp(assistance, 0, 2); changed = true; }
                if (ImGui::Checkbox(Configuration::GetLoc("menu.ai_aggro_radius", "Aggro radius behavior"), &edited.aiAggroRadiusBehavior)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox(Configuration::GetLoc("menu.ai_no_slow_approach", "No slow approach"), &edited.aiNoSlowApproach)) changed = true;
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.ai_warn", "Warn"), &warn)) { edited.aiAggroRadiusWarn = static_cast<std::uint16_t>(std::clamp(warn, 0, 65535)); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.ai_warn_attack", "Warn/Attack"), &warnAndAttack)) { edited.aiAggroRadiusWarnAndAttack = static_cast<std::uint16_t>(std::clamp(warnAndAttack, 0, 65535)); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.ai_attack", "Attack"), &attack)) { edited.aiAggroRadiusAttack = static_cast<std::uint16_t>(std::clamp(attack, 0, 65535)); changed = true; }
                ImGui::Separator();
                changed |= DrawFormRefListEditor("Package", "Package", edited.packages);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.stats", "Stats"))) {
                int health = edited.health;
                int magicka = edited.magicka;
                int stamina = edited.stamina;
                int healthOffset = edited.healthOffset;
                int magickaOffset = edited.magickaOffset;
                int staminaOffset = edited.staminaOffset;
                int minLevel = edited.calcMinLevel;
                int maxLevel = edited.calcMaxLevel;
                int level = edited.npcLevel;
                int speed = edited.speedMult;
                int disposition = edited.dispositionBase;
                int bleedout = edited.bleedoutOverride;
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.health", "Health"), &health)) { edited.health = static_cast<std::uint16_t>(std::clamp(health, 0, 65535)); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.magicka", "Magicka"), &magicka)) { edited.magicka = static_cast<std::uint16_t>(std::clamp(magicka, 0, 65535)); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.stamina", "Stamina"), &stamina)) { edited.stamina = static_cast<std::uint16_t>(std::clamp(stamina, 0, 65535)); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.health_offset", "Health offset"), &healthOffset)) { edited.healthOffset = static_cast<std::int16_t>(std::clamp(healthOffset, -32768, 32767)); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.magicka_offset", "Magicka offset"), &magickaOffset)) { edited.magickaOffset = static_cast<std::int16_t>(std::clamp(magickaOffset, -32768, 32767)); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.stamina_offset", "Stamina offset"), &staminaOffset)) { edited.staminaOffset = static_cast<std::int16_t>(std::clamp(staminaOffset, -32768, 32767)); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.calc_min_level", "Calc min level"), &minLevel)) { edited.calcMinLevel = static_cast<std::uint16_t>(std::clamp(minLevel, 0, 65535)); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.calc_max_level", "Calc max level"), &maxLevel)) { edited.calcMaxLevel = static_cast<std::uint16_t>(std::clamp(maxLevel, 0, 65535)); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.level", "Level"), &level)) { edited.npcLevel = static_cast<std::uint16_t>(std::clamp(level, 0, 65535)); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.speed_mult", "Speed mult"), &speed)) { edited.speedMult = static_cast<std::uint16_t>(std::clamp(speed, 0, 65535)); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.disposition", "Disposition"), &disposition)) { edited.dispositionBase = static_cast<std::uint16_t>(std::clamp(disposition, 0, 65535)); changed = true; }
                ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                if (ImGui::InputInt(Configuration::GetLoc("menu.bleedout_override", "Bleedout override"), &bleedout)) { edited.bleedoutOverride = static_cast<std::int16_t>(std::clamp(bleedout, -32768, 32767)); changed = true; }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.skills", "Skills"))) {
                for (std::size_t i = 0; i < edited.skills.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    int base = edited.skills[i];
                    int offset = edited.skillOffsets[i];
                    ImGui::Text("%s", NPC_SKILL_NAMES[i]);
                    ImGui::SameLine(180.0F);
                    ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                    if (ImGui::InputInt(Configuration::GetLoc("menu.base", "Base"), &base)) {
                        edited.skills[i] = static_cast<std::uint8_t>(std::clamp(base, 0, 255));
                        changed = true;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                    if (ImGui::InputInt(Configuration::GetLoc("menu.offset", "Offset"), &offset)) {
                        edited.skillOffsets[i] = static_cast<std::uint8_t>(std::clamp(offset, 0, 255));
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.lists", "Lists"))) {
                changed |= DrawRankedFormRefListEditor("Faction", "Faction", edited.npcFactions);
                ImGui::Separator();
                changed |= DrawRankedFormRefListEditor("Perk", "Perk", edited.npcPerks);
                ImGui::Separator();
                changed |= DrawFormRefListEditor("Spell", "Spell", edited.spells);
                ImGui::Separator();
                changed |= DrawFormRefListEditor("Package", "Package", edited.packages);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.visual", "Visual"))) {
                SetStableComboWidth(HEAD_PART_FILTER_ITEMS, 220.0F);
                ImGui::Combo(Configuration::GetLoc("menu.headpart_filter", "HeadPart filter"), &selectedNpcHeadPartFilter, HEAD_PART_FILTER_ITEMS.data(), static_cast<int>(HEAD_PART_FILTER_ITEMS.size()));
                changed |= DrawFormRefListEditor("HeadPart", SelectedNpcHeadPartListType(), edited.headParts);
                ImGui::Separator();
                changed |= DrawTintLayerEditor(edited.tintLayers);
                ImGui::Separator();
                ImGui::Text("%s", Configuration::GetLoc("menu.face_morphs", "Face morphs"));
                for (std::size_t i = 0; i < edited.faceMorphs.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    const auto label = i < NPC_MORPH_NAMES.size() ? std::string(NPC_MORPH_NAMES[i]) : std::format("Morph {}", i);
                    ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                    if (ImGui::InputFloat(label.c_str(), &edited.faceMorphs[i])) {
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::Text("%s", Configuration::GetLoc("menu.face_part_presets", "Face part presets"));
                for (std::size_t i = 0; i < edited.faceParts.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    int part = edited.faceParts[i];
                    const auto label = i < NPC_FACE_PART_NAMES.size() ? std::string(NPC_FACE_PART_NAMES[i]) : std::format("Part {}", i);
                    ImGui::SetNextItemWidth(NPC_NUMBER_INPUT_WIDTH);
                    if (ImGui::InputInt(label.c_str(), &part)) {
                        edited.faceParts[i] = part;
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.debug", "Debug"))) {
                ImGui::Text(Configuration::GetLoc("menu.race", "race=%s"), edited.race.Display().c_str());
                ImGui::Text(Configuration::GetLoc("menu.headparts_u_tintlayers_u_factions_u_perks_u_spells_u", "headParts=%zu tintLayers=%zu factions=%zu perks=%zu spells=%zu"),
                    edited.headParts.size(),
                    edited.tintLayers.size(),
                    edited.npcFactions.size(),
                    edited.npcPerks.size(),
                    edited.spells.size());
                ImGui::Text("packages=%zu ai=[agg=%d conf=%d energy=%u morality=%d mood=%d assistance=%d]",
                    edited.packages.size(),
                    edited.aiAggression,
                    edited.aiConfidence,
                    edited.aiEnergyLevel,
                    edited.aiMorality,
                    edited.aiMood,
                    edited.aiAssistance);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (changed && Manager::UpdateForm(index, edited)) {
            form = Manager::GetForms()[index];
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
                const auto editorId = form.editorId;
                if (Manager::DeleteForm(static_cast<std::size_t>(pendingDeleteIndex))) {
                    selectedDeleteForms.erase(editorId);
                    selectedExportForms.erase(editorId);
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

    bool RenderBatchDeletePopup() {
        bool deleted = false;
        bool open = true;
        if (!ImGui::BeginPopupModal(BATCH_DELETE_POPUP_ID, &open, ImGui::ImGuiWindowFlags_AlwaysAutoResize)) {
            return false;
        }

        ImGui::Text("%s", Configuration::GetLoc("menu.delete_selected_forms", "Delete selected forms?"));
        ImGui::Text(Configuration::GetLoc("menu.forms_selected_count", "%zu form(s) selected."), selectedDeleteForms.size());
        if (!selectedDeleteForms.empty()) {
            ImGui::BeginChild("##batchDeleteForms", { 420.0F, 160.0F }, true);
            for (const auto& editorId : selectedDeleteForms) {
                ImGui::TextUnformatted(editorId.c_str());
            }
            ImGui::EndChild();
        }

        if (selectedDeleteForms.empty()) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(Configuration::GetLoc("menu.confirm_delete", "Confirm delete"))) {
            auto& forms = Manager::GetForms();
            std::vector<std::size_t> indices;
            for (std::size_t i = 0; i < forms.size(); ++i) {
                if (selectedDeleteForms.contains(forms[i].editorId)) {
                    indices.push_back(i);
                }
            }
            std::ranges::sort(indices, [](const std::size_t lhs, const std::size_t rhs) {
                return lhs > rhs;
            });

            bool ok = true;
            for (const auto index : indices) {
                ok = Manager::DeleteForm(index) && ok;
            }

            if (ok) {
                for (const auto& editorId : selectedDeleteForms) {
                    selectedExportForms.erase(editorId);
                }
                selectedDeleteForms.clear();
                deleteSelectionMode = false;
                requestBatchDeletePopup = false;
                deleteError.clear();
                ImGui::CloseCurrentPopup();
                deleted = true;
            } else {
                deleteError = Configuration::GetLoc("menu.batch_delete_failed", "Could not delete one or more selected forms. Check if DPF is available.");
            }
        }
        if (selectedDeleteForms.empty()) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button(Configuration::GetLoc("menu.cancel", "Cancel"))) {
            requestBatchDeletePopup = false;
            deleteError.clear();
            ImGui::CloseCurrentPopup();
        }

        if (!deleteError.empty()) {
            ImGui::TextColored(ERROR_COLOR, "%s", deleteError.c_str());
        }

        ImGui::EndPopup();
        return deleted;
    }
}

namespace Configuration {
    void RenderExportMenu();

    void Register() {
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::warn("SKSE Menu Framework is not installed.");
            return;
        }

        LoadLanguage();
        LoadForms();

        SKSEMenuFramework::SetSection(GetLoc("menu.section", "Dynamic Forms Generator"));
        SKSEMenuFramework::AddSectionItem(GetLoc("menu.forms", "Forms"), RenderFormsMenu);
        SKSEMenuFramework::AddSectionItem(GetLoc("menu.export", "Export"), RenderExportMenu);
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

    void RenderExportMenu() {
        auto& forms = Manager::GetForms();
        std::set<std::string> existingEditorIds;
        for (const auto& form : forms) {
            existingEditorIds.insert(form.editorId);
        }
        for (auto it = selectedExportForms.begin(); it != selectedExportForms.end();) {
            if (!existingEditorIds.contains(*it)) {
                it = selectedExportForms.erase(it);
            } else {
                ++it;
            }
        }

        ImGui::Text("%s", Configuration::GetLoc("menu.export_package", "Export Package"));
        ImGui::TextDisabled("%s", Configuration::GetLoc("menu.export_zip_hint", "ZIP files are saved to Data/Viny Mods/Dynamic Forms Generator/Export."));

        ImGui::SetNextItemWidth(320.0F);
        ImGui::InputText(Configuration::GetLoc("menu.package_name", "Package name"), exportPackageName.data(), exportPackageName.size());

        if (ImGui::Button(Configuration::GetLoc("menu.select_all", "Select all"))) {
            selectedExportForms.clear();
            for (const auto& form : forms) {
                if (MatchesExportFilters(form)) {
                    selectedExportForms.insert(form.editorId);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(Configuration::GetLoc("menu.clear", "Clear"))) {
            selectedExportForms.clear();
        }
        ImGui::SameLine();
        if (selectedExportForms.empty()) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(std::format("{} ({})", Configuration::GetLoc("menu.export_selected", "Export selected"), selectedExportForms.size()).c_str())) {
            if (const auto zipPath = ExportSelectedFormsAsZip(exportPackageName.data(), selectedExportForms)) {
                lastExportSucceeded = true;
                exportMessage = std::format("{} {}", Configuration::GetLoc("menu.exported_to", "Exported to"), zipPath->string());
            } else {
                lastExportSucceeded = false;
                exportMessage = Configuration::GetLoc("menu.export_selected_failed", "Could not export selected forms. Check the log.");
            }
        }
        if (selectedExportForms.empty()) {
            ImGui::EndDisabled();
        }

        if (!exportMessage.empty()) {
            ImGui::TextColored(lastExportSucceeded ? SUCCESS_COLOR : ERROR_COLOR, "%s", exportMessage.c_str());
        }

        ImGui::Separator();
        SetStableComboWidth(FILTER_KIND_ITEMS, 220.0F);
        ImGui::Combo(Configuration::GetLoc("menu.filter_by_type", "Filter by type"), &selectedExportFilterKind, FILTER_KIND_ITEMS.data(), static_cast<int>(FILTER_KIND_ITEMS.size()));
        ImGui::SetNextItemWidth(280.0F);
        ImGui::InputText(Configuration::GetLoc("menu.filter_editor_id", "Filter EditorID"), exportFilterEditorIdBuffer.data(), exportFilterEditorIdBuffer.size());

        std::size_t visibleCount = 0;
        for (const auto& form : forms) {
            if (MatchesExportFilters(form)) {
                ++visibleCount;
            }
        }
        ImGui::TextDisabled(Configuration::GetLoc("menu.visible_total", "Visible: %zu / Total: %zu"), visibleCount, forms.size());

        ImGui::BeginChild("##exportFormsList", { 0.0F, 560.0F }, true);
        for (const auto& form : forms) {
            if (!MatchesExportFilters(form)) {
                continue;
            }
            ImGui::PushID(form.editorId.c_str());
            bool selected = selectedExportForms.contains(form.editorId);
            if (ImGui::Checkbox("##exportSelect", &selected)) {
                if (selected) {
                    selectedExportForms.insert(form.editorId);
                } else {
                    selectedExportForms.erase(form.editorId);
                }
            }
            ImGui::SameLine();
            ImGui::Text("%s", form.editorId.c_str());
            ImGui::SameLine(360.0F);
            ImGui::TextColored({ 0.7F, 0.8F, 1.0F, 1.0F }, "%s", FormKindLabel(form.kind));
            if (form.dirty) {
                ImGui::SameLine();
                ImGui::TextColored(DIRTY_COLOR, "%s", Configuration::GetLoc("menu.need_save", "Need save"));
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void RenderFormsMenu() {
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

        if (ImGui::Button(GetLoc("menu.create", "Create"))) {
            ImGui::OpenPopup(GetLoc("menu.create_popup", "Create Form"));
        }

        if (!validEditorId || duplicateEditorId) {
            ImGui::EndDisabled();
        }

        RenderCreatePopup(editorId);

        ImGui::Separator();
        ImGui::TextColored({ 0.6F, 0.8F, 1.0F, 1.0F }, "%s", GetLoc("menu.saved_forms", "Saved forms"));

        const bool hasDirtyForms = Manager::HasDirtyForms();
        if (hasDirtyForms) {
            ImGui::TextColored(DIRTY_COLOR, "%s", GetLoc("menu.unsaved_changes", "There are unsaved changes."));
        }

        if (hasDirtyForms) {
            ImGui::PushStyleColor(ImGui::ImGuiCol_Button, DIRTY_COLOR);
            ImGui::PushStyleColor(ImGui::ImGuiCol_ButtonHovered, { 1.0F, 0.82F, 0.35F, 1.0F });
            ImGui::PushStyleColor(ImGui::ImGuiCol_ButtonActive, { 0.9F, 0.58F, 0.12F, 1.0F });
        }
        if (ImGui::Button(GetLoc("menu.save_all", "Save all"))) {
            if (Manager::SaveAllForms()) {
                lastSaveSucceeded = true;
                saveMessage = GetLoc("menu.save_all_success", "All forms saved.");
            } else {
                lastSaveSucceeded = false;
                saveMessage = GetLoc("menu.save_all_failed", "Could not save all forms. Check the log.");
            }
        }
        if (hasDirtyForms) {
            ImGui::PopStyleColor(3);
        }

        if (!saveMessage.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(lastSaveSucceeded ? SUCCESS_COLOR : ERROR_COLOR, "%s", saveMessage.c_str());
        }

        if (ImGui::Button(deleteSelectionMode ? GetLoc("menu.cancel_delete_selection", "Cancel delete selection") : GetLoc("menu.select_to_delete", "Select to delete"))) {
            deleteSelectionMode = !deleteSelectionMode;
            selectedDeleteForms.clear();
            deleteError.clear();
        }
        if (deleteSelectionMode) {
            ImGui::SameLine();
            if (selectedDeleteForms.empty()) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(std::format("{} ({})", GetLoc("menu.confirm_delete", "Confirm delete"), selectedDeleteForms.size()).c_str())) {
                deleteError.clear();
                requestBatchDeletePopup = true;
            }
            if (selectedDeleteForms.empty()) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (ImGui::Button(GetLoc("menu.clear_selected", "Clear selected"))) {
                selectedDeleteForms.clear();
            }
        }

        SetStableComboWidth(FILTER_KIND_ITEMS, 220.0F);
        ImGui::Combo(GetLoc("menu.filter_type", "Filter by type"), &selectedFilterKind, FILTER_KIND_ITEMS.data(), static_cast<int>(FILTER_KIND_ITEMS.size()));
        ImGui::SetNextItemWidth(280.0F);
        ImGui::InputText(GetLoc("menu.filter_editor_id", "Filter EditorID"), filterEditorIdBuffer.data(), filterEditorIdBuffer.size());

        auto& forms = Manager::GetForms();
        std::set<std::string> existingEditorIds;
        for (const auto& form : forms) {
            existingEditorIds.insert(form.editorId);
        }
        for (auto it = selectedDeleteForms.begin(); it != selectedDeleteForms.end();) {
            if (!existingEditorIds.contains(*it)) {
                it = selectedDeleteForms.erase(it);
            } else {
                ++it;
            }
        }

        for (std::size_t i = 0; i < forms.size(); ++i) {
            auto& form = forms[i];
            if (!MatchesFilters(form)) {
                continue;
            }

            ImGui::PushID(form.editorId.c_str());
            const bool isDirty = Manager::IsDirty(i);
            if (deleteSelectionMode) {
                bool selected = selectedDeleteForms.contains(form.editorId);
                if (ImGui::Checkbox("##selectDelete", &selected)) {
                    if (selected) {
                        selectedDeleteForms.insert(form.editorId);
                    } else {
                        selectedDeleteForms.erase(form.editorId);
                    }
                }
                ImGui::SameLine();
            }
            std::string headerLabel = form.editorId;
            if (isDirty) {
                headerLabel += " (Need save)";
                ImGui::PushStyleColor(ImGui::ImGuiCol_Header, { 0.4F, 0.3F, 0.1F, 1.0F });
            }
            headerLabel += "###";
            headerLabel += form.editorId;
            if (ImGui::CollapsingHeader(headerLabel.c_str())) {
                if (isDirty) {
                    ImGui::PopStyleColor();
                }
                ImGui::Indent();
                if (isDirty) {
                    ImGui::TextColored(DIRTY_COLOR, "%s", GetLoc("menu.unsaved_form", "Unsaved changes"));
                }
                ImGui::Text("%s: %s", GetLoc("menu.form_type", "Form type"), FormKindLabel(form.kind));
                ImGui::Text("%s: %u", GetLoc("menu.local_id", "Local ID"), form.localId);
                if (CanAddToInventory(form.kind)) {
                    if (ImGui::Button(GetLoc("menu.add_to_inventory", "Add to inventory"))) {
                        if (Manager::AddFormToPlayerInventory(i)) {
                            lastTestActionSucceeded = true;
                            testActionMessage = std::format("{} {}", form.editorId, GetLoc("menu.added_to_inventory", "added to inventory."));
                        } else {
                            lastTestActionSucceeded = false;
                            testActionMessage = std::format("{} {}", GetLoc("menu.add_to_inventory_failed", "Could not add to inventory:"), form.editorId);
                        }
                    }
                }
                if (CanSpawnInWorld(form.kind)) {
                    if (CanAddToInventory(form.kind)) {
                        ImGui::SameLine();
                    }
                    if (ImGui::Button(GetLoc("menu.spawn_at_player", "Spawn at player"))) {
                        if (Manager::SpawnFormAtPlayer(i)) {
                            lastTestActionSucceeded = true;
                            testActionMessage = std::format("{} {}", form.editorId, GetLoc("menu.spawned_at_player", "spawned at player."));
                        } else {
                            lastTestActionSucceeded = false;
                            testActionMessage = std::format("{} {}", GetLoc("menu.spawn_at_player_failed", "Could not spawn:"), form.editorId);
                        }
                    }
                    if (form.kind == DynamicForms::FormKind::NPC) {
                        ImGui::SameLine();
                        if (ImGui::Button(GetLoc("menu.spawn_lydia_debug", "Spawn Lydia debug"))) {
                            if (Manager::SpawnLydiaForDebug()) {
                                lastTestActionSucceeded = true;
                                testActionMessage = GetLoc("menu.spawned_lydia_debug", "Lydia debug spawned.");
                            } else {
                                lastTestActionSucceeded = false;
                                testActionMessage = GetLoc("menu.spawn_lydia_debug_failed", "Could not spawn Lydia debug.");
                            }
                        }
                    }
                }
                if (!testActionMessage.empty()) {
                    ImGui::TextColored(lastTestActionSucceeded ? SUCCESS_COLOR : ERROR_COLOR, "%s", testActionMessage.c_str());
                }
                if (form.kind == DynamicForms::FormKind::Global) {
                    RenderGlobalEditor(i, form);
                } else if (form.kind == DynamicForms::FormKind::Outfit) {
                    RenderOutfitEditor(i, form);
                } else if (form.kind == DynamicForms::FormKind::ArmorType) {
                    RenderArmorTypeEditor(i, form);
                } else if (form.kind == DynamicForms::FormKind::Armor) {
                    RenderArmorEditor(i, form);
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
                } else if (form.kind == DynamicForms::FormKind::EffectShader) {
                    RenderEffectShaderEditor(i, form);
                } else if (form.kind == DynamicForms::FormKind::NPC) {
                    RenderNPCEditor(i, form);
                } else {
                    ImGui::Text("%s", GetLoc("menu.no_editable_fields", "No editable fields for this form type yet."));
                }

                if (isDirty) {
                    ImGui::PushStyleColor(ImGui::ImGuiCol_Button, DIRTY_COLOR);
                    ImGui::PushStyleColor(ImGui::ImGuiCol_ButtonHovered, { 1.0F, 0.82F, 0.35F, 1.0F });
                    ImGui::PushStyleColor(ImGui::ImGuiCol_ButtonActive, { 0.9F, 0.58F, 0.12F, 1.0F });
                }
                if (ImGui::Button(GetLoc("menu.save", "Save"))) {
                    if (Manager::SaveForm(i)) {
                        lastSaveSucceeded = true;
                        saveMessage = std::format("{} {}", form.editorId, GetLoc("menu.save_success_suffix", "saved."));
                    } else {
                        lastSaveSucceeded = false;
                        saveMessage = std::format("{} {}", GetLoc("menu.save_failed_prefix", "Could not save"), form.editorId);
                    }
                }
                if (isDirty) {
                    ImGui::PopStyleColor(3);
                }
                ImGui::SameLine();
                if (ImGui::Button(GetLoc("menu.delete", "Delete"))) {
                    pendingDeleteIndex = static_cast<int>(i);
                    deleteError.clear();
                    requestDeletePopup = true;
                }
                ImGui::Unindent();
            } else if (isDirty) {
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }

        if (requestDeletePopup) {
            requestDeletePopup = false;
            ImGui::OpenPopup(DELETE_POPUP_ID);
        }
        if (requestBatchDeletePopup) {
            requestBatchDeletePopup = false;
            ImGui::OpenPopup(BATCH_DELETE_POPUP_ID);
        }

        RenderDeletePopup();
        RenderBatchDeletePopup();
    }
}
