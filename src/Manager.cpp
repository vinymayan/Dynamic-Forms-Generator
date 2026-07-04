#include "Manager.h"

#include "ConditionCatalog.h"
#include "DPFAPI.h"
#include "ListManager.h"
#include "logger.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>
#include <ranges>

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
        case DynamicForms::FormKind::Outfit:
            return "Outfit";
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
        if (normalized == "outfit" || normalized == "otft") {
            return DynamicForms::FormKind::Outfit;
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
        case DynamicForms::FormKind::Outfit:
            return static_cast<std::uint32_t>(RE::FormType::Outfit);
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

        if (source.function == 1) {
            if (auto* functionData = CreateOneValueFunctionDataObject()) {
                functionData->data = source.value;
                entry->functionData = functionData;
            } else {
                logger::warn("Could not allocate BGSEntryPointFunctionDataOneValue for perk '{}'.", perk.GetFormEditorID());
            }
        } else {
            logger::warn("Perk entry function {} is not fully mapped. Creating entry without function data.", source.function);
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

    bool ConfigureNPC(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* npc = tesForm ? tesForm->As<RE::TESNPC>() : nullptr;
        if (!npc) {
            logger::warn("Dynamic form '{}' is not a TESNPC", form.editorId);
            return false;
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

        npc->race = ResolveAs<RE::TESRace>(form.race);
        npc->farSkin = ResolveAs<RE::TESObjectARMO>(form.skin);
        npc->defaultOutfit = ResolveAs<RE::BGSOutfit>(form.defaultOutfit);
        npc->sleepOutfit = ResolveAs<RE::BGSOutfit>(form.sleepOutfit);
        npc->voiceType = ResolveAs<RE::BGSVoiceType>(form.voice);
        npc->npcClass = ResolveAs<RE::TESClass>(form.npcClass);
        npc->combatStyle = ResolveAs<RE::TESCombatStyle>(form.combatStyle);
        npc->giftFilter = ResolveAs<RE::BGSListForm>(form.giftFilter);
        npc->deathItem = ResolveAs<RE::TESLevItem>(form.deathItem);
        npc->defaultPackList = ResolveAs<RE::BGSListForm>(form.defaultPackageList);
        npc->crimeFaction = ResolveAs<RE::TESFaction>(form.crimeFaction);
        npc->soundLevel = static_cast<RE::SOUND_LEVEL>(form.soundLevel);

        if (npc->headRelatedData || !form.hairColor.empty() || !form.faceTexture.empty()) {
            if (!npc->headRelatedData) {
                npc->headRelatedData = new RE::TESNPC::HeadRelatedData();
            }
            npc->headRelatedData->hairColor = ResolveAs<RE::BGSColorForm>(form.hairColor);
            npc->headRelatedData->faceDetails = ResolveAs<RE::BGSTextureSet>(form.faceTexture);
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

        if (npc->headParts) {
            RE::free(npc->headParts);
            npc->headParts = nullptr;
            npc->numHeadParts = 0;
        }
        std::vector<RE::BGSHeadPart*> parts;
        for (const auto& headPartRef : form.headParts) {
            auto* headPart = ResolveAs<RE::BGSHeadPart>(headPartRef);
            if (!headPart) {
                logger::warn("NPC '{}' headpart '{}' could not be resolved.", form.editorId, headPartRef.Display());
                continue;
            }
            parts.push_back(headPart);
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
        } else if (npc->tintLayers) {
            npc->tintLayers->clear();
        }

        if (!npc->faceData) {
            npc->faceData = new RE::TESNPC::FaceData();
        }
        for (std::size_t i = 0; i < form.faceMorphs.size(); ++i) {
            npc->faceData->morphs[i] = form.faceMorphs[i];
        }
        for (std::size_t i = 0; i < form.faceParts.size(); ++i) {
            npc->faceData->parts[i] = form.faceParts[i];
        }

        logger::info("Configured NPC '{}' headParts={} tintLayers={} factions={} perks={} spells={}.",
            form.editorId,
            parts.size(),
            form.tintLayers.size(),
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
        if (form.kind == DynamicForms::FormKind::Outfit) {
            return ConfigureOutfit(tesForm, form);
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

    bool ResolveDPFForm(DynamicForms::DynamicForm& form) {
        auto* api = DPF::GetAPI();
        if (!api) {
            logger::warn("Dynamic Persistent Forms API is not available yet.");
            return false;
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
            return false;
        }

        form.localId = localId;
        if (!ConfigureForm(tesForm, form)) {
            return false;
        }

        logger::info("DPF {} dynamic {} '{}' owner '{}' localId {:06X}.",
            existed ? "recovered" : "created",
            ToString(form.kind),
            form.editorId,
            Manager::DPF_OWNER,
            form.localId);
        return true;
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
        const rapidjson::Value& doc,
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

    bool ReadFormFile(const std::filesystem::path& path, DynamicForms::DynamicForm& out) {
        std::ifstream stream(path);
        if (!stream.is_open()) {
            logger::warn("Could not open dynamic form file: {}", path.string());
            return false;
        }

        rapidjson::IStreamWrapper wrapper(stream);
        rapidjson::Document doc;
        doc.ParseStream(wrapper);
        if (doc.HasParseError() || !doc.IsObject()) {
            logger::warn("Invalid dynamic form JSON: {}", path.string());
            return false;
        }

        if (!doc.HasMember("formKind") || !doc["formKind"].IsString()) {
            return false;
        }

        if (doc.HasMember("editorId") && doc["editorId"].IsString()) {
            out.editorId = doc["editorId"].GetString();
        } else {
            out.editorId = path.stem().string();
        }

        if (out.editorId.empty()) {
            return false;
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
                    path.string(),
                    rawKind,
                    doc["sourceSignature"].GetString(),
                    ToString(*sourceKind));
            }
        } else if (parsedKind) {
            out.kind = *parsedKind;
        } else if (LooksLikeEffectShaderJson(doc)) {
            logger::warn("JSON '{}' has unknown formKind '{}' but looks like an EffectShader; using EffectShader.",
                path.string(),
                rawKind);
            out.kind = DynamicForms::FormKind::EffectShader;
        } else {
            logger::warn("Unknown formKind '{}' in '{}'; skipping file to avoid rewriting it as Global.",
                rawKind,
                path.string());
            return false;
        }

        if (out.kind == DynamicForms::FormKind::Global && LooksLikeEffectShaderJson(doc)) {
            logger::warn("JSON '{}' looks like an EffectShader but formKind is Global; using EffectShader.", path.string());
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
        if (doc.HasMember("outfitPieces") && doc["outfitPieces"].IsArray()) {
            out.outfitPieces.clear();
            for (const auto& piece : doc["outfitPieces"].GetArray()) {
                auto ref = ReadFormRefValue(piece);
                if (!ref.empty()) {
                    out.outfitPieces.push_back(std::move(ref));
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
        ReadUInt8Array18(doc, "skills", out.skills);
        ReadUInt8Array18(doc, "skillOffsets", out.skillOffsets);
        ReadFloatArray19(doc, "faceMorphs", out.faceMorphs);
        ReadIntArray4(doc, "faceParts", out.faceParts);
        ReadFormRefArray(doc, "headParts", out.headParts);
        ReadTintLayers(doc, out.tintLayers);
        ReadRankedFormRefArray(doc, "factions", out.npcFactions);
        ReadRankedFormRefArray(doc, "perks", out.npcPerks);
        ReadFormRefArray(doc, "spells", out.spells);

        return true;
    }
}

namespace Manager {
    std::vector<DynamicForms::DynamicForm>& GetForms() {
        return forms;
    }

    void LoadForms() {
        forms.clear();

        std::error_code ec;
        std::filesystem::create_directories(FORMS_DIR, ec);
        if (ec) {
            logger::warn("Could not create forms directory '{}': {}", FORMS_DIR, ec.message());
            return;
        }

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
                forms.push_back(std::move(form));
            }
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
            AddUInt8Array18(doc, allocator, "skills", form.skills);
            AddUInt8Array18(doc, allocator, "skillOffsets", form.skillOffsets);
            AddFloatArray19(doc, allocator, "faceMorphs", form.faceMorphs);
            AddIntArray4(doc, allocator, "faceParts", form.faceParts);
            AddFormRefArray(doc, allocator, "headParts", form.headParts);
            AddTintLayers(doc, allocator, form.tintLayers);
            AddRankedFormRefArray(doc, allocator, "factions", form.npcFactions);
            AddRankedFormRefArray(doc, allocator, "perks", form.npcPerks);
            AddFormRefArray(doc, allocator, "spells", form.spells);
        }
        if (form.localId != 0) {
            doc.AddMember("localId", form.localId, allocator);
        }

        std::ofstream stream(FormPath(form.editorId));
        if (!stream.is_open()) {
            logger::warn("Could not write dynamic form JSON for '{}'", form.editorId);
            return false;
        }

        rapidjson::OStreamWrapper wrapper(stream);
        rapidjson::PrettyWriter writer(wrapper);
        doc.Accept(writer);
        return true;
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
                DispatchEvent(UPDATED_EVENT, forms[index].editorId, static_cast<float>(forms[index].localId));
            }
        }
        return saved;
    }

    bool SaveAllForms(const bool dispatchUpdate) {
        bool saved = true;
        for (std::size_t i = 0; i < forms.size(); ++i) {
            const bool wasDirty = forms[i].dirty;
            if (ResolveDPFForm(forms[i]) && SaveForm(forms[i])) {
                forms[i].dirty = dispatchUpdate ? false : wasDirty;
            } else {
                saved = false;
            }
        }
        if (saved) {
            ListManager::GetSingleton()->PopulateAllLists(true);
            if (dispatchUpdate) {
                DispatchEvent(UPDATED_EVENT, "All", static_cast<float>(forms.size()));
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
            DispatchEvent(UPDATED_EVENT, forms.back().editorId, static_cast<float>(forms.back().localId));
        }
        return saved;
    }

    bool UpdateForm(const std::size_t index, const DynamicForms::DynamicForm& form) {
        if (index >= forms.size() || form.editorId.empty() || forms[index].editorId != form.editorId) {
            return false;
        }

        auto updatedForm = form;
        if (!ResolveDPFForm(updatedForm)) {
            return false;
        }

        updatedForm.dirty = true;
        forms[index] = std::move(updatedForm);
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

        std::error_code ec;
        std::filesystem::remove(FormPath(form.editorId), ec);
        if (ec) {
            logger::warn("Could not delete dynamic form JSON '{}': {}", FormPath(form.editorId).string(), ec.message());
            return false;
        }

        forms.erase(forms.begin() + static_cast<std::ptrdiff_t>(index));
        ListManager::GetSingleton()->PopulateAllLists(true);
        DispatchEvent(UPDATED_EVENT, form.editorId, static_cast<float>(form.localId));
        logger::info("Deleted dynamic form '{}' localId {:06X}.", form.editorId, form.localId);
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
        for (auto& form : forms) {
            const auto oldLocalId = form.localId;
            if (ResolveDPFForm(form)) {
                if (form.localId != oldLocalId) {
                    changed = true;
                }
            }
        }

        if (changed) {
            SaveAllForms(false);
        }
        DispatchEvent(LOADED_EVENT, "All", static_cast<float>(forms.size()));
    }
}
