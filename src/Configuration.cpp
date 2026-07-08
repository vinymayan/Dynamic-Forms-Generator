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
#include <limits>
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
    bool requestCreateFormPopup = false;
    bool requestCreatePatchPopup = false;
    std::set<std::string> selectedDeleteForms;
    std::set<std::string> selectedExportForms;
    std::set<std::string> selectedPatchForms;
    std::array<char, 128> exportPackageName{ 'D', 'F', 'G', '_', 'E', 'x', 'p', 'o', 'r', 't', '\0' };
    std::array<char, 128> workingPackageName{ 'D', 'F', 'G', '_', 'O', 'v', 'e', 'r', 'r', 'i', 'd', 'e', 's', '\0' };
    std::array<char, 128> newPackageName{};
    std::array<char, 128> filterPackageNameBuffer{};
    std::array<char, 128> patchFilterPackageNameBuffer{};
    std::array<char, 128> patchFilterEditorIdBuffer{};
    int selectedPatchFilterKind = 0;
    std::vector<std::string> previewPackages{ "Local Forms", "DFG_Overrides" };
    std::unordered_map<std::string, std::string> previewFormPackages;
    std::unordered_map<std::string, std::vector<std::string>> previewPatchPackages;
    int selectedPackageFilter = 0;
    std::string exportMessage;
    bool lastExportSucceeded = true;
    bool showSourceDetails = true;
    bool showOnlyOverrideDrafts = false;

    struct PickerType
    {
        const char* typeName;
        const char* label;
    };

    struct PickerRow
    {
        DynamicForms::FormRef ref;
        std::string label;
        std::string searchText;
    };

    struct PickerRowCache
    {
        std::uint64_t generation{ 0 };
        std::string search;
        std::vector<PickerRow> rows;
    };

    std::unordered_map<std::string, PickerRowCache> pickerRowCaches;

    constexpr auto DELETE_POPUP_ID = "Delete Form##dynamic_forms_delete_popup";
    constexpr auto BATCH_DELETE_POPUP_ID = "Delete Selected Forms##dynamic_forms_batch_delete_popup";
    constexpr auto CREATE_PATCH_POPUP_ID = "Create Patch##dynamic_forms_create_patch_popup";
    constexpr auto EXPORT_DIR = "Data/Viny Mods/Dynamic Forms Generator/Export";
    const ImGui::ImVec4 DIRTY_COLOR{ 1.0F, 0.72F, 0.2F, 1.0F };
    const ImGui::ImVec4 SUCCESS_COLOR{ 0.45F, 0.9F, 0.55F, 1.0F };
    const ImGui::ImVec4 ERROR_COLOR{ 1.0F, 0.35F, 0.35F, 1.0F };
    const ImGui::ImVec4 INHERITED_COLOR{ 0.6F, 0.78F, 1.0F, 1.0F };
    const ImGui::ImVec4 OVERRIDE_COLOR{ 1.0F, 0.62F, 0.22F, 1.0F };
    const ImGui::ImVec4 LOCAL_COLOR{ 0.55F, 0.9F, 0.65F, 1.0F };
    constexpr std::array FORM_KIND_ITEMS{ "Global", "Keyword", "Form List", "Equip Slot", "Voice Type", "Outfit", "Armor Type", "Armor", "Book", "Misc Item", "Key", "Soul Gem", "Material Type", "Ammo", "Weapon", "Alchemy Item", "Ingredient", "Color", "Art Object", "Perk", "Head Part", "Sound Description", "Light", "Explosion", "Activator", "Effect Shader", "NPC" };
    constexpr std::array FILTER_KIND_ITEMS{ "All", "Global", "Keyword", "Form List", "Equip Slot", "Voice Type", "Outfit", "Armor Type", "Armor", "Book", "Misc Item", "Key", "Soul Gem", "Material Type", "Ammo", "Weapon", "Alchemy Item", "Ingredient", "Color", "Art Object", "Perk", "Head Part", "Sound Description", "Light", "Explosion", "Activator", "Effect Shader", "NPC" };
    constexpr std::array FORM_KIND_TREE_ORDER{
        DynamicForms::FormKind::Global,
        DynamicForms::FormKind::Keyword,
        DynamicForms::FormKind::FormList,
        DynamicForms::FormKind::EquipSlot,
        DynamicForms::FormKind::VoiceType,
        DynamicForms::FormKind::Outfit,
        DynamicForms::FormKind::ArmorType,
        DynamicForms::FormKind::Armor,
        DynamicForms::FormKind::Book,
        DynamicForms::FormKind::Misc,
        DynamicForms::FormKind::Key,
        DynamicForms::FormKind::SoulGem,
        DynamicForms::FormKind::MaterialType,
        DynamicForms::FormKind::Ammo,
        DynamicForms::FormKind::Weapon,
        DynamicForms::FormKind::AlchemyItem,
        DynamicForms::FormKind::Ingredient,
        DynamicForms::FormKind::Color,
        DynamicForms::FormKind::ArtObject,
        DynamicForms::FormKind::Perk,
        DynamicForms::FormKind::HeadPart,
        DynamicForms::FormKind::SoundDescriptor,
        DynamicForms::FormKind::Light,
        DynamicForms::FormKind::Explosion,
        DynamicForms::FormKind::Activator,
        DynamicForms::FormKind::EffectShader,
        DynamicForms::FormKind::NPC
    };
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
    constexpr std::array SOUL_LEVEL_ITEMS{ "None", "Petty", "Lesser", "Common", "Greater", "Grand" };
    constexpr std::array BOOK_TYPE_ITEMS{ "Book/Tome", "Note/Scroll" };
    constexpr std::array WEAPON_TYPE_ITEMS{ "Hand to Hand", "One-Hand Sword", "One-Hand Dagger", "One-Hand Axe", "One-Hand Mace", "Two-Hand Sword", "Two-Hand Axe", "Bow", "Staff", "Crossbow" };
    constexpr std::array ACTOR_VALUE_ITEMS{ "One-Handed", "Two-Handed", "Archery", "Block", "Smithing", "Heavy Armor", "Light Armor", "Pickpocket", "Lockpicking", "Sneak", "Alchemy", "Speech", "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration", "Enchanting", "Health", "Magicka", "Stamina", "Heal Rate", "Magicka Rate", "Stamina Rate", "None" };
    constexpr std::array<std::uint32_t, ACTOR_VALUE_ITEMS.size()> ACTOR_VALUE_IDS{
        6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u,
        18u, 19u, 20u, 21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u, 29u,
        std::numeric_limits<std::uint32_t>::max()
    };
    constexpr std::array FORM_REFERENCE_PICKER_TYPES{
        PickerType{ "Global", "Global" },
        PickerType{ "Keyword", "Keyword" },
        PickerType{ "FormList", "Form List" },
        PickerType{ "EquipSlot", "Equip Slot" },
        PickerType{ "Voice", "Voice Type" },
        PickerType{ "Outfit", "Outfit" },
        PickerType{ "ArmorType", "Armor Type" },
        PickerType{ "Armor", "Armor" },
        PickerType{ "Book", "Book" },
        PickerType{ "MiscItem", "Misc Item" },
        PickerType{ "Key", "Key" },
        PickerType{ "SoulGem", "Soul Gem" },
        PickerType{ "MaterialType", "Material Type" },
        PickerType{ "Ammo", "Ammo" },
        PickerType{ "Weapon", "Weapon" },
        PickerType{ "AlchemyItem", "Alchemy Item" },
        PickerType{ "Ingredient", "Ingredient" },
        PickerType{ "Color", "Color" },
        PickerType{ "ArtObject", "Art Object" },
        PickerType{ "Perk", "Perk" },
        PickerType{ "HeadPart", "Head Part" },
        PickerType{ "SoundDescriptor", "Sound Descriptor" },
        PickerType{ "Light", "Light" },
        PickerType{ "Explosion", "Explosion" },
        PickerType{ "Activator", "Activator" },
        PickerType{ "EffectShader", "Effect Shader" },
        PickerType{ "NPC", "NPC" }
    };
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
            return DynamicForms::FormKind::FormList;
        case 3:
            return DynamicForms::FormKind::EquipSlot;
        case 4:
            return DynamicForms::FormKind::VoiceType;
        case 5:
            return DynamicForms::FormKind::Outfit;
        case 6:
            return DynamicForms::FormKind::ArmorType;
        case 7:
            return DynamicForms::FormKind::Armor;
        case 8:
            return DynamicForms::FormKind::Book;
        case 9:
            return DynamicForms::FormKind::Misc;
        case 10:
            return DynamicForms::FormKind::Key;
        case 11:
            return DynamicForms::FormKind::SoulGem;
        case 12:
            return DynamicForms::FormKind::MaterialType;
        case 13:
            return DynamicForms::FormKind::Ammo;
        case 14:
            return DynamicForms::FormKind::Weapon;
        case 15:
            return DynamicForms::FormKind::AlchemyItem;
        case 16:
            return DynamicForms::FormKind::Ingredient;
        case 17:
            return DynamicForms::FormKind::Color;
        case 18:
            return DynamicForms::FormKind::ArtObject;
        case 19:
            return DynamicForms::FormKind::Perk;
        case 20:
            return DynamicForms::FormKind::HeadPart;
        case 21:
            return DynamicForms::FormKind::SoundDescriptor;
        case 22:
            return DynamicForms::FormKind::Light;
        case 23:
            return DynamicForms::FormKind::Explosion;
        case 24:
            return DynamicForms::FormKind::Activator;
        case 25:
            return DynamicForms::FormKind::EffectShader;
        case 26:
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
        case DynamicForms::FormKind::FormList:
            return "Form List";
        case DynamicForms::FormKind::EquipSlot:
            return "Equip Slot";
        case DynamicForms::FormKind::VoiceType:
            return "Voice Type";
        case DynamicForms::FormKind::Outfit:
            return "Outfit";
        case DynamicForms::FormKind::ArmorType:
            return "Armor Type";
        case DynamicForms::FormKind::Armor:
            return "Armor";
        case DynamicForms::FormKind::Book:
            return "Book";
        case DynamicForms::FormKind::Misc:
            return "Misc Item";
        case DynamicForms::FormKind::Key:
            return "Key";
        case DynamicForms::FormKind::SoulGem:
            return "Soul Gem";
        case DynamicForms::FormKind::MaterialType:
            return "Material Type";
        case DynamicForms::FormKind::Ammo:
            return "Ammo";
        case DynamicForms::FormKind::Weapon:
            return "Weapon";
        case DynamicForms::FormKind::AlchemyItem:
            return "Alchemy Item";
        case DynamicForms::FormKind::Ingredient:
            return "Ingredient";
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

    std::string SanitizePackageFolder(std::string name) {
        for (char& ch : name) {
            const auto c = static_cast<unsigned char>(ch);
            if (std::isalnum(c) == 0 && ch != '_' && ch != '-' && ch != '.') {
                ch = '_';
            }
        }
        return name.empty() ? "Local_Forms" : name;
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
        std::set<std::string> packageNames;
        for (const auto& form : forms) {
            if (!editorIds.contains(form.editorId)) {
                continue;
            }
            packageNames.insert(form.packageName.empty() ? Manager::DEFAULT_PACKAGE_NAME : form.packageName);
            packageNames.insert(form.patchPackageNames.begin(), form.patchPackageNames.end());
        }

        for (const auto& package : packageNames) {
            const auto folder = SanitizePackageFolder(package);
            const auto sourceDir = std::filesystem::path(Manager::PACKAGES_DIR) / folder;
            ok = AddFileToZipOnce(
                zip,
                addedPaths,
                sourceDir / "manifest.json",
                std::format("Viny Mods/Dynamic Forms Generator/Packages/{}/manifest.json", folder)) && ok;
            ok = AddFileToZipOnce(
                zip,
                addedPaths,
                sourceDir / "package.db",
                std::format("Viny Mods/Dynamic Forms Generator/Packages/{}/package.db", folder)) && ok;
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
        if (kindFilter <= 0 || kindFilter > static_cast<int>(FORM_KIND_TREE_ORDER.size())) {
            return std::nullopt;
        }
        return FORM_KIND_TREE_ORDER[static_cast<std::size_t>(kindFilter - 1)];
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
            kind == DynamicForms::FormKind::Book ||
            kind == DynamicForms::FormKind::Misc ||
            kind == DynamicForms::FormKind::Key ||
            kind == DynamicForms::FormKind::SoulGem ||
            kind == DynamicForms::FormKind::Ammo ||
            kind == DynamicForms::FormKind::Weapon ||
            kind == DynamicForms::FormKind::AlchemyItem ||
            kind == DynamicForms::FormKind::Ingredient ||
            kind == DynamicForms::FormKind::Light;
    }

    bool CanSpawnInWorld(const DynamicForms::FormKind kind) {
        return kind == DynamicForms::FormKind::Armor ||
            kind == DynamicForms::FormKind::Book ||
            kind == DynamicForms::FormKind::Misc ||
            kind == DynamicForms::FormKind::Key ||
            kind == DynamicForms::FormKind::SoulGem ||
            kind == DynamicForms::FormKind::Ammo ||
            kind == DynamicForms::FormKind::Weapon ||
            kind == DynamicForms::FormKind::AlchemyItem ||
            kind == DynamicForms::FormKind::Ingredient ||
            kind == DynamicForms::FormKind::Light ||
            kind == DynamicForms::FormKind::Explosion ||
            kind == DynamicForms::FormKind::Activator ||
            kind == DynamicForms::FormKind::NPC;
    }

    std::vector<const char*> PackageComboItems(const bool includeAll) {
        std::vector<const char*> items;
        if (includeAll) {
            items.push_back("All packages");
        }
        for (const auto& package : previewPackages) {
            items.push_back(package.c_str());
        }
        return items;
    }

    bool HasPreviewPackage(const std::string_view name) {
        return std::ranges::any_of(previewPackages, [name](const std::string& package) {
            return package == name;
        });
    }

    void SetWorkingPackage(const std::string& name) {
        if (name.empty()) {
            return;
        }
        std::snprintf(workingPackageName.data(), workingPackageName.size(), "%s", name.c_str());
    }

    bool CreatePreviewPackage(const std::string& name) {
        if (!IsValidEditorId(name) || HasPreviewPackage(name)) {
            return false;
        }
        previewPackages.push_back(name);
        SetWorkingPackage(name);
        return true;
    }

    const char* ActiveWorkingPackageName() {
        const auto* name = workingPackageName.data();
        return name[0] != '\0' ? name : "DFG_Overrides";
    }

    const char* SourcePackageLabel(const DynamicForms::DynamicForm& form) {
        const auto found = previewFormPackages.find(form.editorId);
        if (found != previewFormPackages.end()) {
            return found->second.c_str();
        }
        if (!form.packageName.empty()) {
            return form.packageName.c_str();
        }
        return form.localId != 0 ? "Local Forms" : "Unsaved Draft";
    }

    std::string PatchPackageLabel(const DynamicForms::DynamicForm& form) {
        std::string label;
        for (const auto& package : form.patchPackageNames) {
            if (!label.empty()) {
                label += " + ";
            }
            label += package;
        }

        const auto found = previewPatchPackages.find(form.editorId);
        if (found == previewPatchPackages.end() || found->second.empty()) {
            return label.empty() ? ActiveWorkingPackageName() : label;
        }

        for (const auto& package : found->second) {
            if (!label.empty()) {
                label += " + ";
            }
            label += package;
        }
        return label;
    }

    bool HasPatchLayer(const DynamicForms::DynamicForm& form, const std::string_view package) {
        if (std::ranges::find(form.patchPackageNames, package) != form.patchPackageNames.end()) {
            return true;
        }
        const auto found = previewPatchPackages.find(form.editorId);
        return found != previewPatchPackages.end() &&
            std::ranges::find(found->second, package) != found->second.end();
    }

    bool HasAnyPatchLayer(const DynamicForms::DynamicForm& form) {
        if (!form.patchPackageNames.empty()) {
            return true;
        }
        const auto found = previewPatchPackages.find(form.editorId);
        return found != previewPatchPackages.end() && !found->second.empty();
    }

    bool HasOverrideDraft(std::size_t index);

    bool MatchesPackageFilter(const DynamicForms::DynamicForm& form) {
        if (selectedPackageFilter <= 0) {
            return true;
        }
        const auto packageIndex = static_cast<std::size_t>(selectedPackageFilter - 1);
        if (packageIndex >= previewPackages.size()) {
            return true;
        }
        const auto& package = previewPackages[packageIndex];
        return SourcePackageLabel(form) == package ||
            HasPatchLayer(form, package);
    }

    bool MatchesPackageNameFilter(const std::string_view package) {
        const std::string filter = filterPackageNameBuffer.data();
        return filter.empty() || ToLower(std::string(package)).find(ToLower(filter)) != std::string::npos;
    }

    bool FormBelongsToPackage(const DynamicForms::DynamicForm& form, const std::string_view package) {
        if (SourcePackageLabel(form) == package) {
            return true;
        }
        return HasPatchLayer(form, package);
    }

    bool MatchesVisibleFormFilters(const std::size_t index, const DynamicForms::DynamicForm& form) {
        return MatchesFilters(form) &&
            MatchesPackageFilter(form) &&
            (!showOnlyOverrideDrafts || HasOverrideDraft(index));
    }

    std::size_t CountVisibleFormsInPackage(const std::string_view package) {
        std::size_t count = 0;
        const auto& forms = Manager::GetForms();
        for (std::size_t i = 0; i < forms.size(); ++i) {
            if (FormBelongsToPackage(forms[i], package) && MatchesVisibleFormFilters(i, forms[i])) {
                ++count;
            }
        }
        return count;
    }

    std::size_t CountVisibleFormsInPackageKind(const std::string_view package, const DynamicForms::FormKind kind) {
        std::size_t count = 0;
        const auto& forms = Manager::GetForms();
        for (std::size_t i = 0; i < forms.size(); ++i) {
            if (forms[i].kind == kind && FormBelongsToPackage(forms[i], package) && MatchesVisibleFormFilters(i, forms[i])) {
                ++count;
            }
        }
        return count;
    }

    bool MatchesPatchPopupFilters(const DynamicForms::DynamicForm& form, const std::string_view package) {
        if (SourcePackageLabel(form) != package || package == ActiveWorkingPackageName()) {
            return false;
        }

        const auto kind = FormKindFromFilterIndex(selectedPatchFilterKind);
        if (kind && form.kind != *kind) {
            return false;
        }

        const std::string packageFilter = patchFilterPackageNameBuffer.data();
        if (!packageFilter.empty() && ToLower(std::string(package)).find(ToLower(packageFilter)) == std::string::npos) {
            return false;
        }

        const std::string editorFilter = patchFilterEditorIdBuffer.data();
        return editorFilter.empty() || ToLower(form.editorId).find(ToLower(editorFilter)) != std::string::npos;
    }

    std::vector<std::size_t> VisibleFormRows(const std::string_view package, std::optional<DynamicForms::FormKind> kind = std::nullopt) {
        std::vector<std::size_t> rows;
        const auto& forms = Manager::GetForms();
        for (std::size_t i = 0; i < forms.size(); ++i) {
            if (kind && forms[i].kind != *kind) {
                continue;
            }
            if (FormBelongsToPackage(forms[i], package) && MatchesVisibleFormFilters(i, forms[i])) {
                rows.push_back(i);
            }
        }
        return rows;
    }

    bool AllRowsSelectedForDelete(const std::vector<std::size_t>& rows) {
        if (rows.empty()) {
            return false;
        }
        const auto& forms = Manager::GetForms();
        return std::ranges::all_of(rows, [&forms](const std::size_t row) {
            return row < forms.size() && selectedDeleteForms.contains(forms[row].editorId);
        });
    }

    void SetRowsSelectedForDelete(const std::vector<std::size_t>& rows, const bool selected) {
        auto& forms = Manager::GetForms();
        for (const auto row : rows) {
            if (row >= forms.size()) {
                continue;
            }
            if (selected) {
                selectedDeleteForms.insert(forms[row].editorId);
            } else {
                selectedDeleteForms.erase(forms[row].editorId);
            }
        }
    }

    void AssignFormToWorkingPackage(const DynamicForms::DynamicForm& form) {
        previewFormPackages[form.editorId] = ActiveWorkingPackageName();
        previewPatchPackages.erase(form.editorId);
        if (!HasPreviewPackage(ActiveWorkingPackageName())) {
            previewPackages.emplace_back(ActiveWorkingPackageName());
        }
        Manager::AssignFormToPackage(form.editorId, ActiveWorkingPackageName());
    }

    void MarkFormAsPatchInWorkingPackage(const DynamicForms::DynamicForm& form) {
        auto& layers = previewPatchPackages[form.editorId];
        const std::string package = ActiveWorkingPackageName();
        if (std::ranges::find(layers, package) == layers.end()) {
            layers.push_back(package);
        }
        if (!HasPreviewPackage(ActiveWorkingPackageName())) {
            previewPackages.emplace_back(ActiveWorkingPackageName());
        }
        Manager::AddPatchLayer(form.editorId, ActiveWorkingPackageName());
    }

    bool HasOverrideDraft(const std::size_t index) {
        const auto& forms = Manager::GetForms();
        if (index >= forms.size()) {
            return false;
        }
        return Manager::IsDirty(index) || HasAnyPatchLayer(forms[index]);
    }

    void DrawStatusBadge(const char* text, const ImGui::ImVec4& color) {
        ImGui::TextColored(color, "[%s]", text);
    }

    void RenderPackageWorkspaceHeader() {
        ImGui::TextColored(INHERITED_COLOR, "%s", Configuration::GetLoc("menu.package_workspace", "Package workspace"));

        ImGui::SetNextItemWidth(220.0F);
        ImGui::InputText(Configuration::GetLoc("menu.new_package", "New package"), newPackageName.data(), newPackageName.size());
        ImGui::SameLine();
        const std::string packageToCreate = newPackageName.data();
        const bool canCreatePackage = IsValidEditorId(packageToCreate) && !HasPreviewPackage(packageToCreate);
        if (!canCreatePackage) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(Configuration::GetLoc("menu.create_package", "Create package"))) {
            if (CreatePreviewPackage(packageToCreate)) {
                newPackageName.fill('\0');
            }
        }
        if (!canCreatePackage) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", Configuration::GetLoc("menu.package_name_hint", "Use letters, numbers and underscore."));

        auto workingItems = PackageComboItems(false);
        int workingIndex = 0;
        for (std::size_t i = 0; i < previewPackages.size(); ++i) {
            if (previewPackages[i] == ActiveWorkingPackageName()) {
                workingIndex = static_cast<int>(i);
                break;
            }
        }
        ImGui::SetNextItemWidth(260.0F);
        if (ImGui::Combo(Configuration::GetLoc("menu.working_package_select", "Working package"), &workingIndex, workingItems.data(), static_cast<int>(workingItems.size()))) {
            if (workingIndex >= 0 && static_cast<std::size_t>(workingIndex) < previewPackages.size()) {
                SetWorkingPackage(previewPackages[static_cast<std::size_t>(workingIndex)]);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(Configuration::GetLoc("menu.create_form", "Create form"))) {
            createError.clear();
            requestCreateFormPopup = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(Configuration::GetLoc("menu.create_patch", "Create patch"))) {
            selectedPatchForms.clear();
            requestCreatePatchPopup = true;
        }

        auto filterItems = PackageComboItems(true);
        ImGui::SetNextItemWidth(210.0F);
        ImGui::Combo(Configuration::GetLoc("menu.package_filter", "Package"), &selectedPackageFilter, filterItems.data(), static_cast<int>(filterItems.size()));
        ImGui::SameLine();
        SetStableComboWidth(FILTER_KIND_ITEMS, 180.0F);
        ImGui::Combo(Configuration::GetLoc("menu.filter_type", "Type"), &selectedFilterKind, FILTER_KIND_ITEMS.data(), static_cast<int>(FILTER_KIND_ITEMS.size()));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0F);
        ImGui::InputText(Configuration::GetLoc("menu.filter_package_name", "Package name"), filterPackageNameBuffer.data(), filterPackageNameBuffer.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(240.0F);
        ImGui::InputText(Configuration::GetLoc("menu.filter_editor_id", "EditorID"), filterEditorIdBuffer.data(), filterEditorIdBuffer.size());

        ImGui::Checkbox(Configuration::GetLoc("menu.show_sources", "Show sources"), &showSourceDetails);
        ImGui::SameLine();
        ImGui::Checkbox(Configuration::GetLoc("menu.only_override_drafts", "Only override drafts"), &showOnlyOverrideDrafts);

        ImGui::Text("%s: %s", Configuration::GetLoc("menu.edit_target", "Edit target"), ActiveWorkingPackageName());
        ImGui::TextDisabled("%s", Configuration::GetLoc(
            "menu.package_flow_hint",
            "Preview mode: edits still save as current JSON forms, but dirty forms are shown as override drafts for the selected package."));
    }

    void RenderResolvedFormPanel(const DynamicForms::DynamicForm& form, const std::size_t index) {
        if (!showSourceDetails) {
            return;
        }

        const bool overrideDraft = HasOverrideDraft(index);
        if (!ImGui::CollapsingHeader(Configuration::GetLoc("menu.resolved_form", "Resolved form"))) {
            return;
        }

        ImGui::Indent();
        ImGui::Text("%s: %s", Configuration::GetLoc("menu.source_package", "Source package"), SourcePackageLabel(form));
        ImGui::Text("%s: %s%s%s",
            Configuration::GetLoc("menu.final_layers", "Final layers"),
            SourcePackageLabel(form),
            overrideDraft ? " + " : "",
            overrideDraft ? PatchPackageLabel(form).c_str() : "");
        ImGui::Text("%s: ", Configuration::GetLoc("menu.form_state", "State"));
        ImGui::SameLine();
        if (overrideDraft) {
            DrawStatusBadge(Configuration::GetLoc("menu.override_draft", "override draft"), OVERRIDE_COLOR);
        } else if (form.localId == 0) {
            DrawStatusBadge(Configuration::GetLoc("menu.new_local", "new local"), LOCAL_COLOR);
        } else {
            DrawStatusBadge(Configuration::GetLoc("menu.inherited_clean", "inherited clean"), INHERITED_COLOR);
        }

        if (overrideDraft) {
            ImGui::TextDisabled("%s", Configuration::GetLoc(
                "menu.override_draft_hint",
                "Saving now writes the whole JSON form; future patch storage would persist only the changed fields in the working package."));
        }

        const bool isInWorkingPackage = SourcePackageLabel(form) == ActiveWorkingPackageName();
        const char* moveButtonLabel = isInWorkingPackage ?
            Configuration::GetLoc("menu.form_in_working_package", "In working package") :
            Configuration::GetLoc("menu.move_to_working_package", "Move to working package");
        if (isInWorkingPackage) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(moveButtonLabel)) {
            AssignFormToWorkingPackage(form);
        }
        if (isInWorkingPackage) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        const bool canCreatePatchLayer = SourcePackageLabel(form) != ActiveWorkingPackageName() && !HasPatchLayer(form, ActiveWorkingPackageName());
        if (!canCreatePatchLayer) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(Configuration::GetLoc("menu.create_patch_layer", "Create patch layer"))) {
            MarkFormAsPatchInWorkingPackage(form);
        }
        if (!canCreatePatchLayer) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        const bool canRemoveFromPackage = previewFormPackages.contains(form.editorId);
        if (!canRemoveFromPackage) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(Configuration::GetLoc("menu.remove_from_package", "Remove from package"))) {
            previewFormPackages.erase(form.editorId);
        }
        if (!canRemoveFromPackage) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button(Configuration::GetLoc("menu.clear_package_preview", "Clear preview"))) {
            previewFormPackages.erase(form.editorId);
            previewPatchPackages.erase(form.editorId);
        }
        ImGui::Unindent();
    }

    bool RenderGlobalEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderFormListEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderEquipSlotEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderVoiceTypeEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderOutfitEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderArmorTypeEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderArmorEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderBookEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderSimpleItemEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderSoulGemEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderMaterialTypeEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderAmmoEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderWeaponEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderAlchemyItemEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderIngredientEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderColorEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderArtObjectEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderPerkEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderHeadPartEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderSoundDescriptorEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderLightEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderExplosionEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderActivatorEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderEffectShaderEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool RenderNPCEditor(std::size_t index, DynamicForms::DynamicForm& form);
    bool FlagCheckbox(const char* label, std::uint32_t& flags, std::uint32_t bit);

    void RenderFormTreeItem(const std::size_t index, DynamicForms::DynamicForm& form) {
        ImGui::PushID(form.editorId.c_str());
        const bool isDirty = Manager::IsDirty(index);
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
        if (isDirty || HasAnyPatchLayer(form)) {
            headerLabel += " [Override draft]";
            ImGui::PushStyleColor(ImGui::ImGuiCol_Header, { 0.4F, 0.3F, 0.1F, 1.0F });
        } else if (form.localId != 0) {
            headerLabel += " [Inherited]";
        } else {
            headerLabel += " [New local]";
        }
        headerLabel += "###";
        headerLabel += form.editorId;

        if (ImGui::CollapsingHeader(headerLabel.c_str())) {
            if (isDirty || HasAnyPatchLayer(form)) {
                ImGui::PopStyleColor();
            }
            ImGui::Indent();
            if (isDirty) {
                ImGui::TextColored(DIRTY_COLOR, "%s", Configuration::GetLoc("menu.unsaved_form", "Unsaved changes"));
            }
            ImGui::Text("%s: %s", Configuration::GetLoc("menu.form_type", "Form type"), FormKindLabel(form.kind));
            ImGui::Text("%s: %u", Configuration::GetLoc("menu.local_id", "Local ID"), form.localId);
            RenderResolvedFormPanel(form, index);
            if (CanAddToInventory(form.kind)) {
                if (ImGui::Button(Configuration::GetLoc("menu.add_to_inventory", "Add to inventory"))) {
                    if (Manager::AddFormToPlayerInventory(index)) {
                        lastTestActionSucceeded = true;
                        testActionMessage = std::format("{} {}", form.editorId, Configuration::GetLoc("menu.added_to_inventory", "added to inventory."));
                    } else {
                        lastTestActionSucceeded = false;
                        testActionMessage = std::format("{} {}", Configuration::GetLoc("menu.add_to_inventory_failed", "Could not add to inventory:"), form.editorId);
                    }
                }
            }
            if (CanSpawnInWorld(form.kind)) {
                if (CanAddToInventory(form.kind)) {
                    ImGui::SameLine();
                }
                if (ImGui::Button(Configuration::GetLoc("menu.spawn_at_player", "Spawn at player"))) {
                    if (Manager::SpawnFormAtPlayer(index)) {
                        lastTestActionSucceeded = true;
                        testActionMessage = std::format("{} {}", form.editorId, Configuration::GetLoc("menu.spawned_at_player", "spawned at player."));
                    } else {
                        lastTestActionSucceeded = false;
                        testActionMessage = std::format("{} {}", Configuration::GetLoc("menu.spawn_at_player_failed", "Could not spawn:"), form.editorId);
                    }
                }
                if (form.kind == DynamicForms::FormKind::NPC) {
                    ImGui::SameLine();
                    if (ImGui::Button(Configuration::GetLoc("menu.spawn_lydia_debug", "Spawn Lydia debug"))) {
                        if (Manager::SpawnLydiaForDebug()) {
                            lastTestActionSucceeded = true;
                            testActionMessage = Configuration::GetLoc("menu.spawned_lydia_debug", "Lydia debug spawned.");
                        } else {
                            lastTestActionSucceeded = false;
                            testActionMessage = Configuration::GetLoc("menu.spawn_lydia_debug_failed", "Could not spawn Lydia debug.");
                        }
                    }
                }
            }
            if (!testActionMessage.empty()) {
                ImGui::TextColored(lastTestActionSucceeded ? SUCCESS_COLOR : ERROR_COLOR, "%s", testActionMessage.c_str());
            }
            if (form.kind == DynamicForms::FormKind::Global) {
                RenderGlobalEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::FormList) {
                RenderFormListEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::EquipSlot) {
                RenderEquipSlotEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::VoiceType) {
                RenderVoiceTypeEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::Outfit) {
                RenderOutfitEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::ArmorType) {
                RenderArmorTypeEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::Armor) {
                RenderArmorEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::Book) {
                RenderBookEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::Misc || form.kind == DynamicForms::FormKind::Key) {
                RenderSimpleItemEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::SoulGem) {
                RenderSoulGemEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::MaterialType) {
                RenderMaterialTypeEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::Ammo) {
                RenderAmmoEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::Weapon) {
                RenderWeaponEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::AlchemyItem) {
                RenderAlchemyItemEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::Ingredient) {
                RenderIngredientEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::Color) {
                RenderColorEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::ArtObject) {
                RenderArtObjectEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::Perk) {
                RenderPerkEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::HeadPart) {
                RenderHeadPartEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::SoundDescriptor) {
                RenderSoundDescriptorEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::Light) {
                RenderLightEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::Explosion) {
                RenderExplosionEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::Activator) {
                RenderActivatorEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::EffectShader) {
                RenderEffectShaderEditor(index, form);
            } else if (form.kind == DynamicForms::FormKind::NPC) {
                RenderNPCEditor(index, form);
            } else {
                ImGui::Text("%s", Configuration::GetLoc("menu.no_editable_fields", "No editable fields for this form type yet."));
            }

            if (isDirty) {
                ImGui::PushStyleColor(ImGui::ImGuiCol_Button, DIRTY_COLOR);
                ImGui::PushStyleColor(ImGui::ImGuiCol_ButtonHovered, { 1.0F, 0.82F, 0.35F, 1.0F });
                ImGui::PushStyleColor(ImGui::ImGuiCol_ButtonActive, { 0.9F, 0.58F, 0.12F, 1.0F });
            }
            const char* saveButtonLabel = isDirty ?
                Configuration::GetLoc("menu.save_override_draft", "Save override draft") :
                Configuration::GetLoc("menu.save", "Save");
            if (ImGui::Button(saveButtonLabel)) {
                if (Manager::SaveForm(index)) {
                    lastSaveSucceeded = true;
                    saveMessage = std::format("{} {}", form.editorId, Configuration::GetLoc("menu.save_success_suffix", "saved."));
                } else {
                    lastSaveSucceeded = false;
                    saveMessage = std::format("{} {}", Configuration::GetLoc("menu.save_failed_prefix", "Could not save"), form.editorId);
                }
            }
            if (isDirty) {
                ImGui::PopStyleColor(3);
            }
            ImGui::SameLine();
            if (ImGui::Button(Configuration::GetLoc("menu.delete", "Delete"))) {
                pendingDeleteIndex = static_cast<int>(index);
                deleteError.clear();
                requestDeletePopup = true;
            }
            ImGui::Unindent();
        } else if (isDirty || HasAnyPatchLayer(form)) {
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
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

    bool RenderCreatePopup() {
        bool created = false;
        if (!ImGui::BeginPopup(Configuration::GetLoc("menu.create_popup", "Create Form"))) {
            return false;
        }

        std::string editorId = editorIdBuffer.data();
        const bool validEditorId = IsValidEditorId(editorId);
        const bool duplicateEditorId = validEditorId && Manager::HasEditorId(editorId);

        ImGui::SetNextItemWidth(280.0F);
        ImGui::InputText(Configuration::GetLoc("menu.editor_id", "EditorID"), editorIdBuffer.data(), editorIdBuffer.size());
        editorId = editorIdBuffer.data();

        if (editorId.empty()) {
            ImGui::TextColored({ 1.0F, 0.75F, 0.35F, 1.0F }, "%s", Configuration::GetLoc("menu.editor_id_required", "EditorID is required."));
        } else if (!validEditorId) {
            ImGui::TextColored({ 1.0F, 0.35F, 0.35F, 1.0F }, "%s", Configuration::GetLoc("menu.editor_id_invalid", "Use only letters, numbers and underscore."));
        } else if (duplicateEditorId) {
            ImGui::TextColored({ 1.0F, 0.35F, 0.35F, 1.0F }, "%s", Configuration::GetLoc("menu.editor_id_duplicate", "A form with this EditorID already exists."));
        }

        ImGui::Text("%s", Configuration::GetLoc("menu.form_type", "Form type"));
        SetStableComboWidth(FORM_KIND_ITEMS, 220.0F);
        ImGui::Combo("##formType", &selectedFormKind, FORM_KIND_ITEMS.data(), static_cast<int>(FORM_KIND_ITEMS.size()));

        ImGui::Separator();
        if (!validEditorId || duplicateEditorId) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(Configuration::GetLoc("menu.confirm", "Confirm"))) {
            DynamicForms::DynamicForm form;
            form.editorId = editorId;
            form.kind = SelectedFormKind();
            form.packageName = ActiveWorkingPackageName();
            form.globalType = SelectedGlobalType();
            form.defaultValue = SelectedDefaultValue();
            form.fullName = createNameBuffer.data();
            form.playable = createPlayable;
            form.red = static_cast<std::uint8_t>(createColor[0]);
            form.green = static_cast<std::uint8_t>(createColor[1]);
            form.blue = static_cast<std::uint8_t>(createColor[2]);
            form.alpha = static_cast<std::uint8_t>(createColor[3]);
            form.modelPath = createModelBuffer.data();
            if (form.kind == DynamicForms::FormKind::Book ||
                form.kind == DynamicForms::FormKind::Misc ||
                form.kind == DynamicForms::FormKind::SoulGem ||
                form.kind == DynamicForms::FormKind::Weapon ||
                form.kind == DynamicForms::FormKind::AlchemyItem ||
                form.kind == DynamicForms::FormKind::Ingredient)
            {
                form.itemWeight = 1.0F;
            }
            if (form.kind == DynamicForms::FormKind::MaterialType) {
                form.materialName = editorId;
                form.red = 255;
                form.green = 255;
                form.blue = 255;
                form.alpha = 255;
            }
            if (form.kind == DynamicForms::FormKind::Ammo) {
                form.damage = 10.0F;
            }
            if (form.kind == DynamicForms::FormKind::Weapon) {
                form.damage = 8.0F;
                form.weaponSpeed = 1.0F;
                form.weaponReach = 1.0F;
                form.weaponType = 1;
                form.weaponSkill = 6;
                form.weaponResist = 24;
            }
            if (form.kind == DynamicForms::FormKind::SoulGem) {
                form.soulCapacity = 5;
            }
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
                if (auto& forms = Manager::GetForms(); !forms.empty()) {
                    AssignFormToWorkingPackage(forms.back());
                }
                ResetCreateState();
                ImGui::CloseCurrentPopup();
                created = true;
            } else {
                createError = Configuration::GetLoc("menu.create_failed", "Could not create form. Check if DPF is available.");
            }
        }
        if (!validEditorId || duplicateEditorId) {
            ImGui::EndDisabled();
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

    bool RenderCreatePatchPopup() {
        bool patched = false;
        if (!ImGui::BeginPopup(CREATE_PATCH_POPUP_ID)) {
            return false;
        }

        ImGui::Text("%s: %s", Configuration::GetLoc("menu.patch_target_package", "Patch target package"), ActiveWorkingPackageName());

        ImGui::SetNextItemWidth(220.0F);
        ImGui::InputText(Configuration::GetLoc("menu.filter_package_name", "Package name"), patchFilterPackageNameBuffer.data(), patchFilterPackageNameBuffer.size());
        ImGui::SameLine();
        SetStableComboWidth(FILTER_KIND_ITEMS, 180.0F);
        ImGui::Combo(Configuration::GetLoc("menu.filter_type", "Type"), &selectedPatchFilterKind, FILTER_KIND_ITEMS.data(), static_cast<int>(FILTER_KIND_ITEMS.size()));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(240.0F);
        ImGui::InputText(Configuration::GetLoc("menu.filter_editor_id", "EditorID"), patchFilterEditorIdBuffer.data(), patchFilterEditorIdBuffer.size());

        auto& forms = Manager::GetForms();
        ImGui::BeginChild("##createPatchForms", { 760.0F, 460.0F }, true);
        std::size_t visibleCount = 0;
        for (const auto& package : previewPackages) {
            std::vector<std::size_t> packageRows;
            for (std::size_t i = 0; i < forms.size(); ++i) {
                if (FormBelongsToPackage(forms[i], package) && MatchesPatchPopupFilters(forms[i], package)) {
                    packageRows.push_back(i);
                }
            }
            if (packageRows.empty()) {
                continue;
            }

            visibleCount += packageRows.size();
            const auto packageHeader = std::format("{} ({})###patch_package_{}", package, packageRows.size(), package);
            if (!ImGui::CollapsingHeader(packageHeader.c_str())) {
                continue;
            }

            ImGui::Indent();
            for (const auto kind : FORM_KIND_TREE_ORDER) {
                std::vector<std::size_t> kindRows;
                for (const auto row : packageRows) {
                    if (forms[row].kind == kind) {
                        kindRows.push_back(row);
                    }
                }
                if (kindRows.empty()) {
                    continue;
                }

                const auto kindHeader = std::format("{} ({})###patch_package_{}_kind_{}", FormKindLabel(kind), kindRows.size(), package, static_cast<int>(kind));
                if (!ImGui::CollapsingHeader(kindHeader.c_str())) {
                    continue;
                }

                ImGui::Indent();
                for (const auto row : kindRows) {
                    auto& form = forms[row];
                    ImGui::PushID(form.editorId.c_str());
                    bool selected = selectedPatchForms.contains(form.editorId);
                    if (ImGui::Checkbox("##patchSelect", &selected)) {
                        if (selected) {
                            selectedPatchForms.insert(form.editorId);
                        } else {
                            selectedPatchForms.erase(form.editorId);
                        }
                    }
                    ImGui::SameLine();
                    ImGui::Text("%s", form.editorId.c_str());
                    if (HasAnyPatchLayer(form)) {
                        ImGui::SameLine();
                        ImGui::TextColored(OVERRIDE_COLOR, "[%s]", PatchPackageLabel(form).c_str());
                    }
                    ImGui::PopID();
                }
                ImGui::Unindent();
            }
            ImGui::Unindent();
        }
        if (visibleCount == 0) {
            ImGui::TextDisabled("%s", Configuration::GetLoc("menu.no_forms_match_filters", "No forms match the current filters."));
        }
        ImGui::EndChild();

        ImGui::Text(Configuration::GetLoc("menu.forms_selected_count", "%zu form(s) selected."), selectedPatchForms.size());
        if (selectedPatchForms.empty()) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(Configuration::GetLoc("menu.create_patch", "Create patch"))) {
            for (const auto& editorId : selectedPatchForms) {
                const auto found = std::ranges::find_if(forms, [&editorId](const DynamicForms::DynamicForm& form) {
                    return form.editorId == editorId;
                });
                if (found != forms.end()) {
                    MarkFormAsPatchInWorkingPackage(*found);
                }
            }
            selectedPatchForms.clear();
            ImGui::CloseCurrentPopup();
            patched = true;
        }
        if (selectedPatchForms.empty()) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button(Configuration::GetLoc("menu.cancel", "Cancel"))) {
            selectedPatchForms.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
        return patched;
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
        ref.formID = info.normalizedFormID;
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

    const std::vector<PickerRow>& CachedPickerRows(const char* typeName, const std::string& search) {
        auto* manager = ListManager::GetSingleton();
        const auto generation = manager->GetGeneration();
        static std::uint64_t cachedGeneration = 0;
        if (cachedGeneration != generation) {
            pickerRowCaches.clear();
            cachedGeneration = generation;
        }
        const std::string cacheKey = std::format("{}\x1F{}", typeName, search);
        auto& cache = pickerRowCaches[cacheKey];
        if (cache.generation == generation && cache.search == search) {
            return cache.rows;
        }

        cache.generation = generation;
        cache.search = search;
        cache.rows.clear();
        const auto& list = manager->GetList(typeName);
        cache.rows.reserve(search.empty() ? list.size() : std::min<std::size_t>(list.size(), 512));
        for (const auto& info : list) {
            const auto ref = MakeFormRef(info);
            if (ref.empty()) {
                continue;
            }

            auto label = ReferenceLabel(info);
            auto searchText = ToLower(label);
            searchText += ' ';
            searchText += ToLower(ref.Display());
            if (!search.empty() && searchText.find(search) == std::string::npos) {
                continue;
            }

            cache.rows.push_back(PickerRow{ ref, std::move(label), std::move(searchText) });
        }
        return cache.rows;
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

    std::vector<const PickerRow*> BuildPieceRows(const char* typeName, const DynamicForms::DynamicForm& edited, const std::string& search) {
        std::vector<const PickerRow*> rows;
        const auto& cachedRows = CachedPickerRows(typeName, search);
        rows.reserve(cachedRows.size());
        for (const auto& row : cachedRows) {
            if (HasPiece(edited, row.ref)) {
                continue;
            }
            rows.push_back(&row);
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
                const auto& row = *rows[static_cast<std::size_t>(rowIndex)];
                if (ImGui::Selectable(row.label.c_str(), false)) {
                    edited.outfitPieces.push_back(row.ref);
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
                const auto search = ToLower(filter);
                const auto& rows = CachedPickerRows(typeName, search);

                if (ImGui::Selectable(Configuration::GetLoc("common.none", "None"), value.empty())) {
                    value = {};
                    filter.clear();
                    changed = true;
                }

                auto* clipper = ImGui::ImGuiListClipperManager::Create();
                ImGui::ImGuiListClipperManager::Begin(clipper, static_cast<int>(rows.size()), 0.0F);
                while (ImGui::ImGuiListClipperManager::Step(clipper)) {
                    for (int rowIndex = clipper->DisplayStart; rowIndex < clipper->DisplayEnd; ++rowIndex) {
                        const auto& row = rows[static_cast<std::size_t>(rowIndex)];
                        if (ImGui::Selectable(row.label.c_str(), SameFormRef(value, row.ref))) {
                            value = row.ref;
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

    bool DrawAnyFormReferencePicker(const char* label, DynamicForms::FormRef& value) {
        bool changed = false;
        auto& filter = formPickerFilters[std::string(label) + ":Any"];
        auto previewText = value.empty() ? std::string(Configuration::GetLoc("common.select", "Select")) : value.Display();

        SetAvailableComboWidth(360.0F);
        SetFixedComboPopupWidth(520.0F);
        if (ImGui::BeginCombo(label, previewText.c_str())) {
            const bool listsReady = ListManager::GetSingleton()->IsPopulated();
            char searchBuf[256]{};
            strcpy_s(searchBuf, filter.c_str());
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::InputText("##filter", searchBuf, sizeof(searchBuf))) {
                filter = searchBuf;
            }
            ImGui::Separator();

            if (ImGui::Selectable(Configuration::GetLoc("common.none", "None"), value.empty())) {
                value = {};
                filter.clear();
                changed = true;
            }

            if (!listsReady) {
                ImGui::TextDisabled("%s", Configuration::GetLoc("menu.dpf_lists_unavailable", "DPF is not available yet."));
            } else if (ImGui::BeginTabBar("##anyFormTabs")) {
                const auto search = ToLower(filter);
                for (const auto& pickerType : FORM_REFERENCE_PICKER_TYPES) {
                    if (!ImGui::BeginTabItem(pickerType.label)) {
                        continue;
                    }

                    const auto& rows = CachedPickerRows(pickerType.typeName, search);

                    ImGui::Text("%s: %zu", Configuration::GetLoc("menu.available", "Available"), rows.size());
                    ImGui::BeginChild("##anyFormRows", { 0.0F, 220.0F }, false);
                    auto* clipper = ImGui::ImGuiListClipperManager::Create();
                    ImGui::ImGuiListClipperManager::Begin(clipper, static_cast<int>(rows.size()), 0.0F);
                    while (ImGui::ImGuiListClipperManager::Step(clipper)) {
                        for (int rowIndex = clipper->DisplayStart; rowIndex < clipper->DisplayEnd; ++rowIndex) {
                            const auto& row = rows[static_cast<std::size_t>(rowIndex)];
                            if (ImGui::Selectable(row.label.c_str(), SameFormRef(value, row.ref))) {
                                value = row.ref;
                                filter.clear();
                                changed = true;
                            }
                        }
                    }
                    ImGui::ImGuiListClipperManager::End(clipper);
                    ImGui::ImGuiListClipperManager::Destroy(clipper);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndCombo();
        }

        return changed;
    }

    bool DrawReferenceArrayEditor(const char* label, const char* typeName, std::vector<DynamicForms::FormRef>& refs) {
        bool changed = false;
        ImGui::Text("%s: %zu", label, refs.size());
        for (std::size_t i = 0; i < refs.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("%s", refs[i].Display().c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(Configuration::GetLoc("menu.remove", "Remove"))) {
                refs.erase(refs.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }

        DynamicForms::FormRef selected;
        ImGui::PushID(label);
        const bool picked = typeName ? DrawFormReferencePicker(Configuration::GetLoc("menu.add_form", "Add form"), typeName, selected) : DrawAnyFormReferencePicker(Configuration::GetLoc("menu.add_form", "Add form"), selected);
        ImGui::PopID();
        if (picked && !selected.empty() && !HasReference(refs, selected)) {
            refs.push_back(selected);
            changed = true;
        }
        return changed;
    }

    bool DrawCommonItemFields(DynamicForms::DynamicForm& edited, const bool includeWeight = true, const bool includeValue = true, const bool includeMessageIcon = true) {
        bool changed = false;
        changed |= InputString(Configuration::GetLoc("menu.full_name", "Name"), edited.fullName);
        changed |= InputString(Configuration::GetLoc("menu.model_path", "Model path"), edited.modelPath, 420.0F);
        if (includeValue) {
            ImGui::SetNextItemWidth(180.0F);
            if (ImGui::InputInt(Configuration::GetLoc("menu.value", "Value"), &edited.itemValue)) {
                changed = true;
            }
        }
        if (includeWeight) {
            ImGui::SetNextItemWidth(180.0F);
            if (ImGui::InputFloat(Configuration::GetLoc("menu.weight", "Weight"), &edited.itemWeight)) {
                changed = true;
            }
        }
        changed |= InputString(Configuration::GetLoc("menu.inventory_icon", "Inventory icon"), edited.inventoryIcon, 420.0F);
        if (includeMessageIcon) {
            changed |= InputString(Configuration::GetLoc("menu.message_icon", "Message icon"), edited.messageIcon, 420.0F);
        }
        changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.pickup_sound", "Pickup sound"), "SoundDescriptor", edited.pickupSound);
        changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.putdown_sound", "Putdown sound"), "SoundDescriptor", edited.putdownSound);
        changed |= DrawReferenceArrayEditor(Configuration::GetLoc("menu.keywords", "Keywords"), "Keyword", edited.keywords);
        return changed;
    }

    bool CommitEditedForm(const std::size_t index, DynamicForms::DynamicForm& form, const DynamicForms::DynamicForm& edited, const bool changed) {
        if (changed && Manager::UpdateForm(index, edited)) {
            form = Manager::GetForms()[index];
            return true;
        }
        return false;
    }

    bool RenderFormListEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        auto edited = form;
        const bool changed = DrawReferenceArrayEditor(Configuration::GetLoc("menu.form_list_items", "Forms"), nullptr, edited.formListItems);
        return CommitEditedForm(index, form, edited, changed);
    }

    bool RenderEquipSlotEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;
        ImGui::TextUnformatted(Configuration::GetLoc("menu.flags", "Flags"));
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_use_all_parents", "Use All Parents"), edited.equipSlotFlags, 1u << 0);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_parents_optional", "Parents Optional"), edited.equipSlotFlags, 1u << 1);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_item_slot", "Item Slot"), edited.equipSlotFlags, 1u << 2);
        changed |= DrawReferenceArrayEditor(Configuration::GetLoc("menu.parent_slots", "Parent slots"), "EquipSlot", edited.equipSlotParents);
        return CommitEditedForm(index, form, edited, changed);
    }

    bool RenderVoiceTypeEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;
        if (ImGui::Checkbox(Configuration::GetLoc("menu.allow_default_dialogue", "Allow default dialogue"), &edited.voiceTypeAllowDefaultDialogue)) {
            changed = true;
        }
        if (ImGui::Checkbox(Configuration::GetLoc("menu.female", "Female"), &edited.voiceTypeFemale)) {
            changed = true;
        }
        return CommitEditedForm(index, form, edited, changed);
    }

    bool RenderSimpleItemEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        auto edited = form;
        const bool changed = DrawCommonItemFields(edited);
        return CommitEditedForm(index, form, edited, changed);
    }

    bool RenderBookEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;
        changed |= DrawCommonItemFields(edited);
        changed |= InputString(Configuration::GetLoc("menu.description", "Description"), edited.description, 520.0F);
        ImGui::TextUnformatted(Configuration::GetLoc("menu.book_flags", "Book flags"));
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_teaches_skill", "Teaches Skill"), edited.bookFlags, 1u << 0);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_cant_take", "Can't be taken"), edited.bookFlags, 1u << 1);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_teaches_spell", "Teaches Spell"), edited.bookFlags, 1u << 2);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_has_been_read", "Has Been Read"), edited.bookFlags, 1u << 3);
        int typeIndex = edited.bookType == std::numeric_limits<std::uint32_t>::max() ? 1 : 0;
        SetStableComboWidth(BOOK_TYPE_ITEMS, 180.0F);
        if (ImGui::Combo(Configuration::GetLoc("menu.book_type", "Book type"), &typeIndex, BOOK_TYPE_ITEMS.data(), static_cast<int>(BOOK_TYPE_ITEMS.size()))) {
            edited.bookType = typeIndex == 1 ? std::numeric_limits<std::uint32_t>::max() : 0u;
            changed = true;
        }
        changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.teaches_spell", "Teaches spell"), "Spell", edited.teachesSpell);
        ImGui::SetNextItemWidth(180.0F);
        if (ImGui::InputInt(Configuration::GetLoc("menu.teaches_actor_value", "Teaches actor value"), &edited.teachesActorValue)) {
            changed = true;
        }
        return CommitEditedForm(index, form, edited, changed);
    }

    bool RenderSoulGemEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;
        changed |= DrawCommonItemFields(edited);
        changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.linked_soul_gem", "Linked soul gem"), "SoulGem", edited.linkedSoulGem);
        int currentSoul = static_cast<int>(edited.currentSoul);
        SetStableComboWidth(SOUL_LEVEL_ITEMS, 180.0F);
        if (ImGui::Combo(Configuration::GetLoc("menu.current_soul", "Current soul"), &currentSoul, SOUL_LEVEL_ITEMS.data(), static_cast<int>(SOUL_LEVEL_ITEMS.size()))) {
            edited.currentSoul = static_cast<std::uint32_t>(std::clamp(currentSoul, 0, static_cast<int>(SOUL_LEVEL_ITEMS.size() - 1)));
            changed = true;
        }
        int capacity = static_cast<int>(edited.soulCapacity);
        SetStableComboWidth(SOUL_LEVEL_ITEMS, 180.0F);
        if (ImGui::Combo(Configuration::GetLoc("menu.soul_capacity", "Soul capacity"), &capacity, SOUL_LEVEL_ITEMS.data(), static_cast<int>(SOUL_LEVEL_ITEMS.size()))) {
            edited.soulCapacity = static_cast<std::uint32_t>(std::clamp(capacity, 0, static_cast<int>(SOUL_LEVEL_ITEMS.size() - 1)));
            changed = true;
        }
        return CommitEditedForm(index, form, edited, changed);
    }

    bool RenderMaterialTypeEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;
        changed |= InputString(Configuration::GetLoc("menu.material_name", "Material name"), edited.materialName);
        changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.parent_material", "Parent material"), "MaterialType", edited.materialParent);
        int materialId = static_cast<int>(edited.materialId);
        ImGui::SetNextItemWidth(180.0F);
        if (ImGui::InputInt(Configuration::GetLoc("menu.material_id", "Material ID"), &materialId)) {
            edited.materialId = static_cast<std::uint32_t>(std::max(materialId, 0));
            changed = true;
        }
        changed |= DrawRGBAColorEditor(Configuration::GetLoc("menu.color", "Color"), edited.red, edited.green, edited.blue, edited.alpha);
        ImGui::SetNextItemWidth(180.0F);
        if (ImGui::InputFloat(Configuration::GetLoc("menu.buoyancy", "Buoyancy"), &edited.buoyancy)) {
            changed = true;
        }
        ImGui::TextUnformatted(Configuration::GetLoc("menu.flags", "Flags"));
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_stairs", "Stairs"), edited.flags, 1u << 0);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_arrows_stick", "Arrows Stick"), edited.flags, 1u << 1);
        changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.impact_data_set", "Impact data set"), "ImpactDataSet", edited.havokImpactDataSet);
        return CommitEditedForm(index, form, edited, changed);
    }

    bool RenderAmmoEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;
        changed |= DrawCommonItemFields(edited, false);
        changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.projectile", "Projectile"), "Projectile", edited.projectile);
        ImGui::SetNextItemWidth(180.0F);
        if (ImGui::InputFloat(Configuration::GetLoc("menu.damage", "Damage"), &edited.damage)) {
            changed = true;
        }
        ImGui::TextUnformatted(Configuration::GetLoc("menu.ammo_flags", "Ammo flags"));
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_ignore_normal_weapon_resistance", "Ignores Normal Weapon Resistance"), edited.ammoFlags, 1u << 0);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_non_playable", "Non-Playable"), edited.ammoFlags, 1u << 1);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_non_bolt", "Non-Bolt"), edited.ammoFlags, 1u << 2);
        return CommitEditedForm(index, form, edited, changed);
    }

    int ActorValueIndex(const std::uint32_t actorValue) {
        for (std::size_t i = 0; i < ACTOR_VALUE_IDS.size(); ++i) {
            if (ACTOR_VALUE_IDS[i] == actorValue) {
                return static_cast<int>(i);
            }
        }
        return static_cast<int>(ACTOR_VALUE_IDS.size() - 1);
    }

    bool DrawActorValueCombo(const char* label, std::uint32_t& actorValue) {
        int index = ActorValueIndex(actorValue);
        SetStableComboWidth(ACTOR_VALUE_ITEMS, 220.0F);
        if (ImGui::Combo(label, &index, ACTOR_VALUE_ITEMS.data(), static_cast<int>(ACTOR_VALUE_ITEMS.size()))) {
            actorValue = ACTOR_VALUE_IDS[static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(ACTOR_VALUE_IDS.size() - 1)))];
            return true;
        }
        return false;
    }

    bool RenderWeaponEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;

        if (ImGui::BeginTabBar("##weaponTabs")) {
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.data", "Data"))) {
                changed |= DrawCommonItemFields(edited);
                int weaponType = static_cast<int>(std::clamp(edited.weaponType, 0u, static_cast<std::uint32_t>(WEAPON_TYPE_ITEMS.size() - 1)));
                SetStableComboWidth(WEAPON_TYPE_ITEMS, 220.0F);
                if (ImGui::Combo(Configuration::GetLoc("menu.weapon_type", "Weapon type"), &weaponType, WEAPON_TYPE_ITEMS.data(), static_cast<int>(WEAPON_TYPE_ITEMS.size()))) {
                    edited.weaponType = static_cast<std::uint32_t>(weaponType);
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputFloat(Configuration::GetLoc("menu.damage", "Damage"), &edited.damage)) {
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputFloat(Configuration::GetLoc("menu.speed", "Speed"), &edited.weaponSpeed)) {
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputFloat(Configuration::GetLoc("menu.reach", "Reach"), &edited.weaponReach)) {
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputFloat(Configuration::GetLoc("menu.min_range", "Min range"), &edited.weaponMinRange)) {
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputFloat(Configuration::GetLoc("menu.max_range", "Max range"), &edited.weaponMaxRange)) {
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputFloat(Configuration::GetLoc("menu.stagger", "Stagger"), &edited.weaponStagger)) {
                    changed = true;
                }
                changed |= DrawActorValueCombo(Configuration::GetLoc("menu.skill", "Skill"), edited.weaponSkill);
                changed |= DrawActorValueCombo(Configuration::GetLoc("menu.resist", "Resist"), edited.weaponResist);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.flags", "Flags"))) {
                ImGui::TextUnformatted(Configuration::GetLoc("menu.weapon_flags", "Weapon flags"));
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_ignores_normal_weapon_resistance", "Ignores Normal Weapon Resistance"), edited.weaponFlags, 1u << 0);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_automatic", "Automatic"), edited.weaponFlags, 1u << 1);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_has_scope", "Has Scope"), edited.weaponFlags, 1u << 2);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_cant_drop", "Can't Drop"), edited.weaponFlags, 1u << 3);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_hide_backpack", "Hide Backpack"), edited.weaponFlags, 1u << 4);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_embedded_weapon", "Embedded Weapon"), edited.weaponFlags, 1u << 5);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_dont_use_first_person_is_anim", "Don't Use First Person IS Anim"), edited.weaponFlags, 1u << 6);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_non_playable", "Non-Playable"), edited.weaponFlags, 1u << 7);
                ImGui::Separator();
                ImGui::TextUnformatted(Configuration::GetLoc("menu.weapon_flags2", "Weapon flags 2"));
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_player_only", "Player Only"), edited.weaponFlags2, 1u << 0);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_npcs_use_ammo", "NPCs Use Ammo"), edited.weaponFlags2, 1u << 1);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_no_jam_after_reload", "No Jam After Reload"), edited.weaponFlags2, 1u << 2);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_minor_crime", "Minor Crime"), edited.weaponFlags2, 1u << 4);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_range_fixed", "Range Fixed"), edited.weaponFlags2, 1u << 5);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_not_used_in_normal_combat", "Not Used In Normal Combat"), edited.weaponFlags2, 1u << 6);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_overrides_condition_damage", "Overrides Condition Damage"), edited.weaponFlags2, 1u << 7);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_dont_use_3rd_person_is_anim", "Don't Use 3rd Person IS Anim"), edited.weaponFlags2, 1u << 8);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_burst_shot", "Burst Shot"), edited.weaponFlags2, 1u << 9);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_rumble_alternate", "Rumble Alternate"), edited.weaponFlags2, 1u << 10);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_long_bursts", "Long Bursts"), edited.weaponFlags2, 1u << 11);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_non_hostile", "Non-Hostile"), edited.weaponFlags2, 1u << 12);
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_bound_weapon", "Bound Weapon"), edited.weaponFlags2, 1u << 13);
                ImGui::Separator();
                changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_critical_on_death", "Critical On Death"), edited.weaponCritFlags, 1u << 0);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Configuration::GetLoc("menu.references", "References"))) {
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.equip_slot", "Equip slot"), "EquipSlot", edited.equipSlot);
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.enchantment", "Enchantment"), "Enchantment", edited.enchantment);
                int enchantmentAmount = edited.enchantmentAmount;
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.enchantment_amount", "Enchantment amount"), &enchantmentAmount)) {
                    edited.enchantmentAmount = static_cast<std::uint16_t>(std::clamp(enchantmentAmount, 0, 65535));
                    changed = true;
                }
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.template_weapon", "Template weapon"), "Weapon", edited.templateWeapon);
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.crit_effect", "Critical effect"), "Spell", edited.critEffect);
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputFloat(Configuration::GetLoc("menu.crit_mult", "Critical mult"), &edited.weaponCritMult)) {
                    changed = true;
                }
                int critDamage = static_cast<int>(edited.weaponCritDamage);
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.crit_damage", "Critical damage"), &critDamage)) {
                    edited.weaponCritDamage = static_cast<std::uint32_t>(std::clamp(critDamage, 0, 65535));
                    changed = true;
                }
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.block_bash_impact_data_set", "Block bash impact data set"), "ImpactDataSet", edited.blockBashImpactDataSet);
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.alt_block_material_type", "Alt block material type"), "MaterialType", edited.altBlockMaterialType);
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.impact_data_set", "Impact data set"), "ImpactDataSet", edited.impactDataSet);
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.first_person_model_object", "1st person model object"), "Static", edited.firstPersonModelObject);
                ImGui::Separator();
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.attack_sound", "Attack sound"), "SoundDescriptor", edited.attackSound);
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.attack_sound_2d", "Attack sound 2D"), "SoundDescriptor", edited.attackSound2D);
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.attack_loop_sound", "Attack loop sound"), "SoundDescriptor", edited.attackLoopSound);
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.attack_fail_sound", "Attack fail sound"), "SoundDescriptor", edited.attackFailSound);
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.idle_sound", "Idle sound"), "SoundDescriptor", edited.idleSound);
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.equip_sound", "Equip sound"), "SoundDescriptor", edited.equipSound);
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.unequip_sound", "Unequip sound"), "SoundDescriptor", edited.unequipSound);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        return CommitEditedForm(index, form, edited, changed);
    }

    bool DrawMagicEffectsEditor(DynamicForms::DynamicForm& edited) {
        bool changed = false;
        if (ImGui::Checkbox(Configuration::GetLoc("menu.use_custom_magic_effects", "Use custom magic effects"), &edited.magicEffectsOverride)) {
            changed = true;
        }

        if (!edited.magicEffectsOverride) {
            return changed;
        }

        ImGui::Text("%s: %zu", Configuration::GetLoc("menu.magic_effects", "Magic effects"), edited.magicEffects.size());
        for (std::size_t i = 0; i < edited.magicEffects.size(); ++i) {
            auto& entry = edited.magicEffects[i];
            ImGui::PushID(static_cast<int>(i));
            const auto header = std::format("{} {}##magicEffect", Configuration::GetLoc("menu.effect", "Effect"), i + 1);
            if (ImGui::CollapsingHeader(header.c_str(), ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.magic_effect", "Magic effect"), "MagicEffect", entry.effectSetting);
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputFloat(Configuration::GetLoc("menu.magnitude", "Magnitude"), &entry.magnitude)) {
                    changed = true;
                }
                int area = static_cast<int>(entry.area);
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.area", "Area"), &area)) {
                    entry.area = static_cast<std::uint32_t>(std::max(area, 0));
                    changed = true;
                }
                int duration = static_cast<int>(entry.duration);
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputInt(Configuration::GetLoc("menu.duration", "Duration"), &duration)) {
                    entry.duration = static_cast<std::uint32_t>(std::max(duration, 0));
                    changed = true;
                }
                ImGui::SetNextItemWidth(160.0F);
                if (ImGui::InputFloat(Configuration::GetLoc("menu.cost", "Cost"), &entry.cost)) {
                    changed = true;
                }
                if (ImGui::SmallButton(Configuration::GetLoc("menu.remove", "Remove"))) {
                    edited.magicEffects.erase(edited.magicEffects.begin() + static_cast<std::ptrdiff_t>(i));
                    changed = true;
                    ImGui::PopID();
                    break;
                }
            }
            ImGui::PopID();
        }
        if (ImGui::Button(Configuration::GetLoc("menu.add_magic_effect", "Add magic effect"))) {
            edited.magicEffects.emplace_back();
            changed = true;
        }
        return changed;
    }

    bool RenderAlchemyItemEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;
        changed |= DrawCommonItemFields(edited, true, false, true);
        changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.equip_slot", "Equip slot"), "EquipSlot", edited.equipSlot);
        int costOverride = edited.alchemyCostOverride;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputInt(Configuration::GetLoc("menu.cost_override", "Cost override"), &costOverride)) {
            edited.alchemyCostOverride = costOverride;
            changed = true;
        }
        ImGui::TextUnformatted(Configuration::GetLoc("menu.alchemy_flags", "Alchemy flags"));
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_cost_override", "Cost Override"), edited.alchemyFlags, 1u << 0);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_food_item", "Food Item"), edited.alchemyFlags, 1u << 1);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_extend_duration", "Extend Duration"), edited.alchemyFlags, 1u << 3);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_medicine", "Medicine"), edited.alchemyFlags, 1u << 16);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_poison", "Poison"), edited.alchemyFlags, 1u << 17);
        changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.addiction_item", "Addiction item"), "Spell", edited.addictionItem);
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputFloat(Configuration::GetLoc("menu.addiction_chance", "Addiction chance"), &edited.addictionChance)) {
            changed = true;
        }
        changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.consumption_sound", "Consumption sound"), "SoundDescriptor", edited.consumptionSound);
        changed |= DrawMagicEffectsEditor(edited);
        return CommitEditedForm(index, form, edited, changed);
    }

    bool RenderIngredientEditor(std::size_t index, DynamicForms::DynamicForm& form) {
        bool changed = false;
        auto edited = form;
        changed |= DrawCommonItemFields(edited, true, true, false);
        changed |= DrawFormReferencePicker(Configuration::GetLoc("menu.equip_slot", "Equip slot"), "EquipSlot", edited.equipSlot);
        int costOverride = edited.ingredientCostOverride;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputInt(Configuration::GetLoc("menu.cost_override", "Cost override"), &costOverride)) {
            edited.ingredientCostOverride = costOverride;
            changed = true;
        }
        ImGui::TextUnformatted(Configuration::GetLoc("menu.ingredient_flags", "Ingredient flags"));
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_cost_override", "Cost Override"), edited.ingredientFlags, 1u << 0);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_food_item", "Food Item"), edited.ingredientFlags, 1u << 1);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_extend_duration", "Extend Duration"), edited.ingredientFlags, 1u << 3);
        changed |= FlagCheckbox(Configuration::GetLoc("menu.flag_references_persist", "References Persist"), edited.ingredientFlags, 1u << 8);
        int knownEffectFlags = edited.knownEffectFlags;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputInt(Configuration::GetLoc("menu.known_effect_flags", "Known effect flags"), &knownEffectFlags)) {
            edited.knownEffectFlags = static_cast<std::uint16_t>(std::clamp(knownEffectFlags, 0, 65535));
            changed = true;
        }
        int playerUses = edited.playerUses;
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::InputInt(Configuration::GetLoc("menu.player_uses", "Player uses"), &playerUses)) {
            edited.playerUses = static_cast<std::uint16_t>(std::clamp(playerUses, 0, 65535));
            changed = true;
        }
        changed |= DrawMagicEffectsEditor(edited);
        return CommitEditedForm(index, form, edited, changed);
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
                std::vector<const PickerRow*> rows;
                const auto& cachedRows = CachedPickerRows("HeadPart", search);
                rows.reserve(cachedRows.size());
                for (const auto& row : cachedRows) {
                    if (HasReference(edited.extraParts, row.ref)) {
                        continue;
                    }
                    rows.push_back(&row);
                }

                auto* clipper = ImGui::ImGuiListClipperManager::Create();
                ImGui::ImGuiListClipperManager::Begin(clipper, static_cast<int>(rows.size()), 0.0F);
                while (ImGui::ImGuiListClipperManager::Step(clipper)) {
                    for (int rowIndex = clipper->DisplayStart; rowIndex < clipper->DisplayEnd; ++rowIndex) {
                        const auto& row = *rows[static_cast<std::size_t>(rowIndex)];
                        if (ImGui::Selectable(row.label.c_str(), false)) {
                            edited.extraParts.push_back(row.ref);
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

        SKSEMenuFramework::SetSection(Configuration::GetLoc("menu.section", "Dynamic Forms Generator"));
        SKSEMenuFramework::AddSectionItem(Configuration::GetLoc("menu.forms", "Forms"), RenderFormsMenu);
        SKSEMenuFramework::AddSectionItem(Configuration::GetLoc("menu.export", "Export"), RenderExportMenu);
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
        RenderPackageWorkspaceHeader();
        if (requestCreateFormPopup) {
            requestCreateFormPopup = false;
            ImGui::OpenPopup(Configuration::GetLoc("menu.create_popup", "Create Form"));
        }
        if (requestCreatePatchPopup) {
            requestCreatePatchPopup = false;
            ImGui::OpenPopup(CREATE_PATCH_POPUP_ID);
        }
        RenderCreatePopup();
        RenderCreatePatchPopup();
        ImGui::Separator();
        ImGui::TextColored({ 0.6F, 0.8F, 1.0F, 1.0F }, "%s", Configuration::GetLoc("menu.saved_forms", "Saved forms"));

        const bool hasDirtyForms = Manager::HasDirtyForms();
        if (hasDirtyForms) {
            ImGui::TextColored(DIRTY_COLOR, "%s", Configuration::GetLoc("menu.unsaved_changes", "There are unsaved changes."));
        }

        if (hasDirtyForms) {
            ImGui::PushStyleColor(ImGui::ImGuiCol_Button, DIRTY_COLOR);
            ImGui::PushStyleColor(ImGui::ImGuiCol_ButtonHovered, { 1.0F, 0.82F, 0.35F, 1.0F });
            ImGui::PushStyleColor(ImGui::ImGuiCol_ButtonActive, { 0.9F, 0.58F, 0.12F, 1.0F });
        }
        if (ImGui::Button(Configuration::GetLoc("menu.save_all", "Save all"))) {
            if (Manager::SaveAllForms()) {
                lastSaveSucceeded = true;
                saveMessage = Configuration::GetLoc("menu.save_all_success", "All forms saved.");
            } else {
                lastSaveSucceeded = false;
                saveMessage = Configuration::GetLoc("menu.save_all_failed", "Could not save all forms. Check the log.");
            }
        }
        if (hasDirtyForms) {
            ImGui::PopStyleColor(3);
        }

        if (!saveMessage.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(lastSaveSucceeded ? SUCCESS_COLOR : ERROR_COLOR, "%s", saveMessage.c_str());
        }

        if (ImGui::Button(deleteSelectionMode ? Configuration::GetLoc("menu.cancel_delete_selection", "Cancel delete selection") : Configuration::GetLoc("menu.select_to_delete", "Select to delete"))) {
            deleteSelectionMode = !deleteSelectionMode;
            selectedDeleteForms.clear();
            deleteError.clear();
        }
        if (deleteSelectionMode) {
            ImGui::SameLine();
            if (selectedDeleteForms.empty()) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(std::format("{} ({})", Configuration::GetLoc("menu.confirm_delete", "Confirm delete"), selectedDeleteForms.size()).c_str())) {
                deleteError.clear();
                requestBatchDeletePopup = true;
            }
            if (selectedDeleteForms.empty()) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (ImGui::Button(Configuration::GetLoc("menu.clear_selected", "Clear selected"))) {
                selectedDeleteForms.clear();
            }
        }

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

        std::size_t renderedPackageCount = 0;
        for (const auto& package : previewPackages) {
            if (!MatchesPackageNameFilter(package)) {
                continue;
            }

            const auto packageRows = VisibleFormRows(package);
            if (packageRows.empty()) {
                continue;
            }

            ++renderedPackageCount;
            if (deleteSelectionMode) {
                bool packageSelected = AllRowsSelectedForDelete(packageRows);
                ImGui::PushID(std::format("delete_package_{}", package).c_str());
                if (ImGui::Checkbox("##selectPackageDelete", &packageSelected)) {
                    SetRowsSelectedForDelete(packageRows, packageSelected);
                }
                ImGui::PopID();
                ImGui::SameLine();
            }
            const auto packageHeader = std::format("{} ({})###package_{}", package, packageRows.size(), package);
            if (!ImGui::CollapsingHeader(packageHeader.c_str())) {
                continue;
            }

            ImGui::Indent();
            for (const auto kind : FORM_KIND_TREE_ORDER) {
                const auto kindRows = VisibleFormRows(package, kind);
                if (kindRows.empty()) {
                    continue;
                }

                if (deleteSelectionMode) {
                    bool kindSelected = AllRowsSelectedForDelete(kindRows);
                    ImGui::PushID(std::format("delete_package_{}_kind_{}", package, static_cast<int>(kind)).c_str());
                    if (ImGui::Checkbox("##selectKindDelete", &kindSelected)) {
                        SetRowsSelectedForDelete(kindRows, kindSelected);
                    }
                    ImGui::PopID();
                    ImGui::SameLine();
                }
                const auto kindHeader = std::format("{} ({})###package_{}_kind_{}", FormKindLabel(kind), kindRows.size(), package, static_cast<int>(kind));
                if (!ImGui::CollapsingHeader(kindHeader.c_str())) {
                    continue;
                }

                ImGui::Indent();
                for (const auto row : kindRows) {
                    RenderFormTreeItem(row, forms[row]);
                }
                ImGui::Unindent();
            }
            ImGui::Unindent();
        }

        if (renderedPackageCount == 0) {
            ImGui::TextDisabled("%s", Configuration::GetLoc("menu.no_forms_match_filters", "No forms match the current filters."));
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
