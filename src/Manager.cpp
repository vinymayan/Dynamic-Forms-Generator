#include "Manager.h"

#include "ConditionCatalog.h"
#include "DPFAPI.h"
#include "ListManager.h"
#include "logger.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <ranges>
#include <set>
#include <sqlite3.h>
#include <unordered_map>

namespace {
    std::vector<DynamicForms::DynamicForm> forms;
    constexpr const char* UPDATED_EVENT = "DynamicFormsGeneratorUpdated";
    constexpr const char* LOADED_EVENT = "DynamicFormsGeneratorLoaded";
    constexpr std::array CONDITION_KIND_NAMES{
        "Raw",
        "GetGlobalValue",
        "GetActorValue",
        "GetBaseActorValue",
        "HasPerk",
        "GetQuestCompleted",
        "HasSpell"
    };

    void DispatchEvent(const char* eventName, const std::string_view strArg = {}, const float numArg = 0.0F) {
        auto* dispatcher = SKSE::GetModCallbackEventSource();
        if (!dispatcher) {
            logger::warn("Could not dispatch {}: event source unavailable.", eventName);
            return;
        }

        const std::string strArgString(strArg);
        SKSE::ModCallbackEvent event{
            RE::BSFixedString(eventName),
            RE::BSFixedString(strArgString.c_str()),
            numArg,
            nullptr
        };
        dispatcher->SendEvent(&event);
        logger::info("Dispatched {} strArg '{}' numArg {}.", eventName, strArg, numArg);
    }

    std::string ToString(const DynamicForms::FormKind kind) {
        switch (kind) {
        case DynamicForms::FormKind::Keyword:
            return "Keyword";
        case DynamicForms::FormKind::FormList:
            return "FormList";
        case DynamicForms::FormKind::EquipSlot:
            return "EquipSlot";
        case DynamicForms::FormKind::VoiceType:
            return "VoiceType";
        case DynamicForms::FormKind::Outfit:
            return "Outfit";
        case DynamicForms::FormKind::ArmorType:
            return "ArmorType";
        case DynamicForms::FormKind::Armor:
            return "Armor";
        case DynamicForms::FormKind::Book:
            return "Book";
        case DynamicForms::FormKind::Misc:
            return "Misc";
        case DynamicForms::FormKind::Key:
            return "Key";
        case DynamicForms::FormKind::SoulGem:
            return "SoulGem";
        case DynamicForms::FormKind::MaterialType:
            return "MaterialType";
        case DynamicForms::FormKind::Ammo:
            return "Ammo";
        case DynamicForms::FormKind::Weapon:
            return "Weapon";
        case DynamicForms::FormKind::AlchemyItem:
            return "AlchemyItem";
        case DynamicForms::FormKind::Ingredient:
            return "Ingredient";
        case DynamicForms::FormKind::Color:
            return "Color";
        case DynamicForms::FormKind::ArtObject:
            return "ArtObject";
        case DynamicForms::FormKind::Perk:
            return "Perk";
        case DynamicForms::FormKind::HeadPart:
            return "HeadPart";
        case DynamicForms::FormKind::SoundDescriptor:
            return "SoundDescriptor";
        case DynamicForms::FormKind::Light:
            return "Light";
        case DynamicForms::FormKind::Explosion:
            return "Explosion";
        case DynamicForms::FormKind::Activator:
            return "Activator";
        case DynamicForms::FormKind::EffectShader:
            return "EffectShader";
        case DynamicForms::FormKind::NPC:
            return "NPC";
        case DynamicForms::FormKind::Global:
        default:
            return "Global";
        }
    }

    std::string NormalizeKindName(const std::string_view value) {
        std::string normalized;
        normalized.reserve(value.size());
        for (const unsigned char ch : value) {
            if (std::isalnum(ch)) {
                normalized.push_back(static_cast<char>(std::tolower(ch)));
            }
        }
        return normalized;
    }

    std::optional<DynamicForms::FormKind> TryFormKindFromString(const std::string_view value) {
        const auto normalized = NormalizeKindName(value);
        if (normalized == "global" || normalized == "glob") {
            return DynamicForms::FormKind::Global;
        }
        if (normalized == "keyword" || normalized == "kywd") {
            return DynamicForms::FormKind::Keyword;
        }
        if (normalized == "formlist" || normalized == "flst") {
            return DynamicForms::FormKind::FormList;
        }
        if (normalized == "equipslot" || normalized == "equp") {
            return DynamicForms::FormKind::EquipSlot;
        }
        if (normalized == "voicetype" || normalized == "vtyp") {
            return DynamicForms::FormKind::VoiceType;
        }
        if (normalized == "outfit" || normalized == "otft") {
            return DynamicForms::FormKind::Outfit;
        }
        if (normalized == "armortype" || normalized == "armoraddon" || normalized == "armature" || normalized == "arma") {
            return DynamicForms::FormKind::ArmorType;
        }
        if (normalized == "armor" || normalized == "armo") {
            return DynamicForms::FormKind::Armor;
        }
        if (normalized == "book") {
            return DynamicForms::FormKind::Book;
        }
        if (normalized == "misc" || normalized == "miscitem") {
            return DynamicForms::FormKind::Misc;
        }
        if (normalized == "key" || normalized == "keym") {
            return DynamicForms::FormKind::Key;
        }
        if (normalized == "soulgem" || normalized == "slgm") {
            return DynamicForms::FormKind::SoulGem;
        }
        if (normalized == "materialtype" || normalized == "matt") {
            return DynamicForms::FormKind::MaterialType;
        }
        if (normalized == "ammo") {
            return DynamicForms::FormKind::Ammo;
        }
        if (normalized == "weapon" || normalized == "weap") {
            return DynamicForms::FormKind::Weapon;
        }
        if (normalized == "alchemyitem" || normalized == "alchemy" || normalized == "alch" || normalized == "potion") {
            return DynamicForms::FormKind::AlchemyItem;
        }
        if (normalized == "ingredient" || normalized == "ingr") {
            return DynamicForms::FormKind::Ingredient;
        }
        if (normalized == "color" || normalized == "colorform" || normalized == "clfm") {
            return DynamicForms::FormKind::Color;
        }
        if (normalized == "artobject" || normalized == "arto") {
            return DynamicForms::FormKind::ArtObject;
        }
        if (normalized == "perk") {
            return DynamicForms::FormKind::Perk;
        }
        if (normalized == "headpart" || normalized == "hdpt") {
            return DynamicForms::FormKind::HeadPart;
        }
        if (normalized == "sounddescriptor" || normalized == "sounddescription" || normalized == "sndr") {
            return DynamicForms::FormKind::SoundDescriptor;
        }
        if (normalized == "light" || normalized == "ligh") {
            return DynamicForms::FormKind::Light;
        }
        if (normalized == "explosion" || normalized == "expl") {
            return DynamicForms::FormKind::Explosion;
        }
        if (normalized == "activator" || normalized == "acti") {
            return DynamicForms::FormKind::Activator;
        }
        if (normalized == "effectshader" || normalized == "efsh") {
            return DynamicForms::FormKind::EffectShader;
        }
        if (normalized == "npc") {
            return DynamicForms::FormKind::NPC;
        }
        return std::nullopt;
    }

    std::string ToString(const DynamicForms::GlobalType type) {
        switch (type) {
        case DynamicForms::GlobalType::Short:
            return "short";
        case DynamicForms::GlobalType::Long:
            return "long";
        case DynamicForms::GlobalType::Float:
        default:
            return "float";
        }
    }

    DynamicForms::GlobalType GlobalTypeFromString(const std::string_view value) {
        if (value == "short") {
            return DynamicForms::GlobalType::Short;
        }
        if (value == "long") {
            return DynamicForms::GlobalType::Long;
        }
        return DynamicForms::GlobalType::Float;
    }

    std::string ToString(const DynamicForms::ArtObjectType type) {
        switch (type) {
        case DynamicForms::ArtObjectType::MagicHitEffect:
            return "MagicHitEffect";
        case DynamicForms::ArtObjectType::MagicEnchantEffect:
            return "MagicEnchantEffect";
        case DynamicForms::ArtObjectType::MagicCasting:
        default:
            return "MagicCasting";
        }
    }

    DynamicForms::ArtObjectType ArtObjectTypeFromString(const std::string_view value) {
        if (value == "MagicHitEffect") {
            return DynamicForms::ArtObjectType::MagicHitEffect;
        }
        if (value == "MagicEnchantEffect") {
            return DynamicForms::ArtObjectType::MagicEnchantEffect;
        }
        return DynamicForms::ArtObjectType::MagicCasting;
    }

    std::string ToString(const DynamicForms::PerkConditionKind kind) {
        const auto index = static_cast<std::size_t>(kind);
        if (index < CONDITION_KIND_NAMES.size()) {
            return CONDITION_KIND_NAMES[index];
        }
        return "Raw";
    }

    DynamicForms::PerkConditionKind PerkConditionKindFromString(const std::string_view value) {
        for (std::size_t i = 0; i < CONDITION_KIND_NAMES.size(); ++i) {
            if (value == CONDITION_KIND_NAMES[i]) {
                return static_cast<DynamicForms::PerkConditionKind>(i);
            }
        }
        return DynamicForms::PerkConditionKind::Raw;
    }

    std::uint32_t FunctionIdByCatalogName(const std::string_view name, const std::uint32_t fallback) {
        for (const auto& function : ConditionCatalog::GetFunctions()) {
            if (std::string_view(function.name) == name) {
                return function.id;
            }
        }
        return fallback;
    }

    std::string ToString(const DynamicForms::HeadPartType type) {
        switch (type) {
        case DynamicForms::HeadPartType::Face:
            return "Face";
        case DynamicForms::HeadPartType::Eyes:
            return "Eyes";
        case DynamicForms::HeadPartType::Hair:
            return "Hair";
        case DynamicForms::HeadPartType::FacialHair:
            return "FacialHair";
        case DynamicForms::HeadPartType::Scar:
            return "Scar";
        case DynamicForms::HeadPartType::Eyebrows:
            return "Eyebrows";
        case DynamicForms::HeadPartType::Misc:
        default:
            return "Misc";
        }
    }

    DynamicForms::HeadPartType HeadPartTypeFromString(const std::string_view value) {
        if (value == "Face") {
            return DynamicForms::HeadPartType::Face;
        }
        if (value == "Eyes") {
            return DynamicForms::HeadPartType::Eyes;
        }
        if (value == "Hair") {
            return DynamicForms::HeadPartType::Hair;
        }
        if (value == "FacialHair" || value == "Facial Hair") {
            return DynamicForms::HeadPartType::FacialHair;
        }
        if (value == "Scar") {
            return DynamicForms::HeadPartType::Scar;
        }
        if (value == "Eyebrows") {
            return DynamicForms::HeadPartType::Eyebrows;
        }
        return DynamicForms::HeadPartType::Misc;
    }

    RE::TESGlobal::Type ToTESGlobalType(const DynamicForms::GlobalType type) {
        switch (type) {
        case DynamicForms::GlobalType::Short:
            return RE::TESGlobal::Type::kShort;
        case DynamicForms::GlobalType::Long:
            return RE::TESGlobal::Type::kLong;
        case DynamicForms::GlobalType::Float:
        default:
            return RE::TESGlobal::Type::kFloat;
        }
    }

    std::filesystem::path FormPath(const std::string& editorId) {
        return std::filesystem::path(Manager::FORMS_DIR) / (editorId + ".json");
    }

    bool LooksLikeFormIDString(const std::string& value) {
        if (value.find('|') != std::string::npos) {
            return true;
        }
        if (value.empty() || value.size() > 8) {
            return false;
        }
        return std::ranges::all_of(value, [](const unsigned char c) {
            return std::isxdigit(c) != 0;
        });
    }

    DynamicForms::FormRef ParseConfigFormRefString(const std::string& value) {
        DynamicForms::FormRef ref;
        const auto open = value.rfind(" (");
        if (open != std::string::npos && value.ends_with(')')) {
            ref.editorID = value.substr(0, open);
            ref.formID = value.substr(open + 2, value.size() - open - 3);
            return ref;
        }

        if (LooksLikeFormIDString(value)) {
            ref.formID = value;
        } else {
            ref.editorID = value;
            ref.formID = value;
        }
        return ref;
    }

    DynamicForms::FormRef ReadFormRefValue(const rapidjson::Value& value) {
        DynamicForms::FormRef ref;
        if (value.IsObject()) {
            if (value.HasMember("editorID") && value["editorID"].IsString()) {
                ref.editorID = value["editorID"].GetString();
            } else if (value.HasMember("editorId") && value["editorId"].IsString()) {
                ref.editorID = value["editorId"].GetString();
            }

            if (value.HasMember("formID") && value["formID"].IsString()) {
                ref.formID = value["formID"].GetString();
            } else if (value.HasMember("formId") && value["formId"].IsString()) {
                ref.formID = value["formId"].GetString();
            } else if (value.HasMember("form") && value["form"].IsString()) {
                ref.formID = value["form"].GetString();
            } else if (value.HasMember("id") && value["id"].IsString()) {
                ref.formID = value["id"].GetString();
            }
            return ref;
        }

        if (value.IsString()) {
            ref = ParseConfigFormRefString(value.GetString());
        } else if (value.IsUint()) {
            ref.formID = std::format("{:08X}", value.GetUint());
        }
        return ref;
    }

    void ReadFormRef(const rapidjson::Document& doc, const char* key, DynamicForms::FormRef& out) {
        if (doc.HasMember(key)) {
            out = ReadFormRefValue(doc[key]);
        }
    }

    void AddFormRef(rapidjson::Document& doc, rapidjson::Document::AllocatorType& allocator, const char* key, const DynamicForms::FormRef& ref) {
        if (ref.empty()) {
            return;
        }

        rapidjson::Value value(rapidjson::kObjectType);
        if (!ref.editorID.empty()) {
            value.AddMember("editorID", rapidjson::Value(ref.editorID.c_str(), allocator), allocator);
        }
        if (!ref.formID.empty()) {
            value.AddMember("formID", rapidjson::Value(ref.formID.c_str(), allocator), allocator);
        }
        doc.AddMember(rapidjson::Value(key, allocator), value, allocator);
    }

    void PushFormRef(rapidjson::Value& array, rapidjson::Document::AllocatorType& allocator, const DynamicForms::FormRef& ref) {
        if (ref.empty()) {
            return;
        }

        rapidjson::Value value(rapidjson::kObjectType);
        if (!ref.editorID.empty()) {
            value.AddMember("editorID", rapidjson::Value(ref.editorID.c_str(), allocator), allocator);
        }
        if (!ref.formID.empty()) {
            value.AddMember("formID", rapidjson::Value(ref.formID.c_str(), allocator), allocator);
        }
        array.PushBack(value, allocator);
    }

    void ReadFormRefArray(const rapidjson::Value& doc, const char* key, std::vector<DynamicForms::FormRef>& target) {
        if (!doc.HasMember(key) || !doc[key].IsArray()) {
            return;
        }

        target.clear();
        for (const auto& item : doc[key].GetArray()) {
            auto ref = ReadFormRefValue(item);
            if (!ref.empty()) {
                target.push_back(std::move(ref));
            }
        }
    }

    void AddFormRefArray(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const char* key, const std::vector<DynamicForms::FormRef>& values) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto& value : values) {
            PushFormRef(array, allocator, value);
        }
        object.AddMember(rapidjson::Value(key, allocator), array, allocator);
    }

    std::uint32_t ReadUInt32(const rapidjson::Value& doc, const char* key, std::uint32_t fallback);
    float ReadFloat(const rapidjson::Value& doc, const char* key, float fallback);

    void ReadMagicEffectArray(const rapidjson::Value& doc, const char* key, std::vector<DynamicForms::MagicEffectEntry>& target) {
        if (!doc.HasMember(key) || !doc[key].IsArray()) {
            return;
        }

        target.clear();
        for (const auto& item : doc[key].GetArray()) {
            if (!item.IsObject()) {
                continue;
            }

            DynamicForms::MagicEffectEntry entry;
            if (item.HasMember("effectSetting")) {
                entry.effectSetting = ReadFormRefValue(item["effectSetting"]);
            } else if (item.HasMember("effect")) {
                entry.effectSetting = ReadFormRefValue(item["effect"]);
            }
            entry.magnitude = ReadFloat(item, "magnitude", entry.magnitude);
            entry.area = ReadUInt32(item, "area", entry.area);
            entry.duration = ReadUInt32(item, "duration", entry.duration);
            entry.cost = ReadFloat(item, "cost", entry.cost);
            if (!entry.effectSetting.empty()) {
                target.push_back(std::move(entry));
            }
        }
    }

    std::string ToSignature(const DynamicForms::FormKind kind) {
        switch (kind) {
        case DynamicForms::FormKind::Global:
            return "GLOB";
        case DynamicForms::FormKind::Keyword:
            return "KYWD";
        case DynamicForms::FormKind::FormList:
            return "FLST";
        case DynamicForms::FormKind::EquipSlot:
            return "EQUP";
        case DynamicForms::FormKind::VoiceType:
            return "VTYP";
        case DynamicForms::FormKind::Outfit:
            return "OTFT";
        case DynamicForms::FormKind::ArmorType:
            return "ARMA";
        case DynamicForms::FormKind::Armor:
            return "ARMO";
        case DynamicForms::FormKind::Book:
            return "BOOK";
        case DynamicForms::FormKind::Misc:
            return "MISC";
        case DynamicForms::FormKind::Key:
            return "KEYM";
        case DynamicForms::FormKind::SoulGem:
            return "SLGM";
        case DynamicForms::FormKind::MaterialType:
            return "MATT";
        case DynamicForms::FormKind::Ammo:
            return "AMMO";
        case DynamicForms::FormKind::Weapon:
            return "WEAP";
        case DynamicForms::FormKind::AlchemyItem:
            return "ALCH";
        case DynamicForms::FormKind::Ingredient:
            return "INGR";
        case DynamicForms::FormKind::Color:
            return "CLFM";
        case DynamicForms::FormKind::ArtObject:
            return "ARTO";
        case DynamicForms::FormKind::Perk:
            return "PERK";
        case DynamicForms::FormKind::HeadPart:
            return "HDPT";
        case DynamicForms::FormKind::SoundDescriptor:
            return "SNDR";
        case DynamicForms::FormKind::Light:
            return "LIGH";
        case DynamicForms::FormKind::Explosion:
            return "EXPL";
        case DynamicForms::FormKind::Activator:
            return "ACTI";
        case DynamicForms::FormKind::EffectShader:
            return "EFSH";
        case DynamicForms::FormKind::NPC:
            return "NPC_";
        default:
            return ToString(kind);
        }
    }

    std::string JoinSignatures(const std::set<std::string>& signatures) {
        std::string joined;
        for (const auto& signature : signatures) {
            if (!joined.empty()) {
                joined += ",";
            }
            joined += signature;
        }
        return joined;
    }

    void AddMagicEffectArray(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const char* key, const std::vector<DynamicForms::MagicEffectEntry>& values) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto& value : values) {
            if (value.effectSetting.empty()) {
                continue;
            }

            rapidjson::Value item(rapidjson::kObjectType);
            rapidjson::Value effectRef(rapidjson::kObjectType);
            if (!value.effectSetting.editorID.empty()) {
                effectRef.AddMember("editorID", rapidjson::Value(value.effectSetting.editorID.c_str(), allocator), allocator);
            }
            if (!value.effectSetting.formID.empty()) {
                effectRef.AddMember("formID", rapidjson::Value(value.effectSetting.formID.c_str(), allocator), allocator);
            }
            item.AddMember("effectSetting", effectRef, allocator);
            item.AddMember("magnitude", value.magnitude, allocator);
            item.AddMember("area", value.area, allocator);
            item.AddMember("duration", value.duration, allocator);
            item.AddMember("cost", value.cost, allocator);
            array.PushBack(item, allocator);
        }
        object.AddMember(rapidjson::Value(key, allocator), array, allocator);
    }

    void ReadRankedFormRefArray(const rapidjson::Value& doc, const char* key, std::vector<DynamicForms::RankedFormRef>& target) {
        if (!doc.HasMember(key) || !doc[key].IsArray()) {
            return;
        }

        target.clear();
        for (const auto& item : doc[key].GetArray()) {
            if (!item.IsObject()) {
                continue;
            }

            DynamicForms::RankedFormRef ranked;
            if (item.HasMember("form")) {
                ranked.form = ReadFormRefValue(item["form"]);
            }
            if (item.HasMember("rank") && item["rank"].IsInt()) {
                ranked.rank = item["rank"].GetInt();
            }
            if (!ranked.form.empty()) {
                target.push_back(std::move(ranked));
            }
        }
    }

    void AddRankedFormRefArray(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const char* key, const std::vector<DynamicForms::RankedFormRef>& values) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto& value : values) {
            if (value.form.empty()) {
                continue;
            }
            rapidjson::Value item(rapidjson::kObjectType);
            rapidjson::Value formRef(rapidjson::kObjectType);
            if (!value.form.editorID.empty()) {
                formRef.AddMember("editorID", rapidjson::Value(value.form.editorID.c_str(), allocator), allocator);
            }
            if (!value.form.formID.empty()) {
                formRef.AddMember("formID", rapidjson::Value(value.form.formID.c_str(), allocator), allocator);
            }
            item.AddMember("form", formRef, allocator);
            item.AddMember("rank", value.rank, allocator);
            array.PushBack(item, allocator);
        }
        object.AddMember(rapidjson::Value(key, allocator), array, allocator);
    }

    std::uint32_t FormTypeForKind(const DynamicForms::FormKind kind) {
        switch (kind) {
        case DynamicForms::FormKind::Keyword:
            return static_cast<std::uint32_t>(RE::FormType::Keyword);
        case DynamicForms::FormKind::FormList:
            return static_cast<std::uint32_t>(RE::FormType::FormList);
        case DynamicForms::FormKind::EquipSlot:
            return static_cast<std::uint32_t>(RE::FormType::EquipSlot);
        case DynamicForms::FormKind::VoiceType:
            return static_cast<std::uint32_t>(RE::FormType::VoiceType);
        case DynamicForms::FormKind::Outfit:
            return static_cast<std::uint32_t>(RE::FormType::Outfit);
        case DynamicForms::FormKind::ArmorType:
            return static_cast<std::uint32_t>(RE::FormType::Armature);
        case DynamicForms::FormKind::Armor:
            return static_cast<std::uint32_t>(RE::FormType::Armor);
        case DynamicForms::FormKind::Book:
            return static_cast<std::uint32_t>(RE::FormType::Book);
        case DynamicForms::FormKind::Misc:
            return static_cast<std::uint32_t>(RE::FormType::Misc);
        case DynamicForms::FormKind::Key:
            return static_cast<std::uint32_t>(RE::FormType::KeyMaster);
        case DynamicForms::FormKind::SoulGem:
            return static_cast<std::uint32_t>(RE::FormType::SoulGem);
        case DynamicForms::FormKind::MaterialType:
            return static_cast<std::uint32_t>(RE::FormType::MaterialType);
        case DynamicForms::FormKind::Ammo:
            return static_cast<std::uint32_t>(RE::FormType::Ammo);
        case DynamicForms::FormKind::Weapon:
            return static_cast<std::uint32_t>(RE::FormType::Weapon);
        case DynamicForms::FormKind::AlchemyItem:
            return static_cast<std::uint32_t>(RE::FormType::AlchemyItem);
        case DynamicForms::FormKind::Ingredient:
            return static_cast<std::uint32_t>(RE::FormType::Ingredient);
        case DynamicForms::FormKind::Color:
            return static_cast<std::uint32_t>(RE::FormType::ColorForm);
        case DynamicForms::FormKind::ArtObject:
            return static_cast<std::uint32_t>(RE::FormType::ArtObject);
        case DynamicForms::FormKind::Perk:
            return static_cast<std::uint32_t>(RE::FormType::Perk);
        case DynamicForms::FormKind::HeadPart:
            return static_cast<std::uint32_t>(RE::FormType::HeadPart);
        case DynamicForms::FormKind::SoundDescriptor:
            return static_cast<std::uint32_t>(RE::FormType::SoundRecord);
        case DynamicForms::FormKind::Light:
            return static_cast<std::uint32_t>(RE::FormType::Light);
        case DynamicForms::FormKind::Explosion:
            return static_cast<std::uint32_t>(RE::FormType::Explosion);
        case DynamicForms::FormKind::Activator:
            return static_cast<std::uint32_t>(RE::FormType::Activator);
        case DynamicForms::FormKind::EffectShader:
            return static_cast<std::uint32_t>(RE::FormType::EffectShader);
        case DynamicForms::FormKind::NPC:
            return static_cast<std::uint32_t>(RE::FormType::NPC);
        case DynamicForms::FormKind::Global:
        default:
            return static_cast<std::uint32_t>(RE::FormType::Global);
        }
    }

    RE::BGSArtObject::ArtType ToTESArtType(const DynamicForms::ArtObjectType type) {
        switch (type) {
        case DynamicForms::ArtObjectType::MagicHitEffect:
            return RE::BGSArtObject::ArtType::kMagicHitEffect;
        case DynamicForms::ArtObjectType::MagicEnchantEffect:
            return RE::BGSArtObject::ArtType::kMagicEnchantEffect;
        case DynamicForms::ArtObjectType::MagicCasting:
        default:
            return RE::BGSArtObject::ArtType::kMagicCastingArt;
        }
    }

    RE::BGSHeadPart::HeadPartType ToTESHeadPartType(const DynamicForms::HeadPartType type) {
        switch (type) {
        case DynamicForms::HeadPartType::Face:
            return RE::BGSHeadPart::HeadPartType::kFace;
        case DynamicForms::HeadPartType::Eyes:
            return RE::BGSHeadPart::HeadPartType::kEyes;
        case DynamicForms::HeadPartType::Hair:
            return RE::BGSHeadPart::HeadPartType::kHair;
        case DynamicForms::HeadPartType::FacialHair:
            return RE::BGSHeadPart::HeadPartType::kFacialHair;
        case DynamicForms::HeadPartType::Scar:
            return RE::BGSHeadPart::HeadPartType::kScar;
        case DynamicForms::HeadPartType::Eyebrows:
            return RE::BGSHeadPart::HeadPartType::kEyebrows;
        case DynamicForms::HeadPartType::Misc:
        default:
            return RE::BGSHeadPart::HeadPartType::kMisc;
        }
    }

    bool ConfigureGlobal(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* global = tesForm ? tesForm->As<RE::TESGlobal>() : nullptr;
        if (!global) {
            logger::warn("Dynamic form '{}' is not a TESGlobal", form.editorId);
            return false;
        }

        global->SetFormEditorID(form.editorId.c_str());
        global->type = ToTESGlobalType(form.globalType);
        global->value = form.defaultValue;
        return true;
    }

    RE::TESForm* ResolveConfigForm(const DynamicForms::FormRef& value) {
        if (!value.editorID.empty()) {
            if (auto* form = RE::TESForm::LookupByEditorID(value.editorID)) {
                return form;
            }
        }
        if (value.formID.empty()) {
            return nullptr;
        }

        try {
            const auto formId = FormUtil::FormIDFromString(value.formID);
            return formId != 0 ? RE::TESForm::LookupByID(formId) : nullptr;
        } catch (...) {
            logger::warn("Invalid form ref '{}'", value.Display());
            return nullptr;
        }
    }

    RE::TESForm* ResolveConfigForm(const std::string& value) {
        return ResolveConfigForm(ParseConfigFormRefString(value));
    }

    bool ConfigureOutfit(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* outfit = tesForm ? tesForm->As<RE::BGSOutfit>() : nullptr;
        if (!outfit) {
            logger::warn("Dynamic form '{}' is not a BGSOutfit", form.editorId);
            return false;
        }

        outfit->outfitItems.clear();
        for (const auto& piece : form.outfitPieces) {
            auto* pieceForm = ResolveConfigForm(piece);
            if (!pieceForm) {
                logger::warn("Outfit '{}' piece '{}' could not be resolved.", form.editorId, piece.Display());
                continue;
            }

            if (!pieceForm->Is(RE::FormType::Armor) && !pieceForm->Is(RE::FormType::LeveledItem)) {
                logger::warn("Outfit '{}' piece '{}' is not Armor or LeveledItem.", form.editorId, piece.Display());
                continue;
            }

            outfit->outfitItems.push_back(pieceForm);
        }

        logger::info("Configured outfit '{}' with {} pieces.", form.editorId, outfit->outfitItems.size());
        return true;
    }

    void ConfigureBipedObject(RE::BGSBipedObjectForm& biped, const DynamicForms::DynamicForm& form) {
        biped.bipedModelData.bipedObjectSlots = static_cast<RE::BIPED_MODEL::BipedObjectSlot>(form.bipedSlots);
        biped.bipedModelData.armorType = static_cast<RE::BIPED_MODEL::ArmorType>(form.armorType);
    }

    void SetModelIfPresent(RE::TESModel& model, const std::string& path) {
        model.SetModel(path.c_str());
    }

    void SetIconIfPresent(RE::TESIcon& icon, const std::string& path) {
        icon.textureName = path.c_str();
    }

    template <class T>
    T* ResolveAs(const DynamicForms::FormRef& ref);

    template <class T>
    T* ResolveOrKeep(const DynamicForms::FormRef& ref, T* current, const std::string_view editorId, const std::string_view fieldName) {
        if (ref.empty()) {
            return current;
        }

        auto* resolved = ResolveAs<T>(ref);
        if (!resolved) {
            logger::warn("NPC '{}' {} '{}' could not be resolved. Keeping template/current value.",
                editorId,
                fieldName,
                ref.Display());
            return current;
        }
        return resolved;
    }

    void ApplyKeywords(RE::BGSKeywordForm& keywordForm, const std::vector<DynamicForms::FormRef>& refs) {
        std::vector<RE::BGSKeyword*> keywords;
        for (const auto& keywordRef : refs) {
            if (auto* keyword = ResolveAs<RE::BGSKeyword>(keywordRef)) {
                keywords.push_back(keyword);
            }
        }
        while (keywordForm.GetNumKeywords() > 0) {
            keywordForm.RemoveKeyword(static_cast<std::uint32_t>(0));
        }
        keywordForm.AddKeywords(keywords);
    }

    void ApplyPickupPutdownSounds(RE::BGSPickupPutdownSounds& sounds, const DynamicForms::DynamicForm& form) {
        sounds.pickupSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.pickupSound);
        sounds.putdownSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.putdownSound);
    }

    void ApplyInventoryIcons(RE::TESIcon& inventoryIcon, RE::BGSMessageIcon& messageIcon, const DynamicForms::DynamicForm& form) {
        SetIconIfPresent(inventoryIcon, form.inventoryIcon);
        SetIconIfPresent(messageIcon.icon, form.messageIcon);
    }

    void ApplyMagicEffects(RE::MagicItem& magicItem, const DynamicForms::DynamicForm& form) {
        if (!form.magicEffectsOverride) {
            return;
        }

        magicItem.effects.clear();
        for (const auto& entry : form.magicEffects) {
            auto* effectSetting = ResolveAs<RE::EffectSetting>(entry.effectSetting);
            if (!effectSetting) {
                logger::warn("Magic item '{}' effect '{}' could not be resolved.", form.editorId, entry.effectSetting.Display());
                continue;
            }

            auto* effect = new RE::Effect();
            effect->baseEffect = effectSetting;
            effect->effectItem.magnitude = entry.magnitude;
            effect->effectItem.area = entry.area;
            effect->effectItem.duration = entry.duration;
            effect->cost = entry.cost;
            magicItem.effects.push_back(effect);
        }
        logger::info("Configured magic item '{}' with {} custom effects.", form.editorId, magicItem.effects.size());
    }

    void ApplyMiscLikeItem(RE::TESObjectMISC& item, const DynamicForms::DynamicForm& form) {
        item.SetFormEditorID(form.editorId.c_str());
        item.fullName = form.fullName.empty() ? form.editorId.c_str() : form.fullName.c_str();
        item.SetModel(form.modelPath.c_str());
        item.value = form.itemValue;
        item.weight = form.itemWeight;
        ApplyInventoryIcons(static_cast<RE::TESIcon&>(item), static_cast<RE::BGSMessageIcon&>(item), form);
        ApplyPickupPutdownSounds(static_cast<RE::BGSPickupPutdownSounds&>(item), form);
        ApplyKeywords(static_cast<RE::BGSKeywordForm&>(item), form.keywords);
    }

    bool ConfigureFormList(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* list = tesForm ? tesForm->As<RE::BGSListForm>() : nullptr;
        if (!list) {
            logger::warn("Dynamic form '{}' is not a BGSListForm", form.editorId);
            return false;
        }

        list->SetFormEditorID(form.editorId.c_str());
        list->forms.clear();
        for (const auto& itemRef : form.formListItems) {
            if (auto* item = ResolveConfigForm(itemRef)) {
                list->forms.push_back(item);
            } else {
                logger::warn("Form list '{}' item '{}' could not be resolved.", form.editorId, itemRef.Display());
            }
        }
        logger::info("Configured form list '{}' with {} forms.", form.editorId, list->forms.size());
        return true;
    }

    bool ConfigureEquipSlot(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* equipSlot = tesForm ? tesForm->As<RE::BGSEquipSlot>() : nullptr;
        if (!equipSlot) {
            logger::warn("Dynamic form '{}' is not a BGSEquipSlot", form.editorId);
            return false;
        }

        equipSlot->SetFormEditorID(form.editorId.c_str());
        equipSlot->flags = static_cast<RE::BGSEquipSlot::Flag>(form.equipSlotFlags);
        equipSlot->parentSlots.clear();
        for (const auto& parentRef : form.equipSlotParents) {
            if (auto* parent = ResolveAs<RE::BGSEquipSlot>(parentRef)) {
                equipSlot->parentSlots.push_back(parent);
            }
        }
        return true;
    }

    bool ConfigureVoiceType(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* voiceType = tesForm ? tesForm->As<RE::BGSVoiceType>() : nullptr;
        if (!voiceType) {
            logger::warn("Dynamic form '{}' is not a BGSVoiceType", form.editorId);
            return false;
        }

        voiceType->SetFormEditorID(form.editorId.c_str());
        voiceType->data.flags = RE::VOICE_TYPE_DATA::Flag::kNone;
        if (form.voiceTypeAllowDefaultDialogue) {
            voiceType->data.flags.set(RE::VOICE_TYPE_DATA::Flag::kAllowDefaultDialogue);
        }
        if (form.voiceTypeFemale) {
            voiceType->data.flags.set(RE::VOICE_TYPE_DATA::Flag::kFemale);
        }
        return true;
    }

    bool ConfigureMisc(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* misc = tesForm ? tesForm->As<RE::TESObjectMISC>() : nullptr;
        if (!misc) {
            logger::warn("Dynamic form '{}' is not a TESObjectMISC", form.editorId);
            return false;
        }
        ApplyMiscLikeItem(*misc, form);
        return true;
    }

    bool ConfigureKey(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* key = tesForm ? tesForm->As<RE::TESKey>() : nullptr;
        if (!key) {
            logger::warn("Dynamic form '{}' is not a TESKey", form.editorId);
            return false;
        }
        ApplyMiscLikeItem(*key, form);
        return true;
    }

    bool ConfigureSoulGem(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* soulGem = tesForm ? tesForm->As<RE::TESSoulGem>() : nullptr;
        if (!soulGem) {
            logger::warn("Dynamic form '{}' is not a TESSoulGem", form.editorId);
            return false;
        }
        ApplyMiscLikeItem(*soulGem, form);
        soulGem->linkedSoulGem = ResolveAs<RE::TESSoulGem>(form.linkedSoulGem);
        soulGem->currentSoul = static_cast<RE::SOUL_LEVEL>(form.currentSoul);
        soulGem->soulCapacity = static_cast<RE::SOUL_LEVEL>(form.soulCapacity);
        return true;
    }

    bool ConfigureMaterialType(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* material = tesForm ? tesForm->As<RE::BGSMaterialType>() : nullptr;
        if (!material) {
            logger::warn("Dynamic form '{}' is not a BGSMaterialType", form.editorId);
            return false;
        }

        material->SetFormEditorID(form.editorId.c_str());
        material->parentType = ResolveAs<RE::BGSMaterialType>(form.materialParent);
        material->materialName = form.materialName.empty() ? form.editorId.c_str() : form.materialName.c_str();
        material->materialID = static_cast<RE::MATERIAL_ID>(form.materialId);
        material->materialColor = RE::NiColor(form.red / 255.0F, form.green / 255.0F, form.blue / 255.0F);
        material->buoyancy = form.buoyancy;
        material->flags = static_cast<RE::BGSMaterialType::FLAG>(form.flags);
        material->havokImpactDataSet = ResolveAs<RE::BGSImpactDataSet>(form.havokImpactDataSet);
        return true;
    }

    bool ConfigureBook(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* book = tesForm ? tesForm->As<RE::TESObjectBOOK>() : nullptr;
        if (!book) {
            logger::warn("Dynamic form '{}' is not a TESObjectBOOK", form.editorId);
            return false;
        }

        book->SetFormEditorID(form.editorId.c_str());
        book->fullName = form.fullName.empty() ? form.editorId.c_str() : form.fullName.c_str();
        book->SetModel(form.modelPath.c_str());
        book->value = form.itemValue;
        book->weight = form.itemWeight;
        book->data.flags = static_cast<RE::OBJ_BOOK::Flag>(form.bookFlags);
        book->data.type = static_cast<RE::OBJ_BOOK::Type>(form.bookType);
        if (!form.teachesSpell.empty()) {
            book->data.teaches.spell = ResolveAs<RE::SpellItem>(form.teachesSpell);
        } else {
            book->data.teaches.actorValueToAdvance = static_cast<RE::ActorValue>(form.teachesActorValue);
        }
        ApplyInventoryIcons(static_cast<RE::TESIcon&>(*book), static_cast<RE::BGSMessageIcon&>(*book), form);
        ApplyPickupPutdownSounds(static_cast<RE::BGSPickupPutdownSounds&>(*book), form);
        ApplyKeywords(static_cast<RE::BGSKeywordForm&>(*book), form.keywords);
        return true;
    }

    bool ConfigureAmmo(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* ammo = tesForm ? tesForm->As<RE::TESAmmo>() : nullptr;
        if (!ammo) {
            logger::warn("Dynamic form '{}' is not a TESAmmo", form.editorId);
            return false;
        }

        ammo->SetFormEditorID(form.editorId.c_str());
        ammo->fullName = form.fullName.empty() ? form.editorId.c_str() : form.fullName.c_str();
        ammo->SetModel(form.modelPath.c_str());
        ammo->value = form.itemValue;
        auto& data = ammo->GetRuntimeData().data;
        data.projectile = ResolveAs<RE::BGSProjectile>(form.projectile);
        data.damage = form.damage;
        data.flags = static_cast<RE::AMMO_DATA::Flag>(form.ammoFlags);
        ApplyInventoryIcons(static_cast<RE::TESIcon&>(*ammo), static_cast<RE::BGSMessageIcon&>(*ammo), form);
        if (auto* sounds = ammo->AsPickupPutdownSoundsForm()) {
            ApplyPickupPutdownSounds(*sounds, form);
        }
        if (auto* keywordForm = ammo->AsKeywordForm()) {
            ApplyKeywords(*keywordForm, form.keywords);
        }
        return true;
    }

    bool ConfigureWeapon(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* weapon = tesForm ? tesForm->As<RE::TESObjectWEAP>() : nullptr;
        if (!weapon) {
            logger::warn("Dynamic form '{}' is not a TESObjectWEAP", form.editorId);
            return false;
        }

        weapon->SetFormEditorID(form.editorId.c_str());
        weapon->fullName = form.fullName.empty() ? form.editorId.c_str() : form.fullName.c_str();
        weapon->SetModel(form.modelPath.c_str());
        weapon->value = form.itemValue;
        weapon->weight = form.itemWeight;
        weapon->attackDamage = static_cast<std::uint16_t>(std::clamp(static_cast<int>(std::lround(form.damage)), 0, 65535));
        weapon->formEnchanting = ResolveAs<RE::EnchantmentItem>(form.enchantment);
        weapon->amountofEnchantment = form.enchantmentAmount;
        weapon->SetEquipSlot(ResolveAs<RE::BGSEquipSlot>(form.equipSlot));
        weapon->templateWeapon = ResolveAs<RE::TESObjectWEAP>(form.templateWeapon);
        weapon->blockBashImpactDataSet = ResolveAs<RE::BGSImpactDataSet>(form.blockBashImpactDataSet);
        weapon->altBlockMaterialType = ResolveAs<RE::BGSMaterialType>(form.altBlockMaterialType);
        weapon->impactDataSet = ResolveAs<RE::BGSImpactDataSet>(form.impactDataSet);
        weapon->firstPersonModelObject = ResolveAs<RE::TESObjectSTAT>(form.firstPersonModelObject);
        weapon->attackSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.attackSound);
        weapon->attackSound2D = ResolveAs<RE::BGSSoundDescriptorForm>(form.attackSound2D);
        weapon->attackLoopSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.attackLoopSound);
        weapon->attackFailSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.attackFailSound);
        weapon->idleSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.idleSound);
        weapon->equipSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.equipSound);
        weapon->unequipSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.unequipSound);
        ApplyInventoryIcons(static_cast<RE::TESIcon&>(*weapon), static_cast<RE::BGSMessageIcon&>(*weapon), form);
        ApplyPickupPutdownSounds(static_cast<RE::BGSPickupPutdownSounds&>(*weapon), form);
        ApplyKeywords(static_cast<RE::BGSKeywordForm&>(*weapon), form.keywords);

        weapon->weaponData.speed = form.weaponSpeed;
        weapon->weaponData.reach = form.weaponReach;
        weapon->weaponData.minRange = form.weaponMinRange;
        weapon->weaponData.maxRange = form.weaponMaxRange;
        weapon->weaponData.staggerValue = form.weaponStagger;
        weapon->weaponData.animationType = static_cast<RE::WEAPON_TYPE>(std::clamp(form.weaponType, 0u, 9u));
        weapon->weaponData.flags = static_cast<RE::TESObjectWEAP::Data::Flag>(form.weaponFlags);
        weapon->weaponData.flags2 = static_cast<RE::TESObjectWEAP::Data::Flag2>(form.weaponFlags2);
        weapon->weaponData.skill = static_cast<RE::ActorValue>(form.weaponSkill);
        weapon->weaponData.resistance = static_cast<RE::ActorValue>(form.weaponResist);
        weapon->criticalData.prcntMult = form.weaponCritMult;
        weapon->criticalData.damage = static_cast<std::uint16_t>(std::clamp(form.weaponCritDamage, 0u, 65535u));
        weapon->criticalData.flags = static_cast<RE::TESObjectWEAP::CriticalData::Flag>(form.weaponCritFlags);
        weapon->criticalData.effect = ResolveAs<RE::SpellItem>(form.critEffect);
        return true;
    }

    bool ConfigureAlchemyItem(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* item = tesForm ? tesForm->As<RE::AlchemyItem>() : nullptr;
        if (!item) {
            logger::warn("Dynamic form '{}' is not an AlchemyItem", form.editorId);
            return false;
        }

        item->SetFormEditorID(form.editorId.c_str());
        item->fullName = form.fullName.empty() ? form.editorId.c_str() : form.fullName.c_str();
        item->SetModel(form.modelPath.c_str());
        item->weight = form.itemWeight;
        item->SetEquipSlot(ResolveAs<RE::BGSEquipSlot>(form.equipSlot));
        item->data.costOverride = form.alchemyCostOverride;
        item->data.flags = static_cast<RE::AlchemyItem::AlchemyFlag>(form.alchemyFlags);
        item->data.addictionItem = ResolveAs<RE::SpellItem>(form.addictionItem);
        item->data.addictionChance = form.addictionChance;
        item->data.consumptionSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.consumptionSound);
        ApplyInventoryIcons(static_cast<RE::TESIcon&>(*item), static_cast<RE::BGSMessageIcon&>(*item), form);
        ApplyPickupPutdownSounds(static_cast<RE::BGSPickupPutdownSounds&>(*item), form);
        ApplyKeywords(static_cast<RE::BGSKeywordForm&>(*item), form.keywords);
        ApplyMagicEffects(static_cast<RE::MagicItem&>(*item), form);
        return true;
    }

    bool ConfigureIngredient(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* item = tesForm ? tesForm->As<RE::IngredientItem>() : nullptr;
        if (!item) {
            logger::warn("Dynamic form '{}' is not an IngredientItem", form.editorId);
            return false;
        }

        item->SetFormEditorID(form.editorId.c_str());
        item->fullName = form.fullName.empty() ? form.editorId.c_str() : form.fullName.c_str();
        item->SetModel(form.modelPath.c_str());
        item->value = form.itemValue;
        item->weight = form.itemWeight;
        item->SetEquipSlot(ResolveAs<RE::BGSEquipSlot>(form.equipSlot));
        item->data.costOverride = form.ingredientCostOverride;
        item->data.flags = static_cast<RE::IngredientItem::IngredientFlag>(form.ingredientFlags);
        item->gamedata.knownEffectFlags = form.knownEffectFlags;
        item->gamedata.playerUses = form.playerUses;
        SetIconIfPresent(static_cast<RE::TESIcon&>(*item), form.inventoryIcon);
        ApplyPickupPutdownSounds(static_cast<RE::BGSPickupPutdownSounds&>(*item), form);
        ApplyKeywords(static_cast<RE::BGSKeywordForm&>(*item), form.keywords);
        ApplyMagicEffects(static_cast<RE::MagicItem&>(*item), form);
        return true;
    }

    bool ConfigureArmorType(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* armorType = tesForm ? tesForm->As<RE::TESObjectARMA>() : nullptr;
        if (!armorType) {
            logger::warn("Dynamic form '{}' is not a TESObjectARMA", form.editorId);
            return false;
        }

        armorType->SetFormEditorID(form.editorId.c_str());
        armorType->race = ResolveAs<RE::TESRace>(form.race);
        ConfigureBipedObject(*armorType, form);
        SetModelIfPresent(armorType->bipedModels[RE::SEX::kMale], form.maleWorldModel);
        SetModelIfPresent(armorType->bipedModels[RE::SEX::kFemale], form.femaleWorldModel);
        SetModelIfPresent(armorType->bipedModel1stPersons[RE::SEX::kMale], form.maleFirstPersonModel);
        SetModelIfPresent(armorType->bipedModel1stPersons[RE::SEX::kFemale], form.femaleFirstPersonModel);
        armorType->skinTextures[RE::SEX::kMale] = ResolveAs<RE::BGSTextureSet>(form.maleSkinTexture);
        armorType->skinTextures[RE::SEX::kFemale] = ResolveAs<RE::BGSTextureSet>(form.femaleSkinTexture);
        armorType->skinTextureSwapLists[RE::SEX::kMale] = ResolveAs<RE::BGSListForm>(form.maleSkinTextureSwapList);
        armorType->skinTextureSwapLists[RE::SEX::kFemale] = ResolveAs<RE::BGSListForm>(form.femaleSkinTextureSwapList);
        armorType->additionalRaces.clear();
        for (const auto& raceRef : form.additionalRaces) {
            if (auto* race = ResolveAs<RE::TESRace>(raceRef)) {
                armorType->additionalRaces.push_back(race);
            }
        }
        armorType->footstepSet = ResolveAs<RE::BGSFootstepSet>(form.footstepSet);
        armorType->artObject = ResolveAs<RE::BGSArtObject>(form.armorArtObject);
        logger::info("Configured armor type '{}' with {} additional races.", form.editorId, armorType->additionalRaces.size());
        return true;
    }

    bool ConfigureArmor(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* armor = tesForm ? tesForm->As<RE::TESObjectARMO>() : nullptr;
        if (!armor) {
            logger::warn("Dynamic form '{}' is not a TESObjectARMO", form.editorId);
            return false;
        }

        armor->SetFormEditorID(form.editorId.c_str());
        armor->fullName = form.fullName.empty() ? form.editorId.c_str() : form.fullName.c_str();
        armor->race = ResolveAs<RE::TESRace>(form.race);
        armor->value = form.armorValue;
        armor->weight = form.armorWeight;
        armor->formEnchanting = ResolveAs<RE::EnchantmentItem>(form.enchantment);
        armor->amountofEnchantment = form.enchantmentAmount;
        armor->SetEquipSlot(ResolveAs<RE::BGSEquipSlot>(form.equipSlot));
        const auto armorRating = std::clamp(
            static_cast<double>(form.armorRating) * 100.0,
            0.0,
            static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
        armor->armorRating = static_cast<std::uint32_t>(armorRating);
        armor->templateArmor = ResolveAs<RE::TESObjectARMO>(form.templateArmor);
        armor->pickupSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.pickupSound);
        armor->putdownSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.putdownSound);
        armor->blockBashImpactDataSet = ResolveAs<RE::BGSImpactDataSet>(form.blockBashImpactDataSet);
        armor->altBlockMaterialType = ResolveAs<RE::BGSMaterialType>(form.altBlockMaterialType);
        ConfigureBipedObject(*armor, form);
        SetModelIfPresent(armor->worldModels[RE::TESBipedModelForm::Sexes::kMale], form.maleWorldModel);
        SetModelIfPresent(armor->worldModels[RE::TESBipedModelForm::Sexes::kFemale], form.femaleWorldModel);
        SetIconIfPresent(armor->inventoryIcons[RE::TESBipedModelForm::Sexes::kMale], form.maleInventoryIcon);
        SetIconIfPresent(armor->inventoryIcons[RE::TESBipedModelForm::Sexes::kFemale], form.femaleInventoryIcon);
        SetIconIfPresent(armor->messageIcons[RE::TESBipedModelForm::Sexes::kMale].icon, form.maleMessageIcon);
        SetIconIfPresent(armor->messageIcons[RE::TESBipedModelForm::Sexes::kFemale].icon, form.femaleMessageIcon);

        armor->armorAddons.clear();
        for (const auto& addonRef : form.armorAddons) {
            if (auto* addon = ResolveAs<RE::TESObjectARMA>(addonRef)) {
                armor->armorAddons.push_back(addon);
            }
        }

        std::vector<RE::BGSKeyword*> keywords;
        for (const auto& keywordRef : form.keywords) {
            if (auto* keyword = ResolveAs<RE::BGSKeyword>(keywordRef)) {
                keywords.push_back(keyword);
            }
        }
        while (armor->GetNumKeywords() > 0) {
            armor->RemoveKeyword(static_cast<std::uint32_t>(0));
        }
        armor->AddKeywords(keywords);
        logger::info("Configured armor '{}' with {} armor add-ons and {} keywords.", form.editorId, armor->armorAddons.size(), keywords.size());
        return true;
    }

    bool ConfigureColor(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* color = tesForm ? tesForm->As<RE::BGSColorForm>() : nullptr;
        if (!color) {
            logger::warn("Dynamic form '{}' is not a BGSColorForm", form.editorId);
            return false;
        }

        color->SetFormEditorID(form.editorId.c_str());
        color->fullName = form.fullName.empty() ? form.editorId.c_str() : form.fullName.c_str();
        color->color = RE::Color(form.red, form.green, form.blue, form.alpha);
        color->flags = form.playable ? RE::BGSColorForm::Flag::kPlayable : RE::BGSColorForm::Flag::kNone;
        return true;
    }

    bool ConfigureArtObject(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* artObject = tesForm ? tesForm->As<RE::BGSArtObject>() : nullptr;
        if (!artObject) {
            logger::warn("Dynamic form '{}' is not a BGSArtObject", form.editorId);
            return false;
        }

        artObject->SetFormEditorID(form.editorId.c_str());
        artObject->SetModel(form.modelPath.c_str());
        artObject->data.artType = ToTESArtType(form.artType);
        artObject->boundData.boundMin.x = form.boundX1;
        artObject->boundData.boundMin.y = form.boundY1;
        artObject->boundData.boundMin.z = form.boundZ1;
        artObject->boundData.boundMax.x = form.boundX2;
        artObject->boundData.boundMax.y = form.boundY2;
        artObject->boundData.boundMax.z = form.boundZ2;
        return true;
    }

    std::uint32_t FunctionIdForCondition(const DynamicForms::PerkCondition& condition) {
        if (condition.functionId != 0 || condition.kind == DynamicForms::PerkConditionKind::Raw) {
            return condition.functionId;
        }

        using FunctionID = RE::FUNCTION_DATA::FunctionID;
        switch (condition.kind) {
        case DynamicForms::PerkConditionKind::GetGlobalValue:
            return FunctionIdByCatalogName("GetGlobalValue", static_cast<std::uint32_t>(FunctionID::kGetGlobalValue));
        case DynamicForms::PerkConditionKind::GetActorValue:
            return FunctionIdByCatalogName("GetActorValue", static_cast<std::uint32_t>(FunctionID::kGetActorValue));
        case DynamicForms::PerkConditionKind::GetBaseActorValue:
            return FunctionIdByCatalogName("GetBaseActorValue", static_cast<std::uint32_t>(FunctionID::kGetBaseActorValue));
        case DynamicForms::PerkConditionKind::HasPerk:
            return FunctionIdByCatalogName("HasPerk", static_cast<std::uint32_t>(FunctionID::kHasPerk));
        case DynamicForms::PerkConditionKind::GetQuestCompleted:
            return FunctionIdByCatalogName("GetQuestCompleted", static_cast<std::uint32_t>(FunctionID::kGetQuestCompleted));
        case DynamicForms::PerkConditionKind::HasSpell:
            return FunctionIdByCatalogName("HasSpell", static_cast<std::uint32_t>(FunctionID::kHasSpell));
        case DynamicForms::PerkConditionKind::Raw:
        default:
            return condition.functionId;
        }
    }

    void ClearCondition(RE::TESCondition& condition) {
        auto* current = condition.head;
        while (current) {
            auto* next = current->next;
            delete current;
            current = next;
        }
        condition.head = nullptr;
    }

    std::uintptr_t ParseIntegerParam(const std::string& value) {
        return value.empty() ? 0 : static_cast<std::uintptr_t>(std::stoll(value, nullptr, 0));
    }

    std::uintptr_t ParseFloatParam(const std::string& value) {
        const auto floatValue = value.empty() ? 0.0F : std::stof(value);
        std::uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(floatValue));
        std::memcpy(&bits, &floatValue, sizeof(bits));
        return bits;
    }

    void* StoreConditionStringParam(const std::string& value) {
        static std::vector<std::unique_ptr<std::string>> storedStrings;
        auto stored = std::make_unique<std::string>(value);
        auto* result = stored->data();
        storedStrings.push_back(std::move(stored));
        return result;
    }

    void SetConditionParam(RE::TESConditionItem* item, const DynamicForms::PerkCondition& condition, const std::size_t index, const std::string& value) {
        if (!item || index >= 2 || value.empty()) {
            return;
        }

        try {
            const auto functionId = FunctionIdForCondition(condition);
            const auto* functionInfo = ConditionCatalog::FindFunction(functionId);
            const std::string_view rawType = functionInfo ? (index == 0 ? functionInfo->rawParam1 : functionInfo->rawParam2) : "";

            if (ConditionCatalog::IsIntegerParam(rawType)) {
                item->data.functionData.params[index] = reinterpret_cast<void*>(ParseIntegerParam(value));
                return;
            }

            if (ConditionCatalog::IsFloatParam(rawType)) {
                item->data.functionData.params[index] = reinterpret_cast<void*>(ParseFloatParam(value));
                return;
            }

            if (ConditionCatalog::IsStringParam(rawType)) {
                item->data.functionData.params[index] = StoreConditionStringParam(value);
                return;
            }

            if (ConditionCatalog::IsFormParam(rawType)) {
                item->data.functionData.params[index] = ResolveConfigForm(value);
                return;
            }

            if (auto* form = ResolveConfigForm(value)) {
                item->data.functionData.params[index] = form;
                return;
            }

            item->data.functionData.params[index] = reinterpret_cast<void*>(ParseIntegerParam(value));
        } catch (...) {
            logger::warn("Invalid condition param '{}' for function {} param {}", value, FunctionIdForCondition(condition), index + 1);
        }
    }

    RE::TESConditionItem* CreateConditionItem(const DynamicForms::PerkCondition& condition) {
        auto* item = new RE::TESConditionItem();
        const auto functionId = FunctionIdForCondition(condition);
        item->data.functionData.function = static_cast<RE::FUNCTION_DATA::FunctionID>(functionId);
        item->data.flags.isOR = condition.isOr;
        item->data.flags.usesAliases = condition.useAliases;
        item->data.flags.opCode = static_cast<RE::CONDITION_ITEM_DATA::OpCode>(std::min(condition.opCode, 5U));
        item->data.flags.global = condition.useGlobalComparison;
        item->data.flags.usePackData = condition.usePackData;
        item->data.flags.swapTarget = condition.swapTarget;
        item->data.object = static_cast<RE::CONDITIONITEMOBJECT>(std::min(condition.runOn, 8U));
        item->data.dataID = condition.dataId;
        item->data.comparisonValue.f = condition.comparisonValue;

        if (!condition.runOnRef.empty()) {
            if (auto* runOnForm = ResolveConfigForm(condition.runOnRef); runOnForm && runOnForm->Is(RE::FormType::Reference)) {
                item->data.runOnRef = runOnForm->As<RE::TESObjectREFR>()->CreateRefHandle();
            }
        }

        if (condition.useGlobalComparison && !condition.comparisonGlobal.empty()) {
            if (auto* global = ResolveConfigForm(condition.comparisonGlobal); global && global->Is(RE::FormType::Global)) {
                item->data.comparisonValue.g = global->As<RE::TESGlobal>();
            } else {
                item->data.flags.global = false;
                item->data.comparisonValue.f = condition.comparisonValue;
            }
        }

        SetConditionParam(item, condition, 0, condition.param1);
        SetConditionParam(item, condition, 1, condition.param2);
        return item;
    }

    void ApplyConditions(RE::TESCondition& target, const std::vector<DynamicForms::PerkCondition>& conditions) {
        ClearCondition(target);

        RE::TESConditionItem* tail = nullptr;
        for (const auto& condition : conditions) {
            auto* item = CreateConditionItem(condition);
            if (!target.head) {
                target.head = item;
            } else {
                tail->next = item;
            }
            tail = item;

            logger::debug("[PerkCondition] kind={} function={} op={} cmp={} or={} p1='{}' p2='{}'",
                ToString(condition.kind),
                FunctionIdForCondition(condition),
                condition.opCode,
                condition.comparisonValue,
                condition.isOr,
                condition.param1,
                condition.param2);
        }
    }

    template <class T>
    void SetRuntimeVTable(T* object, const REL::VariantID& vtable) {
        if (!object) {
            return;
        }

        REL::Relocation<std::uintptr_t> runtimeVTable{ vtable };
        *reinterpret_cast<std::uintptr_t*>(object) = runtimeVTable.address();
    }

    RE::BGSEntryPointPerkEntry* CreateEntryPointPerkEntryObject() {
        auto* entry = RE::calloc<RE::BGSEntryPointPerkEntry>(1);
        SetRuntimeVTable(entry, RE::VTABLE_BGSEntryPointPerkEntry[0]);
        return entry;
    }

    RE::BGSEntryPointFunctionDataOneValue* CreateOneValueFunctionDataObject() {
        auto* functionData = RE::calloc<RE::BGSEntryPointFunctionDataOneValue>(1);
        SetRuntimeVTable(functionData, RE::VTABLE_BGSEntryPointFunctionDataOneValue[0]);
        return functionData;
    }

    bool PerkEntryFunctionUsesOneValue(const std::uint32_t function) {
        using Function = RE::BGSEntryPointFunction::ENTRY_POINT_FUNCTION;
        switch (static_cast<Function>(function)) {
        case Function::kSetValue:
        case Function::kAddValue:
        case Function::kMultiplyValue:
        case Function::kAbsoluteValue:
        case Function::kNegativeAbsoluteValue:
        case Function::kAddActorValueMult:
        case Function::kSetToActorValueMult:
        case Function::kMultiplyActorValueMult:
        case Function::kMultiplyOnePlusActorValueMult:
            return true;
        default:
            return false;
        }
    }

    const char* PerkEntryFunctionName(const std::uint32_t function) {
        using Function = RE::BGSEntryPointFunction::ENTRY_POINT_FUNCTION;
        switch (static_cast<Function>(function)) {
        case Function::kNullFunction:
            return "Null Function";
        case Function::kSetValue:
            return "Set Value";
        case Function::kAddValue:
            return "Add Value";
        case Function::kMultiplyValue:
            return "Multiply Value";
        case Function::kAddRangeToValue:
            return "Add Range To Value";
        case Function::kAddActorValueMult:
            return "Add Actor Value Mult";
        case Function::kAbsoluteValue:
            return "Absolute Value";
        case Function::kNegativeAbsoluteValue:
            return "Negative Absolute Value";
        case Function::kAddLeveledList:
            return "Add Leveled List";
        case Function::kAddActivateChoice:
            return "Add Activate Choice";
        case Function::kSelectSpell:
            return "Select Spell";
        case Function::kSelectText:
            return "Select Text";
        case Function::kSetToActorValueMult:
            return "Set To Actor Value Mult";
        case Function::kMultiplyActorValueMult:
            return "Multiply Actor Value Mult";
        case Function::kMultiplyOnePlusActorValueMult:
            return "Multiply 1 + Actor Value Mult";
        case Function::kSetText:
            return "Set Text";
        default:
            return "Unknown";
        }
    }

    RE::BGSStandardSoundDef* CreateStandardSoundDefObject() {
        auto* soundDef = RE::calloc<RE::BGSStandardSoundDef>(1);
        SetRuntimeVTable(soundDef, RE::VTABLE_BGSStandardSoundDef[0]);
        SetRuntimeVTable(&soundDef->soundCharacteristics, RE::VTABLE_BGSStandardSoundDef__SoundPlaybackCharacteristics[0]);
        return soundDef;
    }

    void ClearPerkEntries(RE::BGSPerk& perk) {
        for (auto* entry : perk.perkEntries) {
            if (!entry) {
                continue;
            }
            auto* entryPoint = static_cast<RE::BGSEntryPointPerkEntry*>(entry);
            for (auto& condition : entryPoint->conditions) {
                ClearCondition(condition);
            }
            entryPoint->conditions.clear();
            RE::free(entryPoint->functionData);
            entryPoint->functionData = nullptr;
            RE::free(entry);
        }
        perk.perkEntries.clear();
    }

    RE::BGSPerkEntry* CreatePerkEntry(RE::BGSPerk& perk, const DynamicForms::PerkEntry& source) {
        auto* entry = CreateEntryPointPerkEntryObject();
        if (!entry) {
            logger::warn("Could not allocate BGSEntryPointPerkEntry for perk '{}'.", perk.GetFormEditorID());
            return nullptr;
        }

        entry->header.rank = static_cast<std::uint8_t>(std::min(source.rank, 255U));
        entry->header.priority = static_cast<std::uint8_t>(std::min(source.priority, 255U));
        entry->entryData.entryPoint = static_cast<RE::BGSPerkEntry::EntryPoint>(std::min(source.entryPoint, 91U));
        entry->entryData.function = static_cast<RE::BGSEntryPointPerkEntry::Function>(std::min(std::max(source.function, 1U), 15U));
        entry->entryData.numArgs = static_cast<std::uint8_t>(std::min(source.numArgs, 255U));
        entry->perk = &perk;

        if (PerkEntryFunctionUsesOneValue(source.function)) {
            if (auto* functionData = CreateOneValueFunctionDataObject()) {
                functionData->data = source.value;
                entry->functionData = functionData;
            } else {
                logger::warn("Could not allocate BGSEntryPointFunctionDataOneValue for perk '{}'.", perk.GetFormEditorID());
            }
        } else if (source.function == static_cast<std::uint32_t>(RE::BGSEntryPointFunction::ENTRY_POINT_FUNCTION::kNullFunction)) {
            entry->functionData = nullptr;
        } else {
            logger::warn(
                "Perk entry function {} ({}) needs function data not represented by the current DFG JSON schema. Creating entry without function data.",
                source.function,
                PerkEntryFunctionName(source.function));
        }

        entry->conditions.resize(1);
        ApplyConditions(entry->conditions[0], source.conditions);

        logger::debug("[PerkEntry] entryPoint={} function={} rank={} priority={} value={} conditions={}",
            source.entryPoint,
            source.function,
            source.rank,
            source.priority,
            source.value,
            source.conditions.size());
        return entry;
    }

    bool ConfigurePerk(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* perk = tesForm ? tesForm->As<RE::BGSPerk>() : nullptr;
        if (!perk) {
            logger::warn("Dynamic form '{}' is not a BGSPerk", form.editorId);
            return false;
        }

        perk->SetFormEditorID(form.editorId.c_str());
        perk->fullName = form.fullName.empty() ? form.editorId.c_str() : form.fullName.c_str();
        if (!form.description.empty()) {
            logger::debug("Perk '{}' description is saved in JSON but cannot be assigned directly with this CommonLib TESDescription layout.", form.editorId);
        }
        perk->data.trait = form.trait;
        perk->data.level = form.level;
        perk->data.numRanks = form.numRanks;
        perk->data.playable = form.playable;
        perk->data.hidden = form.hidden;
        perk->nextPerk = nullptr;
        if (!form.nextPerk.empty()) {
            if (auto* next = ResolveConfigForm(form.nextPerk)) {
                perk->nextPerk = next->As<RE::BGSPerk>();
            }
        }

        ApplyConditions(perk->perkConditions, form.conditions);
        ClearPerkEntries(*perk);
        for (const auto& entry : form.entries) {
            if (auto* perkEntry = CreatePerkEntry(*perk, entry)) {
                perk->perkEntries.push_back(perkEntry);
            }
        }

        logger::info("Configured perk '{}' with {} conditions and {} entries.",
            form.editorId,
            form.conditions.size(),
            form.entries.size());
        return true;
    }

    bool ConfigureHeadPart(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* headPart = tesForm ? tesForm->As<RE::BGSHeadPart>() : nullptr;
        if (!headPart) {
            logger::warn("Dynamic form '{}' is not a BGSHeadPart", form.editorId);
            return false;
        }

        headPart->SetFormEditorID(form.editorId.c_str());
        headPart->fullName = form.fullName.empty() ? form.editorId.c_str() : form.fullName.c_str();
        headPart->SetModel(form.modelPath.c_str());
        headPart->type = ToTESHeadPartType(form.headPartType);
        headPart->flags = RE::BGSHeadPart::Flag::kNone;
        if (form.playable) {
            headPart->flags.set(RE::BGSHeadPart::Flag::kPlayable);
        }
        if (form.male) {
            headPart->flags.set(RE::BGSHeadPart::Flag::kMale);
        }
        if (form.female) {
            headPart->flags.set(RE::BGSHeadPart::Flag::kFemale);
        }
        if (form.isExtraPart) {
            headPart->flags.set(RE::BGSHeadPart::Flag::kIsExtraPart);
        }
        if (form.useSolidTint) {
            headPart->flags.set(RE::BGSHeadPart::Flag::kUseSolidTint);
        }

        headPart->morphs[RE::BGSHeadPart::MorphIndices::kRaceMorph].SetModel(form.raceMorphPath.c_str());
        headPart->morphs[RE::BGSHeadPart::MorphIndices::kDefaultMorph].SetModel(form.defaultMorphPath.c_str());
        headPart->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].SetModel(form.chargenMorphPath.c_str());

        headPart->textureSet = nullptr;
        if (!form.textureSet.empty()) {
            if (auto* textureSet = ResolveConfigForm(form.textureSet)) {
                headPart->textureSet = textureSet->As<RE::BGSTextureSet>();
            }
        }

        headPart->color = nullptr;
        if (!form.colorForm.empty()) {
            if (auto* color = ResolveConfigForm(form.colorForm)) {
                headPart->color = color->As<RE::BGSColorForm>();
            }
        }

        headPart->validRaces = nullptr;
        if (!form.validRaces.empty()) {
            if (auto* validRaces = ResolveConfigForm(form.validRaces)) {
                headPart->validRaces = validRaces->As<RE::BGSListForm>();
            }
        }

        headPart->extraParts.clear();
        for (const auto& extraPartId : form.extraParts) {
            auto* extraPartForm = ResolveConfigForm(extraPartId);
            auto* extraPart = extraPartForm ? extraPartForm->As<RE::BGSHeadPart>() : nullptr;
            if (!extraPart) {
                logger::warn("HeadPart '{}' extra part '{}' could not be resolved as BGSHeadPart.", form.editorId, extraPartId.Display());
                continue;
            }
            headPart->extraParts.push_back(extraPart);
        }

        logger::info("Configured headpart '{}' type {} with {} extra parts.",
            form.editorId,
            ToString(form.headPartType),
            headPart->extraParts.size());
        return true;
    }

    bool ConfigureSoundDescriptor(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* soundForm = tesForm ? tesForm->As<RE::BGSSoundDescriptorForm>() : nullptr;
        if (!soundForm) {
            logger::warn("Dynamic form '{}' is not a BGSSoundDescriptorForm", form.editorId);
            return false;
        }

        soundForm->SetFormEditorID(form.editorId.c_str());
        auto* soundDef = soundForm->soundDescriptor ? static_cast<RE::BGSStandardSoundDef*>(soundForm->soundDescriptor) : CreateStandardSoundDefObject();
        if (!soundDef) {
            logger::warn("Could not allocate BGSStandardSoundDef for '{}'", form.editorId);
            return false;
        }
        soundForm->soundDescriptor = soundDef;

        soundDef->category = nullptr;
        if (!form.category.empty()) {
            if (auto* category = ResolveConfigForm(form.category)) {
                soundDef->category = category->As<RE::BGSSoundCategory>();
            }
        }
        soundDef->alternateSoundFormID = 0;
        if (!form.alternateSound.empty()) {
            if (auto* alternate = ResolveConfigForm(form.alternateSound)) {
                soundDef->alternateSoundFormID = alternate->GetFormID();
            }
        }
        soundDef->outputModel = nullptr;
        if (!form.outputModel.empty()) {
            if (auto* output = ResolveConfigForm(form.outputModel)) {
                soundDef->outputModel = output->As<RE::BGSSoundOutput>();
            }
        }

        soundDef->soundFiles.clear();
        for (const auto& file : form.soundFiles) {
            if (file.empty()) {
                continue;
            }
            RE::BSResource::ID fileId;
            fileId.GenerateFromPath(file.c_str());
            soundDef->soundFiles.push_back(fileId);
        }

        soundDef->soundCharacteristics.frequencyShift = form.frequencyShift;
        soundDef->soundCharacteristics.frequencyVariance = form.frequencyVariance;
        soundDef->soundCharacteristics.priority = form.priority;
        soundDef->soundCharacteristics.dbVariance = form.dbVariance;
        soundDef->soundCharacteristics.staticAttenuation = static_cast<std::uint16_t>(std::clamp(form.staticAttenuation * 100.0F, 0.0F, 65535.0F));
        soundDef->lengthCharacteristics.looping = static_cast<RE::BGSStandardSoundDef::LengthCharacteristics::Looping>(form.looping);
        soundDef->lengthCharacteristics.rumbleSendValue = form.rumbleSendValue;

        if (!form.conditions.empty()) {
            if (!soundDef->conditions) {
                soundDef->conditions = new RE::TESCondition();
            }
            ApplyConditions(*soundDef->conditions, form.conditions);
        } else if (soundDef->conditions) {
            ClearCondition(*soundDef->conditions);
        }

        logger::info("Configured sound descriptor '{}' with {} sound files.", form.editorId, soundDef->soundFiles.size());
        return true;
    }

    bool ConfigureLight(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* light = tesForm ? tesForm->As<RE::TESObjectLIGH>() : nullptr;
        if (!light) {
            logger::warn("Dynamic form '{}' is not a TESObjectLIGH", form.editorId);
            return false;
        }

        light->SetFormEditorID(form.editorId.c_str());
        light->fullName = form.fullName.empty() ? form.editorId.c_str() : form.fullName.c_str();
        light->SetModel(form.modelPath.c_str());
        light->data.time = form.lightTime;
        light->data.radius = form.lightRadius;
        light->data.color = RE::Color(form.red, form.green, form.blue, form.alpha);
        light->data.flags = static_cast<RE::TES_LIGHT_FLAGS>(form.flags);
        light->data.fallofExponent = form.falloffExponent;
        light->data.fov = form.fov;
        light->data.nearDistance = form.nearClip;
        light->data.flickerPeriodRecip = form.flickerPeriod;
        light->data.flickerIntensityAmplitude = form.flickerIntensityAmplitude;
        light->data.flickerMovementAmplitude = form.flickerMovementAmplitude;
        light->fade = form.fade;
        light->sound = nullptr;
        if (!form.sound.empty()) {
            if (auto* sound = ResolveConfigForm(form.sound)) {
                light->sound = sound->As<RE::BGSSoundDescriptorForm>();
            }
        }
        light->lensFlare = nullptr;
        if (!form.lensFlare.empty()) {
            if (auto* lensFlare = ResolveConfigForm(form.lensFlare)) {
                light->lensFlare = lensFlare->As<RE::BGSLensFlare>();
            }
        }
        return true;
    }

    bool ConfigureExplosion(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* explosion = tesForm ? tesForm->As<RE::BGSExplosion>() : nullptr;
        if (!explosion) {
            logger::warn("Dynamic form '{}' is not a BGSExplosion", form.editorId);
            return false;
        }

        explosion->SetFormEditorID(form.editorId.c_str());
        explosion->fullName = form.fullName.empty() ? form.editorId.c_str() : form.fullName.c_str();
        explosion->SetModel(form.modelPath.c_str());
        explosion->formEnchanting = nullptr;
        if (!form.objectEffect.empty()) {
            if (auto* objectEffect = ResolveConfigForm(form.objectEffect)) {
                explosion->formEnchanting = objectEffect->As<RE::EnchantmentItem>();
            }
        }
        explosion->imageSpaceModifying = nullptr;
        if (!form.imageSpaceModifier.empty()) {
            if (auto* imageSpaceModifier = ResolveConfigForm(form.imageSpaceModifier)) {
                explosion->imageSpaceModifying = imageSpaceModifier->As<RE::TESImageSpaceModifier>();
            }
        }
        explosion->data.light = nullptr;
        if (!form.light.empty()) {
            if (auto* light = ResolveConfigForm(form.light)) {
                explosion->data.light = light->As<RE::TESObjectLIGH>();
            }
        }
        explosion->data.sound1 = nullptr;
        if (!form.sound1.empty()) {
            if (auto* sound = ResolveConfigForm(form.sound1)) {
                explosion->data.sound1 = sound->As<RE::BGSSoundDescriptorForm>();
            }
        }
        explosion->data.sound2 = nullptr;
        if (!form.sound2.empty()) {
            if (auto* sound = ResolveConfigForm(form.sound2)) {
                explosion->data.sound2 = sound->As<RE::BGSSoundDescriptorForm>();
            }
        }
        explosion->data.impactDataSet = nullptr;
        if (!form.impactDataSet.empty()) {
            if (auto* impact = ResolveConfigForm(form.impactDataSet)) {
                explosion->data.impactDataSet = impact->As<RE::BGSImpactDataSet>();
            }
        }
        explosion->data.impactPlacedObject = nullptr;
        if (!form.placedObject.empty()) {
            if (auto* placedObject = ResolveConfigForm(form.placedObject)) {
                explosion->data.impactPlacedObject = placedObject->As<RE::TESObjectREFR>();
            }
        }
        explosion->data.spawnProjectile = nullptr;
        if (!form.spawnProjectile.empty()) {
            if (auto* projectile = ResolveConfigForm(form.spawnProjectile)) {
                explosion->data.spawnProjectile = projectile->As<RE::BGSProjectile>();
            }
        }
        explosion->data.force = form.force;
        explosion->data.damage = form.damage;
        explosion->data.radius = form.radius;
        explosion->data.imageSpaceRadius = form.imageSpaceRadius;
        explosion->data.verticalOffsetMult = form.verticalOffsetMult;
        explosion->data.flags = static_cast<RE::BGSExplosionData::Flag>(form.flags);
        explosion->data.eSoundLevel = static_cast<RE::SOUND_LEVEL>(form.soundLevel);
        return true;
    }

    bool ConfigureActivator(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* activator = tesForm ? tesForm->As<RE::TESObjectACTI>() : nullptr;
        if (!activator) {
            logger::warn("Dynamic form '{}' is not a TESObjectACTI", form.editorId);
            return false;
        }

        activator->SetFormEditorID(form.editorId.c_str());
        activator->fullName = form.fullName.empty() ? form.editorId.c_str() : form.fullName.c_str();
        activator->SetModel(form.modelPath.c_str());
        activator->soundLoop = nullptr;
        if (!form.soundLoop.empty()) {
            if (auto* sound = ResolveConfigForm(form.soundLoop)) {
                activator->soundLoop = sound->As<RE::BGSSoundDescriptorForm>();
            }
        }
        activator->soundActivate = nullptr;
        if (!form.soundActivate.empty()) {
            if (auto* sound = ResolveConfigForm(form.soundActivate)) {
                activator->soundActivate = sound->As<RE::BGSSoundDescriptorForm>();
            }
        }
        activator->waterForm = nullptr;
        if (!form.waterType.empty()) {
            if (auto* water = ResolveConfigForm(form.waterType)) {
                activator->waterForm = water->As<RE::TESWaterForm>();
            }
        }
        activator->flags = static_cast<RE::TESObjectACTI::ActiFlags>(form.flags);
        return true;
    }

    bool ConfigureEffectShader(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* shader = tesForm ? tesForm->As<RE::TESEffectShader>() : nullptr;
        if (!shader) {
            logger::warn("Dynamic form '{}' is not a TESEffectShader", form.editorId);
            return false;
        }

        shader->SetFormEditorID(form.editorId.c_str());
        shader->fillTexture.textureName = form.fillTexturePath.c_str();
        shader->particleShaderTexture.textureName = form.particleShaderTexturePath.c_str();
        shader->holesTexture.textureName = form.holesTexturePath.c_str();
        shader->membranePaletteTexture.textureName = form.membranePaletteTexturePath.c_str();
        shader->particlePaletteTexture.textureName = form.particlePaletteTexturePath.c_str();

        auto& data = shader->data;
        data.flags = static_cast<RE::EffectShaderData::Flags>(form.flags);
        data.fillTextureEffectColorKey1 = RE::Color(form.fillColor1Red, form.fillColor1Green, form.fillColor1Blue, form.fillColor1Alpha);
        data.fillTextureEffectColorKey2 = RE::Color(form.fillColor2Red, form.fillColor2Green, form.fillColor2Blue, form.fillColor2Alpha);
        data.fillTextureEffectColorKey3 = RE::Color(form.fillColor3Red, form.fillColor3Green, form.fillColor3Blue, form.fillColor3Alpha);
        data.edgeEffectColor = RE::Color(form.edgeEffectRed, form.edgeEffectGreen, form.edgeEffectBlue, form.edgeEffectAlpha);
        data.edgeColor = RE::Color(form.edgeColorRed, form.edgeColorGreen, form.edgeColorBlue, form.edgeColorAlpha);
        data.colorKey1 = RE::Color(form.particleColor1Red, form.particleColor1Green, form.particleColor1Blue, form.particleColor1Alpha);
        data.colorKey2 = RE::Color(form.particleColor2Red, form.particleColor2Green, form.particleColor2Blue, form.particleColor2Alpha);
        data.colorKey3 = RE::Color(form.particleColor3Red, form.particleColor3Green, form.particleColor3Blue, form.particleColor3Alpha);
        data.fillTextureEffectAlphaFadeInTime = form.fillAlphaFadeIn;
        data.fillTextureEffectFullAlphaTime = form.fillFullAlphaTime;
        data.fillTextureEffectAlphaFadeOutTime = form.fillAlphaFadeOut;
        data.fillTextureEffectPersistentAlphaRatio = form.fillPersistentAlphaRatio;
        data.fillTextureEffectAlphaPulseAmplitude = form.fillAlphaPulseAmplitude;
        data.fillTextureEffectAlphaPulseFrequency = form.fillAlphaPulseFrequency;
        data.fillTextureEffectTextureAnimationSpeedU = form.fillTextureAnimationSpeedU;
        data.fillTextureEffectTextureAnimationSpeedV = form.fillTextureAnimationSpeedV;
        data.fillTextureEffectTextureScaleU = form.fillTextureScaleU;
        data.fillTextureEffectTextureScaleV = form.fillTextureScaleV;
        data.fillTextureEffectFullAlphaRatio = form.fillFullAlphaRatio;
        data.edgeEffectFallOff = form.edgeFalloff;
        data.edgeEffectAlphaFadeInTime = form.edgeAlphaFadeIn;
        data.edgeEffectFullAlphaTime = form.edgeFullAlphaTime;
        data.edgeEffectAlphaFadeOutTime = form.edgeAlphaFadeOut;
        data.edgeEffectPersistentAlphaRatio = form.edgePersistentAlphaRatio;
        data.edgeEffectAlphaPulseAmplitude = form.edgeAlphaPulseAmplitude;
        data.edgeEffectAlphaPulseFrequency = form.edgeAlphaPulseFrequency;
        data.edgeEffectFullAlphaRatio = form.edgeFullAlphaRatio;
        data.edgeWidthAlphaUnits = form.edgeWidthAlphaUnits;
        data.particleShaderParticleBirthRampUpTime = form.particleBirthRampUpTime;
        data.particleShaderFullParticleBirthTime = form.particleFullBirthTime;
        data.particleShaderParticleBirthRampDownTime = form.particleBirthRampDownTime;
        data.particleShaderFullParticleBirthRatio = form.particleFullBirthRatio;
        data.particleShaderPersistantParticleCount = form.particleCount;
        data.particleShaderParticleLifetime = form.particleLifetime;
        data.particleShaderParticleLifetimeVariance = form.particleLifetimeVariance;
        data.particleShaderInitialSpeedAlongNormal = form.particleInitialSpeedAlongNormal;
        data.particleShaderAccelerationAlongNormal = form.particleAccelerationAlongNormal;
        data.particleShaderScaleKey1 = form.particleScaleKey1;
        data.particleShaderScaleKey2 = form.particleScaleKey2;
        data.particleShaderScaleKey1Time = form.particleScaleKey1Time;
        data.particleShaderScaleKey2Time = form.particleScaleKey2Time;
        data.colorKey1ColorAlpha = form.particleColor1AlphaValue;
        data.colorKey2ColorAlpha = form.particleColor2AlphaValue;
        data.colorKey3ColorAlpha = form.particleColor3AlphaValue;
        data.colorKey1ColorKeyTime = form.particleColor1Time;
        data.colorKey2ColorKeyTime = form.particleColor2Time;
        data.colorKey3ColorKeyTime = form.particleColor3Time;
        data.ambientSound = nullptr;
        if (!form.ambientSound.empty()) {
            if (auto* sound = ResolveConfigForm(form.ambientSound)) {
                data.ambientSound = sound->As<RE::BGSSoundDescriptorForm>();
            }
        }
        logger::info("Configured effect shader '{}' flags {:08X}.", form.editorId, form.flags);
        return true;
    }

    void SetActorBaseFlag(RE::TESNPC& npc, const RE::ACTOR_BASE_DATA::Flag flag, const bool enabled) {
        if (enabled) {
            npc.actorData.actorBaseFlags.set(flag);
        } else {
            npc.actorData.actorBaseFlags.reset(flag);
        }
    }

    template <class T>
    T* ResolveAs(const DynamicForms::FormRef& ref) {
        auto* form = ResolveConfigForm(ref);
        return form ? form->As<T>() : nullptr;
    }

    std::string EditorIdOrFormId(const RE::TESForm* form) {
        if (!form) {
            return "<null>";
        }
        auto editorId = clib_util::editorID::get_editorID(const_cast<RE::TESForm*>(form));
        if (editorId.empty()) {
            return std::format("{:08X}", form->GetFormID());
        }
        return std::string(editorId);
    }

    void LogNPCSnapshot(const char* label, const RE::TESNPC* npc) {
        if (!npc) {
            logger::info("{} NPC snapshot: <null>", label);
            return;
        }

        const auto* spellList = static_cast<const RE::TESSpellList*>(npc);
        const auto* spellData = spellList ? spellList->actorEffects : nullptr;
        logger::info(
            "{} NPC snapshot '{}': ptr={} formID={:08X} formFlags={:08X} actorFlags={:08X} templateFlags={:04X} sex={} "
            "race={} originalRace={} faceNPC={} class={} voice={} skin={} defaultOutfit={} sleepOutfit={} packageList={} crimeFaction={} "
            "aiAggression={} aiConfidence={} aiEnergy={} aiMorality={} aiMood={} aiAssistance={} aiPackages={} "
            "level={} calcMin={} calcMax={} health={} magicka={} stamina={} healthOffset={} magickaOffset={} staminaOffset={} speed={} disposition={} bleedout={} "
            "height={} weight={} soundLevel={} headParts={} headPartsPtr={} faceData={} tintLayersPtr={} tintLayers={} headRelatedData={} relationships={} "
            "factions={} perksPtr={} perkCount={} spellData={} spells={} levSpells={} shouts={}.",
            label,
            EditorIdOrFormId(npc),
            fmt::ptr(npc),
            npc->GetFormID(),
            npc->formFlags,
            npc->actorData.actorBaseFlags.underlying(),
            npc->actorData.templateUseFlags.underlying(),
            static_cast<std::uint32_t>(npc->GetSex()),
            EditorIdOrFormId(npc->race),
            EditorIdOrFormId(npc->originalRace),
            EditorIdOrFormId(npc->faceNPC),
            EditorIdOrFormId(npc->npcClass),
            EditorIdOrFormId(npc->voiceType),
            EditorIdOrFormId(npc->farSkin),
            EditorIdOrFormId(npc->defaultOutfit),
            EditorIdOrFormId(npc->sleepOutfit),
            EditorIdOrFormId(npc->defaultPackList),
            EditorIdOrFormId(npc->crimeFaction),
            static_cast<std::int32_t>(npc->GetAggressionLevel()),
            static_cast<std::int32_t>(npc->GetConfidenceLevel()),
            static_cast<std::uint32_t>(npc->GetEnergyLevel()),
            static_cast<std::int32_t>(npc->GetMoralityLevel()),
            static_cast<std::int32_t>(npc->GetMoodLevel()),
            static_cast<std::int32_t>(npc->GetAssistanceLevel()),
            npc->aiPackages.packages.size(),
            npc->actorData.level,
            npc->actorData.calcLevelMin,
            npc->actorData.calcLevelMax,
            npc->playerSkills.health,
            npc->playerSkills.magicka,
            npc->playerSkills.stamina,
            npc->actorData.healthOffset,
            npc->actorData.magickaOffset,
            npc->actorData.staminaOffset,
            npc->actorData.speedMult,
            npc->actorData.baseDisposition,
            npc->actorData.bleedoutOverride,
            npc->height,
            npc->weight,
            static_cast<std::uint32_t>(npc->soundLevel.underlying()),
            static_cast<std::uint32_t>(npc->numHeadParts),
            fmt::ptr(npc->headParts),
            fmt::ptr(npc->faceData),
            fmt::ptr(npc->tintLayers),
            npc->tintLayers ? npc->tintLayers->size() : 0,
            fmt::ptr(npc->headRelatedData),
            fmt::ptr(npc->relationships),
            npc->factions.size(),
            fmt::ptr(npc->perks),
            npc->perkCount,
            fmt::ptr(spellData),
            spellData ? spellData->numSpells : 0,
            spellData ? spellData->numlevSpells : 0,
            spellData ? spellData->numShouts : 0);

        if (npc->headRelatedData) {
            logger::info("{} NPC head data: hairColor={} faceTexture={}.",
                label,
                EditorIdOrFormId(npc->headRelatedData->hairColor),
                EditorIdOrFormId(npc->headRelatedData->faceDetails));
        }

        if (npc->headParts && npc->numHeadParts > 0) {
            const auto count = std::min<std::uint32_t>(static_cast<std::uint32_t>(npc->numHeadParts), 16);
            for (std::uint32_t i = 0; i < count; ++i) {
                logger::info("{} NPC headPart[{}]={}", label, i, EditorIdOrFormId(npc->headParts[i]));
            }
        }

        if (npc->faceData) {
            logger::info("{} NPC face parts: [{}, {}, {}, {}]",
                label,
                npc->faceData->parts[0],
                npc->faceData->parts[1],
                npc->faceData->parts[2],
                npc->faceData->parts[3]);
            logger::info("{} NPC morphs[0..8]: [{:.3f}, {:.3f}, {:.3f}, {:.3f}, {:.3f}, {:.3f}, {:.3f}, {:.3f}, {:.3f}]",
                label,
                npc->faceData->morphs[0],
                npc->faceData->morphs[1],
                npc->faceData->morphs[2],
                npc->faceData->morphs[3],
                npc->faceData->morphs[4],
                npc->faceData->morphs[5],
                npc->faceData->morphs[6],
                npc->faceData->morphs[7],
                npc->faceData->morphs[8]);
        }
    }

    RE::TESNPC* LookupLydiaNPC() {
        if (auto* form = RE::TESForm::LookupByEditorID("HousecarlWhiterun")) {
            if (auto* npc = form->As<RE::TESNPC>()) {
                return npc;
            }
        }
        return RE::TESForm::LookupByID<RE::TESNPC>(0x000A2C8E);
    }

    RE::TESNPC* LookupDPFNpcTemplate() {
        constexpr RE::FormID TEMPLATE_LOCAL_ID = 0xD63;
        constexpr std::string_view TEMPLATE_PLUGIN = "DPF.esp";

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            logger::warn("Could not resolve DPF NPC template: TESDataHandler is unavailable.");
            return nullptr;
        }

        const auto formId = dataHandler->LookupFormID(TEMPLATE_LOCAL_ID, TEMPLATE_PLUGIN);
        if (formId == 0) {
            logger::warn("Could not resolve DPF NPC template {}|{:X}.", TEMPLATE_PLUGIN, TEMPLATE_LOCAL_ID);
            return nullptr;
        }

        auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(formId);
        if (!npc) {
            logger::warn("DPF NPC template {}|{:X} resolved to {:08X}, but it is not a TESNPC.",
                TEMPLATE_PLUGIN,
                TEMPLATE_LOCAL_ID,
                formId);
            return nullptr;
        }

        return npc;
    }

    RE::TESRace* LookupDefaultNPCRace() {
        constexpr RE::FormID NORD_RACE_LOCAL_ID = 0x13746;
        constexpr std::string_view SKYRIM_PLUGIN = "Skyrim.esm";

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return nullptr;
        }

        const auto formId = dataHandler->LookupFormID(NORD_RACE_LOCAL_ID, SKYRIM_PLUGIN);
        return formId != 0 ? RE::TESForm::LookupByID<RE::TESRace>(formId) : nullptr;
    }

    bool HasMeaningfulFaceMorphs(const DynamicForms::DynamicForm& form) {
        return std::ranges::any_of(form.faceMorphs, [](const float value) {
            return std::abs(value) > 0.0001F;
        });
    }

    bool HasMeaningfulFaceParts(const DynamicForms::DynamicForm& form) {
        return std::ranges::any_of(form.faceParts, [](const std::int32_t value) {
            return value != 0;
        });
    }

    bool HeadPartHasUsableModel(RE::BGSHeadPart* headPart) {
        const auto* model = headPart ? headPart->GetModel() : nullptr;
        return model && model[0] != '\0';
    }

    bool IsHeadPartAllowedForRaceSex(RE::BGSHeadPart* headPart, RE::TESRace* race, const bool female) {
        if (!headPart) {
            return false;
        }

        const bool hpFemale = headPart->flags.all(RE::BGSHeadPart::Flag::kFemale);
        const bool hpMale = headPart->flags.all(RE::BGSHeadPart::Flag::kMale);
        if (female && hpMale && !hpFemale) {
            return false;
        }
        if (!female && hpFemale && !hpMale) {
            return false;
        }

        if (!race || !headPart->validRaces) {
            return true;
        }
        if (headPart->validRaces->HasForm(race)) {
            return true;
        }
        if (race->armorParentRace && headPart->validRaces->HasForm(race->armorParentRace)) {
            return true;
        }

        return headPart->validRaces->forms.empty();
    }

    bool IsSafeFaceHeadPart(RE::BGSHeadPart* headPart, RE::TESRace* race, const bool female) {
        return headPart &&
               headPart->type == RE::BGSHeadPart::HeadPartType::kFace &&
               HeadPartHasUsableModel(headPart) &&
               IsHeadPartAllowedForRaceSex(headPart, race, female);
    }

    RE::BGSHeadPart* FindCurrentSafeFaceHeadPart(RE::TESNPC* npc) {
        if (!npc || !npc->headParts) {
            return nullptr;
        }

        const bool female = npc->actorData.actorBaseFlags.all(RE::ACTOR_BASE_DATA::Flag::kFemale);
        for (int i = 0; i < npc->numHeadParts; ++i) {
            if (auto* headPart = npc->headParts[i]; IsSafeFaceHeadPart(headPart, npc->race, female)) {
                return headPart;
            }
        }
        return nullptr;
    }

    void SetAIDataBits(RE::TESNPC& npc, const DynamicForms::DynamicForm& form) {
        npc.SetAggressionLevel(static_cast<RE::ACTOR_AGGRESSION>(std::clamp(form.aiAggression, 0, 3)));
        npc.SetConfidenceLevel(static_cast<RE::ACTOR_CONFIDENCE>(std::clamp(form.aiConfidence, 0, 4)));
        npc.SetAssistanceLevel(static_cast<RE::ACTOR_ASSISTANCE>(std::clamp(form.aiAssistance, 0, 2)));

        const auto energy = static_cast<std::uint8_t>(std::clamp<int>(form.aiEnergyLevel, 0, 100));
        npc.aiData.energyLevel1 = (energy & (1U << 0)) != 0;
        npc.aiData.energyLevel2 = (energy & (1U << 1)) != 0;
        npc.aiData.energyLevel3 = (energy & (1U << 2)) != 0;
        npc.aiData.energyLevel4 = (energy & (1U << 3)) != 0;
        npc.aiData.energyLevel5 = (energy & (1U << 4)) != 0;
        npc.aiData.energyLevel6 = (energy & (1U << 5)) != 0;
        npc.aiData.energyLevel7 = (energy & (1U << 6)) != 0;
        npc.aiData.energyLevel8 = (energy & (1U << 7)) != 0;

        const auto morality = static_cast<std::uint8_t>(std::clamp(form.aiMorality, 0, 3));
        npc.aiData.morality1 = (morality & (1U << 0)) != 0;
        npc.aiData.morality2 = (morality & (1U << 1)) != 0;

        const auto mood = static_cast<std::uint8_t>(std::clamp(form.aiMood, 0, 7));
        npc.aiData.mood1 = (mood & (1U << 0)) != 0;
        npc.aiData.mood2 = (mood & (1U << 1)) != 0;
        npc.aiData.mood3 = (mood & (1U << 2)) != 0;

        npc.aiData.aggroRadiusBehaviour = form.aiAggroRadiusBehavior;
        npc.aiData.aggroRadius[RE::ACTOR_AGGRO_RADIUS::kWarn] = form.aiAggroRadiusWarn;
        npc.aiData.aggroRadius[RE::ACTOR_AGGRO_RADIUS::kWarnAndAttack] = form.aiAggroRadiusWarnAndAttack;
        npc.aiData.aggroRadius[RE::ACTOR_AGGRO_RADIUS::kAttack] = form.aiAggroRadiusAttack;
        npc.aiData.noSlowApproach = form.aiNoSlowApproach;
    }

    bool ConfigureNPC(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* npc = tesForm ? tesForm->As<RE::TESNPC>() : nullptr;
        if (!npc) {
            logger::warn("Dynamic form '{}' is not a TESNPC", form.editorId);
            return false;
        }

        const bool rawFactoryNpc = !npc->race && !npc->headParts && !npc->tintLayers;
        logger::info("Preparing NPC '{}': FormID={:08X} racePtr={} headPartsPtr={} faceData={} tintLayersPtr={} rawFactoryNpc={}",
            form.editorId,
            npc->GetFormID(),
            fmt::ptr(npc->race),
            fmt::ptr(npc->headParts),
            fmt::ptr(npc->faceData),
            fmt::ptr(npc->tintLayers),
            rawFactoryNpc);

        if (auto* templateNpc = LookupDPFNpcTemplate(); templateNpc && templateNpc != npc) {
            const auto oldFormID = npc->GetFormID();
            npc->Copy(templateNpc);
            if (npc->GetFormID() != oldFormID) {
                logger::warn("NPC '{}' template copy changed dynamic FormID from {:08X} to {:08X}. Restoring original FormID.",
                    form.editorId,
                    oldFormID,
                    npc->GetFormID());
                npc->SetFormID(oldFormID, false);
            }
            npc->faceNPC = nullptr;
            logger::info("Copied DPF NPC template into '{}': templateFormID={:08X} dynamicFormIDBefore={:08X} dynamicFormIDAfter={:08X} templateRace={} dynamicRace={}",
                form.editorId,
                templateNpc->GetFormID(),
                oldFormID,
                npc->GetFormID(),
                templateNpc->race ? clib_util::editorID::get_editorID(templateNpc->race) : "<null>",
                npc->race ? clib_util::editorID::get_editorID(npc->race) : "<null>");
        } else if (rawFactoryNpc) {
            logger::warn("Using fallback InitializeData/InitItemImpl for NPC '{}' because the DPF NPC template was not available.", form.editorId);
            npc->InitializeData();
            npc->InitItemImpl();
        }

        npc->SetFormEditorID(form.editorId.c_str());
        npc->fullName = form.fullName.empty() ? form.editorId.c_str() : form.fullName.c_str();
        npc->height = form.height;
        npc->weight = form.weight;
        npc->bodyTintColor = RE::Color(form.red, form.green, form.blue, form.alpha);

        SetActorBaseFlag(*npc, RE::ACTOR_BASE_DATA::Flag::kFemale, form.femaleNpc);
        SetActorBaseFlag(*npc, RE::ACTOR_BASE_DATA::Flag::kOppositeGenderAnims, form.oppositeGenderAnim);
        SetActorBaseFlag(*npc, RE::ACTOR_BASE_DATA::Flag::kEssential, form.essential);
        SetActorBaseFlag(*npc, RE::ACTOR_BASE_DATA::Flag::kProtected, form.protectedNpc);
        SetActorBaseFlag(*npc, RE::ACTOR_BASE_DATA::Flag::kUnique, form.unique);
        SetActorBaseFlag(*npc, RE::ACTOR_BASE_DATA::Flag::kPCLevelMult, form.calcStats);
        SetActorBaseFlag(*npc, RE::ACTOR_BASE_DATA::Flag::kRespawn, form.respawn);
        SetActorBaseFlag(*npc, RE::ACTOR_BASE_DATA::Flag::kDoesntAffectStealthMeter, form.doesntAffectStealthMeter);
        SetActorBaseFlag(*npc, RE::ACTOR_BASE_DATA::Flag::kDoesntBleed, form.doesntBleed);
        SetActorBaseFlag(*npc, RE::ACTOR_BASE_DATA::Flag::kBleedoutOverride, form.bleedoutOverrideFlag);
        SetActorBaseFlag(*npc, RE::ACTOR_BASE_DATA::Flag::kSimpleActor, form.simpleActor);
        SetActorBaseFlag(*npc, RE::ACTOR_BASE_DATA::Flag::kNoActivation, form.noActivation);
        SetActorBaseFlag(*npc, RE::ACTOR_BASE_DATA::Flag::kIsGhost, form.ghost);
        SetActorBaseFlag(*npc, RE::ACTOR_BASE_DATA::Flag::kInvulnerable, form.invulnerable);
        SetActorBaseFlag(*npc, RE::ACTOR_BASE_DATA::Flag::kIsChargenFacePreset, true);

        npc->playerSkills.health = form.health;
        npc->playerSkills.magicka = form.magicka;
        npc->playerSkills.stamina = form.stamina;
        npc->actorData.healthOffset = form.healthOffset;
        npc->actorData.magickaOffset = form.magickaOffset;
        npc->actorData.staminaOffset = form.staminaOffset;
        npc->actorData.calcLevelMin = form.calcMinLevel;
        npc->actorData.calcLevelMax = form.calcMaxLevel;
        npc->actorData.level = form.npcLevel;
        npc->actorData.speedMult = form.speedMult;
        npc->actorData.baseDisposition = form.dispositionBase;
        npc->actorData.bleedoutOverride = form.bleedoutOverride;
        for (std::size_t i = 0; i < form.skills.size(); ++i) {
            npc->playerSkills.values[i] = form.skills[i];
            npc->playerSkills.offsets[i] = form.skillOffsets[i];
        }

        npc->race = ResolveOrKeep<RE::TESRace>(form.race, npc->race, form.editorId, "race");
        if (!npc->race) {
            npc->race = LookupDefaultNPCRace();
            logger::warn("NPC '{}' had no race after template/config apply. Fallback NordRace={}.",
                form.editorId,
                npc->race ? "ok" : "missing");
        }
        npc->originalRace = npc->race;
        npc->farSkin = ResolveOrKeep<RE::TESObjectARMO>(form.skin, npc->farSkin, form.editorId, "skin");
        npc->defaultOutfit = ResolveOrKeep<RE::BGSOutfit>(form.defaultOutfit, npc->defaultOutfit, form.editorId, "default outfit");
        npc->sleepOutfit = ResolveOrKeep<RE::BGSOutfit>(form.sleepOutfit, npc->sleepOutfit, form.editorId, "sleep outfit");
        npc->voiceType = ResolveOrKeep<RE::BGSVoiceType>(form.voice, npc->voiceType, form.editorId, "voice");
        npc->npcClass = ResolveOrKeep<RE::TESClass>(form.npcClass, npc->npcClass, form.editorId, "class");
        npc->combatStyle = ResolveOrKeep<RE::TESCombatStyle>(form.combatStyle, npc->combatStyle, form.editorId, "combat style");
        npc->giftFilter = ResolveOrKeep<RE::BGSListForm>(form.giftFilter, npc->giftFilter, form.editorId, "gift filter");
        npc->deathItem = ResolveOrKeep<RE::TESLevItem>(form.deathItem, npc->deathItem, form.editorId, "death item");
        npc->defaultPackList = ResolveOrKeep<RE::BGSListForm>(form.defaultPackageList, npc->defaultPackList, form.editorId, "default package list");
        npc->crimeFaction = ResolveOrKeep<RE::TESFaction>(form.crimeFaction, npc->crimeFaction, form.editorId, "crime faction");
        npc->soundLevel = static_cast<RE::SOUND_LEVEL>(form.soundLevel);
        SetAIDataBits(*npc, form);

        npc->aiPackages.packages.clear();
        RE::BSSimpleList<RE::TESPackage*>::size_type packageIndex = 0;
        for (const auto& packageRef : form.packages) {
            auto* package = ResolveAs<RE::TESPackage>(packageRef);
            if (!package) {
                logger::warn("NPC '{}' package '{}' could not be resolved.", form.editorId, packageRef.Display());
                continue;
            }
            npc->aiPackages.packages.insert_at(packageIndex++, package);
        }

        if (npc->headRelatedData || !form.hairColor.empty() || !form.faceTexture.empty()) {
            if (!npc->headRelatedData) {
                npc->headRelatedData = new RE::TESNPC::HeadRelatedData();
            }
            npc->headRelatedData->hairColor = ResolveOrKeep<RE::BGSColorForm>(form.hairColor, npc->headRelatedData->hairColor, form.editorId, "hair color");
            npc->headRelatedData->faceDetails = ResolveOrKeep<RE::BGSTextureSet>(form.faceTexture, npc->headRelatedData->faceDetails, form.editorId, "face texture");
        }

        npc->factions.clear();
        for (const auto& source : form.npcFactions) {
            auto* faction = ResolveAs<RE::TESFaction>(source.form);
            if (!faction) {
                logger::warn("NPC '{}' faction '{}' could not be resolved.", form.editorId, source.form.Display());
                continue;
            }
            RE::FACTION_RANK rank;
            rank.faction = faction;
            rank.rank = static_cast<std::int8_t>(std::clamp(source.rank, -128, 127));
            npc->factions.push_back(rank);
        }

        std::vector<RE::BGSPerk*> oldPerks;
        for (std::uint32_t i = 0; i < npc->perkCount; ++i) {
            if (npc->perks && npc->perks[i].perk) {
                oldPerks.push_back(npc->perks[i].perk);
            }
        }
        if (!oldPerks.empty()) {
            npc->RemovePerks(oldPerks);
        }
        for (const auto& source : form.npcPerks) {
            auto* perk = ResolveAs<RE::BGSPerk>(source.form);
            if (!perk) {
                logger::warn("NPC '{}' perk '{}' could not be resolved.", form.editorId, source.form.Display());
                continue;
            }
            npc->AddPerk(perk, static_cast<std::int8_t>(std::clamp(source.rank, -128, 127)));
        }

        auto* spellList = static_cast<RE::TESSpellList*>(npc);
        if (spellList->actorEffects) {
            std::vector<RE::SpellItem*> oldSpells;
            for (std::uint32_t i = 0; i < spellList->actorEffects->numSpells; ++i) {
                if (spellList->actorEffects->spells && spellList->actorEffects->spells[i]) {
                    oldSpells.push_back(spellList->actorEffects->spells[i]);
                }
            }
            for (auto* spell : oldSpells) {
                spellList->actorEffects->RemoveSpell(spell);
            }
        }
        if (!form.spells.empty() && !spellList->actorEffects) {
            spellList->actorEffects = new RE::TESSpellList::SpellData();
        }
        if (spellList->actorEffects) {
            for (const auto& spellRef : form.spells) {
                auto* spell = ResolveAs<RE::SpellItem>(spellRef);
                if (!spell) {
                    logger::warn("NPC '{}' spell '{}' could not be resolved.", form.editorId, spellRef.Display());
                    continue;
                }
                spellList->actorEffects->AddSpell(spell);
            }
        }

        std::vector<RE::BGSHeadPart*> parts;
        if (!form.headParts.empty()) {
            std::set<RE::BGSHeadPart*> processed;
            bool hasFaceHeadPart = false;
            auto* fallbackFaceHeadPart = FindCurrentSafeFaceHeadPart(npc);
            std::function<void(RE::BGSHeadPart*)> addPartAndExtras = [&](RE::BGSHeadPart* headPart) {
                if (!headPart || processed.contains(headPart)) {
                    return;
                }
                processed.insert(headPart);
                if (headPart->type == RE::BGSHeadPart::HeadPartType::kFace) {
                    hasFaceHeadPart = true;
                }
                parts.push_back(headPart);
                for (auto* extraPart : headPart->extraParts) {
                    addPartAndExtras(extraPart);
                }
            };

            for (const auto& headPartRef : form.headParts) {
                auto* headPart = ResolveAs<RE::BGSHeadPart>(headPartRef);
                if (!headPart) {
                    logger::warn("NPC '{}' headpart '{}' could not be resolved.", form.editorId, headPartRef.Display());
                    continue;
                }
                addPartAndExtras(headPart);
            }
            if (!hasFaceHeadPart && fallbackFaceHeadPart) {
                logger::info("NPC '{}' headparts do not include a Face part. Preserving fallback face headpart '{}'.",
                    form.editorId,
                    fallbackFaceHeadPart->GetFormEditorID() ? fallbackFaceHeadPart->GetFormEditorID() : "<no editor id>");
                addPartAndExtras(fallbackFaceHeadPart);
            }
        }
        if (!parts.empty()) {
            const auto partCount = std::min<std::size_t>(parts.size(), 127);
            auto* headParts = RE::calloc<RE::BGSHeadPart*>(partCount);
            for (std::size_t i = 0; i < partCount; ++i) {
                headParts[i] = parts[i];
            }
            npc->headParts = headParts;
            npc->numHeadParts = static_cast<std::int8_t>(partCount);
        }

        if (!form.tintLayers.empty()) {
            if (!npc->tintLayers) {
                npc->tintLayers = new RE::BSTArray<RE::TESNPC::Layer*>();
            } else {
                npc->tintLayers->clear();
            }
            for (const auto& source : form.tintLayers) {
                auto* layer = new RE::TESNPC::Layer();
                layer->tintIndex = source.index;
                layer->preset = source.preset;
                layer->interpolationValue = static_cast<std::uint16_t>(std::clamp(source.interpolation * 100.0F, 0.0F, 65535.0F));
                layer->tintColor = RE::Color(source.red, source.green, source.blue, source.alpha);
                npc->tintLayers->push_back(layer);
            }
        }

        const bool hasFaceMorphs = HasMeaningfulFaceMorphs(form);
        const bool hasFaceParts = HasMeaningfulFaceParts(form);
        if (hasFaceMorphs || hasFaceParts) {
            auto* faceData = new RE::TESNPC::FaceData();
            if (npc->faceData) {
                *faceData = *npc->faceData;
            }
            npc->faceData = faceData;
            if (hasFaceMorphs) {
                for (std::size_t i = 0; i < form.faceMorphs.size(); ++i) {
                    npc->faceData->morphs[i] = form.faceMorphs[i];
                }
            }
            if (hasFaceParts) {
                for (std::size_t i = 0; i < form.faceParts.size(); ++i) {
                    npc->faceData->parts[i] = form.faceParts[i];
                }
            }
        } else {
            logger::info("NPC '{}' has no meaningful faceMorphs/faceParts in JSON; preserving template faceData instead of applying zeroed arrays.",
                form.editorId);
        }

        logger::info("Configured NPC '{}' FormID={:08X} race={} class={} voice={} skin={} defaultOutfit={} sleepOutfit={} flags={:08X} level={} calcMin={} calcMax={} health={} magicka={} stamina={} speedMult={} height={} weight={} aiAggression={} aiConfidence={} aiEnergy={} aiMorality={} aiMood={} aiAssistance={} packages={} headParts={} headPartsPtr={} faceData={} tintLayers={} tintLayersPtr={} factions={} perks={} spells={}.",
            form.editorId,
            npc->GetFormID(),
            npc->race ? clib_util::editorID::get_editorID(npc->race) : "<null>",
            npc->npcClass ? clib_util::editorID::get_editorID(npc->npcClass) : "<null>",
            npc->voiceType ? clib_util::editorID::get_editorID(npc->voiceType) : "<null>",
            npc->farSkin ? clib_util::editorID::get_editorID(npc->farSkin) : "<null>",
            npc->defaultOutfit ? clib_util::editorID::get_editorID(npc->defaultOutfit) : "<null>",
            npc->sleepOutfit ? clib_util::editorID::get_editorID(npc->sleepOutfit) : "<null>",
            npc->actorData.actorBaseFlags.underlying(),
            npc->actorData.level,
            npc->actorData.calcLevelMin,
            npc->actorData.calcLevelMax,
            npc->playerSkills.health,
            npc->playerSkills.magicka,
            npc->playerSkills.stamina,
            npc->actorData.speedMult,
            npc->height,
            npc->weight,
            form.aiAggression,
            form.aiConfidence,
            form.aiEnergyLevel,
            form.aiMorality,
            form.aiMood,
            form.aiAssistance,
            form.packages.size(),
            parts.size(),
            fmt::ptr(npc->headParts),
            fmt::ptr(npc->faceData),
            form.tintLayers.size(),
            fmt::ptr(npc->tintLayers),
            form.npcFactions.size(),
            form.npcPerks.size(),
            form.spells.size());
        return true;
    }

    bool ConfigureForm(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        if (!tesForm) {
            return false;
        }

        tesForm->SetFormEditorID(form.editorId.c_str());
        if (form.kind == DynamicForms::FormKind::Global) {
            return ConfigureGlobal(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::FormList) {
            return ConfigureFormList(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::EquipSlot) {
            return ConfigureEquipSlot(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::VoiceType) {
            return ConfigureVoiceType(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::Outfit) {
            return ConfigureOutfit(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::ArmorType) {
            return ConfigureArmorType(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::Armor) {
            return ConfigureArmor(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::Book) {
            return ConfigureBook(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::Misc) {
            return ConfigureMisc(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::Key) {
            return ConfigureKey(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::SoulGem) {
            return ConfigureSoulGem(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::MaterialType) {
            return ConfigureMaterialType(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::Ammo) {
            return ConfigureAmmo(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::Weapon) {
            return ConfigureWeapon(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::AlchemyItem) {
            return ConfigureAlchemyItem(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::Ingredient) {
            return ConfigureIngredient(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::Color) {
            return ConfigureColor(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::ArtObject) {
            return ConfigureArtObject(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::Perk) {
            return ConfigurePerk(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::HeadPart) {
            return ConfigureHeadPart(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::SoundDescriptor) {
            return ConfigureSoundDescriptor(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::Light) {
            return ConfigureLight(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::Explosion) {
            return ConfigureExplosion(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::Activator) {
            return ConfigureActivator(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::EffectShader) {
            return ConfigureEffectShader(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::NPC) {
            return ConfigureNPC(tesForm, form);
        }

        return true;
    }

    RE::TESForm* ResolveDPFFormObject(DynamicForms::DynamicForm& form, const bool configure = true) {
        auto* api = DPF::GetAPI();
        if (!api) {
            logger::warn("Dynamic Persistent Forms API is not available yet.");
            return nullptr;
        }

        bool existed = false;
        auto localId = form.localId;
        const auto formType = FormTypeForKind(form.kind);
        auto* tesForm = api->GetOrCreateByOwnerKey(
            Manager::DPF_OWNER,
            form.editorId.c_str(),
            formType,
            &localId,
            &existed);
        if (!tesForm && (existed || form.localId != 0)) {
            logger::warn("DPF returned null for dynamic {} '{}'. Releasing stale owner/key and retrying once.",
                ToString(form.kind),
                form.editorId);
            if (api->ReleaseByOwnerKey(Manager::DPF_OWNER, form.editorId.c_str())) {
                localId = 0;
                existed = false;
                tesForm = api->GetOrCreateByOwnerKey(
                    Manager::DPF_OWNER,
                    form.editorId.c_str(),
                    formType,
                    &localId,
                    &existed);
            } else {
                logger::warn("Could not release stale DPF owner/key for dynamic form '{}'.", form.editorId);
            }
        }
        if (!tesForm) {
            logger::warn("DPF returned null for dynamic form '{}'", form.editorId);
            return nullptr;
        }

        form.localId = localId;
        if (configure && !ConfigureForm(tesForm, form)) {
            return nullptr;
        }

        logger::info("DPF {} dynamic {} '{}' owner '{}' localId {:06X}.",
            existed ? "recovered" : "created",
            ToString(form.kind),
            form.editorId,
            Manager::DPF_OWNER,
            form.localId);
        return tesForm;
    }

    bool ResolveDPFForm(DynamicForms::DynamicForm& form) {
        return ResolveDPFFormObject(form, true) != nullptr;
    }

    void AddString(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const char* key, const std::string& value) {
        object.AddMember(rapidjson::Value(key, allocator), rapidjson::Value(value.c_str(), allocator), allocator);
    }

    void AddUIntMember(
        rapidjson::Value& object,
        rapidjson::Document::AllocatorType& allocator,
        const std::string& key,
        const unsigned value)
    {
        rapidjson::Value keyValue(key.c_str(), allocator);
        rapidjson::Value valueValue(value);
        object.AddMember(keyValue, valueValue, allocator);
    }

    void AddColorMembers(
        rapidjson::Value& object,
        rapidjson::Document::AllocatorType& allocator,
        const char* prefix,
        const std::uint8_t red,
        const std::uint8_t green,
        const std::uint8_t blue,
        const std::uint8_t alpha)
    {
        const std::string base(prefix);
        AddUIntMember(object, allocator, base + "Red", static_cast<unsigned>(red));
        AddUIntMember(object, allocator, base + "Green", static_cast<unsigned>(green));
        AddUIntMember(object, allocator, base + "Blue", static_cast<unsigned>(blue));
        AddUIntMember(object, allocator, base + "Alpha", static_cast<unsigned>(alpha));
    }

    std::uint8_t ReadUInt8(const rapidjson::Value& doc, const char* key, const std::uint8_t fallback) {
        if (!doc.HasMember(key) || !doc[key].IsUint()) {
            return fallback;
        }
        return static_cast<std::uint8_t>(std::min(doc[key].GetUint(), 255U));
    }

    std::int8_t ReadInt8(const rapidjson::Value& doc, const char* key, const std::int8_t fallback) {
        if (!doc.HasMember(key) || !doc[key].IsInt()) {
            return fallback;
        }
        return static_cast<std::int8_t>(std::clamp(doc[key].GetInt(), -128, 127));
    }

    std::int16_t ReadInt16(const rapidjson::Value& doc, const char* key, const std::int16_t fallback) {
        if (!doc.HasMember(key) || !doc[key].IsInt()) {
            return fallback;
        }
        return static_cast<std::int16_t>(std::clamp(doc[key].GetInt(), -32768, 32767));
    }

    std::uint32_t ReadUInt32(const rapidjson::Value& doc, const char* key, const std::uint32_t fallback) {
        if (!doc.HasMember(key) || !doc[key].IsUint()) {
            return fallback;
        }
        return doc[key].GetUint();
    }

    std::uint16_t ReadUInt16(const rapidjson::Value& doc, const char* key, const std::uint16_t fallback) {
        if (!doc.HasMember(key) || !doc[key].IsUint()) {
            return fallback;
        }
        return static_cast<std::uint16_t>(std::min(doc[key].GetUint(), 65535U));
    }

    float ReadFloat(const rapidjson::Value& doc, const char* key, const float fallback) {
        if (!doc.HasMember(key) || !doc[key].IsNumber()) {
            return fallback;
        }
        return doc[key].GetFloat();
    }

    void ReadColorMembers(
        const rapidjson::Document& doc,
        const char* prefix,
        std::uint8_t& red,
        std::uint8_t& green,
        std::uint8_t& blue,
        std::uint8_t& alpha)
    {
        const std::string base(prefix);
        red = ReadUInt8(doc, (base + "Red").c_str(), red);
        green = ReadUInt8(doc, (base + "Green").c_str(), green);
        blue = ReadUInt8(doc, (base + "Blue").c_str(), blue);
        alpha = ReadUInt8(doc, (base + "Alpha").c_str(), alpha);
    }

    void ReadString(const rapidjson::Value& doc, const char* key, std::string& target) {
        if (doc.HasMember(key) && doc[key].IsString()) {
            target = doc[key].GetString();
        }
    }

    bool LooksLikeEffectShaderJson(const rapidjson::Value& doc) {
        if (doc.HasMember("sourceSignature") && doc["sourceSignature"].IsString() &&
            NormalizeKindName(doc["sourceSignature"].GetString()) == "efsh") {
            return true;
        }

        return doc.HasMember("fillTexture") ||
               doc.HasMember("particleShaderTexture") ||
               doc.HasMember("holesTexture") ||
               doc.HasMember("membranePaletteTexture") ||
               doc.HasMember("particlePaletteTexture") ||
               doc.HasMember("fillAlphaFadeIn") ||
               doc.HasMember("particleBirthRampUpTime") ||
               doc.HasMember("edgeFalloff");
    }

    void ReadStringArray(const rapidjson::Value& doc, const char* key, std::vector<std::string>& target) {
        if (!doc.HasMember(key) || !doc[key].IsArray()) {
            return;
        }
        target.clear();
        for (const auto& item : doc[key].GetArray()) {
            if (item.IsString()) {
                target.emplace_back(item.GetString());
            }
        }
    }

    void AddStringArray(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const char* key, const std::vector<std::string>& values) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto& value : values) {
            array.PushBack(rapidjson::Value(value.c_str(), allocator), allocator);
        }
        object.AddMember(rapidjson::Value(key, allocator), array, allocator);
    }

    void ReadUInt8Array18(const rapidjson::Value& doc, const char* key, std::array<std::uint8_t, 18>& target) {
        if (!doc.HasMember(key) || !doc[key].IsArray()) {
            return;
        }
        const auto array = doc[key].GetArray();
        for (rapidjson::SizeType i = 0; i < array.Size() && i < target.size(); ++i) {
            if (array[i].IsUint()) {
                target[i] = static_cast<std::uint8_t>(std::min(array[i].GetUint(), 255U));
            }
        }
    }

    void AddUInt8Array18(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const char* key, const std::array<std::uint8_t, 18>& values) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto value : values) {
            array.PushBack(static_cast<unsigned>(value), allocator);
        }
        object.AddMember(rapidjson::Value(key, allocator), array, allocator);
    }

    void ReadFloatArray19(const rapidjson::Value& doc, const char* key, std::array<float, 19>& target) {
        if (!doc.HasMember(key) || !doc[key].IsArray()) {
            return;
        }
        const auto array = doc[key].GetArray();
        for (rapidjson::SizeType i = 0; i < array.Size() && i < target.size(); ++i) {
            if (array[i].IsNumber()) {
                target[i] = array[i].GetFloat();
            }
        }
    }

    void AddFloatArray19(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const char* key, const std::array<float, 19>& values) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto value : values) {
            array.PushBack(value, allocator);
        }
        object.AddMember(rapidjson::Value(key, allocator), array, allocator);
    }

    void ReadIntArray4(const rapidjson::Value& doc, const char* key, std::array<std::int32_t, 4>& target) {
        if (!doc.HasMember(key) || !doc[key].IsArray()) {
            return;
        }
        const auto array = doc[key].GetArray();
        for (rapidjson::SizeType i = 0; i < array.Size() && i < target.size(); ++i) {
            if (array[i].IsInt()) {
                target[i] = array[i].GetInt();
            }
        }
    }

    void AddIntArray4(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const char* key, const std::array<std::int32_t, 4>& values) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto value : values) {
            array.PushBack(value, allocator);
        }
        object.AddMember(rapidjson::Value(key, allocator), array, allocator);
    }

    void ReadTintLayers(const rapidjson::Value& doc, std::vector<DynamicForms::TintLayer>& target) {
        if (!doc.HasMember("tintLayers") || !doc["tintLayers"].IsArray()) {
            return;
        }
        target.clear();
        for (const auto& item : doc["tintLayers"].GetArray()) {
            if (!item.IsObject()) {
                continue;
            }
            DynamicForms::TintLayer layer;
            if (item.HasMember("index") && item["index"].IsUint()) {
                layer.index = static_cast<std::uint16_t>(std::min(item["index"].GetUint(), 65535U));
            }
            if (item.HasMember("preset") && item["preset"].IsUint()) {
                layer.preset = static_cast<std::uint16_t>(std::min(item["preset"].GetUint(), 65535U));
            }
            layer.interpolation = ReadFloat(item, "interpolation", layer.interpolation);
            layer.red = ReadUInt8(item, "red", layer.red);
            layer.green = ReadUInt8(item, "green", layer.green);
            layer.blue = ReadUInt8(item, "blue", layer.blue);
            layer.alpha = ReadUInt8(item, "alpha", layer.alpha);
            target.push_back(layer);
        }
    }

    void AddTintLayers(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const std::vector<DynamicForms::TintLayer>& values) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto& layer : values) {
            rapidjson::Value item(rapidjson::kObjectType);
            item.AddMember("index", static_cast<unsigned>(layer.index), allocator);
            item.AddMember("preset", static_cast<unsigned>(layer.preset), allocator);
            item.AddMember("interpolation", layer.interpolation, allocator);
            item.AddMember("red", static_cast<unsigned>(layer.red), allocator);
            item.AddMember("green", static_cast<unsigned>(layer.green), allocator);
            item.AddMember("blue", static_cast<unsigned>(layer.blue), allocator);
            item.AddMember("alpha", static_cast<unsigned>(layer.alpha), allocator);
            array.PushBack(item, allocator);
        }
        object.AddMember("tintLayers", array, allocator);
    }

    DynamicForms::PerkCondition ReadCondition(const rapidjson::Value& value) {
        DynamicForms::PerkCondition condition;
        if (!value.IsObject()) {
            return condition;
        }
        if (value.HasMember("kind") && value["kind"].IsString()) {
            condition.kind = PerkConditionKindFromString(value["kind"].GetString());
        }
        if (value.HasMember("functionId") && value["functionId"].IsUint()) {
            condition.functionId = value["functionId"].GetUint();
        }
        if (value.HasMember("functionName") && value["functionName"].IsString()) {
            condition.functionName = value["functionName"].GetString();
        }
        if (value.HasMember("opCode") && value["opCode"].IsUint()) {
            condition.opCode = value["opCode"].GetUint();
        }
        if (value.HasMember("comparisonValue") && value["comparisonValue"].IsNumber()) {
            condition.comparisonValue = value["comparisonValue"].GetFloat();
        }
        if (value.HasMember("isOr") && value["isOr"].IsBool()) {
            condition.isOr = value["isOr"].GetBool();
        }
        if (value.HasMember("useAliases") && value["useAliases"].IsBool()) {
            condition.useAliases = value["useAliases"].GetBool();
        }
        if (value.HasMember("useGlobalComparison") && value["useGlobalComparison"].IsBool()) {
            condition.useGlobalComparison = value["useGlobalComparison"].GetBool();
        }
        if (value.HasMember("usePackData") && value["usePackData"].IsBool()) {
            condition.usePackData = value["usePackData"].GetBool();
        }
        if (value.HasMember("swapTarget") && value["swapTarget"].IsBool()) {
            condition.swapTarget = value["swapTarget"].GetBool();
        }
        if (value.HasMember("runOn") && value["runOn"].IsUint()) {
            condition.runOn = value["runOn"].GetUint();
        }
        if (value.HasMember("dataId") && value["dataId"].IsUint()) {
            condition.dataId = value["dataId"].GetUint();
        }
        if (value.HasMember("runOnRef") && value["runOnRef"].IsString()) {
            condition.runOnRef = value["runOnRef"].GetString();
        }
        if (value.HasMember("comparisonGlobal") && value["comparisonGlobal"].IsString()) {
            condition.comparisonGlobal = value["comparisonGlobal"].GetString();
        }
        if (value.HasMember("param1") && value["param1"].IsString()) {
            condition.param1 = value["param1"].GetString();
        }
        if (value.HasMember("param2") && value["param2"].IsString()) {
            condition.param2 = value["param2"].GetString();
        }
        if (condition.functionName.empty()) {
            condition.functionName = ConditionCatalog::GetFunctionName(FunctionIdForCondition(condition));
        }
        return condition;
    }

    void WriteCondition(rapidjson::Value& array, rapidjson::Document::AllocatorType& allocator, const DynamicForms::PerkCondition& condition) {
        rapidjson::Value item(rapidjson::kObjectType);
        const auto functionId = FunctionIdForCondition(condition);
        AddString(item, allocator, "kind", ToString(condition.kind));
        AddString(item, allocator, "functionName", condition.functionName.empty() ? ConditionCatalog::GetFunctionName(functionId) : condition.functionName);
        item.AddMember("functionId", functionId, allocator);
        item.AddMember("opCode", condition.opCode, allocator);
        item.AddMember("comparisonValue", condition.comparisonValue, allocator);
        item.AddMember("isOr", condition.isOr, allocator);
        item.AddMember("useAliases", condition.useAliases, allocator);
        item.AddMember("useGlobalComparison", condition.useGlobalComparison, allocator);
        item.AddMember("usePackData", condition.usePackData, allocator);
        item.AddMember("swapTarget", condition.swapTarget, allocator);
        item.AddMember("runOn", condition.runOn, allocator);
        item.AddMember("dataId", condition.dataId, allocator);
        AddString(item, allocator, "runOnRef", condition.runOnRef);
        AddString(item, allocator, "comparisonGlobal", condition.comparisonGlobal);
        AddString(item, allocator, "param1", condition.param1);
        AddString(item, allocator, "param2", condition.param2);
        array.PushBack(item, allocator);
    }

    DynamicForms::PerkEntry ReadPerkEntry(const rapidjson::Value& value) {
        DynamicForms::PerkEntry entry;
        if (!value.IsObject()) {
            return entry;
        }
        if (value.HasMember("rank") && value["rank"].IsUint()) {
            entry.rank = value["rank"].GetUint();
        }
        if (value.HasMember("priority") && value["priority"].IsUint()) {
            entry.priority = value["priority"].GetUint();
        }
        if (value.HasMember("entryPoint") && value["entryPoint"].IsUint()) {
            entry.entryPoint = value["entryPoint"].GetUint();
        }
        if (value.HasMember("function") && value["function"].IsUint()) {
            entry.function = value["function"].GetUint();
        }
        if (value.HasMember("numArgs") && value["numArgs"].IsUint()) {
            entry.numArgs = value["numArgs"].GetUint();
        }
        if (value.HasMember("value") && value["value"].IsNumber()) {
            entry.value = value["value"].GetFloat();
        }
        if (value.HasMember("conditions") && value["conditions"].IsArray()) {
            for (const auto& condition : value["conditions"].GetArray()) {
                entry.conditions.push_back(ReadCondition(condition));
            }
        }
        return entry;
    }

    void WritePerkEntry(rapidjson::Value& array, rapidjson::Document::AllocatorType& allocator, const DynamicForms::PerkEntry& entry) {
        rapidjson::Value item(rapidjson::kObjectType);
        item.AddMember("rank", entry.rank, allocator);
        item.AddMember("priority", entry.priority, allocator);
        item.AddMember("entryPoint", entry.entryPoint, allocator);
        item.AddMember("function", entry.function, allocator);
        item.AddMember("numArgs", entry.numArgs, allocator);
        item.AddMember("value", entry.value, allocator);
        rapidjson::Value conditions(rapidjson::kArrayType);
        for (const auto& condition : entry.conditions) {
            WriteCondition(conditions, allocator, condition);
        }
        item.AddMember("conditions", conditions, allocator);
        array.PushBack(item, allocator);
    }

    bool ReadFormDocument(
        const rapidjson::Document& doc,
        const std::string& sourceLabel,
        const std::string& fallbackEditorId,
        DynamicForms::DynamicForm& out)
    {
        if (!doc.IsObject()) {
            logger::warn("Invalid dynamic form JSON: {}", sourceLabel);
            return false;
        }

        if (!doc.HasMember("formKind") || !doc["formKind"].IsString()) {
            return false;
        }

        if (doc.HasMember("editorId") && doc["editorId"].IsString()) {
            out.editorId = doc["editorId"].GetString();
        } else {
            out.editorId = fallbackEditorId;
        }

        if (out.editorId.empty()) {
            return false;
        }

        if (doc.HasMember("packageName") && doc["packageName"].IsString()) {
            out.packageName = doc["packageName"].GetString();
        }
        if (out.packageName.empty()) {
            out.packageName = Manager::DEFAULT_PACKAGE_NAME;
        }
        if (doc.HasMember("basePackageName") && doc["basePackageName"].IsString()) {
            out.basePackageName = doc["basePackageName"].GetString();
        }
        if (doc.HasMember("patchPackageNames") && doc["patchPackageNames"].IsArray()) {
            out.patchPackageNames.clear();
            for (const auto& package : doc["patchPackageNames"].GetArray()) {
                if (package.IsString() && package.GetStringLength() > 0) {
                    out.patchPackageNames.emplace_back(package.GetString());
                }
            }
        }

        const std::string_view rawKind = doc["formKind"].GetString();
        const auto parsedKind = TryFormKindFromString(rawKind);
        std::optional<DynamicForms::FormKind> sourceKind;
        if (doc.HasMember("sourceSignature") && doc["sourceSignature"].IsString()) {
            sourceKind = TryFormKindFromString(doc["sourceSignature"].GetString());
        }

        if (sourceKind) {
            out.kind = *sourceKind;
            if (!parsedKind || *parsedKind != *sourceKind) {
                logger::warn("JSON '{}' has formKind '{}' but sourceSignature '{}'; using source signature kind '{}'.",
                    sourceLabel,
                    rawKind,
                    doc["sourceSignature"].GetString(),
                    ToString(*sourceKind));
            }
        } else if (parsedKind) {
            out.kind = *parsedKind;
        } else if (LooksLikeEffectShaderJson(doc)) {
            logger::warn("JSON '{}' has unknown formKind '{}' but looks like an EffectShader; using EffectShader.",
                sourceLabel,
                rawKind);
            out.kind = DynamicForms::FormKind::EffectShader;
        } else {
            logger::warn("Unknown formKind '{}' in '{}'; skipping file to avoid rewriting it as Global.",
                rawKind,
                sourceLabel);
            return false;
        }

        if (out.kind == DynamicForms::FormKind::Global && LooksLikeEffectShaderJson(doc)) {
            logger::warn("JSON '{}' looks like an EffectShader but formKind is Global; using EffectShader.", sourceLabel);
            out.kind = DynamicForms::FormKind::EffectShader;
        }
        if (doc.HasMember("globalType") && doc["globalType"].IsString()) {
            out.globalType = GlobalTypeFromString(doc["globalType"].GetString());
        }
        if (doc.HasMember("defaultValue") && doc["defaultValue"].IsNumber()) {
            out.defaultValue = doc["defaultValue"].GetFloat();
        }
        if (doc.HasMember("localId") && doc["localId"].IsUint()) {
            out.localId = doc["localId"].GetUint();
        }
        ReadFormRefArray(doc, "formListItems", out.formListItems);
        ReadFormRefArray(doc, "equipSlotParents", out.equipSlotParents);
        out.equipSlotFlags = ReadUInt32(doc, "equipSlotFlags", out.equipSlotFlags);
        if (doc.HasMember("voiceTypeAllowDefaultDialogue") && doc["voiceTypeAllowDefaultDialogue"].IsBool()) {
            out.voiceTypeAllowDefaultDialogue = doc["voiceTypeAllowDefaultDialogue"].GetBool();
        }
        if (doc.HasMember("voiceTypeFemale") && doc["voiceTypeFemale"].IsBool()) {
            out.voiceTypeFemale = doc["voiceTypeFemale"].GetBool();
        }
        if (doc.HasMember("itemValue") && doc["itemValue"].IsInt()) {
            out.itemValue = doc["itemValue"].GetInt();
        }
        out.itemWeight = ReadFloat(doc, "itemWeight", out.itemWeight);
        ReadString(doc, "inventoryIcon", out.inventoryIcon);
        ReadString(doc, "messageIcon", out.messageIcon);
        ReadString(doc, "materialName", out.materialName);
        ReadFormRef(doc, "materialParent", out.materialParent);
        ReadFormRef(doc, "havokImpactDataSet", out.havokImpactDataSet);
        out.materialId = ReadUInt32(doc, "materialId", out.materialId);
        out.buoyancy = ReadFloat(doc, "buoyancy", out.buoyancy);
        ReadFormRef(doc, "projectile", out.projectile);
        out.damage = ReadFloat(doc, "damage", out.damage);
        out.ammoFlags = ReadUInt32(doc, "ammoFlags", out.ammoFlags);
        out.weaponType = ReadUInt32(doc, "weaponType", out.weaponType);
        out.weaponFlags = ReadUInt32(doc, "weaponFlags", out.weaponFlags);
        out.weaponFlags2 = ReadUInt32(doc, "weaponFlags2", out.weaponFlags2);
        out.weaponSkill = ReadUInt32(doc, "weaponSkill", out.weaponSkill);
        out.weaponResist = ReadUInt32(doc, "weaponResist", out.weaponResist);
        out.weaponCritFlags = ReadUInt32(doc, "weaponCritFlags", out.weaponCritFlags);
        out.weaponCritDamage = ReadUInt32(doc, "weaponCritDamage", out.weaponCritDamage);
        out.weaponSpeed = ReadFloat(doc, "weaponSpeed", out.weaponSpeed);
        out.weaponReach = ReadFloat(doc, "weaponReach", out.weaponReach);
        out.weaponMinRange = ReadFloat(doc, "weaponMinRange", out.weaponMinRange);
        out.weaponMaxRange = ReadFloat(doc, "weaponMaxRange", out.weaponMaxRange);
        out.weaponStagger = ReadFloat(doc, "weaponStagger", out.weaponStagger);
        out.weaponCritMult = ReadFloat(doc, "weaponCritMult", out.weaponCritMult);
        ReadFormRef(doc, "templateWeapon", out.templateWeapon);
        ReadFormRef(doc, "critEffect", out.critEffect);
        ReadFormRef(doc, "attackSound", out.attackSound);
        ReadFormRef(doc, "attackSound2D", out.attackSound2D);
        ReadFormRef(doc, "attackLoopSound", out.attackLoopSound);
        ReadFormRef(doc, "attackFailSound", out.attackFailSound);
        ReadFormRef(doc, "idleSound", out.idleSound);
        ReadFormRef(doc, "equipSound", out.equipSound);
        ReadFormRef(doc, "unequipSound", out.unequipSound);
        ReadFormRef(doc, "firstPersonModelObject", out.firstPersonModelObject);
        out.alchemyFlags = ReadUInt32(doc, "alchemyFlags", out.alchemyFlags);
        if (doc.HasMember("alchemyCostOverride") && doc["alchemyCostOverride"].IsInt()) {
            out.alchemyCostOverride = doc["alchemyCostOverride"].GetInt();
        }
        ReadFormRef(doc, "addictionItem", out.addictionItem);
        out.addictionChance = ReadFloat(doc, "addictionChance", out.addictionChance);
        ReadFormRef(doc, "consumptionSound", out.consumptionSound);
        out.ingredientFlags = ReadUInt32(doc, "ingredientFlags", out.ingredientFlags);
        if (doc.HasMember("ingredientCostOverride") && doc["ingredientCostOverride"].IsInt()) {
            out.ingredientCostOverride = doc["ingredientCostOverride"].GetInt();
        }
        out.knownEffectFlags = ReadUInt16(doc, "knownEffectFlags", out.knownEffectFlags);
        out.playerUses = ReadUInt16(doc, "playerUses", out.playerUses);
        if (doc.HasMember("magicEffectsOverride") && doc["magicEffectsOverride"].IsBool()) {
            out.magicEffectsOverride = doc["magicEffectsOverride"].GetBool();
        }
        if (doc.HasMember("magicEffects")) {
            out.magicEffectsOverride = true;
            ReadMagicEffectArray(doc, "magicEffects", out.magicEffects);
        }
        out.bookFlags = ReadUInt32(doc, "bookFlags", out.bookFlags);
        out.bookType = ReadUInt32(doc, "bookType", out.bookType);
        ReadFormRef(doc, "teachesSpell", out.teachesSpell);
        if (doc.HasMember("teachesActorValue") && doc["teachesActorValue"].IsInt()) {
            out.teachesActorValue = doc["teachesActorValue"].GetInt();
        }
        ReadFormRef(doc, "linkedSoulGem", out.linkedSoulGem);
        out.currentSoul = ReadUInt32(doc, "currentSoul", out.currentSoul);
        out.soulCapacity = ReadUInt32(doc, "soulCapacity", out.soulCapacity);
        if (doc.HasMember("outfitPieces") && doc["outfitPieces"].IsArray()) {
            out.outfitPieces.clear();
            for (const auto& piece : doc["outfitPieces"].GetArray()) {
                auto ref = ReadFormRefValue(piece);
                if (!ref.empty()) {
                    out.outfitPieces.push_back(std::move(ref));
                }
            }
        }
        out.bipedSlots = ReadUInt32(doc, "bipedSlots", out.bipedSlots);
        out.armorType = ReadUInt32(doc, "armorType", out.armorType);
        if (doc.HasMember("armorValue") && doc["armorValue"].IsInt()) {
            out.armorValue = doc["armorValue"].GetInt();
        }
        out.armorWeight = ReadFloat(doc, "armorWeight", out.armorWeight);
        out.armorRating = ReadFloat(doc, "armorRating", out.armorRating);
        out.enchantmentAmount = ReadUInt16(doc, "enchantmentAmount", out.enchantmentAmount);
        ReadString(doc, "maleWorldModel", out.maleWorldModel);
        ReadString(doc, "femaleWorldModel", out.femaleWorldModel);
        ReadString(doc, "maleFirstPersonModel", out.maleFirstPersonModel);
        ReadString(doc, "femaleFirstPersonModel", out.femaleFirstPersonModel);
        ReadString(doc, "maleInventoryIcon", out.maleInventoryIcon);
        ReadString(doc, "femaleInventoryIcon", out.femaleInventoryIcon);
        ReadString(doc, "maleMessageIcon", out.maleMessageIcon);
        ReadString(doc, "femaleMessageIcon", out.femaleMessageIcon);
        ReadFormRef(doc, "enchantment", out.enchantment);
        ReadFormRef(doc, "equipSlot", out.equipSlot);
        ReadFormRef(doc, "templateArmor", out.templateArmor);
        ReadFormRef(doc, "pickupSound", out.pickupSound);
        ReadFormRef(doc, "putdownSound", out.putdownSound);
        ReadFormRef(doc, "blockBashImpactDataSet", out.blockBashImpactDataSet);
        ReadFormRef(doc, "altBlockMaterialType", out.altBlockMaterialType);
        ReadFormRef(doc, "maleSkinTexture", out.maleSkinTexture);
        ReadFormRef(doc, "femaleSkinTexture", out.femaleSkinTexture);
        ReadFormRef(doc, "maleSkinTextureSwapList", out.maleSkinTextureSwapList);
        ReadFormRef(doc, "femaleSkinTextureSwapList", out.femaleSkinTextureSwapList);
        ReadFormRef(doc, "footstepSet", out.footstepSet);
        ReadFormRef(doc, "armorArtObject", out.armorArtObject);
        if (doc.HasMember("armorAddons") && doc["armorAddons"].IsArray()) {
            out.armorAddons.clear();
            for (const auto& item : doc["armorAddons"].GetArray()) {
                auto ref = ReadFormRefValue(item);
                if (!ref.empty()) {
                    out.armorAddons.push_back(std::move(ref));
                }
            }
        }
        if (doc.HasMember("keywords") && doc["keywords"].IsArray()) {
            out.keywords.clear();
            for (const auto& item : doc["keywords"].GetArray()) {
                auto ref = ReadFormRefValue(item);
                if (!ref.empty()) {
                    out.keywords.push_back(std::move(ref));
                }
            }
        }
        if (doc.HasMember("additionalRaces") && doc["additionalRaces"].IsArray()) {
            out.additionalRaces.clear();
            for (const auto& item : doc["additionalRaces"].GetArray()) {
                auto ref = ReadFormRefValue(item);
                if (!ref.empty()) {
                    out.additionalRaces.push_back(std::move(ref));
                }
            }
        }
        if (doc.HasMember("fullName") && doc["fullName"].IsString()) {
            out.fullName = doc["fullName"].GetString();
        }
        if (doc.HasMember("description") && doc["description"].IsString()) {
            out.description = doc["description"].GetString();
        }
        out.red = ReadUInt8(doc, "red", out.red);
        out.green = ReadUInt8(doc, "green", out.green);
        out.blue = ReadUInt8(doc, "blue", out.blue);
        out.alpha = ReadUInt8(doc, "alpha", out.alpha);
        if (doc.HasMember("playable") && doc["playable"].IsBool()) {
            out.playable = doc["playable"].GetBool();
        }
        if (doc.HasMember("modelPath") && doc["modelPath"].IsString()) {
            out.modelPath = doc["modelPath"].GetString();
        }
        if (doc.HasMember("artType") && doc["artType"].IsString()) {
            out.artType = ArtObjectTypeFromString(doc["artType"].GetString());
        }
        out.boundX1 = ReadInt16(doc, "x1", out.boundX1);
        out.boundY1 = ReadInt16(doc, "y1", out.boundY1);
        out.boundZ1 = ReadInt16(doc, "z1", out.boundZ1);
        out.boundX2 = ReadInt16(doc, "x2", out.boundX2);
        out.boundY2 = ReadInt16(doc, "y2", out.boundY2);
        out.boundZ2 = ReadInt16(doc, "z2", out.boundZ2);
        if (doc.HasMember("trait") && doc["trait"].IsBool()) {
            out.trait = doc["trait"].GetBool();
        }
        out.level = ReadInt8(doc, "level", out.level);
        out.numRanks = ReadInt8(doc, "numRanks", out.numRanks);
        if (doc.HasMember("hidden") && doc["hidden"].IsBool()) {
            out.hidden = doc["hidden"].GetBool();
        }
        ReadFormRef(doc, "nextPerk", out.nextPerk);
        if (doc.HasMember("conditions") && doc["conditions"].IsArray()) {
            out.conditions.clear();
            for (const auto& condition : doc["conditions"].GetArray()) {
                out.conditions.push_back(ReadCondition(condition));
            }
        }
        if (doc.HasMember("entries") && doc["entries"].IsArray()) {
            out.entries.clear();
            for (const auto& entry : doc["entries"].GetArray()) {
                out.entries.push_back(ReadPerkEntry(entry));
            }
        }
        if (doc.HasMember("headPartType") && doc["headPartType"].IsString()) {
            out.headPartType = HeadPartTypeFromString(doc["headPartType"].GetString());
        }
        if (doc.HasMember("male") && doc["male"].IsBool()) {
            out.male = doc["male"].GetBool();
        }
        if (doc.HasMember("female") && doc["female"].IsBool()) {
            out.female = doc["female"].GetBool();
        }
        if (doc.HasMember("isExtraPart") && doc["isExtraPart"].IsBool()) {
            out.isExtraPart = doc["isExtraPart"].GetBool();
        }
        if (doc.HasMember("useSolidTint") && doc["useSolidTint"].IsBool()) {
            out.useSolidTint = doc["useSolidTint"].GetBool();
        }
        if (doc.HasMember("raceMorphPath") && doc["raceMorphPath"].IsString()) {
            out.raceMorphPath = doc["raceMorphPath"].GetString();
        }
        if (doc.HasMember("defaultMorphPath") && doc["defaultMorphPath"].IsString()) {
            out.defaultMorphPath = doc["defaultMorphPath"].GetString();
        }
        if (doc.HasMember("chargenMorphPath") && doc["chargenMorphPath"].IsString()) {
            out.chargenMorphPath = doc["chargenMorphPath"].GetString();
        }
        ReadFormRef(doc, "textureSet", out.textureSet);
        ReadFormRef(doc, "colorForm", out.colorForm);
        ReadFormRef(doc, "validRaces", out.validRaces);
        if (doc.HasMember("extraParts") && doc["extraParts"].IsArray()) {
            out.extraParts.clear();
            for (const auto& extraPart : doc["extraParts"].GetArray()) {
                auto ref = ReadFormRefValue(extraPart);
                if (!ref.empty()) {
                    out.extraParts.push_back(std::move(ref));
                }
            }
        }
        ReadStringArray(doc, "soundFiles", out.soundFiles);
        ReadFormRef(doc, "category", out.category);
        ReadFormRef(doc, "alternateSound", out.alternateSound);
        ReadFormRef(doc, "outputModel", out.outputModel);
        out.frequencyShift = ReadUInt8(doc, "frequencyShift", out.frequencyShift);
        out.frequencyVariance = ReadUInt8(doc, "frequencyVariance", out.frequencyVariance);
        out.priority = ReadUInt8(doc, "priority", out.priority);
        out.dbVariance = ReadUInt8(doc, "dbVariance", out.dbVariance);
        out.staticAttenuation = ReadFloat(doc, "staticAttenuation", out.staticAttenuation);
        out.looping = ReadUInt8(doc, "looping", out.looping);
        out.rumbleSendValue = ReadUInt8(doc, "rumbleSendValue", out.rumbleSendValue);
        if (doc.HasMember("lightTime") && doc["lightTime"].IsInt()) {
            out.lightTime = doc["lightTime"].GetInt();
        }
        out.lightRadius = ReadUInt32(doc, "lightRadius", out.lightRadius);
        out.flags = ReadUInt32(doc, "flags", out.flags);
        out.falloffExponent = ReadFloat(doc, "falloffExponent", out.falloffExponent);
        out.fov = ReadFloat(doc, "fov", out.fov);
        out.nearClip = ReadFloat(doc, "nearClip", out.nearClip);
        out.flickerPeriod = ReadFloat(doc, "flickerPeriod", out.flickerPeriod);
        out.flickerIntensityAmplitude = ReadFloat(doc, "flickerIntensityAmplitude", out.flickerIntensityAmplitude);
        out.flickerMovementAmplitude = ReadFloat(doc, "flickerMovementAmplitude", out.flickerMovementAmplitude);
        out.fade = ReadFloat(doc, "fade", out.fade);
        ReadFormRef(doc, "sound", out.sound);
        ReadFormRef(doc, "lensFlare", out.lensFlare);
        ReadFormRef(doc, "light", out.light);
        ReadFormRef(doc, "sound1", out.sound1);
        ReadFormRef(doc, "sound2", out.sound2);
        ReadFormRef(doc, "impactDataSet", out.impactDataSet);
        ReadFormRef(doc, "placedObject", out.placedObject);
        ReadFormRef(doc, "spawnProjectile", out.spawnProjectile);
        ReadFormRef(doc, "objectEffect", out.objectEffect);
        ReadFormRef(doc, "imageSpaceModifier", out.imageSpaceModifier);
        out.force = ReadFloat(doc, "force", out.force);
        out.damage = ReadFloat(doc, "damage", out.damage);
        out.radius = ReadFloat(doc, "radius", out.radius);
        out.imageSpaceRadius = ReadFloat(doc, "imageSpaceRadius", out.imageSpaceRadius);
        out.verticalOffsetMult = ReadFloat(doc, "verticalOffsetMult", out.verticalOffsetMult);
        out.soundLevel = ReadUInt32(doc, "soundLevel", out.soundLevel);
        ReadFormRef(doc, "soundLoop", out.soundLoop);
        ReadFormRef(doc, "soundActivate", out.soundActivate);
        ReadFormRef(doc, "waterType", out.waterType);
        if (doc.HasMember("fillTexture") && doc["fillTexture"].IsString()) {
            out.fillTexturePath = doc["fillTexture"].GetString();
        }
        if (doc.HasMember("particleShaderTexture") && doc["particleShaderTexture"].IsString()) {
            out.particleShaderTexturePath = doc["particleShaderTexture"].GetString();
        }
        if (doc.HasMember("holesTexture") && doc["holesTexture"].IsString()) {
            out.holesTexturePath = doc["holesTexture"].GetString();
        }
        if (doc.HasMember("membranePaletteTexture") && doc["membranePaletteTexture"].IsString()) {
            out.membranePaletteTexturePath = doc["membranePaletteTexture"].GetString();
        }
        if (doc.HasMember("particlePaletteTexture") && doc["particlePaletteTexture"].IsString()) {
            out.particlePaletteTexturePath = doc["particlePaletteTexture"].GetString();
        }
        ReadFormRef(doc, "ambientSound", out.ambientSound);
        ReadColorMembers(doc, "fillColor1", out.fillColor1Red, out.fillColor1Green, out.fillColor1Blue, out.fillColor1Alpha);
        ReadColorMembers(doc, "fillColor2", out.fillColor2Red, out.fillColor2Green, out.fillColor2Blue, out.fillColor2Alpha);
        ReadColorMembers(doc, "fillColor3", out.fillColor3Red, out.fillColor3Green, out.fillColor3Blue, out.fillColor3Alpha);
        ReadColorMembers(doc, "edgeEffect", out.edgeEffectRed, out.edgeEffectGreen, out.edgeEffectBlue, out.edgeEffectAlpha);
        ReadColorMembers(doc, "edgeColor", out.edgeColorRed, out.edgeColorGreen, out.edgeColorBlue, out.edgeColorAlpha);
        ReadColorMembers(doc, "particleColor1", out.particleColor1Red, out.particleColor1Green, out.particleColor1Blue, out.particleColor1Alpha);
        ReadColorMembers(doc, "particleColor2", out.particleColor2Red, out.particleColor2Green, out.particleColor2Blue, out.particleColor2Alpha);
        ReadColorMembers(doc, "particleColor3", out.particleColor3Red, out.particleColor3Green, out.particleColor3Blue, out.particleColor3Alpha);
        out.fillAlphaFadeIn = ReadFloat(doc, "fillAlphaFadeIn", out.fillAlphaFadeIn);
        out.fillFullAlphaTime = ReadFloat(doc, "fillFullAlphaTime", out.fillFullAlphaTime);
        out.fillAlphaFadeOut = ReadFloat(doc, "fillAlphaFadeOut", out.fillAlphaFadeOut);
        out.fillPersistentAlphaRatio = ReadFloat(doc, "fillPersistentAlphaRatio", out.fillPersistentAlphaRatio);
        out.fillAlphaPulseAmplitude = ReadFloat(doc, "fillAlphaPulseAmplitude", out.fillAlphaPulseAmplitude);
        out.fillAlphaPulseFrequency = ReadFloat(doc, "fillAlphaPulseFrequency", out.fillAlphaPulseFrequency);
        out.fillTextureAnimationSpeedU = ReadFloat(doc, "fillTextureAnimationSpeedU", out.fillTextureAnimationSpeedU);
        out.fillTextureAnimationSpeedV = ReadFloat(doc, "fillTextureAnimationSpeedV", out.fillTextureAnimationSpeedV);
        out.fillTextureScaleU = ReadFloat(doc, "fillTextureScaleU", out.fillTextureScaleU);
        out.fillTextureScaleV = ReadFloat(doc, "fillTextureScaleV", out.fillTextureScaleV);
        out.fillFullAlphaRatio = ReadFloat(doc, "fillFullAlphaRatio", out.fillFullAlphaRatio);
        out.edgeFalloff = ReadFloat(doc, "edgeFalloff", out.edgeFalloff);
        out.edgeAlphaFadeIn = ReadFloat(doc, "edgeAlphaFadeIn", out.edgeAlphaFadeIn);
        out.edgeFullAlphaTime = ReadFloat(doc, "edgeFullAlphaTime", out.edgeFullAlphaTime);
        out.edgeAlphaFadeOut = ReadFloat(doc, "edgeAlphaFadeOut", out.edgeAlphaFadeOut);
        out.edgePersistentAlphaRatio = ReadFloat(doc, "edgePersistentAlphaRatio", out.edgePersistentAlphaRatio);
        out.edgeAlphaPulseAmplitude = ReadFloat(doc, "edgeAlphaPulseAmplitude", out.edgeAlphaPulseAmplitude);
        out.edgeAlphaPulseFrequency = ReadFloat(doc, "edgeAlphaPulseFrequency", out.edgeAlphaPulseFrequency);
        out.edgeFullAlphaRatio = ReadFloat(doc, "edgeFullAlphaRatio", out.edgeFullAlphaRatio);
        out.edgeWidthAlphaUnits = ReadFloat(doc, "edgeWidthAlphaUnits", out.edgeWidthAlphaUnits);
        out.particleBirthRampUpTime = ReadFloat(doc, "particleBirthRampUpTime", out.particleBirthRampUpTime);
        out.particleFullBirthTime = ReadFloat(doc, "particleFullBirthTime", out.particleFullBirthTime);
        out.particleBirthRampDownTime = ReadFloat(doc, "particleBirthRampDownTime", out.particleBirthRampDownTime);
        out.particleFullBirthRatio = ReadFloat(doc, "particleFullBirthRatio", out.particleFullBirthRatio);
        out.particleCount = ReadFloat(doc, "particleCount", out.particleCount);
        out.particleLifetime = ReadFloat(doc, "particleLifetime", out.particleLifetime);
        out.particleLifetimeVariance = ReadFloat(doc, "particleLifetimeVariance", out.particleLifetimeVariance);
        out.particleInitialSpeedAlongNormal = ReadFloat(doc, "particleInitialSpeedAlongNormal", out.particleInitialSpeedAlongNormal);
        out.particleAccelerationAlongNormal = ReadFloat(doc, "particleAccelerationAlongNormal", out.particleAccelerationAlongNormal);
        out.particleScaleKey1 = ReadFloat(doc, "particleScaleKey1", out.particleScaleKey1);
        out.particleScaleKey2 = ReadFloat(doc, "particleScaleKey2", out.particleScaleKey2);
        out.particleScaleKey1Time = ReadFloat(doc, "particleScaleKey1Time", out.particleScaleKey1Time);
        out.particleScaleKey2Time = ReadFloat(doc, "particleScaleKey2Time", out.particleScaleKey2Time);
        out.particleColor1AlphaValue = ReadFloat(doc, "particleColor1AlphaValue", out.particleColor1AlphaValue);
        out.particleColor2AlphaValue = ReadFloat(doc, "particleColor2AlphaValue", out.particleColor2AlphaValue);
        out.particleColor3AlphaValue = ReadFloat(doc, "particleColor3AlphaValue", out.particleColor3AlphaValue);
        out.particleColor1Time = ReadFloat(doc, "particleColor1Time", out.particleColor1Time);
        out.particleColor2Time = ReadFloat(doc, "particleColor2Time", out.particleColor2Time);
        out.particleColor3Time = ReadFloat(doc, "particleColor3Time", out.particleColor3Time);
        ReadFormRef(doc, "race", out.race);
        ReadFormRef(doc, "skin", out.skin);
        ReadFormRef(doc, "defaultOutfit", out.defaultOutfit);
        ReadFormRef(doc, "sleepOutfit", out.sleepOutfit);
        ReadFormRef(doc, "voice", out.voice);
        ReadFormRef(doc, "hairColor", out.hairColor);
        ReadFormRef(doc, "faceTexture", out.faceTexture);
        ReadFormRef(doc, "class", out.npcClass);
        ReadFormRef(doc, "combatStyle", out.combatStyle);
        ReadFormRef(doc, "giftFilter", out.giftFilter);
        ReadFormRef(doc, "deathItem", out.deathItem);
        ReadFormRef(doc, "defaultPackageList", out.defaultPackageList);
        ReadFormRef(doc, "crimeFaction", out.crimeFaction);
        if (doc.HasMember("female") && doc["female"].IsBool()) {
            out.femaleNpc = doc["female"].GetBool();
        }
        if (doc.HasMember("oppositeGenderAnim") && doc["oppositeGenderAnim"].IsBool()) {
            out.oppositeGenderAnim = doc["oppositeGenderAnim"].GetBool();
        }
        if (doc.HasMember("essential") && doc["essential"].IsBool()) {
            out.essential = doc["essential"].GetBool();
        }
        if (doc.HasMember("protected") && doc["protected"].IsBool()) {
            out.protectedNpc = doc["protected"].GetBool();
        }
        if (doc.HasMember("unique") && doc["unique"].IsBool()) {
            out.unique = doc["unique"].GetBool();
        }
        if (doc.HasMember("calcStats") && doc["calcStats"].IsBool()) {
            out.calcStats = doc["calcStats"].GetBool();
        }
        if (doc.HasMember("respawn") && doc["respawn"].IsBool()) {
            out.respawn = doc["respawn"].GetBool();
        }
        if (doc.HasMember("doesntAffectStealthMeter") && doc["doesntAffectStealthMeter"].IsBool()) {
            out.doesntAffectStealthMeter = doc["doesntAffectStealthMeter"].GetBool();
        }
        if (doc.HasMember("doesntBleed") && doc["doesntBleed"].IsBool()) {
            out.doesntBleed = doc["doesntBleed"].GetBool();
        }
        if (doc.HasMember("bleedoutOverrideFlag") && doc["bleedoutOverrideFlag"].IsBool()) {
            out.bleedoutOverrideFlag = doc["bleedoutOverrideFlag"].GetBool();
        }
        if (doc.HasMember("simpleActor") && doc["simpleActor"].IsBool()) {
            out.simpleActor = doc["simpleActor"].GetBool();
        }
        if (doc.HasMember("noActivation") && doc["noActivation"].IsBool()) {
            out.noActivation = doc["noActivation"].GetBool();
        }
        if (doc.HasMember("ghost") && doc["ghost"].IsBool()) {
            out.ghost = doc["ghost"].GetBool();
        }
        if (doc.HasMember("invulnerable") && doc["invulnerable"].IsBool()) {
            out.invulnerable = doc["invulnerable"].GetBool();
        }
        out.height = ReadFloat(doc, "height", out.height);
        out.weight = ReadFloat(doc, "weight", out.weight);
        out.health = ReadUInt16(doc, "health", out.health);
        out.magicka = ReadUInt16(doc, "magicka", out.magicka);
        out.stamina = ReadUInt16(doc, "stamina", out.stamina);
        out.healthOffset = ReadInt16(doc, "healthOffset", out.healthOffset);
        out.magickaOffset = ReadInt16(doc, "magickaOffset", out.magickaOffset);
        out.staminaOffset = ReadInt16(doc, "staminaOffset", out.staminaOffset);
        out.calcMinLevel = ReadUInt16(doc, "calcMinLevel", out.calcMinLevel);
        out.calcMaxLevel = ReadUInt16(doc, "calcMaxLevel", out.calcMaxLevel);
        out.npcLevel = ReadUInt16(doc, "npcLevel", out.npcLevel);
        out.speedMult = ReadUInt16(doc, "speedMult", out.speedMult);
        out.dispositionBase = ReadUInt16(doc, "dispositionBase", out.dispositionBase);
        out.bleedoutOverride = ReadInt16(doc, "bleedoutOverride", out.bleedoutOverride);
        if (doc.HasMember("aiAggression") && doc["aiAggression"].IsInt()) {
            out.aiAggression = std::clamp(doc["aiAggression"].GetInt(), 0, 3);
        }
        if (doc.HasMember("aiConfidence") && doc["aiConfidence"].IsInt()) {
            out.aiConfidence = std::clamp(doc["aiConfidence"].GetInt(), 0, 4);
        }
        out.aiEnergyLevel = ReadUInt8(doc, "aiEnergyLevel", out.aiEnergyLevel);
        if (doc.HasMember("aiMorality") && doc["aiMorality"].IsInt()) {
            out.aiMorality = std::clamp(doc["aiMorality"].GetInt(), 0, 3);
        }
        if (doc.HasMember("aiMood") && doc["aiMood"].IsInt()) {
            out.aiMood = std::clamp(doc["aiMood"].GetInt(), 0, 7);
        }
        if (doc.HasMember("aiAssistance") && doc["aiAssistance"].IsInt()) {
            out.aiAssistance = std::clamp(doc["aiAssistance"].GetInt(), 0, 2);
        }
        if (doc.HasMember("aiAggroRadiusBehavior") && doc["aiAggroRadiusBehavior"].IsBool()) {
            out.aiAggroRadiusBehavior = doc["aiAggroRadiusBehavior"].GetBool();
        }
        out.aiAggroRadiusWarn = ReadUInt16(doc, "aiAggroRadiusWarn", out.aiAggroRadiusWarn);
        out.aiAggroRadiusWarnAndAttack = ReadUInt16(doc, "aiAggroRadiusWarnAndAttack", out.aiAggroRadiusWarnAndAttack);
        out.aiAggroRadiusAttack = ReadUInt16(doc, "aiAggroRadiusAttack", out.aiAggroRadiusAttack);
        if (doc.HasMember("aiNoSlowApproach") && doc["aiNoSlowApproach"].IsBool()) {
            out.aiNoSlowApproach = doc["aiNoSlowApproach"].GetBool();
        }
        ReadUInt8Array18(doc, "skills", out.skills);
        ReadUInt8Array18(doc, "skillOffsets", out.skillOffsets);
        ReadFloatArray19(doc, "faceMorphs", out.faceMorphs);
        ReadIntArray4(doc, "faceParts", out.faceParts);
        ReadFormRefArray(doc, "headParts", out.headParts);
        ReadTintLayers(doc, out.tintLayers);
        ReadRankedFormRefArray(doc, "factions", out.npcFactions);
        ReadRankedFormRefArray(doc, "perks", out.npcPerks);
        ReadFormRefArray(doc, "spells", out.spells);
        ReadFormRefArray(doc, "packages", out.packages);

        return true;
    }

    bool ReadFormPayload(const std::string& payload, const std::string& sourceLabel, const std::string& fallbackEditorId, DynamicForms::DynamicForm& out) {
        rapidjson::Document doc;
        doc.Parse(payload.c_str());
        if (doc.HasParseError()) {
            logger::warn("Invalid dynamic form payload: {}", sourceLabel);
            return false;
        }
        return ReadFormDocument(doc, sourceLabel, fallbackEditorId, out);
    }

    bool ReadFormFile(const std::filesystem::path& path, DynamicForms::DynamicForm& out) {
        std::ifstream stream(path);
        if (!stream.is_open()) {
            logger::warn("Could not open dynamic form file: {}", path.string());
            return false;
        }

        rapidjson::IStreamWrapper wrapper(stream);
        rapidjson::Document doc;
        doc.ParseStream(wrapper);
        if (doc.HasParseError()) {
            logger::warn("Invalid dynamic form JSON: {}", path.string());
            return false;
        }
        return ReadFormDocument(doc, path.string(), path.stem().string(), out);
    }

    std::string EffectivePackageName(const DynamicForms::DynamicForm& form) {
        return form.packageName.empty() ? Manager::DEFAULT_PACKAGE_NAME : form.packageName;
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

    std::filesystem::path PackageDirectory(const std::string_view packageName) {
        return std::filesystem::path(Manager::PACKAGES_DIR) / SanitizePackageFolder(std::string(packageName));
    }

    std::filesystem::path PackageManifestPath(const std::string_view packageName) {
        return PackageDirectory(packageName) / "manifest.json";
    }

    std::filesystem::path PackageDbPath(const std::string_view packageName) {
        return PackageDirectory(packageName) / "package.db";
    }

    std::filesystem::path PackageImportDirectory(const std::string_view packageName) {
        return PackageDirectory(packageName) / "imports";
    }

    std::string JsonString(const rapidjson::Document& doc) {
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter writer(buffer);
        doc.Accept(writer);
        return buffer.GetString();
    }

    struct SqliteDb
    {
        sqlite3* handle{ nullptr };

        ~SqliteDb()
        {
            if (handle) {
                sqlite3_close(handle);
            }
        }
    };

    struct SqliteStatement
    {
        sqlite3_stmt* handle{ nullptr };

        ~SqliteStatement()
        {
            if (handle) {
                sqlite3_finalize(handle);
            }
        }
    };

    bool ExecSql(sqlite3* db, const char* sql, const std::string_view context) {
        char* error = nullptr;
        const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
        if (rc == SQLITE_OK) {
            return true;
        }

        logger::warn("SQLite exec failed in '{}': {}", context, error ? error : sqlite3_errmsg(db));
        sqlite3_free(error);
        return false;
    }

    bool PrepareSql(sqlite3* db, const char* sql, SqliteStatement& statement, const std::string_view context) {
        const int rc = sqlite3_prepare_v2(db, sql, -1, &statement.handle, nullptr);
        if (rc == SQLITE_OK) {
            return true;
        }

        logger::warn("SQLite prepare failed in '{}': {}", context, sqlite3_errmsg(db));
        return false;
    }

    bool WritePackageManifest(const std::string_view packageName) {
        std::error_code ec;
        std::filesystem::create_directories(PackageDirectory(packageName), ec);
        if (ec) {
            logger::warn("Could not create package directory '{}': {}", PackageDirectory(packageName).string(), ec.message());
            return false;
        }

        const auto manifestPath = PackageManifestPath(packageName);
        if (std::filesystem::exists(manifestPath)) {
            return true;
        }

        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();
        doc.AddMember("schemaVersion", 1, allocator);
        doc.AddMember("displayName", rapidjson::Value(std::string(packageName).c_str(), allocator), allocator);
        doc.AddMember("enabled", true, allocator);
        doc.AddMember("priority", 0, allocator);
        doc.AddMember("database", "package.db", allocator);

        std::ofstream stream(manifestPath);
        if (!stream.is_open()) {
            logger::warn("Could not write package manifest '{}'.", manifestPath.string());
            return false;
        }

        rapidjson::OStreamWrapper wrapper(stream);
        rapidjson::PrettyWriter writer(wrapper);
        doc.Accept(writer);
        return true;
    }

    bool OpenPackageDb(const std::string_view packageName, SqliteDb& db) {
        if (!WritePackageManifest(packageName)) {
            return false;
        }

        const auto dbPath = PackageDbPath(packageName);
        const int rc = sqlite3_open_v2(dbPath.string().c_str(), &db.handle, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        if (rc != SQLITE_OK) {
            logger::warn("Could not open package database '{}': {}", dbPath.string(), db.handle ? sqlite3_errmsg(db.handle) : "unknown error");
            return false;
        }

        return ExecSql(db.handle, "PRAGMA journal_mode=WAL;", packageName) &&
            ExecSql(db.handle, "PRAGMA synchronous=NORMAL;", packageName) &&
            ExecSql(db.handle,
                "CREATE TABLE IF NOT EXISTS forms ("
                "editor_id TEXT PRIMARY KEY NOT NULL,"
                "form_kind TEXT NOT NULL,"
                "local_id INTEGER NOT NULL DEFAULT 0,"
                "payload TEXT NOT NULL,"
                "updated_at INTEGER NOT NULL DEFAULT (unixepoch())"
                ");",
                packageName) &&
            ExecSql(db.handle,
                "CREATE TABLE IF NOT EXISTS patches ("
                "target_editor_id TEXT PRIMARY KEY NOT NULL,"
                "target_package TEXT NOT NULL,"
                "form_kind TEXT NOT NULL,"
                "payload TEXT NOT NULL,"
                "updated_at INTEGER NOT NULL DEFAULT (unixepoch())"
                ");",
                packageName);
    }

    void SetStringMember(
        rapidjson::Document& doc,
        rapidjson::Document::AllocatorType& allocator,
        const char* key,
        const std::string& value)
    {
        const auto member = doc.FindMember(key);
        if (member != doc.MemberEnd()) {
            member->value.SetString(value.c_str(), allocator);
            return;
        }

        doc.AddMember(rapidjson::Value(key, allocator), rapidjson::Value(value.c_str(), allocator), allocator);
    }

    bool UpsertPackageFormPayload(
        sqlite3* db,
        const std::string_view packageName,
        const DynamicForms::DynamicForm& form,
        const std::string& payload)
    {
        SqliteStatement statement;
        if (!PrepareSql(db,
                "INSERT INTO forms(editor_id, form_kind, local_id, payload, updated_at) "
                "VALUES(?1, ?2, ?3, ?4, unixepoch()) "
                "ON CONFLICT(editor_id) DO UPDATE SET "
                "form_kind=excluded.form_kind, local_id=excluded.local_id, payload=excluded.payload, updated_at=excluded.updated_at;",
                statement,
                packageName)) {
            return false;
        }

        sqlite3_bind_text(statement.handle, 1, form.editorId.c_str(), -1, SQLITE_TRANSIENT);
        const auto formKind = ToString(form.kind);
        sqlite3_bind_text(statement.handle, 2, formKind.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(statement.handle, 3, form.localId);
        sqlite3_bind_text(statement.handle, 4, payload.c_str(), -1, SQLITE_TRANSIENT);

        const int rc = sqlite3_step(statement.handle);
        if (rc == SQLITE_DONE) {
            return true;
        }

        logger::warn("Could not import dynamic form '{}' in package '{}': {}", form.editorId, packageName, sqlite3_errmsg(db));
        return false;
    }

    void ImportPackageJsonQueue(const std::string& packageName, sqlite3* db) {
        const auto importDir = PackageImportDirectory(packageName);
        if (!std::filesystem::exists(importDir)) {
            return;
        }

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(importDir, ec)) {
            if (ec) {
                logger::warn("Could not enumerate package import directory '{}': {}", importDir.string(), ec.message());
                return;
            }
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }

            std::ifstream stream(entry.path());
            if (!stream.is_open()) {
                logger::warn("Could not open package import file '{}'.", entry.path().string());
                continue;
            }

            std::string payload{
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>() };

            rapidjson::Document doc;
            doc.Parse(payload.c_str());
            if (doc.HasParseError() || !doc.IsObject()) {
                logger::warn("Invalid package import JSON '{}'.", entry.path().string());
                continue;
            }

            DynamicForms::DynamicForm form;
            if (!ReadFormDocument(doc, entry.path().string(), entry.path().stem().string(), form)) {
                continue;
            }

            form.packageName = packageName;
            auto& allocator = doc.GetAllocator();
            SetStringMember(doc, allocator, "editorId", form.editorId);
            SetStringMember(doc, allocator, "packageName", packageName);
            const auto normalizedPayload = JsonString(doc);

            if (!UpsertPackageFormPayload(db, packageName, form, normalizedPayload)) {
                continue;
            }

            std::error_code removeEc;
            std::filesystem::remove(entry.path(), removeEc);
            if (removeEc) {
                logger::warn("Imported '{}' but could not remove import file: {}", entry.path().string(), removeEc.message());
            } else {
                logger::info("Imported dynamic form '{}' into package '{}'.", form.editorId, packageName);
            }
        }
    }

    bool PersistFormDocument(const DynamicForms::DynamicForm& form, const rapidjson::Document& doc) {
        const bool saveAsPatch = !form.patchPackageNames.empty();
        const std::string packageName = saveAsPatch ? form.patchPackageNames.back() : EffectivePackageName(form);

        SqliteDb db;
        if (!OpenPackageDb(packageName, db)) {
            return false;
        }

        const auto payload = JsonString(doc);
        SqliteStatement statement;
        if (saveAsPatch) {
            if (!PrepareSql(db.handle,
                    "INSERT INTO patches(target_editor_id, target_package, form_kind, payload, updated_at) "
                    "VALUES(?1, ?2, ?3, ?4, unixepoch()) "
                    "ON CONFLICT(target_editor_id) DO UPDATE SET "
                    "target_package=excluded.target_package, form_kind=excluded.form_kind, payload=excluded.payload, updated_at=excluded.updated_at;",
                    statement,
                    packageName)) {
                return false;
            }
            sqlite3_bind_text(statement.handle, 1, form.editorId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement.handle, 2, EffectivePackageName(form).c_str(), -1, SQLITE_TRANSIENT);
            const auto formKind = ToString(form.kind);
            sqlite3_bind_text(statement.handle, 3, formKind.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement.handle, 4, payload.c_str(), -1, SQLITE_TRANSIENT);
        } else {
            if (!PrepareSql(db.handle,
                    "INSERT INTO forms(editor_id, form_kind, local_id, payload, updated_at) "
                    "VALUES(?1, ?2, ?3, ?4, unixepoch()) "
                    "ON CONFLICT(editor_id) DO UPDATE SET "
                    "form_kind=excluded.form_kind, local_id=excluded.local_id, payload=excluded.payload, updated_at=excluded.updated_at;",
                    statement,
                    packageName)) {
                return false;
            }
            sqlite3_bind_text(statement.handle, 1, form.editorId.c_str(), -1, SQLITE_TRANSIENT);
            const auto formKind = ToString(form.kind);
            sqlite3_bind_text(statement.handle, 2, formKind.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(statement.handle, 3, form.localId);
            sqlite3_bind_text(statement.handle, 4, payload.c_str(), -1, SQLITE_TRANSIENT);
        }

        const int rc = sqlite3_step(statement.handle);
        if (rc == SQLITE_DONE) {
            return true;
        }

        logger::warn("Could not persist dynamic form '{}' in package '{}': {}", form.editorId, packageName, sqlite3_errmsg(db.handle));
        return false;
    }

    bool DeleteStoredForm(const DynamicForms::DynamicForm& form) {
        bool ok = true;
        std::vector<std::string> packages{ EffectivePackageName(form) };
        packages.insert(packages.end(), form.patchPackageNames.begin(), form.patchPackageNames.end());

        for (const auto& package : packages) {
            SqliteDb db;
            if (!OpenPackageDb(package, db)) {
                ok = false;
                continue;
            }

            SqliteStatement formStatement;
            if (PrepareSql(db.handle, "DELETE FROM forms WHERE editor_id=?1;", formStatement, package)) {
                sqlite3_bind_text(formStatement.handle, 1, form.editorId.c_str(), -1, SQLITE_TRANSIENT);
                ok = sqlite3_step(formStatement.handle) == SQLITE_DONE && ok;
            }

            SqliteStatement patchStatement;
            if (PrepareSql(db.handle, "DELETE FROM patches WHERE target_editor_id=?1;", patchStatement, package)) {
                sqlite3_bind_text(patchStatement.handle, 1, form.editorId.c_str(), -1, SQLITE_TRANSIENT);
                ok = sqlite3_step(patchStatement.handle) == SQLITE_DONE && ok;
            }
        }

        return ok;
    }

    std::vector<std::string> DiscoverPackageNames() {
        std::vector<std::string> packages;
        packages.emplace_back(Manager::DEFAULT_PACKAGE_NAME);

        std::error_code ec;
        std::filesystem::create_directories(Manager::PACKAGES_DIR, ec);
        if (ec) {
            logger::warn("Could not create packages directory '{}': {}", Manager::PACKAGES_DIR, ec.message());
            return packages;
        }

        for (const auto& entry : std::filesystem::directory_iterator(Manager::PACKAGES_DIR, ec)) {
            if (ec) {
                logger::warn("Could not enumerate packages directory '{}': {}", Manager::PACKAGES_DIR, ec.message());
                break;
            }
            if (!entry.is_directory()) {
                continue;
            }

            const auto manifestPath = entry.path() / "manifest.json";
            if (!std::filesystem::exists(manifestPath)) {
                continue;
            }

            std::ifstream stream(manifestPath);
            rapidjson::IStreamWrapper wrapper(stream);
            rapidjson::Document doc;
            doc.ParseStream(wrapper);
            if (doc.HasParseError() || !doc.IsObject()) {
                continue;
            }
            if (doc.HasMember("enabled") && doc["enabled"].IsBool() && !doc["enabled"].GetBool()) {
                continue;
            }
            if (doc.HasMember("displayName") && doc["displayName"].IsString()) {
                const std::string displayName = doc["displayName"].GetString();
                if (!displayName.empty() && std::ranges::find(packages, displayName) == packages.end()) {
                    packages.push_back(displayName);
                }
            }
        }

        return packages;
    }

    void AddOrReplaceResolvedForm(DynamicForms::DynamicForm form, const std::string& sourcePackage) {
        if (form.packageName.empty()) {
            form.packageName = sourcePackage;
        }
        const auto existing = std::ranges::find_if(forms, [&form](const DynamicForms::DynamicForm& current) {
            return current.editorId == form.editorId;
        });
        if (existing == forms.end()) {
            forms.push_back(std::move(form));
            return;
        }

        const auto localId = existing->localId;
        const auto packageName = existing->packageName;
        auto patchPackageNames = existing->patchPackageNames;
        if (!sourcePackage.empty() && sourcePackage != packageName && std::ranges::find(patchPackageNames, sourcePackage) == patchPackageNames.end()) {
            patchPackageNames.push_back(sourcePackage);
        }

        form.localId = localId;
        form.packageName = packageName;
        form.patchPackageNames = std::move(patchPackageNames);
        *existing = std::move(form);
    }

    void LoadPackageForms(const std::string& packageName) {
        SqliteDb db;
        if (!OpenPackageDb(packageName, db)) {
            return;
        }

        ImportPackageJsonQueue(packageName, db.handle);

        SqliteStatement formsStatement;
        if (PrepareSql(db.handle, "SELECT editor_id, payload FROM forms ORDER BY editor_id;", formsStatement, packageName)) {
            while (sqlite3_step(formsStatement.handle) == SQLITE_ROW) {
                const auto* editorText = reinterpret_cast<const char*>(sqlite3_column_text(formsStatement.handle, 0));
                const auto* payloadText = reinterpret_cast<const char*>(sqlite3_column_text(formsStatement.handle, 1));
                if (!editorText || !payloadText) {
                    continue;
                }

                DynamicForms::DynamicForm form;
                if (ReadFormPayload(payloadText, std::format("{}:{}", packageName, editorText), editorText, form)) {
                    form.packageName = form.packageName.empty() ? packageName : form.packageName;
                    AddOrReplaceResolvedForm(std::move(form), packageName);
                }
            }
        }

        SqliteStatement patchesStatement;
        if (PrepareSql(db.handle, "SELECT target_editor_id, target_package, payload FROM patches ORDER BY target_editor_id;", patchesStatement, packageName)) {
            while (sqlite3_step(patchesStatement.handle) == SQLITE_ROW) {
                const auto* editorText = reinterpret_cast<const char*>(sqlite3_column_text(patchesStatement.handle, 0));
                const auto* targetPackageText = reinterpret_cast<const char*>(sqlite3_column_text(patchesStatement.handle, 1));
                const auto* payloadText = reinterpret_cast<const char*>(sqlite3_column_text(patchesStatement.handle, 2));
                if (!editorText || !payloadText) {
                    continue;
                }

                const auto existing = std::ranges::find_if(forms, [editorText](const DynamicForms::DynamicForm& current) {
                    return current.editorId == editorText;
                });
                if (existing == forms.end()) {
                    logger::warn("Patch '{}' in package '{}' was skipped because the target form does not exist.", editorText, packageName);
                    continue;
                }

                DynamicForms::DynamicForm patch;
                if (ReadFormPayload(payloadText, std::format("{} patch:{}", packageName, editorText), editorText, patch)) {
                    patch.packageName = targetPackageText && targetPackageText[0] != '\0' ? targetPackageText : existing->packageName;
                    AddOrReplaceResolvedForm(std::move(patch), packageName);
                }
            }
        }
    }
}

namespace Manager {
    std::vector<DynamicForms::DynamicForm>& GetForms() {
        return forms;
    }

    void LoadForms() {
        forms.clear();

        const auto packageNames = DiscoverPackageNames();
        for (const auto& packageName : packageNames) {
            LoadPackageForms(packageName);
        }

        if (!forms.empty()) {
            return;
        }

        std::error_code ec;
        std::filesystem::create_directories(FORMS_DIR, ec);
        for (const auto& entry : std::filesystem::directory_iterator(FORMS_DIR, ec)) {
            if (ec) {
                logger::warn("Could not enumerate forms directory '{}': {}", FORMS_DIR, ec.message());
                return;
            }
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }

            DynamicForms::DynamicForm form;
            if (ReadFormFile(entry.path(), form) && !HasEditorId(form.editorId)) {
                form.packageName = Manager::DEFAULT_PACKAGE_NAME;
                forms.push_back(std::move(form));
            }
        }

        for (const auto& form : forms) {
            SaveForm(form);
        }
    }

    bool SaveForm(const DynamicForms::DynamicForm& form) {
        std::error_code ec;
        std::filesystem::create_directories(FORMS_DIR, ec);
        if (ec) {
            logger::warn("Could not create forms directory '{}': {}", FORMS_DIR, ec.message());
            return false;
        }

        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();
        doc.AddMember("schemaVersion", 1, allocator);
        const auto formKind = ToString(form.kind);
        doc.AddMember("formKind", rapidjson::Value(formKind.c_str(), allocator), allocator);
        doc.AddMember("editorId", rapidjson::Value(form.editorId.c_str(), allocator), allocator);
        AddString(doc, allocator, "packageName", EffectivePackageName(form));
        AddString(doc, allocator, "basePackageName", form.basePackageName);
        AddStringArray(doc, allocator, "patchPackageNames", form.patchPackageNames);
        if (form.kind == DynamicForms::FormKind::FormList) {
            AddFormRefArray(doc, allocator, "formListItems", form.formListItems);
        }
        if (form.kind == DynamicForms::FormKind::EquipSlot) {
            AddFormRefArray(doc, allocator, "equipSlotParents", form.equipSlotParents);
            doc.AddMember("equipSlotFlags", form.equipSlotFlags, allocator);
        }
        if (form.kind == DynamicForms::FormKind::VoiceType) {
            doc.AddMember("voiceTypeAllowDefaultDialogue", form.voiceTypeAllowDefaultDialogue, allocator);
            doc.AddMember("voiceTypeFemale", form.voiceTypeFemale, allocator);
        }
        if (form.kind == DynamicForms::FormKind::Book ||
            form.kind == DynamicForms::FormKind::Misc ||
            form.kind == DynamicForms::FormKind::Key ||
            form.kind == DynamicForms::FormKind::SoulGem ||
            form.kind == DynamicForms::FormKind::Ammo ||
            form.kind == DynamicForms::FormKind::Weapon ||
            form.kind == DynamicForms::FormKind::AlchemyItem ||
            form.kind == DynamicForms::FormKind::Ingredient)
        {
            AddString(doc, allocator, "fullName", form.fullName);
            AddString(doc, allocator, "modelPath", form.modelPath);
            doc.AddMember("itemValue", form.itemValue, allocator);
            doc.AddMember("itemWeight", form.itemWeight, allocator);
            AddString(doc, allocator, "inventoryIcon", form.inventoryIcon);
            AddString(doc, allocator, "messageIcon", form.messageIcon);
            AddFormRef(doc, allocator, "pickupSound", form.pickupSound);
            AddFormRef(doc, allocator, "putdownSound", form.putdownSound);
            AddFormRefArray(doc, allocator, "keywords", form.keywords);
        }
        if (form.kind == DynamicForms::FormKind::Book) {
            AddString(doc, allocator, "description", form.description);
            doc.AddMember("bookFlags", form.bookFlags, allocator);
            doc.AddMember("bookType", form.bookType, allocator);
            AddFormRef(doc, allocator, "teachesSpell", form.teachesSpell);
            doc.AddMember("teachesActorValue", form.teachesActorValue, allocator);
        }
        if (form.kind == DynamicForms::FormKind::SoulGem) {
            AddFormRef(doc, allocator, "linkedSoulGem", form.linkedSoulGem);
            doc.AddMember("currentSoul", form.currentSoul, allocator);
            doc.AddMember("soulCapacity", form.soulCapacity, allocator);
        }
        if (form.kind == DynamicForms::FormKind::MaterialType) {
            AddString(doc, allocator, "materialName", form.materialName);
            AddFormRef(doc, allocator, "materialParent", form.materialParent);
            AddFormRef(doc, allocator, "havokImpactDataSet", form.havokImpactDataSet);
            doc.AddMember("materialId", form.materialId, allocator);
            doc.AddMember("red", static_cast<unsigned>(form.red), allocator);
            doc.AddMember("green", static_cast<unsigned>(form.green), allocator);
            doc.AddMember("blue", static_cast<unsigned>(form.blue), allocator);
            doc.AddMember("buoyancy", form.buoyancy, allocator);
            doc.AddMember("flags", form.flags, allocator);
        }
        if (form.kind == DynamicForms::FormKind::Ammo) {
            AddFormRef(doc, allocator, "projectile", form.projectile);
            doc.AddMember("damage", form.damage, allocator);
            doc.AddMember("ammoFlags", form.ammoFlags, allocator);
        }
        if (form.kind == DynamicForms::FormKind::Weapon) {
            doc.AddMember("damage", form.damage, allocator);
            doc.AddMember("enchantmentAmount", form.enchantmentAmount, allocator);
            AddFormRef(doc, allocator, "enchantment", form.enchantment);
            AddFormRef(doc, allocator, "equipSlot", form.equipSlot);
            AddFormRef(doc, allocator, "templateWeapon", form.templateWeapon);
            AddFormRef(doc, allocator, "critEffect", form.critEffect);
            AddFormRef(doc, allocator, "blockBashImpactDataSet", form.blockBashImpactDataSet);
            AddFormRef(doc, allocator, "altBlockMaterialType", form.altBlockMaterialType);
            AddFormRef(doc, allocator, "impactDataSet", form.impactDataSet);
            AddFormRef(doc, allocator, "firstPersonModelObject", form.firstPersonModelObject);
            AddFormRef(doc, allocator, "attackSound", form.attackSound);
            AddFormRef(doc, allocator, "attackSound2D", form.attackSound2D);
            AddFormRef(doc, allocator, "attackLoopSound", form.attackLoopSound);
            AddFormRef(doc, allocator, "attackFailSound", form.attackFailSound);
            AddFormRef(doc, allocator, "idleSound", form.idleSound);
            AddFormRef(doc, allocator, "equipSound", form.equipSound);
            AddFormRef(doc, allocator, "unequipSound", form.unequipSound);
            doc.AddMember("weaponType", form.weaponType, allocator);
            doc.AddMember("weaponFlags", form.weaponFlags, allocator);
            doc.AddMember("weaponFlags2", form.weaponFlags2, allocator);
            doc.AddMember("weaponSkill", form.weaponSkill, allocator);
            doc.AddMember("weaponResist", form.weaponResist, allocator);
            doc.AddMember("weaponCritFlags", form.weaponCritFlags, allocator);
            doc.AddMember("weaponCritDamage", form.weaponCritDamage, allocator);
            doc.AddMember("weaponSpeed", form.weaponSpeed, allocator);
            doc.AddMember("weaponReach", form.weaponReach, allocator);
            doc.AddMember("weaponMinRange", form.weaponMinRange, allocator);
            doc.AddMember("weaponMaxRange", form.weaponMaxRange, allocator);
            doc.AddMember("weaponStagger", form.weaponStagger, allocator);
            doc.AddMember("weaponCritMult", form.weaponCritMult, allocator);
        }
        if (form.kind == DynamicForms::FormKind::AlchemyItem) {
            AddFormRef(doc, allocator, "equipSlot", form.equipSlot);
            AddFormRef(doc, allocator, "addictionItem", form.addictionItem);
            AddFormRef(doc, allocator, "consumptionSound", form.consumptionSound);
            doc.AddMember("alchemyFlags", form.alchemyFlags, allocator);
            doc.AddMember("alchemyCostOverride", form.alchemyCostOverride, allocator);
            doc.AddMember("addictionChance", form.addictionChance, allocator);
        }
        if (form.kind == DynamicForms::FormKind::Ingredient) {
            AddFormRef(doc, allocator, "equipSlot", form.equipSlot);
            doc.AddMember("ingredientFlags", form.ingredientFlags, allocator);
            doc.AddMember("ingredientCostOverride", form.ingredientCostOverride, allocator);
            doc.AddMember("knownEffectFlags", static_cast<unsigned>(form.knownEffectFlags), allocator);
            doc.AddMember("playerUses", static_cast<unsigned>(form.playerUses), allocator);
        }
        if ((form.kind == DynamicForms::FormKind::AlchemyItem || form.kind == DynamicForms::FormKind::Ingredient) && form.magicEffectsOverride) {
            doc.AddMember("magicEffectsOverride", form.magicEffectsOverride, allocator);
            AddMagicEffectArray(doc, allocator, "magicEffects", form.magicEffects);
        }
        if (form.kind == DynamicForms::FormKind::Global) {
            const auto globalType = ToString(form.globalType);
            doc.AddMember("globalType", rapidjson::Value(globalType.c_str(), allocator), allocator);
            doc.AddMember("defaultValue", form.defaultValue, allocator);
        }
        if (form.kind == DynamicForms::FormKind::Outfit) {
            rapidjson::Value pieces(rapidjson::kArrayType);
            for (const auto& piece : form.outfitPieces) {
                PushFormRef(pieces, allocator, piece);
            }
            doc.AddMember("outfitPieces", pieces, allocator);
        }
        if (form.kind == DynamicForms::FormKind::ArmorType || form.kind == DynamicForms::FormKind::Armor) {
            doc.AddMember("bipedSlots", form.bipedSlots, allocator);
            doc.AddMember("armorType", form.armorType, allocator);
            AddString(doc, allocator, "maleWorldModel", form.maleWorldModel);
            AddString(doc, allocator, "femaleWorldModel", form.femaleWorldModel);
            AddString(doc, allocator, "maleFirstPersonModel", form.maleFirstPersonModel);
            AddString(doc, allocator, "femaleFirstPersonModel", form.femaleFirstPersonModel);
            AddFormRef(doc, allocator, "race", form.race);
        }
        if (form.kind == DynamicForms::FormKind::ArmorType) {
            AddFormRef(doc, allocator, "maleSkinTexture", form.maleSkinTexture);
            AddFormRef(doc, allocator, "femaleSkinTexture", form.femaleSkinTexture);
            AddFormRef(doc, allocator, "maleSkinTextureSwapList", form.maleSkinTextureSwapList);
            AddFormRef(doc, allocator, "femaleSkinTextureSwapList", form.femaleSkinTextureSwapList);
            AddFormRef(doc, allocator, "footstepSet", form.footstepSet);
            AddFormRef(doc, allocator, "armorArtObject", form.armorArtObject);
            AddFormRefArray(doc, allocator, "additionalRaces", form.additionalRaces);
        }
        if (form.kind == DynamicForms::FormKind::Armor) {
            AddString(doc, allocator, "fullName", form.fullName);
            doc.AddMember("armorValue", form.armorValue, allocator);
            doc.AddMember("armorWeight", form.armorWeight, allocator);
            doc.AddMember("armorRating", form.armorRating, allocator);
            doc.AddMember("enchantmentAmount", form.enchantmentAmount, allocator);
            AddString(doc, allocator, "maleInventoryIcon", form.maleInventoryIcon);
            AddString(doc, allocator, "femaleInventoryIcon", form.femaleInventoryIcon);
            AddString(doc, allocator, "maleMessageIcon", form.maleMessageIcon);
            AddString(doc, allocator, "femaleMessageIcon", form.femaleMessageIcon);
            AddFormRef(doc, allocator, "enchantment", form.enchantment);
            AddFormRef(doc, allocator, "equipSlot", form.equipSlot);
            AddFormRef(doc, allocator, "templateArmor", form.templateArmor);
            AddFormRef(doc, allocator, "pickupSound", form.pickupSound);
            AddFormRef(doc, allocator, "putdownSound", form.putdownSound);
            AddFormRef(doc, allocator, "blockBashImpactDataSet", form.blockBashImpactDataSet);
            AddFormRef(doc, allocator, "altBlockMaterialType", form.altBlockMaterialType);
            AddFormRefArray(doc, allocator, "armorAddons", form.armorAddons);
            AddFormRefArray(doc, allocator, "keywords", form.keywords);
        }
        if (form.kind == DynamicForms::FormKind::Color) {
            AddString(doc, allocator, "fullName", form.fullName);
            doc.AddMember("red", static_cast<unsigned>(form.red), allocator);
            doc.AddMember("green", static_cast<unsigned>(form.green), allocator);
            doc.AddMember("blue", static_cast<unsigned>(form.blue), allocator);
            doc.AddMember("alpha", static_cast<unsigned>(form.alpha), allocator);
            doc.AddMember("playable", form.playable, allocator);
        }
        if (form.kind == DynamicForms::FormKind::ArtObject) {
            AddString(doc, allocator, "modelPath", form.modelPath);
            AddString(doc, allocator, "artType", ToString(form.artType));
            doc.AddMember("x1", static_cast<int>(form.boundX1), allocator);
            doc.AddMember("y1", static_cast<int>(form.boundY1), allocator);
            doc.AddMember("z1", static_cast<int>(form.boundZ1), allocator);
            doc.AddMember("x2", static_cast<int>(form.boundX2), allocator);
            doc.AddMember("y2", static_cast<int>(form.boundY2), allocator);
            doc.AddMember("z2", static_cast<int>(form.boundZ2), allocator);
        }
        if (form.kind == DynamicForms::FormKind::Perk) {
            AddString(doc, allocator, "fullName", form.fullName);
            AddString(doc, allocator, "description", form.description);
            doc.AddMember("trait", form.trait, allocator);
            doc.AddMember("level", static_cast<int>(form.level), allocator);
            doc.AddMember("numRanks", static_cast<int>(form.numRanks), allocator);
            doc.AddMember("playable", form.playable, allocator);
            doc.AddMember("hidden", form.hidden, allocator);
            AddFormRef(doc, allocator, "nextPerk", form.nextPerk);

            rapidjson::Value conditions(rapidjson::kArrayType);
            for (const auto& condition : form.conditions) {
                WriteCondition(conditions, allocator, condition);
            }
            doc.AddMember("conditions", conditions, allocator);

            rapidjson::Value entries(rapidjson::kArrayType);
            for (const auto& entry : form.entries) {
                WritePerkEntry(entries, allocator, entry);
            }
            doc.AddMember("entries", entries, allocator);
        }
        if (form.kind == DynamicForms::FormKind::HeadPart) {
            AddString(doc, allocator, "fullName", form.fullName);
            AddString(doc, allocator, "modelPath", form.modelPath);
            AddString(doc, allocator, "headPartType", ToString(form.headPartType));
            doc.AddMember("playable", form.playable, allocator);
            doc.AddMember("male", form.male, allocator);
            doc.AddMember("female", form.female, allocator);
            doc.AddMember("isExtraPart", form.isExtraPart, allocator);
            doc.AddMember("useSolidTint", form.useSolidTint, allocator);
            AddString(doc, allocator, "raceMorphPath", form.raceMorphPath);
            AddString(doc, allocator, "defaultMorphPath", form.defaultMorphPath);
            AddString(doc, allocator, "chargenMorphPath", form.chargenMorphPath);
            AddFormRef(doc, allocator, "textureSet", form.textureSet);
            AddFormRef(doc, allocator, "colorForm", form.colorForm);
            AddFormRef(doc, allocator, "validRaces", form.validRaces);

            rapidjson::Value extraParts(rapidjson::kArrayType);
            for (const auto& extraPart : form.extraParts) {
                PushFormRef(extraParts, allocator, extraPart);
            }
            doc.AddMember("extraParts", extraParts, allocator);
        }
        if (form.kind == DynamicForms::FormKind::SoundDescriptor) {
            AddStringArray(doc, allocator, "soundFiles", form.soundFiles);
            AddFormRef(doc, allocator, "category", form.category);
            AddFormRef(doc, allocator, "alternateSound", form.alternateSound);
            AddFormRef(doc, allocator, "outputModel", form.outputModel);
            doc.AddMember("frequencyShift", static_cast<unsigned>(form.frequencyShift), allocator);
            doc.AddMember("frequencyVariance", static_cast<unsigned>(form.frequencyVariance), allocator);
            doc.AddMember("priority", static_cast<unsigned>(form.priority), allocator);
            doc.AddMember("dbVariance", static_cast<unsigned>(form.dbVariance), allocator);
            doc.AddMember("staticAttenuation", form.staticAttenuation, allocator);
            doc.AddMember("looping", static_cast<unsigned>(form.looping), allocator);
            doc.AddMember("rumbleSendValue", static_cast<unsigned>(form.rumbleSendValue), allocator);

            rapidjson::Value conditions(rapidjson::kArrayType);
            for (const auto& condition : form.conditions) {
                WriteCondition(conditions, allocator, condition);
            }
            doc.AddMember("conditions", conditions, allocator);
        }
        if (form.kind == DynamicForms::FormKind::Light) {
            AddString(doc, allocator, "fullName", form.fullName);
            AddString(doc, allocator, "modelPath", form.modelPath);
            doc.AddMember("lightTime", form.lightTime, allocator);
            doc.AddMember("lightRadius", form.lightRadius, allocator);
            doc.AddMember("red", static_cast<unsigned>(form.red), allocator);
            doc.AddMember("green", static_cast<unsigned>(form.green), allocator);
            doc.AddMember("blue", static_cast<unsigned>(form.blue), allocator);
            doc.AddMember("alpha", static_cast<unsigned>(form.alpha), allocator);
            doc.AddMember("flags", form.flags, allocator);
            doc.AddMember("falloffExponent", form.falloffExponent, allocator);
            doc.AddMember("fov", form.fov, allocator);
            doc.AddMember("nearClip", form.nearClip, allocator);
            doc.AddMember("flickerPeriod", form.flickerPeriod, allocator);
            doc.AddMember("flickerIntensityAmplitude", form.flickerIntensityAmplitude, allocator);
            doc.AddMember("flickerMovementAmplitude", form.flickerMovementAmplitude, allocator);
            doc.AddMember("fade", form.fade, allocator);
            AddFormRef(doc, allocator, "sound", form.sound);
            AddFormRef(doc, allocator, "lensFlare", form.lensFlare);
        }
        if (form.kind == DynamicForms::FormKind::Explosion) {
            AddString(doc, allocator, "fullName", form.fullName);
            AddString(doc, allocator, "modelPath", form.modelPath);
            AddFormRef(doc, allocator, "light", form.light);
            AddFormRef(doc, allocator, "sound1", form.sound1);
            AddFormRef(doc, allocator, "sound2", form.sound2);
            AddFormRef(doc, allocator, "impactDataSet", form.impactDataSet);
            AddFormRef(doc, allocator, "placedObject", form.placedObject);
            AddFormRef(doc, allocator, "spawnProjectile", form.spawnProjectile);
            AddFormRef(doc, allocator, "objectEffect", form.objectEffect);
            AddFormRef(doc, allocator, "imageSpaceModifier", form.imageSpaceModifier);
            doc.AddMember("force", form.force, allocator);
            doc.AddMember("damage", form.damage, allocator);
            doc.AddMember("radius", form.radius, allocator);
            doc.AddMember("imageSpaceRadius", form.imageSpaceRadius, allocator);
            doc.AddMember("verticalOffsetMult", form.verticalOffsetMult, allocator);
            doc.AddMember("flags", form.flags, allocator);
            doc.AddMember("soundLevel", form.soundLevel, allocator);
        }
        if (form.kind == DynamicForms::FormKind::Activator) {
            AddString(doc, allocator, "fullName", form.fullName);
            AddString(doc, allocator, "modelPath", form.modelPath);
            AddFormRef(doc, allocator, "soundLoop", form.soundLoop);
            AddFormRef(doc, allocator, "soundActivate", form.soundActivate);
            AddFormRef(doc, allocator, "waterType", form.waterType);
            doc.AddMember("flags", form.flags, allocator);
        }
        if (form.kind == DynamicForms::FormKind::EffectShader) {
            AddString(doc, allocator, "fillTexture", form.fillTexturePath);
            AddString(doc, allocator, "particleShaderTexture", form.particleShaderTexturePath);
            AddString(doc, allocator, "holesTexture", form.holesTexturePath);
            AddString(doc, allocator, "membranePaletteTexture", form.membranePaletteTexturePath);
            AddString(doc, allocator, "particlePaletteTexture", form.particlePaletteTexturePath);
            AddFormRef(doc, allocator, "ambientSound", form.ambientSound);
            doc.AddMember("flags", form.flags, allocator);
            AddColorMembers(doc, allocator, "fillColor1", form.fillColor1Red, form.fillColor1Green, form.fillColor1Blue, form.fillColor1Alpha);
            AddColorMembers(doc, allocator, "fillColor2", form.fillColor2Red, form.fillColor2Green, form.fillColor2Blue, form.fillColor2Alpha);
            AddColorMembers(doc, allocator, "fillColor3", form.fillColor3Red, form.fillColor3Green, form.fillColor3Blue, form.fillColor3Alpha);
            AddColorMembers(doc, allocator, "edgeEffect", form.edgeEffectRed, form.edgeEffectGreen, form.edgeEffectBlue, form.edgeEffectAlpha);
            AddColorMembers(doc, allocator, "edgeColor", form.edgeColorRed, form.edgeColorGreen, form.edgeColorBlue, form.edgeColorAlpha);
            AddColorMembers(doc, allocator, "particleColor1", form.particleColor1Red, form.particleColor1Green, form.particleColor1Blue, form.particleColor1Alpha);
            AddColorMembers(doc, allocator, "particleColor2", form.particleColor2Red, form.particleColor2Green, form.particleColor2Blue, form.particleColor2Alpha);
            AddColorMembers(doc, allocator, "particleColor3", form.particleColor3Red, form.particleColor3Green, form.particleColor3Blue, form.particleColor3Alpha);
            doc.AddMember("fillAlphaFadeIn", form.fillAlphaFadeIn, allocator);
            doc.AddMember("fillFullAlphaTime", form.fillFullAlphaTime, allocator);
            doc.AddMember("fillAlphaFadeOut", form.fillAlphaFadeOut, allocator);
            doc.AddMember("fillPersistentAlphaRatio", form.fillPersistentAlphaRatio, allocator);
            doc.AddMember("fillAlphaPulseAmplitude", form.fillAlphaPulseAmplitude, allocator);
            doc.AddMember("fillAlphaPulseFrequency", form.fillAlphaPulseFrequency, allocator);
            doc.AddMember("fillTextureAnimationSpeedU", form.fillTextureAnimationSpeedU, allocator);
            doc.AddMember("fillTextureAnimationSpeedV", form.fillTextureAnimationSpeedV, allocator);
            doc.AddMember("fillTextureScaleU", form.fillTextureScaleU, allocator);
            doc.AddMember("fillTextureScaleV", form.fillTextureScaleV, allocator);
            doc.AddMember("fillFullAlphaRatio", form.fillFullAlphaRatio, allocator);
            doc.AddMember("edgeFalloff", form.edgeFalloff, allocator);
            doc.AddMember("edgeAlphaFadeIn", form.edgeAlphaFadeIn, allocator);
            doc.AddMember("edgeFullAlphaTime", form.edgeFullAlphaTime, allocator);
            doc.AddMember("edgeAlphaFadeOut", form.edgeAlphaFadeOut, allocator);
            doc.AddMember("edgePersistentAlphaRatio", form.edgePersistentAlphaRatio, allocator);
            doc.AddMember("edgeAlphaPulseAmplitude", form.edgeAlphaPulseAmplitude, allocator);
            doc.AddMember("edgeAlphaPulseFrequency", form.edgeAlphaPulseFrequency, allocator);
            doc.AddMember("edgeFullAlphaRatio", form.edgeFullAlphaRatio, allocator);
            doc.AddMember("edgeWidthAlphaUnits", form.edgeWidthAlphaUnits, allocator);
            doc.AddMember("particleBirthRampUpTime", form.particleBirthRampUpTime, allocator);
            doc.AddMember("particleFullBirthTime", form.particleFullBirthTime, allocator);
            doc.AddMember("particleBirthRampDownTime", form.particleBirthRampDownTime, allocator);
            doc.AddMember("particleFullBirthRatio", form.particleFullBirthRatio, allocator);
            doc.AddMember("particleCount", form.particleCount, allocator);
            doc.AddMember("particleLifetime", form.particleLifetime, allocator);
            doc.AddMember("particleLifetimeVariance", form.particleLifetimeVariance, allocator);
            doc.AddMember("particleInitialSpeedAlongNormal", form.particleInitialSpeedAlongNormal, allocator);
            doc.AddMember("particleAccelerationAlongNormal", form.particleAccelerationAlongNormal, allocator);
            doc.AddMember("particleScaleKey1", form.particleScaleKey1, allocator);
            doc.AddMember("particleScaleKey2", form.particleScaleKey2, allocator);
            doc.AddMember("particleScaleKey1Time", form.particleScaleKey1Time, allocator);
            doc.AddMember("particleScaleKey2Time", form.particleScaleKey2Time, allocator);
            doc.AddMember("particleColor1AlphaValue", form.particleColor1AlphaValue, allocator);
            doc.AddMember("particleColor2AlphaValue", form.particleColor2AlphaValue, allocator);
            doc.AddMember("particleColor3AlphaValue", form.particleColor3AlphaValue, allocator);
            doc.AddMember("particleColor1Time", form.particleColor1Time, allocator);
            doc.AddMember("particleColor2Time", form.particleColor2Time, allocator);
            doc.AddMember("particleColor3Time", form.particleColor3Time, allocator);
        }
        if (form.kind == DynamicForms::FormKind::NPC) {
            AddString(doc, allocator, "fullName", form.fullName);
            AddFormRef(doc, allocator, "race", form.race);
            AddFormRef(doc, allocator, "skin", form.skin);
            AddFormRef(doc, allocator, "defaultOutfit", form.defaultOutfit);
            AddFormRef(doc, allocator, "sleepOutfit", form.sleepOutfit);
            AddFormRef(doc, allocator, "voice", form.voice);
            AddFormRef(doc, allocator, "hairColor", form.hairColor);
            AddFormRef(doc, allocator, "faceTexture", form.faceTexture);
            AddFormRef(doc, allocator, "class", form.npcClass);
            AddFormRef(doc, allocator, "combatStyle", form.combatStyle);
            AddFormRef(doc, allocator, "giftFilter", form.giftFilter);
            AddFormRef(doc, allocator, "deathItem", form.deathItem);
            AddFormRef(doc, allocator, "defaultPackageList", form.defaultPackageList);
            AddFormRef(doc, allocator, "crimeFaction", form.crimeFaction);
            doc.AddMember("female", form.femaleNpc, allocator);
            doc.AddMember("oppositeGenderAnim", form.oppositeGenderAnim, allocator);
            doc.AddMember("essential", form.essential, allocator);
            doc.AddMember("protected", form.protectedNpc, allocator);
            doc.AddMember("unique", form.unique, allocator);
            doc.AddMember("calcStats", form.calcStats, allocator);
            doc.AddMember("respawn", form.respawn, allocator);
            doc.AddMember("doesntAffectStealthMeter", form.doesntAffectStealthMeter, allocator);
            doc.AddMember("doesntBleed", form.doesntBleed, allocator);
            doc.AddMember("bleedoutOverrideFlag", form.bleedoutOverrideFlag, allocator);
            doc.AddMember("simpleActor", form.simpleActor, allocator);
            doc.AddMember("noActivation", form.noActivation, allocator);
            doc.AddMember("ghost", form.ghost, allocator);
            doc.AddMember("invulnerable", form.invulnerable, allocator);
            doc.AddMember("height", form.height, allocator);
            doc.AddMember("weight", form.weight, allocator);
            doc.AddMember("red", static_cast<unsigned>(form.red), allocator);
            doc.AddMember("green", static_cast<unsigned>(form.green), allocator);
            doc.AddMember("blue", static_cast<unsigned>(form.blue), allocator);
            doc.AddMember("alpha", static_cast<unsigned>(form.alpha), allocator);
            doc.AddMember("health", static_cast<unsigned>(form.health), allocator);
            doc.AddMember("magicka", static_cast<unsigned>(form.magicka), allocator);
            doc.AddMember("stamina", static_cast<unsigned>(form.stamina), allocator);
            doc.AddMember("healthOffset", static_cast<int>(form.healthOffset), allocator);
            doc.AddMember("magickaOffset", static_cast<int>(form.magickaOffset), allocator);
            doc.AddMember("staminaOffset", static_cast<int>(form.staminaOffset), allocator);
            doc.AddMember("calcMinLevel", static_cast<unsigned>(form.calcMinLevel), allocator);
            doc.AddMember("calcMaxLevel", static_cast<unsigned>(form.calcMaxLevel), allocator);
            doc.AddMember("npcLevel", static_cast<unsigned>(form.npcLevel), allocator);
            doc.AddMember("speedMult", static_cast<unsigned>(form.speedMult), allocator);
            doc.AddMember("dispositionBase", static_cast<unsigned>(form.dispositionBase), allocator);
            doc.AddMember("bleedoutOverride", static_cast<int>(form.bleedoutOverride), allocator);
            doc.AddMember("soundLevel", form.soundLevel, allocator);
            doc.AddMember("aiAggression", form.aiAggression, allocator);
            doc.AddMember("aiConfidence", form.aiConfidence, allocator);
            doc.AddMember("aiEnergyLevel", static_cast<unsigned>(form.aiEnergyLevel), allocator);
            doc.AddMember("aiMorality", form.aiMorality, allocator);
            doc.AddMember("aiMood", form.aiMood, allocator);
            doc.AddMember("aiAssistance", form.aiAssistance, allocator);
            doc.AddMember("aiAggroRadiusBehavior", form.aiAggroRadiusBehavior, allocator);
            doc.AddMember("aiAggroRadiusWarn", static_cast<unsigned>(form.aiAggroRadiusWarn), allocator);
            doc.AddMember("aiAggroRadiusWarnAndAttack", static_cast<unsigned>(form.aiAggroRadiusWarnAndAttack), allocator);
            doc.AddMember("aiAggroRadiusAttack", static_cast<unsigned>(form.aiAggroRadiusAttack), allocator);
            doc.AddMember("aiNoSlowApproach", form.aiNoSlowApproach, allocator);
            AddUInt8Array18(doc, allocator, "skills", form.skills);
            AddUInt8Array18(doc, allocator, "skillOffsets", form.skillOffsets);
            if (HasMeaningfulFaceMorphs(form)) {
                AddFloatArray19(doc, allocator, "faceMorphs", form.faceMorphs);
            }
            if (HasMeaningfulFaceParts(form)) {
                AddIntArray4(doc, allocator, "faceParts", form.faceParts);
            }
            AddFormRefArray(doc, allocator, "headParts", form.headParts);
            if (!form.tintLayers.empty()) {
                AddTintLayers(doc, allocator, form.tintLayers);
            }
            AddRankedFormRefArray(doc, allocator, "factions", form.npcFactions);
            AddRankedFormRefArray(doc, allocator, "perks", form.npcPerks);
            AddFormRefArray(doc, allocator, "spells", form.spells);
            AddFormRefArray(doc, allocator, "packages", form.packages);
        }
        if (form.localId != 0) {
            doc.AddMember("localId", form.localId, allocator);
        }

        return PersistFormDocument(form, doc);
    }

    bool SaveForm(const std::size_t index, const bool dispatchUpdate) {
        if (index >= forms.size()) {
            return false;
        }

        if (!ResolveDPFForm(forms[index])) {
            return false;
        }

        const bool saved = SaveForm(forms[index]);
        if (saved) {
            forms[index].dirty = false;
            ListManager::GetSingleton()->PopulateAllLists(true);
            if (dispatchUpdate) {
                DispatchEvent(UPDATED_EVENT, ToSignature(forms[index].kind), static_cast<float>(forms[index].localId));
            }
        }
        return saved;
    }

    bool SaveAllForms(const bool dispatchUpdate) {
        bool saved = true;
        std::set<std::string> updatedSignatures;
        std::size_t savedCount = 0;
        for (std::size_t i = 0; i < forms.size(); ++i) {
            const bool wasDirty = forms[i].dirty;
            if (ResolveDPFForm(forms[i]) && SaveForm(forms[i])) {
                forms[i].dirty = dispatchUpdate ? false : wasDirty;
                updatedSignatures.insert(ToSignature(forms[i].kind));
                ++savedCount;
            } else {
                saved = false;
            }
        }
        if (saved) {
            ListManager::GetSingleton()->PopulateAllLists(true);
            if (dispatchUpdate && !updatedSignatures.empty()) {
                DispatchEvent(UPDATED_EVENT, JoinSignatures(updatedSignatures), static_cast<float>(savedCount));
            }
        }
        return saved;
    }

    bool AddForm(const DynamicForms::DynamicForm& form) {
        if (form.editorId.empty() || HasEditorId(form.editorId)) {
            return false;
        }

        auto createdForm = form;
        if (!ResolveDPFForm(createdForm)) {
            return false;
        }

        forms.push_back(std::move(createdForm));
        const bool saved = SaveForm(forms.back());
        if (saved) {
            forms.back().dirty = false;
            ListManager::GetSingleton()->PopulateAllLists(true);
        }
        return saved;
    }

    bool UpdateForm(const std::size_t index, const DynamicForms::DynamicForm& form) {
        if (index >= forms.size() || form.editorId.empty() || forms[index].editorId != form.editorId) {
            return false;
        }

        forms[index] = form;
        forms[index].dirty = true;
        return true;
    }

    bool DeleteForm(const std::size_t index) {
        if (index >= forms.size()) {
            return false;
        }

        auto* api = DPF::GetAPI();
        if (!api) {
            logger::warn("Dynamic Persistent Forms API is not available. '{}' will not be deleted.", forms[index].editorId);
            return false;
        }

        const auto form = forms[index];
        bool released = false;
        if (form.localId != 0) {
            released = api->ReleaseByLocalId(form.localId, DPF_OWNER);
        }
        if (!released) {
            released = api->ReleaseByOwnerKey(DPF_OWNER, form.editorId.c_str());
        }
        if (!released) {
            logger::warn("DPF release failed for dynamic form '{}' localId {:06X}.", form.editorId, form.localId);
            return false;
        }

        if (!DeleteStoredForm(form)) {
            logger::warn("Could not delete dynamic form '{}' from package storage.", form.editorId);
            return false;
        }

        const auto signature = ToSignature(form.kind);
        forms.erase(forms.begin() + static_cast<std::ptrdiff_t>(index));
        ListManager::GetSingleton()->PopulateAllLists(true);
        DispatchEvent(UPDATED_EVENT, signature, static_cast<float>(form.localId));
        logger::info("Deleted dynamic form '{}' localId {:06X}.", form.editorId, form.localId);
        return true;
    }

    bool AssignFormToPackage(const std::string_view editorId, const std::string_view packageName, const bool save) {
        if (editorId.empty() || packageName.empty()) {
            return false;
        }

        const auto found = std::ranges::find_if(forms, [editorId](const DynamicForms::DynamicForm& form) {
            return form.editorId == editorId;
        });
        if (found == forms.end()) {
            return false;
        }

        const auto oldForm = *found;
        if (!DeleteStoredForm(oldForm)) {
            logger::warn("Could not remove '{}' from its previous package before moving it.", found->editorId);
            return false;
        }

        found->packageName = packageName;
        found->basePackageName.clear();
        found->patchPackageNames.clear();
        found->dirty = true;
        return !save || SaveForm(static_cast<std::size_t>(std::distance(forms.begin(), found)));
    }

    bool AddPatchLayer(const std::string_view editorId, const std::string_view packageName, const bool save) {
        if (editorId.empty() || packageName.empty()) {
            return false;
        }

        const auto found = std::ranges::find_if(forms, [editorId](const DynamicForms::DynamicForm& form) {
            return form.editorId == editorId;
        });
        if (found == forms.end() || EffectivePackageName(*found) == packageName) {
            return false;
        }

        if (std::ranges::find(found->patchPackageNames, packageName) == found->patchPackageNames.end()) {
            found->patchPackageNames.emplace_back(packageName);
        }
        found->basePackageName = EffectivePackageName(*found);
        found->dirty = true;
        return !save || SaveForm(static_cast<std::size_t>(std::distance(forms.begin(), found)));
    }

    bool AddFormToPlayerInventory(const std::size_t index) {
        if (index >= forms.size()) {
            return false;
        }
        if (forms[index].dirty) {
            logger::warn("Could not add '{}' to inventory: save the form before testing it.", forms[index].editorId);
            return false;
        }

        const auto oldLocalId = forms[index].localId;
        const bool configureBeforeTest = forms[index].kind == DynamicForms::FormKind::NPC;
        auto* runtimeForm = ResolveDPFFormObject(forms[index], configureBeforeTest);
        if (!runtimeForm) {
            return false;
        }
        if (forms[index].localId != oldLocalId) {
            SaveForm(forms[index]);
        }

        auto* boundObject = runtimeForm->As<RE::TESBoundObject>();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!boundObject || !player) {
            logger::warn("Could not add '{}' to inventory: runtime form is not a bound object or player is unavailable.", forms[index].editorId);
            return false;
        }

        player->AddObjectToContainer(boundObject, nullptr, 1, nullptr);
        logger::info("Added dynamic form '{}' to player inventory.", forms[index].editorId);
        return true;
    }

    bool SpawnFormAtPlayer(const std::size_t index) {
        if (index >= forms.size()) {
            return false;
        }
        if (forms[index].dirty) {
            logger::warn("Could not spawn '{}': save the form before testing it.", forms[index].editorId);
            return false;
        }

        const auto oldLocalId = forms[index].localId;
        const bool configureBeforeTest = forms[index].kind == DynamicForms::FormKind::NPC;
        auto* runtimeForm = ResolveDPFFormObject(forms[index], configureBeforeTest);
        if (!runtimeForm) {
            return false;
        }
        if (forms[index].localId != oldLocalId) {
            SaveForm(forms[index]);
        }

        auto* boundObject = runtimeForm->As<RE::TESBoundObject>();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!boundObject || !player) {
            logger::warn("Could not spawn '{}': runtime form is not a bound object or player is unavailable.", forms[index].editorId);
            return false;
        }

        if (forms[index].kind == DynamicForms::FormKind::NPC) {
            auto* npc = runtimeForm->As<RE::TESNPC>();
            LogNPCSnapshot("Reference Lydia before DFG spawn", LookupLydiaNPC());
            logger::info("Spawn debug for NPC '{}': runtimeForm={} boundObject={} npc={} formID={:08X} formType={} localId={:06X}",
                forms[index].editorId,
                fmt::ptr(runtimeForm),
                fmt::ptr(boundObject),
                fmt::ptr(npc),
                runtimeForm->GetFormID(),
                static_cast<std::uint32_t>(runtimeForm->GetFormType()),
                forms[index].localId);
            if (!npc) {
                logger::warn("Spawn aborted for '{}': runtime form is not TESNPC.", forms[index].editorId);
                return false;
            }
            LogNPCSnapshot("DFG dynamic before spawn", npc);
            logger::info("Spawn debug for NPC '{}': race={} class={} voice={} skin={} defaultOutfit={} headParts={} headPartsPtr={} faceData={} tintLayersPtr={} flags={:08X} level={} health={} magicka={} stamina={}",
                forms[index].editorId,
                npc->race ? clib_util::editorID::get_editorID(npc->race) : "<null>",
                npc->npcClass ? clib_util::editorID::get_editorID(npc->npcClass) : "<null>",
                npc->voiceType ? clib_util::editorID::get_editorID(npc->voiceType) : "<null>",
                npc->farSkin ? clib_util::editorID::get_editorID(npc->farSkin) : "<null>",
                npc->defaultOutfit ? clib_util::editorID::get_editorID(npc->defaultOutfit) : "<null>",
                static_cast<std::uint32_t>(npc->numHeadParts),
                fmt::ptr(npc->headParts),
                fmt::ptr(npc->faceData),
                fmt::ptr(npc->tintLayers),
                npc->actorData.actorBaseFlags.underlying(),
                npc->actorData.level,
                npc->playerSkills.health,
                npc->playerSkills.magicka,
                npc->playerSkills.stamina);
            if (!npc->race) {
                logger::warn("Spawn aborted for NPC '{}': race is null. Set a race before spawning.", forms[index].editorId);
                return false;
            }
        }

        logger::info("Calling PlaceObjectAtMe for dynamic form '{}' FormID={:08X}.", forms[index].editorId, runtimeForm->GetFormID());
        const auto placed = player->PlaceObjectAtMe(boundObject, true);
        if (!placed) {
            logger::warn("PlaceObjectAtMe failed for dynamic form '{}'.", forms[index].editorId);
            return false;
        }

        logger::info("Spawned dynamic form '{}' at player.", forms[index].editorId);
        return true;
    }

    bool SpawnLydiaForDebug() {
        auto* lydia = LookupLydiaNPC();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!lydia || !player) {
            logger::warn("Could not run Lydia debug spawn: Lydia NPC or player is unavailable.");
            return false;
        }

        LogNPCSnapshot("Lydia debug spawn", lydia);
        auto* boundObject = lydia->As<RE::TESBoundObject>();
        if (!boundObject) {
            logger::warn("Could not run Lydia debug spawn: Lydia is not a TESBoundObject.");
            return false;
        }

        logger::info("Calling PlaceObjectAtMe for Lydia debug NPC FormID={:08X}.", lydia->GetFormID());
        const auto placed = player->PlaceObjectAtMe(boundObject, true);
        if (!placed) {
            logger::warn("PlaceObjectAtMe failed for Lydia debug NPC.");
            return false;
        }

        logger::info("Spawned Lydia debug NPC at player.");
        return true;
    }

    bool HasEditorId(const std::string_view editorId) {
        return std::ranges::any_of(forms, [editorId](const DynamicForms::DynamicForm& form) {
            return form.editorId == editorId;
        });
    }

    bool IsDirty(const std::size_t index) {
        return index < forms.size() && forms[index].dirty;
    }

    bool HasDirtyForms() {
        return std::ranges::any_of(forms, [](const DynamicForms::DynamicForm& form) {
            return form.dirty;
        });
    }

    void ApplyAllForms() {
        bool changed = false;
        bool allApplied = true;
        for (auto& form : forms) {
            const auto oldLocalId = form.localId;
            if (ResolveDPFForm(form)) {
                if (form.localId != oldLocalId) {
                    changed = true;
                }
            } else {
                allApplied = false;
            }
        }

        if (!allApplied) {
            logger::warn("DynamicFormsGenerator load/apply did not finish because one or more forms could not be resolved through DPF. Loaded event will not be dispatched yet.");
            return;
        }

        if (changed) {
            if (!SaveAllForms(false)) {
                logger::warn("DynamicFormsGenerator resolved forms but could not persist updated local IDs. Loaded event will not be dispatched.");
                return;
            }
        } else {
            ListManager::GetSingleton()->PopulateAllLists(true);
        }
        DispatchEvent(LOADED_EVENT, "All", static_cast<float>(forms.size()));
    }
}
