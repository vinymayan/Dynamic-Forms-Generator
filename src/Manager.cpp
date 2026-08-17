#include "Manager.h"

#include "ConditionCatalog.h"
#include "DFGAPI.h"
#include "DPFAPI.h"
#include "ListManager.h"
#include "logger.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <detours/detours.h>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <ranges>
#include <set>
#include <sqlite3.h>
#include <stdexcept>
#include <unordered_map>

namespace Manager {
    bool BuildFormDocument(const DynamicForms::DynamicForm& form, rapidjson::Document& doc);
}

namespace {
    std::vector<DynamicForms::DynamicForm> forms;
    std::atomic_bool apiReady{ false };
    std::atomic_bool responseListHookInstallationAttempted{ false };
    std::atomic_bool responseListHookInstalled{ false };
    bool questPerkEntryLayoutValid{ true };
    std::unordered_map<RE::TESTopicInfo*, RE::TESTopicInfo::TESResponse*> dynamicDialogueResponses;
    std::unordered_map<std::string, std::vector<RE::TESForm*>> externalFormsByEditorId;
    RE::BSSoundHandle soundPreviewHandle;
    std::string soundPreviewEditorId;
    using GetResponseListFn = RE::TESTopicInfo::TESResponseList* (*)(RE::TESTopicInfo*, RE::TESTopicInfo::TESResponseList*);
    GetResponseListFn originalGetResponseList{ nullptr };
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

    std::string NormalizeEditorId(const std::string_view value) {
        std::string normalized;
        normalized.reserve(value.size());
        for (const auto ch : value) {
            normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        return normalized;
    }

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
        case DynamicForms::FormKind::Spell:
            return "Spell";
        case DynamicForms::FormKind::MagicEffect:
            return "MagicEffect";
        case DynamicForms::FormKind::Enchantment:
            return "Enchantment";
        case DynamicForms::FormKind::Scroll:
            return "Scroll";
        case DynamicForms::FormKind::Projectile:
            return "Projectile";
        case DynamicForms::FormKind::TextureSet:
            return "TextureSet";
        case DynamicForms::FormKind::Hazard:
            return "Hazard";
        case DynamicForms::FormKind::ImpactData:
            return "ImpactData";
        case DynamicForms::FormKind::ReferenceEffect:
            return "ReferenceEffect";
        case DynamicForms::FormKind::DualCastData:
            return "DualCastData";
        case DynamicForms::FormKind::Static:
            return "Static";
        case DynamicForms::FormKind::MovableStatic:
            return "MovableStatic";
        case DynamicForms::FormKind::Door:
            return "Door";
        case DynamicForms::FormKind::CombatStyle:
            return "CombatStyle";
        case DynamicForms::FormKind::SoundCategory:
            return "SoundCategory";
        case DynamicForms::FormKind::Class:
            return "Class";
        case DynamicForms::FormKind::Flora:
            return "Flora";
        case DynamicForms::FormKind::Tree:
            return "Tree";
        case DynamicForms::FormKind::ConstructibleObject:
            return "ConstructibleObject";
        case DynamicForms::FormKind::Container:
            return "Container";
        case DynamicForms::FormKind::ImpactDataSet: return "ImpactDataSet";
        case DynamicForms::FormKind::CollisionLayer: return "CollisionLayer";
        case DynamicForms::FormKind::Footstep: return "Footstep";
        case DynamicForms::FormKind::FootstepSet: return "FootstepSet";
        case DynamicForms::FormKind::ReverbParameters: return "ReverbParameters";
        case DynamicForms::FormKind::AcousticSpace: return "AcousticSpace";
        case DynamicForms::FormKind::Apparatus: return "Apparatus";
        case DynamicForms::FormKind::StaticCollection: return "StaticCollection";
        case DynamicForms::FormKind::Grass: return "Grass";
        case DynamicForms::FormKind::IdleMarker: return "IdleMarker";
        case DynamicForms::FormKind::EncounterZone: return "EncounterZone";
        case DynamicForms::FormKind::Relationship: return "Relationship";
        case DynamicForms::FormKind::AssociationType: return "AssociationType";
        case DynamicForms::FormKind::MovementType: return "MovementType";
        case DynamicForms::FormKind::WordOfPower: return "WordOfPower";
        case DynamicForms::FormKind::Water: return "Water";
        case DynamicForms::FormKind::ImageSpace: return "ImageSpace";
        case DynamicForms::FormKind::LightingTemplate: return "LightingTemplate";
        case DynamicForms::FormKind::Shout: return "Shout";
        case DynamicForms::FormKind::LeveledItem: return "LeveledItem";
        case DynamicForms::FormKind::LeveledNPC: return "LeveledNPC";
        case DynamicForms::FormKind::LeveledSpell: return "LeveledSpell";
        case DynamicForms::FormKind::LocationRefType: return "LocationRefType";
        case DynamicForms::FormKind::Action: return "Action";
        case DynamicForms::FormKind::MenuIcon: return "MenuIcon";
        case DynamicForms::FormKind::Eyes: return "Eyes";
        case DynamicForms::FormKind::Note: return "Note";
        case DynamicForms::FormKind::AnimatedObject: return "AnimatedObject";
        case DynamicForms::FormKind::LoadScreen: return "LoadScreen";
        case DynamicForms::FormKind::ShaderParticleGeometry: return "ShaderParticleGeometry";
        case DynamicForms::FormKind::AddonNode: return "AddonNode";
        case DynamicForms::FormKind::Faction: return "Faction";
        case DynamicForms::FormKind::IdleAnimation: return "IdleAnimation";
        case DynamicForms::FormKind::MaterialObject: return "MaterialObject";
        case DynamicForms::FormKind::Message: return "Message";
        case DynamicForms::FormKind::LandTexture: return "LandTexture";
        case DynamicForms::FormKind::SoundOutputModel: return "SoundOutputModel";
        case DynamicForms::FormKind::LensFlare: return "LensFlare";
        case DynamicForms::FormKind::Debris: return "Debris";
        case DynamicForms::FormKind::ImageSpaceModifier: return "ImageSpaceModifier";
        case DynamicForms::FormKind::CameraShot: return "CameraShot";
        case DynamicForms::FormKind::CameraPath: return "CameraPath";
        case DynamicForms::FormKind::TalkingActivator: return "TalkingActivator";
        case DynamicForms::FormKind::Furniture: return "Furniture";
        case DynamicForms::FormKind::Weather: return "Weather";
        case DynamicForms::FormKind::Climate: return "Climate";
        case DynamicForms::FormKind::Location: return "Location";
        case DynamicForms::FormKind::MusicType: return "MusicType";
        case DynamicForms::FormKind::MusicTrack: return "MusicTrack";
        case DynamicForms::FormKind::BodyPartData: return "BodyPartData";
        case DynamicForms::FormKind::VolumetricLighting: return "VolumetricLighting";
        case DynamicForms::FormKind::Sound: return "Sound";
        case DynamicForms::FormKind::ActorValueInfo: return "ActorValueInfo";
        case DynamicForms::FormKind::DialogueBranch: return "DialogueBranch";
        case DynamicForms::FormKind::DialogueTopic: return "DialogueTopic";
        case DynamicForms::FormKind::DialogueInfo: return "DialogueInfo";
        case DynamicForms::FormKind::Quest: return "Quest";
        case DynamicForms::FormKind::Scene: return "Scene";
        case DynamicForms::FormKind::StoryManagerBranchNode: return "StoryManagerBranchNode";
        case DynamicForms::FormKind::StoryManagerQuestNode: return "StoryManagerQuestNode";
        case DynamicForms::FormKind::StoryManagerEventNode: return "StoryManagerEventNode";
        case DynamicForms::FormKind::Package: return "Package";
        case DynamicForms::FormKind::Race: return "Race";
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
        if (normalized == "spell" || normalized == "spel") {
            return DynamicForms::FormKind::Spell;
        }
        if (normalized == "magiceffect" || normalized == "effectsetting" || normalized == "mgef") {
            return DynamicForms::FormKind::MagicEffect;
        }
        if (normalized == "enchantment" || normalized == "ench") {
            return DynamicForms::FormKind::Enchantment;
        }
        if (normalized == "scroll" || normalized == "scrl") {
            return DynamicForms::FormKind::Scroll;
        }
        if (normalized == "projectile" || normalized == "proj") {
            return DynamicForms::FormKind::Projectile;
        }
        if (normalized == "textureset" || normalized == "txst") return DynamicForms::FormKind::TextureSet;
        if (normalized == "hazard" || normalized == "hazd") return DynamicForms::FormKind::Hazard;
        if (normalized == "impactdata" || normalized == "ipct") return DynamicForms::FormKind::ImpactData;
        if (normalized == "referenceeffect" || normalized == "rfct") return DynamicForms::FormKind::ReferenceEffect;
        if (normalized == "dualcastdata" || normalized == "dual") return DynamicForms::FormKind::DualCastData;
        if (normalized == "static" || normalized == "stat") return DynamicForms::FormKind::Static;
        if (normalized == "movablestatic" || normalized == "mstt") return DynamicForms::FormKind::MovableStatic;
        if (normalized == "door") return DynamicForms::FormKind::Door;
        if (normalized == "combatstyle" || normalized == "csty") return DynamicForms::FormKind::CombatStyle;
        if (normalized == "soundcategory" || normalized == "snct") return DynamicForms::FormKind::SoundCategory;
        if (normalized == "class" || normalized == "clas") return DynamicForms::FormKind::Class;
        if (normalized == "flora" || normalized == "flor") return DynamicForms::FormKind::Flora;
        if (normalized == "tree") return DynamicForms::FormKind::Tree;
        if (normalized == "constructibleobject" || normalized == "constructible" || normalized == "cobj" || normalized == "recipe") return DynamicForms::FormKind::ConstructibleObject;
        if (normalized == "container" || normalized == "cont") return DynamicForms::FormKind::Container;
        if (normalized == "impactdataset" || normalized == "ipds") return DynamicForms::FormKind::ImpactDataSet;
        if (normalized == "collisionlayer" || normalized == "coll") return DynamicForms::FormKind::CollisionLayer;
        if (normalized == "footstep" || normalized == "fstp") return DynamicForms::FormKind::Footstep;
        if (normalized == "footstepset" || normalized == "fsts") return DynamicForms::FormKind::FootstepSet;
        if (normalized == "reverbparameters" || normalized == "reverbparam" || normalized == "revb") return DynamicForms::FormKind::ReverbParameters;
        if (normalized == "acousticspace" || normalized == "aspc") return DynamicForms::FormKind::AcousticSpace;
        if (normalized == "apparatus" || normalized == "appa") return DynamicForms::FormKind::Apparatus;
        if (normalized == "staticcollection" || normalized == "scol") return DynamicForms::FormKind::StaticCollection;
        if (normalized == "grass" || normalized == "gras") return DynamicForms::FormKind::Grass;
        if (normalized == "idlemarker" || normalized == "idlm") return DynamicForms::FormKind::IdleMarker;
        if (normalized == "encounterzone" || normalized == "eczn") return DynamicForms::FormKind::EncounterZone;
        if (normalized == "relationship" || normalized == "rela") return DynamicForms::FormKind::Relationship;
        if (normalized == "associationtype" || normalized == "astp") return DynamicForms::FormKind::AssociationType;
        if (normalized == "movementtype" || normalized == "movt") return DynamicForms::FormKind::MovementType;
        if (normalized == "wordofpower" || normalized == "woop") return DynamicForms::FormKind::WordOfPower;
        if (normalized == "water" || normalized == "watr") return DynamicForms::FormKind::Water;
        if (normalized == "imagespace" || normalized == "imgs") return DynamicForms::FormKind::ImageSpace;
        if (normalized == "lightingtemplate" || normalized == "lgtm") return DynamicForms::FormKind::LightingTemplate;
        if (normalized == "shout" || normalized == "shou") return DynamicForms::FormKind::Shout;
        if (normalized == "leveleditem" || normalized == "lvli") return DynamicForms::FormKind::LeveledItem;
        if (normalized == "levelednpc" || normalized == "leveledcharacter" || normalized == "lvln") return DynamicForms::FormKind::LeveledNPC;
        if (normalized == "leveledspell" || normalized == "lvsp") return DynamicForms::FormKind::LeveledSpell;
        if (normalized == "locationreftype" || normalized == "lcrt") return DynamicForms::FormKind::LocationRefType;
        if (normalized == "action" || normalized == "aact") return DynamicForms::FormKind::Action;
        if (normalized == "menuicon" || normalized == "micn") return DynamicForms::FormKind::MenuIcon;
        if (normalized == "eyes") return DynamicForms::FormKind::Eyes;
        if (normalized == "note") return DynamicForms::FormKind::Note;
        if (normalized == "animatedobject" || normalized == "anio") return DynamicForms::FormKind::AnimatedObject;
        if (normalized == "loadscreen" || normalized == "lscr") return DynamicForms::FormKind::LoadScreen;
        if (normalized == "shaderparticlegeometry" || normalized == "spgd") return DynamicForms::FormKind::ShaderParticleGeometry;
        if (normalized == "addonnode" || normalized == "addn") return DynamicForms::FormKind::AddonNode;
        if (normalized == "faction" || normalized == "fact") return DynamicForms::FormKind::Faction;
        if (normalized == "idleanimation" || normalized == "idle") return DynamicForms::FormKind::IdleAnimation;
        if (normalized == "materialobject" || normalized == "mato") return DynamicForms::FormKind::MaterialObject;
        if (normalized == "message" || normalized == "mesg") return DynamicForms::FormKind::Message;
        if (normalized == "landtexture" || normalized == "ltex") return DynamicForms::FormKind::LandTexture;
        if (normalized == "soundoutputmodel" || normalized == "sopm") return DynamicForms::FormKind::SoundOutputModel;
        if (normalized == "lensflare" || normalized == "lens") return DynamicForms::FormKind::LensFlare;
        if (normalized == "debris" || normalized == "debr") return DynamicForms::FormKind::Debris;
        if (normalized == "imagespacemodifier" || normalized == "imad") return DynamicForms::FormKind::ImageSpaceModifier;
        if (normalized == "camerashot" || normalized == "cams") return DynamicForms::FormKind::CameraShot;
        if (normalized == "camerapath" || normalized == "cpth") return DynamicForms::FormKind::CameraPath;
        if (normalized == "talkingactivator" || normalized == "tact") return DynamicForms::FormKind::TalkingActivator;
        if (normalized == "furniture" || normalized == "furn") return DynamicForms::FormKind::Furniture;
        if (normalized == "weather" || normalized == "wthr") return DynamicForms::FormKind::Weather;
        if (normalized == "climate" || normalized == "clmt") return DynamicForms::FormKind::Climate;
        if (normalized == "location" || normalized == "lctn") return DynamicForms::FormKind::Location;
        if (normalized == "musictype" || normalized == "musc") return DynamicForms::FormKind::MusicType;
        if (normalized == "musictrack" || normalized == "must") return DynamicForms::FormKind::MusicTrack;
        if (normalized == "bodypartdata" || normalized == "bptd") return DynamicForms::FormKind::BodyPartData;
        if (normalized == "volumetriclighting" || normalized == "voli") return DynamicForms::FormKind::VolumetricLighting;
        if (normalized == "sound" || normalized == "soun") return DynamicForms::FormKind::Sound;
        if (normalized == "actorvalueinfo" || normalized == "avif") return DynamicForms::FormKind::ActorValueInfo;
        if (normalized == "dialoguebranch" || normalized == "dlbr") return DynamicForms::FormKind::DialogueBranch;
        if (normalized == "dialoguetopic" || normalized == "dialogue" || normalized == "dial") return DynamicForms::FormKind::DialogueTopic;
        if (normalized == "dialogueinfo" || normalized == "topicinfo" || normalized == "info") return DynamicForms::FormKind::DialogueInfo;
        if (normalized == "quest" || normalized == "qust") return DynamicForms::FormKind::Quest;
        if (normalized == "scene" || normalized == "scen") return DynamicForms::FormKind::Scene;
        if (normalized == "storymanagerbranchnode" || normalized == "smbn") return DynamicForms::FormKind::StoryManagerBranchNode;
        if (normalized == "storymanagerquestnode" || normalized == "smqn") return DynamicForms::FormKind::StoryManagerQuestNode;
        if (normalized == "storymanagereventnode" || normalized == "smen") return DynamicForms::FormKind::StoryManagerEventNode;
        if (normalized == "package" || normalized == "pack") return DynamicForms::FormKind::Package;
        if (normalized == "race") return DynamicForms::FormKind::Race;
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

    const char* ListTypeName(const DynamicForms::FormKind kind) {
        switch (kind) {
        case DynamicForms::FormKind::Misc: return "MiscItem";
        case DynamicForms::FormKind::IdleAnimation: return "Idle";
        default: {
            static thread_local std::string name;
            name = ToString(kind);
            return name.c_str();
        }
        }
    }

    void AddFormRef(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const char* key, const DynamicForms::FormRef& ref) {
        if (ref.empty()) return;
        rapidjson::Value value(rapidjson::kObjectType);
        if (!ref.editorID.empty()) value.AddMember("editorID", rapidjson::Value(ref.editorID.c_str(), allocator), allocator);
        if (!ref.formID.empty()) value.AddMember("formID", rapidjson::Value(ref.formID.c_str(), allocator), allocator);
        object.AddMember(rapidjson::Value(key, allocator), value, allocator);
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
    std::int32_t ReadInt32(const rapidjson::Value& doc, const char* key, std::int32_t fallback);
    float ReadFloat(const rapidjson::Value& doc, const char* key, float fallback);
    DynamicForms::PerkCondition ReadCondition(const rapidjson::Value& value);
    void WriteCondition(rapidjson::Value& array, rapidjson::Document::AllocatorType& allocator, const DynamicForms::PerkCondition& condition);
    void ApplyConditions(RE::TESCondition& target, const std::vector<DynamicForms::PerkCondition>& conditions);

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
            if (item.HasMember("conditions") && item["conditions"].IsArray()) {
                for (const auto& condition : item["conditions"].GetArray()) {
                    entry.conditions.push_back(ReadCondition(condition));
                }
            }
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
        case DynamicForms::FormKind::Spell:
            return "SPEL";
        case DynamicForms::FormKind::MagicEffect:
            return "MGEF";
        case DynamicForms::FormKind::Enchantment:
            return "ENCH";
        case DynamicForms::FormKind::Scroll:
            return "SCRL";
        case DynamicForms::FormKind::Projectile:
            return "PROJ";
        case DynamicForms::FormKind::TextureSet: return "TXST";
        case DynamicForms::FormKind::Hazard: return "HAZD";
        case DynamicForms::FormKind::ImpactData: return "IPCT";
        case DynamicForms::FormKind::ReferenceEffect: return "RFCT";
        case DynamicForms::FormKind::DualCastData: return "DUAL";
        case DynamicForms::FormKind::Static: return "STAT";
        case DynamicForms::FormKind::MovableStatic: return "MSTT";
        case DynamicForms::FormKind::Door: return "DOOR";
        case DynamicForms::FormKind::CombatStyle: return "CSTY";
        case DynamicForms::FormKind::SoundCategory: return "SNCT";
        case DynamicForms::FormKind::Class: return "CLAS";
        case DynamicForms::FormKind::Flora: return "FLOR";
        case DynamicForms::FormKind::Tree: return "TREE";
        case DynamicForms::FormKind::ConstructibleObject: return "COBJ";
        case DynamicForms::FormKind::Container: return "CONT";
        case DynamicForms::FormKind::ImpactDataSet: return "IPDS";
        case DynamicForms::FormKind::CollisionLayer: return "COLL";
        case DynamicForms::FormKind::Footstep: return "FSTP";
        case DynamicForms::FormKind::FootstepSet: return "FSTS";
        case DynamicForms::FormKind::ReverbParameters: return "REVB";
        case DynamicForms::FormKind::AcousticSpace: return "ASPC";
        case DynamicForms::FormKind::Apparatus: return "APPA";
        case DynamicForms::FormKind::StaticCollection: return "SCOL";
        case DynamicForms::FormKind::Grass: return "GRAS";
        case DynamicForms::FormKind::IdleMarker: return "IDLM";
        case DynamicForms::FormKind::EncounterZone: return "ECZN";
        case DynamicForms::FormKind::Relationship: return "RELA";
        case DynamicForms::FormKind::AssociationType: return "ASTP";
        case DynamicForms::FormKind::MovementType: return "MOVT";
        case DynamicForms::FormKind::WordOfPower: return "WOOP";
        case DynamicForms::FormKind::Water: return "WATR";
        case DynamicForms::FormKind::ImageSpace: return "IMGS";
        case DynamicForms::FormKind::LightingTemplate: return "LGTM";
        case DynamicForms::FormKind::Shout: return "SHOU";
        case DynamicForms::FormKind::LeveledItem: return "LVLI";
        case DynamicForms::FormKind::LeveledNPC: return "LVLN";
        case DynamicForms::FormKind::LeveledSpell: return "LVSP";
        case DynamicForms::FormKind::LocationRefType: return "LCRT";
        case DynamicForms::FormKind::Action: return "AACT";
        case DynamicForms::FormKind::MenuIcon: return "MICN";
        case DynamicForms::FormKind::Eyes: return "EYES";
        case DynamicForms::FormKind::Note: return "NOTE";
        case DynamicForms::FormKind::AnimatedObject: return "ANIO";
        case DynamicForms::FormKind::LoadScreen: return "LSCR";
        case DynamicForms::FormKind::ShaderParticleGeometry: return "SPGD";
        case DynamicForms::FormKind::AddonNode: return "ADDN";
        case DynamicForms::FormKind::Faction: return "FACT";
        case DynamicForms::FormKind::IdleAnimation: return "IDLE";
        case DynamicForms::FormKind::MaterialObject: return "MATO";
        case DynamicForms::FormKind::Message: return "MESG";
        case DynamicForms::FormKind::LandTexture: return "LTEX";
        case DynamicForms::FormKind::SoundOutputModel: return "SOPM";
        case DynamicForms::FormKind::LensFlare: return "LENS";
        case DynamicForms::FormKind::Debris: return "DEBR";
        case DynamicForms::FormKind::ImageSpaceModifier: return "IMAD";
        case DynamicForms::FormKind::CameraShot: return "CAMS";
        case DynamicForms::FormKind::CameraPath: return "CPTH";
        case DynamicForms::FormKind::TalkingActivator: return "TACT";
        case DynamicForms::FormKind::Furniture: return "FURN";
        case DynamicForms::FormKind::Weather: return "WTHR";
        case DynamicForms::FormKind::Climate: return "CLMT";
        case DynamicForms::FormKind::Location: return "LCTN";
        case DynamicForms::FormKind::MusicType: return "MUSC";
        case DynamicForms::FormKind::MusicTrack: return "MUST";
        case DynamicForms::FormKind::BodyPartData: return "BPTD";
        case DynamicForms::FormKind::VolumetricLighting: return "VOLI";
        case DynamicForms::FormKind::Sound: return "SOUN";
        case DynamicForms::FormKind::ActorValueInfo: return "AVIF";
        case DynamicForms::FormKind::DialogueBranch: return "DLBR";
        case DynamicForms::FormKind::DialogueTopic: return "DIAL";
        case DynamicForms::FormKind::DialogueInfo: return "INFO";
        case DynamicForms::FormKind::Quest: return "QUST";
        case DynamicForms::FormKind::Scene: return "SCEN";
        case DynamicForms::FormKind::StoryManagerBranchNode: return "SMBN";
        case DynamicForms::FormKind::StoryManagerQuestNode: return "SMQN";
        case DynamicForms::FormKind::StoryManagerEventNode: return "SMEN";
        case DynamicForms::FormKind::Package: return "PACK";
        case DynamicForms::FormKind::Race: return "RACE";
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
            if (!value.conditions.empty()) {
                rapidjson::Value conditions(rapidjson::kArrayType);
                for (const auto& condition : value.conditions) {
                    WriteCondition(conditions, allocator, condition);
                }
                item.AddMember("conditions", conditions, allocator);
            }
            array.PushBack(item, allocator);
        }
        object.AddMember(rapidjson::Value(key, allocator), array, allocator);
    }

    template <class T, std::size_t N>
    void ReadNumberArray(const rapidjson::Value& object, const char* key, std::array<T, N>& values) {
        if (!object.HasMember(key) || !object[key].IsArray()) return;
        const auto array = object[key].GetArray();
        const auto count = std::min<std::size_t>(N, array.Size());
        for (std::size_t i = 0; i < count; ++i) {
            if (array[static_cast<rapidjson::SizeType>(i)].IsNumber()) {
                values[i] = static_cast<T>(array[static_cast<rapidjson::SizeType>(i)].GetDouble());
            }
        }
    }

    template <class T, std::size_t N>
    void AddNumberArray(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const char* key, const std::array<T, N>& values) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto value : values) array.PushBack(value, allocator);
        object.AddMember(rapidjson::Value(key, allocator), array, allocator);
    }

    template <class T>
    void AddNumberVector(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const char* key, const std::vector<T>& values) {
        rapidjson::Value array(rapidjson::kArrayType); for (const auto value : values) array.PushBack(value, allocator); object.AddMember(rapidjson::Value(key, allocator), array, allocator);
    }

    template <std::size_t N>
    void ReadStringArray(const rapidjson::Value& object, const char* key, std::array<std::string, N>& values) {
        if (!object.HasMember(key) || !object[key].IsArray()) return;
        const auto array = object[key].GetArray();
        const auto count = std::min<std::size_t>(N, array.Size());
        for (std::size_t i = 0; i < count; ++i) {
            const auto& value = array[static_cast<rapidjson::SizeType>(i)];
            if (value.IsString()) values[i] = value.GetString();
        }
    }

    template <std::size_t N>
    void AddStringArray(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const char* key, const std::array<std::string, N>& values) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto& value : values) array.PushBack(rapidjson::Value(value.c_str(), allocator), allocator);
        object.AddMember(rapidjson::Value(key, allocator), array, allocator);
    }

    void ReadContainerEntries(const rapidjson::Value& object, const char* key, std::vector<DynamicForms::ContainerEntry>& values) {
        if (!object.HasMember(key) || !object[key].IsArray()) return;
        values.clear();
        for (const auto& entry : object[key].GetArray()) {
            if (!entry.IsObject() || !entry.HasMember("item")) continue;
            DynamicForms::ContainerEntry component;
            component.item = ReadFormRefValue(entry["item"]);
            component.count = std::max(1, ReadInt32(entry, "count", component.count));
            if (entry.HasMember("owner")) component.owner = ReadFormRefValue(entry["owner"]);
            if (entry.HasMember("conditionGlobal")) component.conditionGlobal = ReadFormRefValue(entry["conditionGlobal"]);
            component.requiredRank = ReadInt32(entry, "requiredRank", component.requiredRank);
            component.healthMult = ReadFloat(entry, "healthMult", component.healthMult);
            if (!component.item.empty()) values.push_back(std::move(component));
        }
    }

    void AddContainerEntries(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const char* key, const std::vector<DynamicForms::ContainerEntry>& values) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto& component : values) {
            if (component.item.empty()) continue;
            rapidjson::Value entry(rapidjson::kObjectType);
            rapidjson::Value item(rapidjson::kObjectType);
            if (!component.item.editorID.empty()) item.AddMember("editorID", rapidjson::Value(component.item.editorID.c_str(), allocator), allocator);
            if (!component.item.formID.empty()) item.AddMember("formID", rapidjson::Value(component.item.formID.c_str(), allocator), allocator);
            entry.AddMember("item", item, allocator);
            entry.AddMember("count", std::max(1, component.count), allocator);
            const auto addEntryRef = [&](const char* name, const DynamicForms::FormRef& ref) {
                if (ref.empty()) return;
                rapidjson::Value value(rapidjson::kObjectType);
                if (!ref.editorID.empty()) value.AddMember("editorID", rapidjson::Value(ref.editorID.c_str(), allocator), allocator);
                if (!ref.formID.empty()) value.AddMember("formID", rapidjson::Value(ref.formID.c_str(), allocator), allocator);
                entry.AddMember(rapidjson::Value(name, allocator), value, allocator);
            };
            addEntryRef("owner", component.owner);
            addEntryRef("conditionGlobal", component.conditionGlobal);
            entry.AddMember("requiredRank", component.requiredRank, allocator);
            entry.AddMember("healthMult", component.healthMult, allocator);
            array.PushBack(entry, allocator);
        }
        object.AddMember(rapidjson::Value(key, allocator), array, allocator);
    }

    void ReadFormRefPairs(const rapidjson::Value& object, const char* key, std::vector<DynamicForms::FormRefPair>& values) {
        if (!object.HasMember(key) || !object[key].IsArray()) return;
        values.clear();
        for (const auto& entry : object[key].GetArray()) {
            if (!entry.IsObject() || !entry.HasMember("key") || !entry.HasMember("value")) continue;
            DynamicForms::FormRefPair pair{ ReadFormRefValue(entry["key"]), ReadFormRefValue(entry["value"]) };
            if (!pair.key.empty() && !pair.value.empty()) values.push_back(std::move(pair));
        }
    }

    void AddFormRefPairs(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const char* key, const std::vector<DynamicForms::FormRefPair>& values) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto& pair : values) {
            if (pair.key.empty() || pair.value.empty()) continue;
            rapidjson::Value entry(rapidjson::kObjectType);
            const auto makeRef = [&](const DynamicForms::FormRef& ref) {
                rapidjson::Value value(rapidjson::kObjectType);
                if (!ref.editorID.empty()) value.AddMember("editorID", rapidjson::Value(ref.editorID.c_str(), allocator), allocator);
                if (!ref.formID.empty()) value.AddMember("formID", rapidjson::Value(ref.formID.c_str(), allocator), allocator);
                return value;
            };
            entry.AddMember("key", makeRef(pair.key), allocator);
            entry.AddMember("value", makeRef(pair.value), allocator);
            array.PushBack(entry, allocator);
        }
        object.AddMember(rapidjson::Value(key, allocator), array, allocator);
    }

    void ReadLeveledEntries(const rapidjson::Value& object, const char* key, std::vector<DynamicForms::LeveledEntry>& values) {
        if (!object.HasMember(key) || !object[key].IsArray()) return;
        values.clear();
        for (const auto& entry : object[key].GetArray()) {
            if (!entry.IsObject() || !entry.HasMember("form")) continue;
            DynamicForms::LeveledEntry value;
            value.form = ReadFormRefValue(entry["form"]);
            value.level = static_cast<std::uint16_t>(std::clamp(ReadUInt32(entry, "level", value.level), 1u, 65535u));
            value.count = static_cast<std::uint16_t>(std::clamp(ReadUInt32(entry, "count", value.count), 1u, 65535u));
            if (entry.HasMember("owner")) value.owner = ReadFormRefValue(entry["owner"]);
            if (entry.HasMember("conditionGlobal")) value.conditionGlobal = ReadFormRefValue(entry["conditionGlobal"]);
            value.requiredRank = ReadInt32(entry, "requiredRank", value.requiredRank);
            value.healthMult = ReadFloat(entry, "healthMult", value.healthMult);
            if (!value.form.empty()) values.push_back(std::move(value));
        }
    }

    void AddLeveledEntries(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const char* key, const std::vector<DynamicForms::LeveledEntry>& values) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto& value : values) {
            if (value.form.empty()) continue;
            rapidjson::Value entry(rapidjson::kObjectType);
            const auto makeRef = [&](const DynamicForms::FormRef& ref) {
                rapidjson::Value result(rapidjson::kObjectType);
                if (!ref.editorID.empty()) result.AddMember("editorID", rapidjson::Value(ref.editorID.c_str(), allocator), allocator);
                if (!ref.formID.empty()) result.AddMember("formID", rapidjson::Value(ref.formID.c_str(), allocator), allocator);
                return result;
            };
            entry.AddMember("form", makeRef(value.form), allocator);
            entry.AddMember("level", value.level, allocator); entry.AddMember("count", value.count, allocator);
            if (!value.owner.empty()) entry.AddMember("owner", makeRef(value.owner), allocator);
            if (!value.conditionGlobal.empty()) entry.AddMember("conditionGlobal", makeRef(value.conditionGlobal), allocator);
            entry.AddMember("requiredRank", value.requiredRank, allocator); entry.AddMember("healthMult", value.healthMult, allocator);
            array.PushBack(entry, allocator);
        }
        object.AddMember(rapidjson::Value(key, allocator), array, allocator);
    }

    void ReadFactionReactions(const rapidjson::Value& object, std::vector<DynamicForms::FactionReaction>& values) {
        if (!object.HasMember("factionReactions") || !object["factionReactions"].IsArray()) return;
        values.clear();
        for (const auto& item : object["factionReactions"].GetArray()) {
            if (!item.IsObject() || !item.HasMember("faction")) continue;
            DynamicForms::FactionReaction value;
            value.faction = ReadFormRefValue(item["faction"]);
            value.reaction = ReadInt32(item, "reaction", value.reaction);
            value.fightReaction = ReadUInt32(item, "fightReaction", value.fightReaction);
            if (!value.faction.empty()) values.push_back(std::move(value));
        }
    }

    void AddFactionReactions(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const std::vector<DynamicForms::FactionReaction>& values) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto& value : values) {
            if (value.faction.empty()) continue;
            rapidjson::Value item(rapidjson::kObjectType), faction(rapidjson::kObjectType);
            if (!value.faction.editorID.empty()) faction.AddMember("editorID", rapidjson::Value(value.faction.editorID.c_str(), allocator), allocator);
            if (!value.faction.formID.empty()) faction.AddMember("formID", rapidjson::Value(value.faction.formID.c_str(), allocator), allocator);
            item.AddMember("faction", faction, allocator);
            item.AddMember("reaction", value.reaction, allocator);
            item.AddMember("fightReaction", value.fightReaction, allocator);
            array.PushBack(item, allocator);
        }
        object.AddMember("factionReactions", array, allocator);
    }

    void ReadFactionRanks(const rapidjson::Value& object, std::vector<DynamicForms::FactionRank>& values) {
        if (!object.HasMember("factionRanks") || !object["factionRanks"].IsArray()) return;
        values.clear();
        for (const auto& item : object["factionRanks"].GetArray()) {
            if (!item.IsObject()) continue;
            DynamicForms::FactionRank value;
            if (item.HasMember("maleTitle") && item["maleTitle"].IsString()) value.maleTitle = item["maleTitle"].GetString();
            if (item.HasMember("femaleTitle") && item["femaleTitle"].IsString()) value.femaleTitle = item["femaleTitle"].GetString();
            if (item.HasMember("insigniaPath") && item["insigniaPath"].IsString()) value.insigniaPath = item["insigniaPath"].GetString();
            values.push_back(std::move(value));
        }
    }

    void AddFactionRanks(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const std::vector<DynamicForms::FactionRank>& values) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto& value : values) {
            rapidjson::Value item(rapidjson::kObjectType);
            item.AddMember("maleTitle", rapidjson::Value(value.maleTitle.c_str(), allocator), allocator);
            item.AddMember("femaleTitle", rapidjson::Value(value.femaleTitle.c_str(), allocator), allocator);
            item.AddMember("insigniaPath", rapidjson::Value(value.insigniaPath.c_str(), allocator), allocator);
            array.PushBack(item, allocator);
        }
        object.AddMember("factionRanks", array, allocator);
    }

    void ReadMessageButtons(const rapidjson::Value& object, std::vector<DynamicForms::MessageButton>& values) {
        if (!object.HasMember("messageButtons") || !object["messageButtons"].IsArray()) return; values.clear();
        for (const auto& item : object["messageButtons"].GetArray()) { if (!item.IsObject()) continue; DynamicForms::MessageButton value; if (item.HasMember("text") && item["text"].IsString()) value.text = item["text"].GetString(); if (item.HasMember("conditions") && item["conditions"].IsArray()) for (const auto& condition : item["conditions"].GetArray()) value.conditions.push_back(ReadCondition(condition)); values.push_back(std::move(value)); }
    }

    void AddMessageButtons(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const std::vector<DynamicForms::MessageButton>& values) {
        rapidjson::Value array(rapidjson::kArrayType); for (const auto& value : values) { rapidjson::Value item(rapidjson::kObjectType), conditions(rapidjson::kArrayType); item.AddMember("text", rapidjson::Value(value.text.c_str(), allocator), allocator); for (const auto& condition : value.conditions) WriteCondition(conditions, allocator, condition); item.AddMember("conditions", conditions, allocator); array.PushBack(item, allocator); } object.AddMember("messageButtons", array, allocator);
    }

    void ReadDialogueResponses(const rapidjson::Value& object, std::vector<DynamicForms::DialogueResponse>& values) {
        if (!object.HasMember("dialogueResponses") || !object["dialogueResponses"].IsArray()) return;
        values.clear();
        for (const auto& item : object["dialogueResponses"].GetArray()) {
            if (!item.IsObject()) continue;
            DynamicForms::DialogueResponse value;
            value.emotionType = ReadUInt32(item, "emotionType", value.emotionType); value.emotionValue = ReadUInt32(item, "emotionValue", value.emotionValue); value.responseNumber = static_cast<std::uint8_t>(ReadUInt32(item, "responseNumber", value.responseNumber)); value.flags = ReadUInt32(item, "flags", value.flags);
            if (item.HasMember("text") && item["text"].IsString()) value.text = item["text"].GetString();
            if (item.HasMember("sound")) value.sound = ReadFormRefValue(item["sound"]); if (item.HasMember("speakerIdle")) value.speakerIdle = ReadFormRefValue(item["speakerIdle"]); if (item.HasMember("listenerIdle")) value.listenerIdle = ReadFormRefValue(item["listenerIdle"]);
            values.push_back(std::move(value));
        }
    }

    void AddDialogueResponses(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const std::vector<DynamicForms::DialogueResponse>& values) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto& value : values) {
            rapidjson::Value item(rapidjson::kObjectType); item.AddMember("emotionType", value.emotionType, allocator); item.AddMember("emotionValue", value.emotionValue, allocator); item.AddMember("responseNumber", value.responseNumber, allocator); item.AddMember("flags", value.flags, allocator); item.AddMember("text", rapidjson::Value(value.text.c_str(), allocator), allocator); AddFormRef(item, allocator, "sound", value.sound); AddFormRef(item, allocator, "speakerIdle", value.speakerIdle); AddFormRef(item, allocator, "listenerIdle", value.listenerIdle); array.PushBack(item, allocator);
        }
        object.AddMember("dialogueResponses", array, allocator);
    }

    std::vector<DynamicForms::PerkCondition> ReadConditionArray(const rapidjson::Value& object, const char* key) {
        std::vector<DynamicForms::PerkCondition> result; if (!object.HasMember(key) || !object[key].IsArray()) return result; for (const auto& item : object[key].GetArray()) result.push_back(ReadCondition(item)); return result;
    }

    void AddConditionArray(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const char* key, const std::vector<DynamicForms::PerkCondition>& values) {
        rapidjson::Value array(rapidjson::kArrayType); for (const auto& value : values) WriteCondition(array, allocator, value); object.AddMember(rapidjson::Value(key, allocator), array, allocator);
    }

    void ReadAdvancedForms(const rapidjson::Value& object, DynamicForms::DynamicForm& form) {
        if (object.HasMember("raceAttacks") && object["raceAttacks"].IsArray()) { form.raceAttacks.clear(); for (const auto& item : object["raceAttacks"].GetArray()) { if (!item.IsObject()) continue; DynamicForms::RaceAttack value; if (item.HasMember("event") && item["event"].IsString()) value.event=item["event"].GetString(); value.damageMult=ReadFloat(item,"damageMult",value.damageMult); value.attackChance=ReadFloat(item,"attackChance",value.attackChance); if(item.HasMember("attackSpell"))value.attackSpell=ReadFormRefValue(item["attackSpell"]); value.flags=ReadUInt32(item,"flags",value.flags); value.attackAngle=ReadFloat(item,"attackAngle",value.attackAngle); value.strikeAngle=ReadFloat(item,"strikeAngle",value.strikeAngle); value.staggerOffset=ReadFloat(item,"staggerOffset",value.staggerOffset); if(item.HasMember("attackType"))value.attackType=ReadFormRefValue(item["attackType"]); value.knockDown=ReadFloat(item,"knockDown",value.knockDown); value.recoveryTime=ReadFloat(item,"recoveryTime",value.recoveryTime); value.staminaMult=ReadFloat(item,"staminaMult",value.staminaMult); form.raceAttacks.push_back(std::move(value)); } }
        if (object.HasMember("questStages") && object["questStages"].IsArray()) { form.questStages.clear(); for (const auto& item : object["questStages"].GetArray()) { if (!item.IsObject()) continue; DynamicForms::QuestStage value; value.index=static_cast<std::uint16_t>(ReadUInt32(item,"index",0)); value.flags=ReadUInt32(item,"flags",0); form.questStages.push_back(value); } }
        if (object.HasMember("questObjectives") && object["questObjectives"].IsArray()) { form.questObjectives.clear(); for (const auto& item : object["questObjectives"].GetArray()) { if (!item.IsObject()) continue; DynamicForms::QuestObjective value; value.index=static_cast<std::uint16_t>(ReadUInt32(item,"index",0)); value.flags=ReadUInt32(item,"flags",0); if(item.HasMember("text")&&item["text"].IsString()) value.text=item["text"].GetString(); if(item.HasMember("targets")&&item["targets"].IsArray()) for(const auto& target:item["targets"].GetArray()){ if(!target.IsObject())continue; DynamicForms::QuestTarget targetValue; targetValue.aliasId=ReadUInt32(target,"aliasId",0); targetValue.flags=ReadUInt32(target,"flags",0); targetValue.conditions=ReadConditionArray(target,"conditions"); value.targets.push_back(std::move(targetValue)); } form.questObjectives.push_back(std::move(value)); } }
        if (object.HasMember("questAliases") && object["questAliases"].IsArray()) { form.questAliases.clear(); for(const auto& item:object["questAliases"].GetArray()){ if(!item.IsObject())continue; DynamicForms::QuestAlias value; value.id=ReadUInt32(item,"id",0); if(item.HasMember("name")&&item["name"].IsString())value.name=item["name"].GetString(); value.flags=ReadUInt32(item,"flags",0); value.fillType=ReadUInt32(item,"fillType",0); if(item.HasMember("forcedReference"))value.forcedReference=ReadFormRefValue(item["forcedReference"]); if(item.HasMember("uniqueActor"))value.uniqueActor=ReadFormRefValue(item["uniqueActor"]); if(item.HasMember("externalQuest"))value.externalQuest=ReadFormRefValue(item["externalQuest"]); value.externalAliasId=ReadUInt32(item,"externalAliasId",0); value.sourceAliasId=ReadUInt32(item,"sourceAliasId",0); if(item.HasMember("sourceRefType"))value.sourceRefType=ReadFormRefValue(item["sourceRefType"]); value.conditions=ReadConditionArray(item,"conditions"); form.questAliases.push_back(std::move(value)); } }
        if(object.HasMember("scenePhases")&&object["scenePhases"].IsArray()){form.scenePhases.clear();for(const auto& item:object["scenePhases"].GetArray()){if(!item.IsObject())continue;DynamicForms::ScenePhase value;value.startConditions=ReadConditionArray(item,"startConditions");value.completionConditions=ReadConditionArray(item,"completionConditions");if(item.HasMember("questNode"))value.questNode=ReadFormRefValue(item["questNode"]);form.scenePhases.push_back(std::move(value));}}
        if(object.HasMember("sceneActions")&&object["sceneActions"].IsArray()){form.sceneActions.clear();for(const auto& item:object["sceneActions"].GetArray()){if(!item.IsObject())continue;DynamicForms::SceneAction value;value.type=ReadUInt32(item,"type",0);value.actorId=ReadUInt32(item,"actorId",0);value.startPhase=static_cast<std::uint16_t>(ReadUInt32(item,"startPhase",0));value.endPhase=static_cast<std::uint16_t>(ReadUInt32(item,"endPhase",0));value.flags=ReadUInt32(item,"flags",0);value.index=ReadUInt32(item,"index",0);if(item.HasMember("topic"))value.topic=ReadFormRefValue(item["topic"]);value.headtrackActorId=ReadInt32(item,"headtrackActorId",-1);value.loopingMin=ReadFloat(item,"loopingMin",0);value.loopingMax=ReadFloat(item,"loopingMax",0);value.emotionType=ReadUInt32(item,"emotionType",0);value.emotionValue=ReadUInt32(item,"emotionValue",50);ReadFormRefArray(item,"packages",value.packages);value.timerSeconds=ReadFloat(item,"timerSeconds",1);form.sceneActions.push_back(std::move(value));}}
        if(object.HasMember("storyQuests")&&object["storyQuests"].IsArray()){form.storyQuests.clear();for(const auto& item:object["storyQuests"].GetArray()){if(!item.IsObject())continue;DynamicForms::StoryQuestEntry value;if(item.HasMember("quest"))value.quest=ReadFormRefValue(item["quest"]);value.flags=ReadUInt32(item,"flags",0);value.hoursUntilReset=ReadFloat(item,"hoursUntilReset",0);form.storyQuests.push_back(std::move(value));}}
        const auto readEvent=[&](const char* key,DynamicForms::PackageEvent& value){if(!object.HasMember(key)||!object[key].IsObject())return;const auto& item=object[key];if(item.HasMember("idle"))value.idle=ReadFormRefValue(item["idle"]);value.type=ReadUInt32(item,"type",0);value.topicType=ReadUInt32(item,"topicType",0);if(item.HasMember("topic"))value.topic=ReadFormRefValue(item["topic"]);};readEvent("packageOnBegin",form.packageOnBegin);readEvent("packageOnEnd",form.packageOnEnd);readEvent("packageOnChange",form.packageOnChange);
    }

    void AddAdvancedForms(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const DynamicForms::DynamicForm& form) {
        rapidjson::Value attacks(rapidjson::kArrayType);for(const auto& source:form.raceAttacks){rapidjson::Value item(rapidjson::kObjectType);item.AddMember("event",rapidjson::Value(source.event.c_str(),allocator),allocator);item.AddMember("damageMult",source.damageMult,allocator);item.AddMember("attackChance",source.attackChance,allocator);AddFormRef(item,allocator,"attackSpell",source.attackSpell);item.AddMember("flags",source.flags,allocator);item.AddMember("attackAngle",source.attackAngle,allocator);item.AddMember("strikeAngle",source.strikeAngle,allocator);item.AddMember("staggerOffset",source.staggerOffset,allocator);AddFormRef(item,allocator,"attackType",source.attackType);item.AddMember("knockDown",source.knockDown,allocator);item.AddMember("recoveryTime",source.recoveryTime,allocator);item.AddMember("staminaMult",source.staminaMult,allocator);attacks.PushBack(item,allocator);}object.AddMember("raceAttacks",attacks,allocator);
        rapidjson::Value stages(rapidjson::kArrayType);for(const auto& source:form.questStages){rapidjson::Value item(rapidjson::kObjectType);item.AddMember("index",source.index,allocator);item.AddMember("flags",source.flags,allocator);stages.PushBack(item,allocator);}object.AddMember("questStages",stages,allocator);
        rapidjson::Value objectives(rapidjson::kArrayType);for(const auto& source:form.questObjectives){rapidjson::Value item(rapidjson::kObjectType),targets(rapidjson::kArrayType);item.AddMember("index",source.index,allocator);item.AddMember("flags",source.flags,allocator);item.AddMember("text",rapidjson::Value(source.text.c_str(),allocator),allocator);for(const auto& target:source.targets){rapidjson::Value targetItem(rapidjson::kObjectType);targetItem.AddMember("aliasId",target.aliasId,allocator);targetItem.AddMember("flags",target.flags,allocator);AddConditionArray(targetItem,allocator,"conditions",target.conditions);targets.PushBack(targetItem,allocator);}item.AddMember("targets",targets,allocator);objectives.PushBack(item,allocator);}object.AddMember("questObjectives",objectives,allocator);
        rapidjson::Value aliases(rapidjson::kArrayType);for(const auto& source:form.questAliases){rapidjson::Value item(rapidjson::kObjectType);item.AddMember("id",source.id,allocator);item.AddMember("name",rapidjson::Value(source.name.c_str(),allocator),allocator);item.AddMember("flags",source.flags,allocator);item.AddMember("fillType",source.fillType,allocator);AddFormRef(item,allocator,"forcedReference",source.forcedReference);AddFormRef(item,allocator,"uniqueActor",source.uniqueActor);AddFormRef(item,allocator,"externalQuest",source.externalQuest);item.AddMember("externalAliasId",source.externalAliasId,allocator);item.AddMember("sourceAliasId",source.sourceAliasId,allocator);AddFormRef(item,allocator,"sourceRefType",source.sourceRefType);AddConditionArray(item,allocator,"conditions",source.conditions);aliases.PushBack(item,allocator);}object.AddMember("questAliases",aliases,allocator);
        rapidjson::Value phases(rapidjson::kArrayType);for(const auto& source:form.scenePhases){rapidjson::Value item(rapidjson::kObjectType);AddConditionArray(item,allocator,"startConditions",source.startConditions);AddConditionArray(item,allocator,"completionConditions",source.completionConditions);AddFormRef(item,allocator,"questNode",source.questNode);phases.PushBack(item,allocator);}object.AddMember("scenePhases",phases,allocator);
        rapidjson::Value actions(rapidjson::kArrayType);for(const auto& source:form.sceneActions){rapidjson::Value item(rapidjson::kObjectType);item.AddMember("type",source.type,allocator);item.AddMember("actorId",source.actorId,allocator);item.AddMember("startPhase",source.startPhase,allocator);item.AddMember("endPhase",source.endPhase,allocator);item.AddMember("flags",source.flags,allocator);item.AddMember("index",source.index,allocator);AddFormRef(item,allocator,"topic",source.topic);item.AddMember("headtrackActorId",source.headtrackActorId,allocator);item.AddMember("loopingMin",source.loopingMin,allocator);item.AddMember("loopingMax",source.loopingMax,allocator);item.AddMember("emotionType",source.emotionType,allocator);item.AddMember("emotionValue",source.emotionValue,allocator);AddFormRefArray(item,allocator,"packages",source.packages);item.AddMember("timerSeconds",source.timerSeconds,allocator);actions.PushBack(item,allocator);}object.AddMember("sceneActions",actions,allocator);
        rapidjson::Value quests(rapidjson::kArrayType);for(const auto& source:form.storyQuests){rapidjson::Value item(rapidjson::kObjectType);AddFormRef(item,allocator,"quest",source.quest);item.AddMember("flags",source.flags,allocator);item.AddMember("hoursUntilReset",source.hoursUntilReset,allocator);quests.PushBack(item,allocator);}object.AddMember("storyQuests",quests,allocator);
        const auto addEvent=[&](const char* key,const DynamicForms::PackageEvent& source){rapidjson::Value item(rapidjson::kObjectType);AddFormRef(item,allocator,"idle",source.idle);item.AddMember("type",source.type,allocator);item.AddMember("topicType",source.topicType,allocator);AddFormRef(item,allocator,"topic",source.topic);object.AddMember(rapidjson::Value(key,allocator),item,allocator);};addEvent("packageOnBegin",form.packageOnBegin);addEvent("packageOnEnd",form.packageOnEnd);addEvent("packageOnChange",form.packageOnChange);
    }

    void ReadDebrisEntries(const rapidjson::Value& object, std::vector<DynamicForms::DebrisEntry>& values) {
        if (!object.HasMember("debrisEntries") || !object["debrisEntries"].IsArray()) return; values.clear();
        for (const auto& item : object["debrisEntries"].GetArray()) { if (!item.IsObject()) continue; DynamicForms::DebrisEntry value; value.percentage = static_cast<std::int8_t>(std::clamp(ReadInt32(item, "percentage", value.percentage), -128, 127)); value.flags = static_cast<std::uint8_t>(std::min(ReadUInt32(item, "flags", value.flags), 255u)); if (item.HasMember("modelPath") && item["modelPath"].IsString()) value.modelPath = item["modelPath"].GetString(); values.push_back(std::move(value)); }
    }

    void AddDebrisEntries(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const std::vector<DynamicForms::DebrisEntry>& values) {
        rapidjson::Value array(rapidjson::kArrayType); for (const auto& value : values) { rapidjson::Value item(rapidjson::kObjectType); item.AddMember("percentage", value.percentage, allocator); item.AddMember("flags", value.flags, allocator); item.AddMember("modelPath", rapidjson::Value(value.modelPath.c_str(), allocator), allocator); array.PushBack(item, allocator); } object.AddMember("debrisEntries", array, allocator);
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
        case DynamicForms::FormKind::Spell:
            return static_cast<std::uint32_t>(RE::FormType::Spell);
        case DynamicForms::FormKind::MagicEffect:
            return static_cast<std::uint32_t>(RE::FormType::MagicEffect);
        case DynamicForms::FormKind::Enchantment:
            return static_cast<std::uint32_t>(RE::FormType::Enchantment);
        case DynamicForms::FormKind::Scroll:
            return static_cast<std::uint32_t>(RE::FormType::Scroll);
        case DynamicForms::FormKind::Projectile:
            return static_cast<std::uint32_t>(RE::FormType::Projectile);
        case DynamicForms::FormKind::TextureSet: return static_cast<std::uint32_t>(RE::FormType::TextureSet);
        case DynamicForms::FormKind::Hazard: return static_cast<std::uint32_t>(RE::FormType::Hazard);
        case DynamicForms::FormKind::ImpactData: return static_cast<std::uint32_t>(RE::FormType::Impact);
        case DynamicForms::FormKind::ReferenceEffect: return static_cast<std::uint32_t>(RE::FormType::ReferenceEffect);
        case DynamicForms::FormKind::DualCastData: return static_cast<std::uint32_t>(RE::FormType::DualCastData);
        case DynamicForms::FormKind::Static: return static_cast<std::uint32_t>(RE::FormType::Static);
        case DynamicForms::FormKind::MovableStatic: return static_cast<std::uint32_t>(RE::FormType::MovableStatic);
        case DynamicForms::FormKind::Door: return static_cast<std::uint32_t>(RE::FormType::Door);
        case DynamicForms::FormKind::CombatStyle: return static_cast<std::uint32_t>(RE::FormType::CombatStyle);
        case DynamicForms::FormKind::SoundCategory: return static_cast<std::uint32_t>(RE::FormType::SoundCategory);
        case DynamicForms::FormKind::Class: return static_cast<std::uint32_t>(RE::FormType::Class);
        case DynamicForms::FormKind::Flora: return static_cast<std::uint32_t>(RE::FormType::Flora);
        case DynamicForms::FormKind::Tree: return static_cast<std::uint32_t>(RE::FormType::Tree);
        case DynamicForms::FormKind::ConstructibleObject: return static_cast<std::uint32_t>(RE::FormType::ConstructibleObject);
        case DynamicForms::FormKind::Container: return static_cast<std::uint32_t>(RE::FormType::Container);
        case DynamicForms::FormKind::ImpactDataSet: return static_cast<std::uint32_t>(RE::FormType::ImpactDataSet);
        case DynamicForms::FormKind::CollisionLayer: return static_cast<std::uint32_t>(RE::FormType::CollisionLayer);
        case DynamicForms::FormKind::Footstep: return static_cast<std::uint32_t>(RE::FormType::Footstep);
        case DynamicForms::FormKind::FootstepSet: return static_cast<std::uint32_t>(RE::FormType::FootstepSet);
        case DynamicForms::FormKind::ReverbParameters: return static_cast<std::uint32_t>(RE::FormType::ReverbParam);
        case DynamicForms::FormKind::AcousticSpace: return static_cast<std::uint32_t>(RE::FormType::AcousticSpace);
        case DynamicForms::FormKind::Apparatus: return static_cast<std::uint32_t>(RE::FormType::Apparatus);
        case DynamicForms::FormKind::StaticCollection: return static_cast<std::uint32_t>(RE::FormType::StaticCollection);
        case DynamicForms::FormKind::Grass: return static_cast<std::uint32_t>(RE::FormType::Grass);
        case DynamicForms::FormKind::IdleMarker: return static_cast<std::uint32_t>(RE::FormType::IdleMarker);
        case DynamicForms::FormKind::EncounterZone: return static_cast<std::uint32_t>(RE::FormType::EncounterZone);
        case DynamicForms::FormKind::Relationship: return static_cast<std::uint32_t>(RE::FormType::Relationship);
        case DynamicForms::FormKind::AssociationType: return static_cast<std::uint32_t>(RE::FormType::AssociationType);
        case DynamicForms::FormKind::MovementType: return static_cast<std::uint32_t>(RE::FormType::MovementType);
        case DynamicForms::FormKind::WordOfPower: return static_cast<std::uint32_t>(RE::FormType::WordOfPower);
        case DynamicForms::FormKind::Water: return static_cast<std::uint32_t>(RE::FormType::Water);
        case DynamicForms::FormKind::ImageSpace: return static_cast<std::uint32_t>(RE::FormType::ImageSpace);
        case DynamicForms::FormKind::LightingTemplate: return static_cast<std::uint32_t>(RE::FormType::LightingMaster);
        case DynamicForms::FormKind::Shout: return static_cast<std::uint32_t>(RE::FormType::Shout);
        case DynamicForms::FormKind::LeveledItem: return static_cast<std::uint32_t>(RE::FormType::LeveledItem);
        case DynamicForms::FormKind::LeveledNPC: return static_cast<std::uint32_t>(RE::FormType::LeveledNPC);
        case DynamicForms::FormKind::LeveledSpell: return static_cast<std::uint32_t>(RE::FormType::LeveledSpell);
        case DynamicForms::FormKind::LocationRefType: return static_cast<std::uint32_t>(RE::FormType::LocationRefType);
        case DynamicForms::FormKind::Action: return static_cast<std::uint32_t>(RE::FormType::Action);
        case DynamicForms::FormKind::MenuIcon: return static_cast<std::uint32_t>(RE::FormType::MenuIcon);
        case DynamicForms::FormKind::Eyes: return static_cast<std::uint32_t>(RE::FormType::Eyes);
        case DynamicForms::FormKind::Note: return static_cast<std::uint32_t>(RE::FormType::Note);
        case DynamicForms::FormKind::AnimatedObject: return static_cast<std::uint32_t>(RE::FormType::AnimatedObject);
        case DynamicForms::FormKind::LoadScreen: return static_cast<std::uint32_t>(RE::FormType::LoadScreen);
        case DynamicForms::FormKind::ShaderParticleGeometry: return static_cast<std::uint32_t>(RE::FormType::ShaderParticleGeometryData);
        case DynamicForms::FormKind::AddonNode: return static_cast<std::uint32_t>(RE::FormType::AddonNode);
        case DynamicForms::FormKind::Faction: return static_cast<std::uint32_t>(RE::FormType::Faction);
        case DynamicForms::FormKind::IdleAnimation: return static_cast<std::uint32_t>(RE::FormType::Idle);
        case DynamicForms::FormKind::MaterialObject: return static_cast<std::uint32_t>(RE::FormType::MaterialObject);
        case DynamicForms::FormKind::Message: return static_cast<std::uint32_t>(RE::FormType::Message);
        case DynamicForms::FormKind::LandTexture: return static_cast<std::uint32_t>(RE::FormType::LandTexture);
        case DynamicForms::FormKind::SoundOutputModel: return static_cast<std::uint32_t>(RE::FormType::SoundOutputModel);
        case DynamicForms::FormKind::LensFlare: return static_cast<std::uint32_t>(RE::FormType::LensFlare);
        case DynamicForms::FormKind::Debris: return static_cast<std::uint32_t>(RE::FormType::Debris);
        case DynamicForms::FormKind::ImageSpaceModifier: return static_cast<std::uint32_t>(RE::FormType::ImageAdapter);
        case DynamicForms::FormKind::CameraShot: return static_cast<std::uint32_t>(RE::FormType::CameraShot);
        case DynamicForms::FormKind::CameraPath: return static_cast<std::uint32_t>(RE::FormType::CameraPath);
        case DynamicForms::FormKind::TalkingActivator: return static_cast<std::uint32_t>(RE::FormType::TalkingActivator);
        case DynamicForms::FormKind::Furniture: return static_cast<std::uint32_t>(RE::FormType::Furniture);
        case DynamicForms::FormKind::Weather: return static_cast<std::uint32_t>(RE::FormType::Weather);
        case DynamicForms::FormKind::Climate: return static_cast<std::uint32_t>(RE::FormType::Climate);
        case DynamicForms::FormKind::Location: return static_cast<std::uint32_t>(RE::FormType::Location);
        case DynamicForms::FormKind::MusicType: return static_cast<std::uint32_t>(RE::FormType::MusicType);
        case DynamicForms::FormKind::MusicTrack: return static_cast<std::uint32_t>(RE::FormType::MusicTrack);
        case DynamicForms::FormKind::BodyPartData: return static_cast<std::uint32_t>(RE::FormType::BodyPartData);
        case DynamicForms::FormKind::VolumetricLighting: return static_cast<std::uint32_t>(RE::FormType::VolumetricLighting);
        case DynamicForms::FormKind::Sound: return static_cast<std::uint32_t>(RE::FormType::Sound);
        case DynamicForms::FormKind::ActorValueInfo: return static_cast<std::uint32_t>(RE::FormType::ActorValueInfo);
        case DynamicForms::FormKind::DialogueBranch: return static_cast<std::uint32_t>(RE::FormType::DialogueBranch);
        case DynamicForms::FormKind::DialogueTopic: return static_cast<std::uint32_t>(RE::FormType::Dialogue);
        case DynamicForms::FormKind::DialogueInfo: return static_cast<std::uint32_t>(RE::FormType::Info);
        case DynamicForms::FormKind::Quest: return static_cast<std::uint32_t>(RE::FormType::Quest);
        case DynamicForms::FormKind::Scene: return static_cast<std::uint32_t>(RE::FormType::Scene);
        case DynamicForms::FormKind::StoryManagerBranchNode: return static_cast<std::uint32_t>(RE::FormType::StoryManagerBranchNode);
        case DynamicForms::FormKind::StoryManagerQuestNode: return static_cast<std::uint32_t>(RE::FormType::StoryManagerQuestNode);
        case DynamicForms::FormKind::StoryManagerEventNode: return static_cast<std::uint32_t>(RE::FormType::StoryManagerEventNode);
        case DynamicForms::FormKind::Package: return static_cast<std::uint32_t>(RE::FormType::Package);
        case DynamicForms::FormKind::Race: return static_cast<std::uint32_t>(RE::FormType::Race);
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

    bool IsDPFPluginName(const std::string_view name) {
        if (name == "DPF.esp") {
            return true;
        }
        if (!name.starts_with("DPF ") || !name.ends_with(".esp")) {
            return false;
        }
        const auto number = name.substr(4, name.size() - 8);
        return !number.empty() && std::ranges::all_of(number, [](const char value) {
            return value >= '0' && value <= '9';
        });
    }

    bool IsDPFForm(const RE::TESForm* form) {
        if (!form) {
            return false;
        }
        const auto* file = form->GetFile(0);
        return file && IsDPFPluginName(file->GetFilename());
    }

    void BuildExternalEditorIdIndex() {
        externalFormsByEditorId.clear();

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            logger::warn("Could not build the CLibUtil EditorID index: TESDataHandler is unavailable.");
            return;
        }

        std::set<RE::FormType> supportedTypes;
        const auto lastKind = static_cast<std::uint32_t>(DynamicForms::FormKind::Race);
        for (std::uint32_t rawKind = 0; rawKind <= lastKind; ++rawKind) {
            const auto kind = static_cast<DynamicForms::FormKind>(rawKind);
            supportedTypes.insert(static_cast<RE::FormType>(FormTypeForKind(kind)));
        }

        std::size_t indexedForms = 0;
        std::size_t missingEditorIds = 0;
        std::size_t skippedDPFForms = 0;
        for (const auto formType : supportedTypes) {
            for (auto* form : dataHandler->GetFormArray(formType)) {
                if (!form || form->IsDeleted() || form->IsIgnored()) {
                    continue;
                }
                if (IsDPFForm(form)) {
                    ++skippedDPFForms;
                    continue;
                }

                const auto editorId = FormUtil::GetEditorIDSafe(form);
                if (editorId.empty()) {
                    ++missingEditorIds;
                    continue;
                }

                externalFormsByEditorId[NormalizeEditorId(editorId)].push_back(form);
                ++indexedForms;
            }
        }

        const auto duplicateEditorIds = std::ranges::count_if(externalFormsByEditorId, [](const auto& entry) {
            return entry.second.size() > 1;
        });
        logger::info(
            "Built CLibUtil EditorID index: {} forms, {} unique EditorIDs, {} duplicate EditorIDs, {} forms without an EditorID, {} DPF forms excluded, po3 Tweaks={}",
            indexedForms,
            externalFormsByEditorId.size(),
            duplicateEditorIds,
            missingEditorIds,
            skippedDPFForms,
            GetModuleHandleW(L"po3_Tweaks") != nullptr);
    }

    const std::vector<RE::TESForm*>* FindExternalFormsByEditorId(const std::string_view editorId) {
        if (editorId.empty()) {
            return nullptr;
        }
        const auto found = externalFormsByEditorId.find(NormalizeEditorId(editorId));
        return found != externalFormsByEditorId.end() ? std::addressof(found->second) : nullptr;
    }

    RE::TESForm* FindManagedFormByEditorId(const std::string_view editorId) {
        const auto key = NormalizeEditorId(editorId);
        const auto found = std::ranges::find_if(forms, [&key](const DynamicForms::DynamicForm& form) {
            return !form.externalPatch && NormalizeEditorId(form.editorId) == key;
        });
        if (found == forms.end() || found->pluginNumber == 0 || found->localId == 0) {
            return nullptr;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        const auto pluginName = DPF::PluginNameForNumber(found->pluginNumber);
        return dataHandler && !pluginName.empty() ? dataHandler->LookupForm(found->localId, pluginName) : nullptr;
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
            if (auto* form = FindManagedFormByEditorId(value.editorID)) {
                return form;
            }
        }

        RE::TESForm* explicitForm = nullptr;
        try {
            if (!value.formID.empty()) {
                const auto formId = FormUtil::FormIDFromString(value.formID);
                explicitForm = formId != 0 ? RE::TESForm::LookupByID(formId) : nullptr;
            }
        } catch (...) {
            logger::warn("Invalid form ref '{}'", value.Display());
        }

        if (!value.editorID.empty()) {
            if (const auto* indexed = FindExternalFormsByEditorId(value.editorID); indexed && !indexed->empty()) {
                if (explicitForm && std::ranges::find(*indexed, explicitForm) != indexed->end()) {
                    return explicitForm;
                }
                if (indexed->size() == 1) {
                    return indexed->front();
                }
                logger::warn(
                    "Form ref '{}' is ambiguous: CLibUtil found {} loaded forms with that EditorID.",
                    value.Display(),
                    indexed->size());
                return explicitForm;
            }

            // Keep the game lookup only as a compatibility fallback. It is not
            // authoritative when deciding whether an EditorID is available.
            if (auto* form = RE::TESForm::LookupByEditorID(value.editorID)) {
                return form;
            }
        }

        return explicitForm;
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
        keywords.reserve(refs.size());
        for (const auto& keywordRef : refs) {
            if (auto* keyword = ResolveAs<RE::BGSKeyword>(keywordRef)) {
                if (std::ranges::find(keywords, keyword) == keywords.end()) {
                    keywords.push_back(keyword);
                }
            }
        }

        RE::BGSKeyword** replacement = nullptr;
        if (!keywords.empty()) {
            replacement = RE::calloc<RE::BGSKeyword*>(keywords.size());
            if (!replacement) {
                logger::warn("Could not allocate storage for {} keywords.", keywords.size());
                return;
            }
            std::ranges::copy(keywords, replacement);
        }

        auto* previous = keywordForm.keywords;
        const auto previousCount = keywordForm.numKeywords;
        keywordForm.keywords = replacement;
        keywordForm.numKeywords = static_cast<std::uint32_t>(keywords.size());

        // MemoryManager may return a non-owning sentinel for a zero-byte allocation.
        // Only arrays that actually held entries are safe and necessary to release.
        if (previous && previousCount > 0) {
            RE::free(previous);
        }
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
            ApplyConditions(effect->conditions, entry.conditions);
            magicItem.effects.push_back(effect);
        }
        logger::info("Configured magic item '{}' with {} custom effects.", form.editorId, magicItem.effects.size());
    }

    void ApplyMiscLikeItem(RE::TESObjectMISC& item, const DynamicForms::DynamicForm& form) {
        item.SetFormEditorID(form.editorId.c_str());
        item.fullName = form.fullName.c_str();
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
        book->fullName = form.fullName.c_str();
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
        ammo->fullName = form.fullName.c_str();
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
        weapon->fullName = form.fullName.c_str();
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
        item->fullName = form.fullName.c_str();
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
        item->fullName = form.fullName.c_str();
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

    bool ConfigureSpell(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* spell = tesForm ? tesForm->As<RE::SpellItem>() : nullptr;
        if (!spell) {
            logger::warn("Dynamic form '{}' is not a SpellItem", form.editorId);
            return false;
        }

        spell->SetFormEditorID(form.editorId.c_str());
        spell->fullName = form.fullName.c_str();
        spell->data.costOverride = form.spellCostOverride;
        spell->data.flags = static_cast<RE::SpellItem::SpellFlag>(form.spellFlags);
        spell->data.spellType = static_cast<RE::MagicSystem::SpellType>(std::clamp(form.spellType, 0u, 13u));
        spell->data.chargeTime = form.spellChargeTime;
        spell->data.castingType = static_cast<RE::MagicSystem::CastingType>(std::clamp(form.spellCastingType, 0u, 3u));
        spell->data.delivery = static_cast<RE::MagicSystem::Delivery>(std::clamp(form.spellDelivery, 0u, 5u));
        spell->data.castDuration = form.spellCastDuration;
        spell->data.range = form.spellRange;
        spell->data.castingPerk = ResolveAs<RE::BGSPerk>(form.castingPerk);
        spell->SetEquipSlot(ResolveAs<RE::BGSEquipSlot>(form.equipSlot));
        spell->menuDispObject = ResolveAs<RE::TESBoundObject>(form.menuDisplayObject);
        ApplyKeywords(static_cast<RE::BGSKeywordForm&>(*spell), form.keywords);
        ApplyMagicEffects(static_cast<RE::MagicItem&>(*spell), form);
        if (!form.description.empty()) {
            logger::debug("Spell '{}' description is saved in JSON but cannot be assigned directly with this CommonLib TESDescription layout.", form.editorId);
        }
        logger::info("Configured spell '{}' FormID={:08X} type={} casting={} delivery={} range={} cost={} effects={}.",
            form.editorId,
            spell->GetFormID(),
            form.spellType,
            form.spellCastingType,
            form.spellDelivery,
            form.spellRange,
            form.spellCostOverride,
            spell->effects.size());
        return true;
    }

    bool ConfigureEnchantment(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* enchantment = tesForm ? tesForm->As<RE::EnchantmentItem>() : nullptr;
        if (!enchantment) {
            logger::warn("Dynamic form '{}' is not an EnchantmentItem", form.editorId);
            return false;
        }

        enchantment->SetFormEditorID(form.editorId.c_str());
        enchantment->fullName = form.fullName.c_str();
        enchantment->data.costOverride = form.enchantmentCostOverride;
        enchantment->data.flags = static_cast<RE::EnchantmentItem::EnchantmentFlag>(form.enchantmentFlags);
        enchantment->data.castingType = static_cast<RE::MagicSystem::CastingType>(std::clamp(form.enchantmentCastingType, 0u, 3u));
        enchantment->data.chargeOverride = form.enchantmentChargeOverride;
        enchantment->data.delivery = static_cast<RE::MagicSystem::Delivery>(std::clamp(form.enchantmentDelivery, 0u, 5u));
        enchantment->data.spellType = static_cast<RE::MagicSystem::SpellType>(std::clamp(form.enchantmentSpellType, 0u, 13u));
        enchantment->data.chargeTime = form.enchantmentChargeTime;
        enchantment->data.baseEnchantment = ResolveAs<RE::EnchantmentItem>(form.baseEnchantment);
        enchantment->data.wornRestrictions = ResolveAs<RE::BGSListForm>(form.wornRestrictions);
        ApplyKeywords(static_cast<RE::BGSKeywordForm&>(*enchantment), form.keywords);
        ApplyMagicEffects(static_cast<RE::MagicItem&>(*enchantment), form);
        logger::info("Configured enchantment '{}' FormID={:08X} type={} casting={} delivery={} cost={} charge={} effects={}.",
            form.editorId,
            enchantment->GetFormID(),
            form.enchantmentSpellType,
            form.enchantmentCastingType,
            form.enchantmentDelivery,
            form.enchantmentCostOverride,
            form.enchantmentChargeOverride,
            enchantment->effects.size());
        return true;
    }

    bool ConfigureScroll(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* scroll = tesForm ? tesForm->As<RE::ScrollItem>() : nullptr;
        if (!scroll) {
            logger::warn("Dynamic form '{}' is not a ScrollItem", form.editorId);
            return false;
        }

        scroll->SetFormEditorID(form.editorId.c_str());
        scroll->fullName = form.fullName.c_str();
        scroll->SetModel(form.modelPath.c_str());
        scroll->value = form.itemValue;
        scroll->weight = form.itemWeight;
        auto& spellData = static_cast<RE::SpellItem&>(*scroll).data;
        spellData.costOverride = form.scrollCostOverride;
        spellData.flags = static_cast<RE::SpellItem::SpellFlag>(form.scrollFlags);
        spellData.spellType = RE::MagicSystem::SpellType::kScroll;
        spellData.chargeTime = form.scrollChargeTime;
        spellData.castingType = RE::MagicSystem::CastingType::kFireAndForget;
        spellData.delivery = static_cast<RE::MagicSystem::Delivery>(std::clamp(form.scrollDelivery, 0u, 5u));
        spellData.castDuration = form.scrollCastDuration;
        spellData.range = form.scrollRange;
        spellData.castingPerk = ResolveAs<RE::BGSPerk>(form.scrollCastingPerk);
        scroll->SetEquipSlot(ResolveAs<RE::BGSEquipSlot>(form.equipSlot));
        scroll->menuDispObject = ResolveAs<RE::TESBoundObject>(form.menuDisplayObject);
        ApplyPickupPutdownSounds(static_cast<RE::BGSPickupPutdownSounds&>(*scroll), form);
        ApplyKeywords(static_cast<RE::BGSKeywordForm&>(*scroll), form.keywords);
        ApplyMagicEffects(static_cast<RE::MagicItem&>(*scroll), form);
        logger::info("Configured scroll '{}' FormID={:08X} delivery={} range={} cost={} effects={}.",
            form.editorId,
            scroll->GetFormID(),
            form.scrollDelivery,
            form.scrollRange,
            form.scrollCostOverride,
            scroll->effects.size());
        return true;
    }

    bool ConfigureProjectile(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* projectile = tesForm ? tesForm->As<RE::BGSProjectile>() : nullptr;
        if (!projectile) {
            logger::warn("Dynamic form '{}' is not a BGSProjectile", form.editorId);
            return false;
        }

        projectile->SetFormEditorID(form.editorId.c_str());
        projectile->fullName = form.fullName.c_str();
        projectile->SetModel(form.modelPath.c_str());
        projectile->muzzleFlashModel.SetModel(form.projectileMuzzleFlashModel.c_str());

        auto& data = projectile->data;
        data.flags = static_cast<RE::BGSProjectileData::BGSProjectileFlags>(form.projectileFlags);
        data.types = static_cast<RE::BGSProjectileData::Type>(form.projectileTypes);
        data.gravity = form.projectileGravity;
        data.speed = form.projectileSpeed;
        data.range = form.projectileRange;
        data.light = ResolveAs<RE::TESObjectLIGH>(form.projectileLight);
        data.muzzleFlashLight = ResolveAs<RE::TESObjectLIGH>(form.projectileMuzzleFlashLight);
        data.tracerChance = form.projectileTracerChance;
        data.explosionProximity = form.projectileExplosionProximity;
        data.explosionTimer = form.projectileExplosionTimer;
        data.explosionType = ResolveAs<RE::BGSExplosion>(form.projectileExplosionType);
        data.activeSoundLoop = ResolveAs<RE::BGSSoundDescriptorForm>(form.projectileActiveSoundLoop);
        data.muzzleFlashDuration = form.projectileMuzzleFlashDuration;
        data.fadeOutTime = form.projectileFadeOutTime;
        data.force = form.projectileForce;
        data.countdownSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.projectileCountdownSound);
        data.deactivateSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.projectileDeactivateSound);
        data.defaultWeaponSource = ResolveAs<RE::TESObjectWEAP>(form.projectileDefaultWeaponSource);
        data.coneSpread = form.projectileConeSpread;
        data.collisionRadius = form.projectileCollisionRadius;
        data.lifetime = form.projectileLifetime;
        data.relaunchInterval = form.projectileRelaunchInterval;
        data.decalData = ResolveAs<RE::BGSTextureSet>(form.projectileDecalData);
        data.collisionLayer = ResolveAs<RE::BGSCollisionLayer>(form.projectileCollisionLayer);
        projectile->soundLevel = static_cast<RE::SOUND_LEVEL>(std::clamp(form.projectileSoundLevel, 0u, 4u));
        logger::info("Configured projectile '{}' FormID={:08X} types={:04X} flags={:04X} speed={} range={}.",
            form.editorId,
            projectile->GetFormID(),
            form.projectileTypes,
            form.projectileFlags,
            form.projectileSpeed,
            form.projectileRange);
        return true;
    }

    void ApplyDecalData(RE::DECAL_DATA_DATA& data, const DynamicForms::DynamicForm& form) {
        data.decalMinWidth = form.decalMinWidth;
        data.decalMaxWidth = form.decalMaxWidth;
        data.decalMinHeight = form.decalMinHeight;
        data.decalMaxHeight = form.decalMaxHeight;
        data.depth = form.decalDepth;
        data.shininess = form.decalShininess;
        data.parallaxScale = form.decalParallaxScale;
        data.parallaxPasses = static_cast<std::int8_t>(std::clamp(form.decalParallaxPasses, -128, 127));
        data.flags = static_cast<RE::DECAL_DATA_DATA::Flag>(form.decalFlags);
        data.color = RE::Color(form.decalRed, form.decalGreen, form.decalBlue, form.decalAlpha);
    }

    bool ConfigureTextureSet(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* textureSet = tesForm ? tesForm->As<RE::BGSTextureSet>() : nullptr;
        if (!textureSet) return false;
        textureSet->SetFormEditorID(form.editorId.c_str());
        for (std::size_t i = 0; i < form.textureSetPaths.size(); ++i) {
            textureSet->textures[i].textureName = form.textureSetPaths[i].c_str();
            if (!form.textureSetPaths[i].empty()) {
                textureSet->textureFileIDs[i].GenerateFromPath(form.textureSetPaths[i].c_str());
            } else {
                textureSet->textureFileIDs[i] = {};
            }
        }
        textureSet->flags = static_cast<RE::BGSTextureSet::Flag>(form.textureSetFlags);
        if (form.textureSetHasDecal) {
            if (!textureSet->decalData) textureSet->decalData = new RE::DecalData();
            ApplyDecalData(textureSet->decalData->data, form);
        } else {
            textureSet->decalData = nullptr;
        }
        return true;
    }

    bool ConfigureHazard(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* hazard = tesForm ? tesForm->As<RE::BGSHazard>() : nullptr;
        if (!hazard) return false;
        hazard->SetFormEditorID(form.editorId.c_str());
        hazard->fullName = form.fullName.c_str();
        hazard->SetModel(form.modelPath.c_str());
        hazard->imageSpaceModifying = ResolveAs<RE::TESImageSpaceModifier>(form.hazardImageSpaceModifier);
        hazard->data.limit = form.hazardLimit;
        hazard->data.radius = form.hazardRadius;
        hazard->data.lifetime = form.hazardLifetime;
        hazard->data.imageSpaceRadius = form.hazardImageSpaceRadius;
        hazard->data.targetInterval = form.hazardTargetInterval;
        hazard->data.flags = static_cast<RE::BGSHazardData::BGSHazardFlags>(form.hazardFlags);
        hazard->data.spell = ResolveAs<RE::SpellItem>(form.hazardSpell);
        hazard->data.light = ResolveAs<RE::TESObjectLIGH>(form.hazardLight);
        hazard->data.impactDataSet = ResolveAs<RE::BGSImpactDataSet>(form.hazardImpactDataSet);
        hazard->data.sound = ResolveAs<RE::BGSSoundDescriptorForm>(form.hazardSound);
        return true;
    }

    bool ConfigureImpactData(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* impact = tesForm ? tesForm->As<RE::BGSImpactData>() : nullptr;
        if (!impact) return false;
        impact->SetFormEditorID(form.editorId.c_str());
        impact->SetModel(form.modelPath.c_str());
        impact->data.effectDuration = form.impactEffectDuration;
        impact->data.orient = static_cast<RE::BGSImpactData::ORIENTATION>(std::clamp(form.impactOrientation, 0u, 2u));
        impact->data.angleThreshold = form.impactAngleThreshold;
        impact->data.placementRadius = form.impactPlacementRadius;
        impact->data.soundLevel = static_cast<RE::SOUND_LEVEL>(std::clamp(form.impactSoundLevel, 0u, 4u));
        impact->data.flags = static_cast<RE::BGSImpactData::IMPACT_DATA_DATA::Flag>(form.impactFlags);
        impact->data.resultOverride = static_cast<RE::ImpactResult>(form.impactResultOverride);
        impact->decalTextureSet = ResolveAs<RE::BGSTextureSet>(form.impactDecalTextureSet);
        impact->decalTextureSet2 = ResolveAs<RE::BGSTextureSet>(form.impactDecalTextureSet2);
        impact->sound1 = ResolveAs<RE::BGSSoundDescriptorForm>(form.impactSound1);
        impact->sound2 = ResolveAs<RE::BGSSoundDescriptorForm>(form.impactSound2);
        impact->hazard = ResolveAs<RE::BGSHazard>(form.impactHazard);
        ApplyDecalData(impact->dData.data, form);
        return true;
    }

    bool ConfigureReferenceEffect(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* effect = tesForm ? tesForm->As<RE::BGSReferenceEffect>() : nullptr;
        if (!effect) return false;
        effect->SetFormEditorID(form.editorId.c_str());
        effect->data.artObject = ResolveAs<RE::BGSArtObject>(form.referenceEffectArtObject);
        effect->data.effectShader = ResolveAs<RE::TESEffectShader>(form.referenceEffectShader);
        effect->data.flags = static_cast<RE::BGSReferenceEffect::Flag>(form.referenceEffectFlags);
        return true;
    }

    bool ConfigureDualCastData(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* dual = tesForm ? tesForm->As<RE::BGSDualCastData>() : nullptr;
        if (!dual) return false;
        dual->SetFormEditorID(form.editorId.c_str());
        dual->data.pProjectile = ResolveAs<RE::BGSProjectile>(form.dualCastProjectile);
        dual->data.pExplosion = ResolveAs<RE::BGSExplosion>(form.dualCastExplosion);
        dual->data.pEffectShader = ResolveAs<RE::TESEffectShader>(form.dualCastEffectShader);
        dual->data.pHitEffectArt = ResolveAs<RE::BGSArtObject>(form.dualCastHitEffectArt);
        dual->data.pImpactDataSet = ResolveAs<RE::BGSImpactDataSet>(form.dualCastImpactDataSet);
        dual->data.flags = static_cast<RE::BGSDualCastDataDEF::Flags>(form.dualCastFlags);
        return true;
    }

    void ApplyRecordFlags(RE::TESForm& tesForm, const std::uint32_t requested, const std::uint32_t mask) {
        tesForm.formFlags = (tesForm.formFlags & ~mask) | (requested & mask);
    }

    void ApplyStaticData(RE::TESObjectSTAT& stat, const DynamicForms::DynamicForm& form) {
        stat.SetModel(form.modelPath.c_str());
        stat.data.materialThresholdAngle = form.staticMaterialThresholdAngle;
        stat.data.materialObj = ResolveAs<RE::BGSMaterialObject>(form.staticMaterialObject);
        stat.data.flags = static_cast<RE::TESObjectSTATData::Flag>(form.staticFlags);
    }

    bool ConfigureStatic(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* stat = tesForm ? tesForm->As<RE::TESObjectSTAT>() : nullptr;
        if (!stat) return false;
        stat->SetFormEditorID(form.editorId.c_str());
        ApplyStaticData(*stat, form);
        ApplyRecordFlags(*stat, form.recordFlags, (1u << 2) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 9) | (1u << 15) | (1u << 17) | (1u << 19) | (1u << 23) | (1u << 25) | (1u << 26) | (1u << 27) | (1u << 28) | (1u << 30));
        return true;
    }

    bool ConfigureMovableStatic(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* stat = tesForm ? tesForm->As<RE::BGSMovableStatic>() : nullptr;
        if (!stat) return false;
        stat->SetFormEditorID(form.editorId.c_str());
        stat->fullName = form.fullName.c_str();
        ApplyStaticData(static_cast<RE::TESObjectSTAT&>(*stat), form);
        stat->soundLoop = ResolveAs<RE::BGSSoundDescriptorForm>(form.movableStaticSoundLoop);
        stat->data.flags = static_cast<RE::MOVABLE_STATIC_DATA::Flag>(form.movableStaticFlags);
        ApplyRecordFlags(*stat, form.recordFlags, (1u << 8) | (1u << 9) | (1u << 15) | (1u << 16) | (1u << 19) | (1u << 25) | (1u << 26) | (1u << 27) | (1u << 30));
        return true;
    }

    bool ConfigureDoor(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* door = tesForm ? tesForm->As<RE::TESObjectDOOR>() : nullptr;
        if (!door) return false;
        door->SetFormEditorID(form.editorId.c_str());
        door->fullName = form.fullName.c_str();
        door->SetModel(form.modelPath.c_str());
        door->openSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.doorOpenSound);
        door->closeSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.doorCloseSound);
        door->loopSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.doorLoopSound);
        door->flags = static_cast<RE::TESObjectDOOR::Flag>(form.doorFlags);
        ApplyRecordFlags(*door, form.recordFlags, (1u << 15) | (1u << 16) | (1u << 23));
        return true;
    }

    template <std::size_t N, class T>
    void CopyFloatArray(T& destination, const std::array<float, N>& source) {
        static_assert(sizeof(T) >= sizeof(float) * N);
        std::copy(source.begin(), source.end(), reinterpret_cast<float*>(std::addressof(destination)));
    }

    bool ConfigureCombatStyle(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* style = tesForm ? tesForm->As<RE::TESCombatStyle>() : nullptr;
        if (!style) return false;
        style->SetFormEditorID(form.editorId.c_str());
        CopyFloatArray(style->generalData, form.combatGeneral);
        CopyFloatArray(style->meleeData, form.combatMelee);
        CopyFloatArray(style->closeRangeData, form.combatCloseRange);
        style->longRangeData.strafeMult = form.combatLongRangeStrafe;
        CopyFloatArray(style->flightData, form.combatFlight);
        style->flags = static_cast<RE::TESCombatStyle::FLAG>(form.combatStyleFlags);
        return true;
    }

    bool ConfigureSoundCategory(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* category = tesForm ? tesForm->As<RE::BGSSoundCategory>() : nullptr;
        if (!category) return false;
        category->SetFormEditorID(form.editorId.c_str());
        category->fullName = form.fullName.c_str();
        category->flags = static_cast<RE::BGSSoundCategory::Flag>(form.soundCategoryFlags);
        category->parentCategory = ResolveAs<RE::BGSSoundCategory>(form.soundCategoryParent);
        category->attenuation = form.soundCategoryAttenuation;
        category->SetStaticVolumeMultiplier(form.soundCategoryStaticMult);
        category->SetDefaultMenuValue(form.soundCategoryDefaultMenuValue);
        category->volumeMult = form.soundCategoryVolumeMult;
        category->frequencyMult = form.soundCategoryFrequencyMult;
        return true;
    }

    bool ConfigureClass(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* npcClass = tesForm ? tesForm->As<RE::TESClass>() : nullptr;
        if (!npcClass) return false;
        npcClass->SetFormEditorID(form.editorId.c_str());
        npcClass->fullName = form.fullName.c_str();
        npcClass->data.teaches = static_cast<RE::CLASS_DATA::Skill>(std::clamp(form.classTeachesSkill, 0u, 17u));
        npcClass->data.maximumTrainingLevel = form.classMaximumTrainingLevel;
        std::copy(form.classSkillWeights.begin(), form.classSkillWeights.end(), std::addressof(npcClass->data.skillWeights.oneHanded));
        npcClass->data.bleedoutDefault = form.classBleedoutDefault;
        npcClass->data.voicePoints = form.classVoicePoints;
        std::copy(form.classAttributeWeights.begin(), form.classAttributeWeights.end(), std::addressof(npcClass->data.attributeWeights.health));
        static_cast<RE::TESTexture&>(*npcClass).textureName = form.classIconPath.c_str();
        if (!form.description.empty()) logger::debug("Class '{}' description persisted but not assigned to TESDescription.", form.editorId);
        return true;
    }

    void ApplyProduceData(RE::TESProduceForm& produce, const DynamicForms::DynamicForm& form) {
        produce.harvestSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.harvestSound);
        produce.produceItem = ResolveAs<RE::TESBoundObject>(form.produceItem);
        std::copy(form.produceChance.begin(), form.produceChance.end(), produce.produceChance);
    }

    bool ConfigureFlora(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* flora = tesForm ? tesForm->As<RE::TESFlora>() : nullptr;
        if (!flora) return false;
        flora->SetFormEditorID(form.editorId.c_str());
        flora->fullName = form.fullName.c_str();
        flora->SetModel(form.modelPath.c_str());
        flora->soundLoop = ResolveAs<RE::BGSSoundDescriptorForm>(form.floraSoundLoop);
        flora->soundActivate = ResolveAs<RE::BGSSoundDescriptorForm>(form.floraSoundActivate);
        flora->waterForm = ResolveAs<RE::TESWaterForm>(form.floraWaterType);
        flora->flags = static_cast<RE::TESObjectACTI::ActiFlags>(form.floraFlags);
        ApplyKeywords(static_cast<RE::BGSKeywordForm&>(*flora), form.keywords);
        ApplyProduceData(static_cast<RE::TESProduceForm&>(*flora), form);
        return true;
    }

    bool ConfigureTree(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* tree = tesForm ? tesForm->As<RE::TESObjectTREE>() : nullptr;
        if (!tree) return false;
        tree->SetFormEditorID(form.editorId.c_str());
        tree->fullName = form.fullName.c_str();
        tree->SetModel(form.modelPath.c_str());
        CopyFloatArray(tree->data, form.treeAnimation);
        tree->type = static_cast<RE::TESObjectTREE::etTreeType>(std::clamp(form.treeType, 0u, 3u));
        ApplyProduceData(static_cast<RE::TESProduceForm&>(*tree), form);
        ApplyRecordFlags(*tree, form.recordFlags, 1u << 15);
        return true;
    }

    void ApplyContainerEntries(RE::TESContainer& target, const std::vector<DynamicForms::ContainerEntry>& entries, const std::string_view editorId, const bool applyExtras) {
        target.ClearDataComponent();
        std::vector<RE::ContainerObject*> runtimeEntries;
        for (const auto& component : entries) {
            auto* item = ResolveAs<RE::TESBoundObject>(component.item);
            if (!item) {
                logger::warn("Container '{}' item '{}' is not a resolvable bound object.", editorId, component.item.Display());
                continue;
            }
            auto* owner = applyExtras ? ResolveConfigForm(component.owner) : nullptr;
            auto* runtimeEntry = new RE::ContainerObject(item, std::max(1, component.count));
            const bool needsExtra = applyExtras && (owner || !component.conditionGlobal.empty() || component.requiredRank != 0 || component.healthMult != 100.0F);
            if (needsExtra && !runtimeEntry->itemExtra) runtimeEntry->itemExtra = new RE::ContainerItemExtra(owner);
            if (runtimeEntry->itemExtra) {
                runtimeEntry->itemExtra->owner = owner;
                if (auto* global = ResolveAs<RE::TESGlobal>(component.conditionGlobal)) {
                    runtimeEntry->itemExtra->conditional.global = global;
                } else {
                    runtimeEntry->itemExtra->conditional.rank = component.requiredRank;
                }
                runtimeEntry->itemExtra->healthMult = component.healthMult;
            }
            runtimeEntries.push_back(runtimeEntry);
        }
        if (!runtimeEntries.empty()) {
            target.containerObjects = RE::calloc<RE::ContainerObject*>(runtimeEntries.size());
            std::ranges::copy(runtimeEntries, target.containerObjects);
            target.numContainerObjects = static_cast<std::uint32_t>(runtimeEntries.size());
        }
    }

    bool ConfigureConstructibleObject(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* recipe = tesForm ? tesForm->As<RE::BGSConstructibleObject>() : nullptr;
        if (!recipe) {
            logger::warn("Dynamic form '{}' is not a BGSConstructibleObject", form.editorId);
            return false;
        }

        recipe->SetFormEditorID(form.editorId.c_str());
        recipe->createdItem = ResolveAs<RE::TESBoundObject>(form.createdItem);
        recipe->benchKeyword = ResolveAs<RE::BGSKeyword>(form.benchKeyword);
        recipe->data.numConstructed = std::max<std::uint16_t>(1, form.numConstructed);
        ApplyContainerEntries(recipe->requiredItems, form.requiredItems, form.editorId, false);
        ApplyConditions(recipe->conditions, form.conditions);

        if (!recipe->createdItem) {
            logger::warn("Recipe '{}' has no valid created item and will not be craftable.", form.editorId);
        }
        if (!recipe->benchKeyword) {
            logger::warn("Recipe '{}' has no valid bench keyword and will not appear at a workbench.", form.editorId);
        }
        logger::info("Configured recipe '{}' result={} count={} components={} conditions={}.",
            form.editorId,
            form.createdItem.Display(),
            recipe->data.numConstructed,
            recipe->requiredItems.numContainerObjects,
            form.conditions.size());
        return true;
    }

    bool ConfigureContainer(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* container = tesForm ? tesForm->As<RE::TESObjectCONT>() : nullptr;
        if (!container) {
            logger::warn("Dynamic form '{}' is not a TESObjectCONT", form.editorId);
            return false;
        }
        container->SetFormEditorID(form.editorId.c_str());
        container->fullName = form.fullName.c_str();
        container->SetModel(form.modelPath.c_str());
        container->weight = form.itemWeight;
        container->data.flags = static_cast<RE::CONT_DATA::Flag>(form.containerFlags);
        container->openSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.containerOpenSound);
        container->closeSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.containerCloseSound);
        ApplyContainerEntries(static_cast<RE::TESContainer&>(*container), form.containerItems, form.editorId, true);
        container->allowStolenItems = form.containerAllowStolenItems;
        ApplyRecordFlags(*container, form.recordFlags, (1u << 15) | (1u << 16) | (1u << 25) | (1u << 26) | (1u << 27) | (1u << 30));
        logger::info("Configured container '{}' items={} flags={:02X}.", form.editorId, container->numContainerObjects, form.containerFlags);
        return true;
    }

    bool ConfigureMagicEffect(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* effect = tesForm ? tesForm->As<RE::EffectSetting>() : nullptr;
        if (!effect) {
            logger::warn("Dynamic form '{}' is not an EffectSetting", form.editorId);
            return false;
        }

        effect->SetFormEditorID(form.editorId.c_str());
        effect->fullName = form.fullName.c_str();
        effect->menuDispObject = ResolveAs<RE::TESBoundObject>(form.menuDisplayObject);
        ApplyKeywords(static_cast<RE::BGSKeywordForm&>(*effect), form.keywords);

        auto& data = effect->data;
        data.flags = static_cast<RE::EffectSetting::EffectSettingData::Flag>(form.magicEffectFlags);
        data.baseCost = form.magicEffectBaseCost;
        data.associatedForm = ResolveConfigForm(form.magicEffectAssociatedForm);
        data.associatedSkill = static_cast<RE::ActorValue>(form.magicEffectAssociatedSkill);
        data.resistVariable = static_cast<RE::ActorValue>(form.magicEffectResistVariable);
        data.light = ResolveAs<RE::TESObjectLIGH>(form.magicEffectLight);
        data.taperWeight = form.magicEffectTaperWeight;
        data.effectShader = ResolveAs<RE::TESEffectShader>(form.magicEffectShader);
        data.enchantShader = ResolveAs<RE::TESEffectShader>(form.magicEffectEnchantShader);
        data.minimumSkill = form.magicEffectMinimumSkill;
        data.spellmakingArea = form.magicEffectSpellmakingArea;
        data.spellmakingChargeTime = form.magicEffectSpellmakingChargeTime;
        data.taperCurve = form.magicEffectTaperCurve;
        data.taperDuration = form.magicEffectTaperDuration;
        data.secondAVWeight = form.magicEffectSecondAVWeight;
        data.archetype = static_cast<RE::EffectSetting::Archetype>(std::clamp(form.magicEffectArchetype, -1, 46));
        data.primaryAV = static_cast<RE::ActorValue>(form.magicEffectPrimaryAV);
        data.projectileBase = ResolveAs<RE::BGSProjectile>(form.magicEffectProjectile);
        data.explosion = ResolveAs<RE::BGSExplosion>(form.magicEffectExplosion);
        data.castingType = static_cast<RE::MagicSystem::CastingType>(std::clamp(form.magicEffectCastingType, 0u, 3u));
        data.delivery = static_cast<RE::MagicSystem::Delivery>(std::clamp(form.magicEffectDelivery, 0u, 5u));
        data.secondaryAV = static_cast<RE::ActorValue>(form.magicEffectSecondaryAV);
        data.castingArt = ResolveAs<RE::BGSArtObject>(form.magicEffectCastingArt);
        data.hitEffectArt = ResolveAs<RE::BGSArtObject>(form.magicEffectHitEffectArt);
        data.impactDataSet = ResolveAs<RE::BGSImpactDataSet>(form.magicEffectImpactDataSet);
        data.skillUsageMult = form.magicEffectSkillUsageMult;
        data.dualCastData = ResolveAs<RE::BGSDualCastData>(form.magicEffectDualCastData);
        data.dualCastScale = form.magicEffectDualCastScale;
        data.enchantEffectArt = ResolveAs<RE::BGSArtObject>(form.magicEffectEnchantEffectArt);
        data.hitVisuals = ResolveAs<RE::BGSReferenceEffect>(form.magicEffectHitVisuals);
        data.enchantVisuals = ResolveAs<RE::BGSReferenceEffect>(form.magicEffectEnchantVisuals);
        data.equipAbility = ResolveAs<RE::SpellItem>(form.magicEffectEquipAbility);
        data.imageSpaceMod = ResolveAs<RE::TESImageSpaceModifier>(form.magicEffectImageSpaceMod);
        data.perk = ResolveAs<RE::BGSPerk>(form.magicEffectPerk);
        data.castingSoundLevel = static_cast<RE::SOUND_LEVEL>(std::clamp(form.magicEffectCastingSoundLevel, 0u, 4u));
        data.aiScore = form.magicEffectAIScore;
        data.aiDelayTimer = form.magicEffectAIDelayTime;

        effect->counterEffects.clear();
        for (auto it = form.magicEffectCounterEffects.rbegin(); it != form.magicEffectCounterEffects.rend(); ++it) {
            if (auto* counter = ResolveAs<RE::EffectSetting>(*it)) {
                effect->counterEffects.push_front(counter);
            }
        }
        data.numCounterEffects = static_cast<std::int16_t>(std::min<std::size_t>(effect->counterEffects.size(), std::numeric_limits<std::int16_t>::max()));

        effect->effectSounds.clear();
        for (std::size_t i = 0; i < form.magicEffectSounds.size(); ++i) {
            auto* sound = ResolveAs<RE::BGSSoundDescriptorForm>(form.magicEffectSounds[i]);
            if (!sound) {
                continue;
            }
            RE::EffectSetting::SoundPair pair{};
            pair.id = static_cast<RE::MagicSystem::SoundID>(i);
            pair.sound = sound;
            effect->effectSounds.push_back(pair);
        }

        effect->magicItemDescription = form.magicItemDescription.c_str();
        ApplyConditions(effect->conditions, form.conditions);
        logger::info("Configured magic effect '{}' FormID={:08X} archetype={} casting={} delivery={} flags={:08X} counters={} sounds={} conditions={}.",
            form.editorId,
            effect->GetFormID(),
            form.magicEffectArchetype,
            form.magicEffectCastingType,
            form.magicEffectDelivery,
            form.magicEffectFlags,
            effect->counterEffects.size(),
            effect->effectSounds.size(),
            form.conditions.size());
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
        armor->fullName = form.fullName.c_str();
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

        ApplyKeywords(static_cast<RE::BGSKeywordForm&>(*armor), form.keywords);
        logger::info("Configured armor '{}' with {} armor add-ons and {} keywords.", form.editorId, armor->armorAddons.size(), armor->GetNumKeywords());
        return true;
    }

    bool ConfigureColor(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* color = tesForm ? tesForm->As<RE::BGSColorForm>() : nullptr;
        if (!color) {
            logger::warn("Dynamic form '{}' is not a BGSColorForm", form.editorId);
            return false;
        }

        color->SetFormEditorID(form.editorId.c_str());
        color->fullName = form.fullName.c_str();
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

    struct QuestPerkEntryLayout : RE::BGSPerkEntry
    {
        RE::TESQuest* quest;
        std::uint8_t questStage;
        std::uint8_t pad19[7];
    };
    static_assert(offsetof(QuestPerkEntryLayout, quest) == 0x10);
    static_assert(offsetof(QuestPerkEntryLayout, questStage) == 0x18);
    static_assert(sizeof(QuestPerkEntryLayout) == 0x20);

    struct EntryPointFunctionDataTwoValueLayout : RE::BGSEntryPointFunctionData
    {
        float value1;
        float value2;
    };
    static_assert(sizeof(EntryPointFunctionDataTwoValueLayout) == 0x10);

    struct EntryPointFunctionDataLeveledListLayout : RE::BGSEntryPointFunctionData
    {
        RE::TESLevItem* leveledList;
    };
    static_assert(sizeof(EntryPointFunctionDataLeveledListLayout) == 0x10);

    struct EntryPointFunctionDataBooleanGraphVariableLayout : RE::BGSEntryPointFunctionData
    {
        RE::BSFixedString variableName;
    };
    static_assert(sizeof(EntryPointFunctionDataBooleanGraphVariableLayout) == 0x10);

    void ValidatePerkEntryRuntimeLayouts() {
        questPerkEntryLayoutValid = true;
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            logger::warn("Could not validate perk entry layouts: TESDataHandler is unavailable.");
            return;
        }

        const auto& quests = dataHandler->GetFormArray<RE::TESQuest>();
        const auto& perks = dataHandler->GetFormArray<RE::BGSPerk>();
        for (const auto* perk : perks) {
            if (!perk) {
                continue;
            }
            for (const auto* base : perk->perkEntries) {
                if (!base || base->GetType() != RE::PERK_ENTRY_TYPE::kQuest) {
                    continue;
                }

                const auto* entry = reinterpret_cast<const QuestPerkEntryLayout*>(base);
                const auto found = std::ranges::find(quests, entry->quest);
                if (!entry->quest || found == quests.end()) {
                    questPerkEntryLayoutValid = false;
                    logger::error(
                        "BGSQuestPerkEntry runtime layout validation failed for perk '{}'; "
                        "DFG Quest perk entries will be disabled.",
                        perk->GetFormEditorID());
                    return;
                }

                logger::info(
                    "Validated BGSQuestPerkEntry layout using perk '{}' -> quest '{}' stage {} "
                    "(quest=0x10, stage=0x18, size=0x20).",
                    perk->GetFormEditorID(),
                    entry->quest->GetFormEditorID(),
                    entry->questStage);
                return;
            }
        }

        logger::info(
            "No loaded Quest perk entry was available for live layout validation; "
            "using the verified 0x20 layout.");
    }

    RE::BGSEntryPointPerkEntry* CreateEntryPointPerkEntryObject() {
        auto* entry = RE::calloc<RE::BGSEntryPointPerkEntry>(1);
        SetRuntimeVTable(entry, RE::VTABLE_BGSEntryPointPerkEntry[0]);
        return entry;
    }

    RE::BGSAbilityPerkEntry* CreateAbilityPerkEntryObject() {
        auto* entry = RE::calloc<RE::BGSAbilityPerkEntry>(1);
        SetRuntimeVTable(entry, RE::VTABLE_BGSAbilityPerkEntry[0]);
        return entry;
    }

    QuestPerkEntryLayout* CreateQuestPerkEntryObject() {
        auto* entry = RE::calloc<QuestPerkEntryLayout>(1);
        SetRuntimeVTable(entry, RE::VTABLE_BGSQuestPerkEntry[0]);
        return entry;
    }

    DynamicForms::PerkFunctionDataKind ExpectedPerkFunctionDataKind(const std::uint32_t function) {
        using Function = RE::BGSEntryPointFunction::ENTRY_POINT_FUNCTION;
        switch (static_cast<Function>(function)) {
        case Function::kSetValue:
        case Function::kAddValue:
        case Function::kMultiplyValue:
            return DynamicForms::PerkFunctionDataKind::OneValue;
        case Function::kAddRangeToValue:
            return DynamicForms::PerkFunctionDataKind::TwoValue;
        case Function::kAddActorValueMult:
        case Function::kSetToActorValueMult:
        case Function::kMultiplyActorValueMult:
        case Function::kMultiplyOnePlusActorValueMult:
            return DynamicForms::PerkFunctionDataKind::ActorValueAndValue;
        case Function::kAddLeveledList:
            return DynamicForms::PerkFunctionDataKind::LeveledList;
        case Function::kAddActivateChoice:
            return DynamicForms::PerkFunctionDataKind::ActivateChoice;
        case Function::kSelectSpell:
            return DynamicForms::PerkFunctionDataKind::Spell;
        case Function::kSelectText:
            return DynamicForms::PerkFunctionDataKind::BooleanGraphVariable;
        case Function::kSetText:
            return DynamicForms::PerkFunctionDataKind::Text;
        case Function::kAbsoluteValue:
        case Function::kNegativeAbsoluteValue:
        case Function::kNullFunction:
        default:
            return DynamicForms::PerkFunctionDataKind::None;
        }
    }

    const RE::BGSEntryPoint::EntryPoint* GetPerkEntryPointDefinition(const std::uint32_t entryPoint) {
        if (entryPoint >= static_cast<std::uint32_t>(RE::BGSEntryPoint::ENTRY_POINT::kTotal)) {
            return nullptr;
        }
        return RE::BGSEntryPoint::GetEntryPoint(
            static_cast<RE::BGSEntryPoint::ENTRY_POINT>(entryPoint));
    }

    const RE::BGSEntryPointFunction::EntryPointFunction* GetPerkFunctionDefinition(
        const std::uint32_t function)
    {
        if (function >= static_cast<std::uint32_t>(
                            RE::BGSEntryPointFunction::ENTRY_POINT_FUNCTION::kTotal)) {
            return nullptr;
        }
        return RE::BGSEntryPointFunction::GetEntryPointFunction(
            static_cast<RE::BGSEntryPointFunction::ENTRY_POINT_FUNCTION>(function));
    }

    bool IsPerkFunctionCompatible(const std::uint32_t entryPoint, const std::uint32_t function) {
        const auto* entryPointDefinition = GetPerkEntryPointDefinition(entryPoint);
        const auto* functionDefinition = GetPerkFunctionDefinition(function);
        return entryPointDefinition && functionDefinition &&
               entryPointDefinition->functionType == functionDefinition->type;
    }

    std::uint32_t NativePerkConditionTabCount(const std::uint32_t entryPoint) {
        const auto* definition = GetPerkEntryPointDefinition(entryPoint);
        return definition ? definition->parameters.count : 0U;
    }

    void NormalizePerkForm(DynamicForms::DynamicForm& form) {
        if (form.kind != DynamicForms::FormKind::Perk) {
            return;
        }
        for (auto& entry : form.entries) {
            if (entry.kind != DynamicForms::PerkEntryKind::EntryPoint) {
                continue;
            }
            entry.numArgs = NativePerkConditionTabCount(entry.entryPoint);
            entry.functionData.kind = ExpectedPerkFunctionDataKind(entry.function);
        }
    }

    void ValidateCondition(
        const DynamicForms::PerkCondition& condition,
        const std::string_view path,
        std::vector<std::string>& errors)
    {
        const auto functionId = FunctionIdForCondition(condition);
        if (functionId >= static_cast<std::uint32_t>(RE::FUNCTION_DATA::FunctionID::kTotal)) {
            errors.push_back(std::format("{} uses invalid condition function ID {}.", path, functionId));
        }
        if (condition.opCode > 5U) {
            errors.push_back(std::format("{} uses invalid comparison operator {}.", path, condition.opCode));
        }
        if (condition.runOn > 8U) {
            errors.push_back(std::format("{} uses invalid Run On value {}.", path, condition.runOn));
        }

        if (condition.useGlobalComparison) {
            const auto* comparisonGlobal =
                ResolveConfigForm(condition.comparisonGlobal);
            if (!comparisonGlobal ||
                !comparisonGlobal->Is(RE::FormType::Global)) {
                errors.push_back(std::format(
                    "{} requires a valid Global for global comparison.",
                    path));
            }
        }
        if (condition.runOn == 2U) {
            const auto* runOnForm = ResolveConfigForm(condition.runOnRef);
            if (!runOnForm || !runOnForm->Is(RE::FormType::Reference)) {
                errors.push_back(std::format(
                    "{} requires a valid placed reference when Run On is Reference.",
                    path));
            }
        }

        const auto* function = ConditionCatalog::FindFunction(functionId);
        if (!function) {
            return;
        }
        const std::array params{
            std::pair{ function->rawParam1, std::string_view(condition.param1) },
            std::pair{ function->rawParam2, std::string_view(condition.param2) }
        };
        for (std::size_t index = 0; index < params.size(); ++index) {
            const auto [rawType, value] = params[index];
            if (value.empty()) {
                continue;
            }
            try {
                if (ConditionCatalog::IsFormParam(rawType)) {
                    if (!ResolveConfigForm(std::string(value))) {
                        errors.push_back(std::format(
                            "{} parameter {} does not resolve to a form: '{}'.",
                            path,
                            index + 1,
                            value));
                    }
                } else if (ConditionCatalog::IsIntegerParam(rawType)) {
                    std::size_t parsed = 0;
                    const auto parsedValue =
                        std::stoll(std::string(value), &parsed, 0);
                    static_cast<void>(parsedValue);
                    if (parsed != value.size()) {
                        throw std::invalid_argument("trailing characters");
                    }
                } else if (ConditionCatalog::IsFloatParam(rawType)) {
                    std::size_t parsed = 0;
                    const auto parsedValue =
                        std::stof(std::string(value), &parsed);
                    static_cast<void>(parsedValue);
                    if (parsed != value.size()) {
                        throw std::invalid_argument("trailing characters");
                    }
                }
            } catch (...) {
                errors.push_back(std::format(
                    "{} parameter {} is not valid for {}: '{}'.",
                    path,
                    index + 1,
                    rawType,
                    value));
            }
        }
    }

    bool ValidatePerkForm(
        const DynamicForms::DynamicForm& form,
        std::vector<std::string>& errors)
    {
        if (form.numRanks < 1) {
            errors.emplace_back("PERK numRanks must be between 1 and 127.");
        }
        if (!form.nextPerk.empty() && !ResolveAs<RE::BGSPerk>(form.nextPerk)) {
            errors.push_back(std::format(
                "Next perk '{}' does not resolve to a PERK.",
                form.nextPerk.Display()));
        }

        for (std::size_t index = 0; index < form.conditions.size(); ++index) {
            ValidateCondition(
                form.conditions[index],
                std::format("PERK condition {}", index),
                errors);
        }

        const auto rankCount = static_cast<std::uint32_t>(
            std::max<std::int32_t>(form.numRanks, 1));
        for (std::size_t index = 0; index < form.entries.size(); ++index) {
            const auto& entry = form.entries[index];
            const auto path = std::format("PERK entry {}", index);
            if (entry.rank > 255U) {
                errors.push_back(std::format("{} rank {} exceeds 255.", path, entry.rank));
            } else if (entry.rank >= rankCount) {
                errors.push_back(std::format(
                    "{} rank {} is outside numRanks {}.",
                    path,
                    entry.rank,
                    rankCount));
            }
            if (entry.priority > 255U) {
                errors.push_back(std::format("{} priority {} exceeds 255.", path, entry.priority));
            }

            if (entry.kind == DynamicForms::PerkEntryKind::Quest) {
                if (!ResolveAs<RE::TESQuest>(entry.quest)) {
                    errors.push_back(std::format(
                        "{} requires a valid Quest reference.",
                        path));
                }
                if (entry.questStage > 255U) {
                    errors.push_back(std::format(
                        "{} quest stage {} exceeds 255.",
                        path,
                        entry.questStage));
                }
                continue;
            }
            if (entry.kind == DynamicForms::PerkEntryKind::Ability) {
                const auto* ability = ResolveAs<RE::SpellItem>(entry.ability);
                if (!ability) {
                    errors.push_back(std::format(
                        "{} requires a valid Spell reference.",
                        path));
                } else if (ability->GetSpellType() != RE::MagicSystem::SpellType::kAbility) {
                    errors.push_back(std::format(
                        "{} spell '{}' is not an Ability.",
                        path,
                        entry.ability.Display()));
                }
                continue;
            }

            const auto* entryPointDefinition = GetPerkEntryPointDefinition(entry.entryPoint);
            const auto* functionDefinition = GetPerkFunctionDefinition(entry.function);
            if (!entryPointDefinition) {
                errors.push_back(std::format(
                    "{} uses invalid entry point {}.",
                    path,
                    entry.entryPoint));
                continue;
            }
            if (!functionDefinition) {
                errors.push_back(std::format(
                    "{} uses invalid function {}.",
                    path,
                    entry.function));
            } else if (!IsPerkFunctionCompatible(entry.entryPoint, entry.function)) {
                errors.push_back(std::format(
                    "{} function '{}' is incompatible with entry point '{}'.",
                    path,
                    functionDefinition->name ? functionDefinition->name : "Unknown",
                    entryPointDefinition->name ? entryPointDefinition->name : "Unknown"));
            }

            const auto nativeTabCount = entryPointDefinition->parameters.count;
            if (entry.numArgs != nativeTabCount) {
                errors.push_back(std::format(
                    "{} has {} condition tabs in JSON, but '{}' requires {}.",
                    path,
                    entry.numArgs,
                    entryPointDefinition->name ? entryPointDefinition->name : "Unknown",
                    nativeTabCount));
            }
            std::set<std::uint32_t> tabIndices;
            for (const auto& tab : entry.conditionTabs) {
                if (tab.index >= nativeTabCount) {
                    errors.push_back(std::format(
                        "{} condition tab {} is outside the native range 0..{}.",
                        path,
                        tab.index,
                        nativeTabCount > 0 ? nativeTabCount - 1 : 0));
                    continue;
                }
                if (!tabIndices.insert(tab.index).second) {
                    errors.push_back(std::format(
                        "{} contains duplicate condition tab {}.",
                        path,
                        tab.index));
                }
                for (std::size_t conditionIndex = 0;
                     conditionIndex < tab.conditions.size();
                     ++conditionIndex) {
                    ValidateCondition(
                        tab.conditions[conditionIndex],
                        std::format(
                            "{} tab {} condition {}",
                            path,
                            tab.index,
                            conditionIndex),
                        errors);
                }
            }

            switch (ExpectedPerkFunctionDataKind(entry.function)) {
            case DynamicForms::PerkFunctionDataKind::ActorValueAndValue:
                if (entry.functionData.actorValue != std::numeric_limits<std::uint32_t>::max() &&
                    entry.functionData.actorValue >=
                        static_cast<std::uint32_t>(RE::ActorValue::kTotal)) {
                    errors.push_back(std::format(
                        "{} uses invalid actor value {}.",
                        path,
                        entry.functionData.actorValue));
                }
                break;
            case DynamicForms::PerkFunctionDataKind::LeveledList:
                if (!ResolveAs<RE::TESLevItem>(entry.functionData.form)) {
                    errors.push_back(std::format(
                        "{} requires a valid Leveled Item function-data reference.",
                        path));
                }
                break;
            case DynamicForms::PerkFunctionDataKind::Spell:
                if (!ResolveAs<RE::SpellItem>(entry.functionData.form)) {
                    errors.push_back(std::format(
                        "{} requires a valid Spell function-data reference.",
                        path));
                }
                break;
            case DynamicForms::PerkFunctionDataKind::ActivateChoice:
                if (!entry.functionData.form.empty() &&
                    !ResolveAs<RE::SpellItem>(entry.functionData.form)) {
                    errors.push_back(std::format(
                        "{} Activate Choice spell '{}' does not resolve.",
                        path,
                        entry.functionData.form.Display()));
                }
                if ((entry.functionData.flags & ~0x3U) != 0U) {
                    errors.push_back(std::format(
                        "{} Activate Choice uses unsupported flags 0x{:X}.",
                        path,
                        entry.functionData.flags));
                }
                if (entry.functionData.fragmentIndex > 65535U) {
                    errors.push_back(std::format(
                        "{} Activate Choice fragment index {} exceeds 65535.",
                        path,
                        entry.functionData.fragmentIndex));
                }
                break;
            case DynamicForms::PerkFunctionDataKind::BooleanGraphVariable:
                if (entry.functionData.text.empty()) {
                    errors.push_back(std::format(
                        "{} requires a graph-variable name.",
                        path));
                }
                break;
            case DynamicForms::PerkFunctionDataKind::Text:
                if (entry.functionData.text.empty()) {
                    errors.push_back(std::format("{} requires text.", path));
                }
                break;
            default:
                break;
            }
        }
        return errors.empty();
    }

    std::string JoinValidationErrors(const std::vector<std::string>& errors) {
        std::string message;
        constexpr std::size_t maxErrors = 4;
        for (std::size_t index = 0; index < std::min(errors.size(), maxErrors); ++index) {
            if (!message.empty()) {
                message += " ";
            }
            message += errors[index];
        }
        if (errors.size() > maxErrors) {
            message += std::format(" (and {} more)", errors.size() - maxErrors);
        }
        return message;
    }

    RE::BGSEntryPointFunctionData* CreatePerkFunctionData(
        RE::BGSPerk& perk,
        const DynamicForms::PerkEntry& source)
    {
        const auto kind = ExpectedPerkFunctionDataKind(source.function);
        RE::BGSEntryPointFunctionData* result = nullptr;
        switch (kind) {
        case DynamicForms::PerkFunctionDataKind::None:
            return nullptr;
        case DynamicForms::PerkFunctionDataKind::OneValue: {
            auto* value = RE::calloc<RE::BGSEntryPointFunctionDataOneValue>(1);
            SetRuntimeVTable(value, RE::VTABLE_BGSEntryPointFunctionDataOneValue[0]);
            if (value) {
                value->data = source.functionData.value1;
            }
            result = value;
            break;
        }
        case DynamicForms::PerkFunctionDataKind::TwoValue:
        case DynamicForms::PerkFunctionDataKind::ActorValueAndValue: {
            auto* value = RE::calloc<EntryPointFunctionDataTwoValueLayout>(1);
            SetRuntimeVTable(value, RE::VTABLE_BGSEntryPointFunctionDataTwoValue[0]);
            if (value) {
                if (kind == DynamicForms::PerkFunctionDataKind::ActorValueAndValue) {
                    const auto actorValue = static_cast<float>(
                        static_cast<std::int32_t>(source.functionData.actorValue));
                    std::memcpy(&value->value1, &actorValue, sizeof(actorValue));
                } else {
                    value->value1 = source.functionData.value1;
                }
                value->value2 = source.functionData.value2;
            }
            result = value;
            break;
        }
        case DynamicForms::PerkFunctionDataKind::LeveledList: {
            auto* value = RE::calloc<EntryPointFunctionDataLeveledListLayout>(1);
            SetRuntimeVTable(value, RE::VTABLE_BGSEntryPointFunctionDataLeveledList[0]);
            if (value) {
                value->leveledList = ResolveAs<RE::TESLevItem>(source.functionData.form);
            }
            result = value;
            break;
        }
        case DynamicForms::PerkFunctionDataKind::ActivateChoice: {
            auto* value = RE::calloc<RE::BGSEntryPointFunctionDataActivateChoice>(1);
            SetRuntimeVTable(value, RE::VTABLE_BGSEntryPointFunctionDataActivateChoice[0]);
            if (value) {
                std::construct_at(&value->label);
                value->label = source.functionData.buttonLabel.c_str();
                value->perk = &perk;
                value->appliedSpell = ResolveAs<RE::SpellItem>(source.functionData.form);
                value->flags = static_cast<RE::BGSEntryPointFunctionDataActivateChoice::Flag>(
                    source.functionData.flags & 0x3U);
                value->id = static_cast<std::uint16_t>(
                    std::min(source.functionData.fragmentIndex, 65535U));
            }
            result = value;
            break;
        }
        case DynamicForms::PerkFunctionDataKind::Spell: {
            auto* value = RE::calloc<RE::BGSEntryPointFunctionDataSpellItem>(1);
            SetRuntimeVTable(value, RE::VTABLE_BGSEntryPointFunctionDataSpellItem[0]);
            if (value) {
                value->spell = ResolveAs<RE::SpellItem>(source.functionData.form);
            }
            result = value;
            break;
        }
        case DynamicForms::PerkFunctionDataKind::BooleanGraphVariable: {
            auto* value = RE::calloc<EntryPointFunctionDataBooleanGraphVariableLayout>(1);
            SetRuntimeVTable(value, RE::VTABLE_BGSEntryPointFunctionDataBooleanGraphVariable[0]);
            if (value) {
                std::construct_at(&value->variableName);
                value->variableName = source.functionData.text.c_str();
            }
            result = value;
            break;
        }
        case DynamicForms::PerkFunctionDataKind::Text: {
            auto* value = RE::calloc<RE::BGSEntryPointFunctionDataText>(1);
            SetRuntimeVTable(value, RE::VTABLE_BGSEntryPointFunctionDataText[0]);
            if (value) {
                std::construct_at(&value->text);
                value->text = source.functionData.text.c_str();
            }
            result = value;
            break;
        }
        }

        if (!result) {
            logger::warn(
                "Could not allocate function data for perk '{}' function {}.",
                perk.GetFormEditorID(),
                source.function);
        }
        return result;
    }

    RE::BGSStandardSoundDef* CreateStandardSoundDefObject() {
        auto* soundDef = RE::calloc<RE::BGSStandardSoundDef>(1);
        SetRuntimeVTable(soundDef, RE::VTABLE_BGSStandardSoundDef[0]);
        SetRuntimeVTable(&soundDef->soundCharacteristics, RE::VTABLE_BGSStandardSoundDef__SoundPlaybackCharacteristics[0]);
        return soundDef;
    }

    void ClearPerkFunctionData(RE::BGSEntryPointFunctionData*& functionData) {
        if (!functionData) {
            return;
        }

        switch (functionData->GetType()) {
        case RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kActivateChoice: {
            auto* value = static_cast<RE::BGSEntryPointFunctionDataActivateChoice*>(functionData);
            std::destroy_at(&value->label);
            break;
        }
        case RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kBooleanGraphVariable: {
            auto* value = reinterpret_cast<EntryPointFunctionDataBooleanGraphVariableLayout*>(functionData);
            std::destroy_at(&value->variableName);
            break;
        }
        case RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kText: {
            auto* value = static_cast<RE::BGSEntryPointFunctionDataText*>(functionData);
            std::destroy_at(&value->text);
            break;
        }
        default:
            break;
        }

        RE::free(functionData);
        functionData = nullptr;
    }

    void ClearPerkEntries(RE::BGSPerk& perk) {
        for (auto* entry : perk.perkEntries) {
            if (!entry) {
                continue;
            }

            if (entry->GetType() == RE::PERK_ENTRY_TYPE::kEntryPoint) {
                auto* entryPoint = static_cast<RE::BGSEntryPointPerkEntry*>(entry);
                for (auto& condition : entryPoint->conditions) {
                    ClearCondition(condition);
                }
                entryPoint->conditions.clear();
                ClearPerkFunctionData(entryPoint->functionData);
            }
            RE::free(entry);
        }
        perk.perkEntries.clear();
    }

    RE::BGSPerkEntry* CreatePerkEntry(RE::BGSPerk& perk, const DynamicForms::PerkEntry& source) {
        if (source.kind == DynamicForms::PerkEntryKind::Quest) {
            if (!questPerkEntryLayoutValid) {
                logger::warn(
                    "Skipped quest entry for perk '{}' because the runtime layout validation failed.",
                    perk.GetFormEditorID());
                return nullptr;
            }
            auto* quest = ResolveAs<RE::TESQuest>(source.quest);
            if (!quest) {
                logger::warn(
                    "Could not resolve quest entry '{}' for perk '{}'.",
                    source.quest.Display(),
                    perk.GetFormEditorID());
                return nullptr;
            }
            auto* entry = CreateQuestPerkEntryObject();
            if (!entry) {
                logger::warn("Could not allocate BGSQuestPerkEntry for perk '{}'.", perk.GetFormEditorID());
                return nullptr;
            }
            entry->header.rank = static_cast<std::uint8_t>(std::min(source.rank, 255U));
            entry->header.priority = static_cast<std::uint8_t>(std::min(source.priority, 255U));
            entry->quest = quest;
            entry->questStage = static_cast<std::uint8_t>(std::min(source.questStage, 255U));
            entry->SetParent(&perk);
            logger::debug(
                "[PerkEntry] type=Quest quest={} stage={} rank={} priority={}",
                quest->GetFormEditorID(),
                source.questStage,
                source.rank,
                source.priority);
            return entry;
        }

        if (source.kind == DynamicForms::PerkEntryKind::Ability) {
            auto* ability = ResolveAs<RE::SpellItem>(source.ability);
            if (!ability) {
                logger::warn(
                    "Could not resolve ability entry '{}' for perk '{}'.",
                    source.ability.Display(),
                    perk.GetFormEditorID());
                return nullptr;
            }
            if (ability->GetSpellType() != RE::MagicSystem::SpellType::kAbility) {
                logger::warn(
                    "Perk '{}' ability entry references spell '{}' whose spell type is not Ability.",
                    perk.GetFormEditorID(),
                    ability->GetFormEditorID());
            }
            auto* entry = CreateAbilityPerkEntryObject();
            if (!entry) {
                logger::warn("Could not allocate BGSAbilityPerkEntry for perk '{}'.", perk.GetFormEditorID());
                return nullptr;
            }
            entry->header.rank = static_cast<std::uint8_t>(std::min(source.rank, 255U));
            entry->header.priority = static_cast<std::uint8_t>(std::min(source.priority, 255U));
            entry->ability = ability;
            entry->SetParent(&perk);
            logger::debug(
                "[PerkEntry] type=Ability spell={} rank={} priority={}",
                ability->GetFormEditorID(),
                source.rank,
                source.priority);
            return entry;
        }

        auto* entry = CreateEntryPointPerkEntryObject();
        if (!entry) {
            logger::warn("Could not allocate BGSEntryPointPerkEntry for perk '{}'.", perk.GetFormEditorID());
            return nullptr;
        }

        entry->header.rank = static_cast<std::uint8_t>(std::min(source.rank, 255U));
        entry->header.priority = static_cast<std::uint8_t>(std::min(source.priority, 255U));
        entry->entryData.entryPoint = static_cast<RE::BGSPerkEntry::EntryPoint>(std::min(source.entryPoint, 91U));
        entry->entryData.function =
            static_cast<RE::BGSEntryPointPerkEntry::Function>(std::min(source.function, 15U));
        entry->perk = &perk;

        const auto tabCount = NativePerkConditionTabCount(source.entryPoint);
        entry->entryData.numArgs = static_cast<std::uint8_t>(tabCount);
        entry->conditions.resize(tabCount);
        std::vector<std::vector<DynamicForms::PerkCondition>> conditionsByTab(tabCount);
        for (const auto& tab : source.conditionTabs) {
            const auto tabIndex = std::min(tab.index, 254U);
            if (tabIndex < conditionsByTab.size()) {
                auto& conditions = conditionsByTab[tabIndex];
                conditions.insert(conditions.end(), tab.conditions.begin(), tab.conditions.end());
            }
        }
        for (std::size_t tabIndex = 0; tabIndex < conditionsByTab.size(); ++tabIndex) {
            ApplyConditions(entry->conditions[tabIndex], conditionsByTab[tabIndex]);
        }

        entry->functionData = CreatePerkFunctionData(perk, source);
        entry->SetParent(&perk);

        logger::debug("[PerkEntry] type=EntryPoint entryPoint={} function={} rank={} priority={} tabs={}",
            source.entryPoint,
            source.function,
            source.rank,
            source.priority,
            source.conditionTabs.size());
        return entry;
    }

    struct LoadedActorPerkSnapshot
    {
        RE::Actor* actor{ nullptr };
        std::vector<std::pair<RE::TESQuest*, std::uint8_t>> questEntries;
    };

    class MatchingPerkEntriesVisitor final : public RE::PerkEntryVisitor
    {
    public:
        explicit MatchingPerkEntriesVisitor(const RE::BSTArray<RE::BGSPerkEntry*>& candidates) :
            candidates_(candidates)
        {}

        RE::BSContainer::ForEachResult Visit(RE::BGSPerkEntry* entry) override
        {
            if (entry && std::ranges::find(candidates_, entry) != candidates_.end()) {
                matches.push_back(entry);
            }
            return RE::BSContainer::ForEachResult::kContinue;
        }

        std::vector<RE::BGSPerkEntry*> matches;

    private:
        const RE::BSTArray<RE::BGSPerkEntry*>& candidates_;
    };

    std::vector<LoadedActorPerkSnapshot> RemoveAppliedAbilityEntries(RE::BGSPerk& perk) {
        std::vector<LoadedActorPerkSnapshot> snapshots;
        if (perk.perkEntries.empty()) {
            return snapshots;
        }
        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) {
            return snapshots;
        }

        processLists->ForAllActors([&](RE::Actor* actor) {
            if (!actor || !actor->HasPerk(&perk)) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            MatchingPerkEntriesVisitor visitor(perk.perkEntries);
            actor->ForEachPerk(visitor);
            LoadedActorPerkSnapshot snapshot;
            snapshot.actor = actor;
            for (auto* entry : visitor.matches) {
                if (entry->GetType() == RE::PERK_ENTRY_TYPE::kAbility) {
                    entry->RemovePerkEntry(actor);
                } else if (
                    questPerkEntryLayoutValid &&
                    entry->GetType() == RE::PERK_ENTRY_TYPE::kQuest) {
                    const auto* questEntry = reinterpret_cast<const QuestPerkEntryLayout*>(entry);
                    snapshot.questEntries.emplace_back(questEntry->quest, questEntry->questStage);
                }
            }
            snapshots.push_back(std::move(snapshot));
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return snapshots;
    }

    void ApplyUpdatedAbilityAndQuestEntries(
        RE::BGSPerk& perk,
        const std::vector<LoadedActorPerkSnapshot>& snapshots)
    {
        for (const auto& snapshot : snapshots) {
            if (!snapshot.actor || !snapshot.actor->HasPerk(&perk)) {
                continue;
            }
            MatchingPerkEntriesVisitor visitor(perk.perkEntries);
            snapshot.actor->ForEachPerk(visitor);
            for (auto* entry : visitor.matches) {
                if (entry->GetType() == RE::PERK_ENTRY_TYPE::kAbility) {
                    entry->ApplyPerkEntry(snapshot.actor);
                } else if (
                    questPerkEntryLayoutValid &&
                    entry->GetType() == RE::PERK_ENTRY_TYPE::kQuest) {
                    const auto* questEntry = reinterpret_cast<const QuestPerkEntryLayout*>(entry);
                    const auto key = std::pair{ questEntry->quest, questEntry->questStage };
                    if (std::ranges::find(snapshot.questEntries, key) == snapshot.questEntries.end()) {
                        entry->ApplyPerkEntry(snapshot.actor);
                    }
                }
            }
        }
    }

    struct RemovedActorPerkSnapshot
    {
        RE::Actor* actor{ nullptr };
        std::uint32_t rank{ 0 };
    };

    std::vector<RemovedActorPerkSnapshot> RemovePerkFromLoadedActors(
        RE::BGSPerk& perk)
    {
        std::vector<RemovedActorPerkSnapshot> snapshots;
        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) {
            return snapshots;
        }

        processLists->ForAllActors([&](RE::Actor* actor) {
            if (!actor || !actor->HasPerk(&perk)) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            MatchingPerkEntriesVisitor visitor(perk.perkEntries);
            actor->ForEachPerk(visitor);
            std::uint32_t rank = 0;
            for (const auto* entry : visitor.matches) {
                rank = std::max(rank, static_cast<std::uint32_t>(entry->GetRank()));
            }
            snapshots.push_back(RemovedActorPerkSnapshot{ actor, rank });
            actor->RemovePerk(&perk);
            return RE::BSContainer::ForEachResult::kContinue;
        });

        if (!snapshots.empty()) {
            logger::info(
                "Removed dynamic perk '{}' from {} loaded actor(s) before DPF release.",
                perk.GetFormEditorID(),
                snapshots.size());
        }
        return snapshots;
    }

    void RestorePerkToLoadedActors(
        RE::BGSPerk& perk,
        const std::vector<RemovedActorPerkSnapshot>& snapshots)
    {
        for (const auto& snapshot : snapshots) {
            if (snapshot.actor && !snapshot.actor->HasPerk(&perk)) {
                snapshot.actor->AddPerk(&perk, snapshot.rank);
            }
        }
        if (!snapshots.empty()) {
            logger::info(
                "Restored dynamic perk '{}' to {} loaded actor(s) after delete rollback.",
                perk.GetFormEditorID(),
                snapshots.size());
        }
    }

    bool ConfigurePerk(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        auto* perk = tesForm ? tesForm->As<RE::BGSPerk>() : nullptr;
        if (!perk) {
            logger::warn("Dynamic form '{}' is not a BGSPerk", form.editorId);
            return false;
        }

        std::vector<std::string> validationErrors;
        if (!ValidatePerkForm(form, validationErrors)) {
            logger::warn(
                "Perk '{}' was not configured: {}",
                form.editorId,
                JoinValidationErrors(validationErrors));
            return false;
        }

        perk->SetFormEditorID(form.editorId.c_str());
        perk->fullName = form.fullName.c_str();
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
        const auto loadedActorSnapshots = RemoveAppliedAbilityEntries(*perk);
        ClearPerkEntries(*perk);
        for (const auto& entry : form.entries) {
            if (auto* perkEntry = CreatePerkEntry(*perk, entry)) {
                perk->perkEntries.push_back(perkEntry);
            }
        }
        ApplyUpdatedAbilityAndQuestEntries(*perk, loadedActorSnapshots);

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
        headPart->fullName = form.fullName.c_str();
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
        light->fullName = form.fullName.c_str();
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
        explosion->fullName = form.fullName.c_str();
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
        activator->fullName = form.fullName.c_str();
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

    DynamicForms::FormRef RuntimeFormRef(RE::TESForm* form) {
        DynamicForms::FormRef ref;
        if (!form) {
            return ref;
        }
        ref.editorID = FormUtil::GetEditorIDSafe(form);
        ref.formID = FormUtil::NormalizeFormID(form);
        return ref;
    }

    void CaptureKeywords(const RE::BGSKeywordForm& source, std::vector<DynamicForms::FormRef>& target) {
        target.clear();
        target.reserve(source.GetNumKeywords());
        for (auto* keyword : source.GetKeywords()) {
            if (keyword) {
                target.push_back(RuntimeFormRef(keyword));
            }
        }
    }

    std::string CaptureConditionParam(const void* raw, const std::string_view rawType) {
        if (!raw) return {};
        if (ConditionCatalog::IsFormParam(rawType)) return RuntimeFormRef(reinterpret_cast<RE::TESForm*>(const_cast<void*>(raw))).Display();
        if (ConditionCatalog::IsStringParam(rawType)) return reinterpret_cast<const char*>(raw);
        const auto bits = reinterpret_cast<std::uintptr_t>(raw);
        if (ConditionCatalog::IsFloatParam(rawType)) {
            const auto low = static_cast<std::uint32_t>(bits);
            float value{};
            std::memcpy(&value, &low, sizeof(value));
            return std::format("{}", value);
        }
        return std::to_string(static_cast<std::intptr_t>(bits));
    }

    void CaptureConditions(const RE::TESCondition& source, std::vector<DynamicForms::PerkCondition>& target) {
        target.clear();
        for (auto* item = source.head; item; item = item->next) {
            DynamicForms::PerkCondition condition;
            condition.kind = DynamicForms::PerkConditionKind::Raw;
            condition.functionId = item->data.functionData.function.underlying();
            condition.isOr = item->data.flags.isOR;
            condition.useAliases = item->data.flags.usesAliases;
            condition.opCode = static_cast<std::uint32_t>(item->data.flags.opCode);
            condition.useGlobalComparison = item->data.flags.global;
            condition.usePackData = item->data.flags.usePackData;
            condition.swapTarget = item->data.flags.swapTarget;
            condition.runOn = item->data.object.underlying();
            condition.dataId = item->data.dataID;
            if (condition.useGlobalComparison)
                condition.comparisonGlobal = RuntimeFormRef(item->data.comparisonValue.g).Display();
            else
                condition.comparisonValue = item->data.comparisonValue.f;
            if (auto ref = item->data.runOnRef.get()) condition.runOnRef = RuntimeFormRef(ref.get()).Display();
            if (const auto* info = ConditionCatalog::FindFunction(condition.functionId)) {
                condition.param1 = CaptureConditionParam(item->data.functionData.params[0], info->rawParam1);
                condition.param2 = CaptureConditionParam(item->data.functionData.params[1], info->rawParam2);
            } else {
                condition.param1 = CaptureConditionParam(item->data.functionData.params[0], {});
                condition.param2 = CaptureConditionParam(item->data.functionData.params[1], {});
            }
            target.push_back(std::move(condition));
        }
    }

    bool CaptureArmorTemplate(DynamicForms::DynamicForm& target, RE::TESObjectARMO& source) {
        target.fullName = source.fullName.c_str();
        target.race = RuntimeFormRef(source.race);
        target.armorValue = source.value;
        target.armorWeight = source.weight;
        target.enchantment = RuntimeFormRef(source.formEnchanting);
        target.enchantmentAmount = source.amountofEnchantment;
        target.equipSlot = RuntimeFormRef(source.GetEquipSlot());
        target.armorRating = static_cast<float>(source.armorRating) / 100.0F;
        target.templateArmor = RuntimeFormRef(source.templateArmor);
        target.pickupSound = RuntimeFormRef(source.pickupSound);
        target.putdownSound = RuntimeFormRef(source.putdownSound);
        target.blockBashImpactDataSet = RuntimeFormRef(source.blockBashImpactDataSet);
        target.altBlockMaterialType = RuntimeFormRef(source.altBlockMaterialType);
        target.bipedSlots = source.bipedModelData.bipedObjectSlots.underlying();
        target.armorType = source.bipedModelData.armorType.underlying();
        target.maleWorldModel = source.worldModels[RE::TESBipedModelForm::Sexes::kMale].GetModel();
        target.femaleWorldModel = source.worldModels[RE::TESBipedModelForm::Sexes::kFemale].GetModel();
        target.maleInventoryIcon = source.inventoryIcons[RE::TESBipedModelForm::Sexes::kMale].textureName.c_str();
        target.femaleInventoryIcon = source.inventoryIcons[RE::TESBipedModelForm::Sexes::kFemale].textureName.c_str();
        target.maleMessageIcon = source.messageIcons[RE::TESBipedModelForm::Sexes::kMale].icon.textureName.c_str();
        target.femaleMessageIcon = source.messageIcons[RE::TESBipedModelForm::Sexes::kFemale].icon.textureName.c_str();
        target.armorAddons.clear();
        target.armorAddons.reserve(source.armorAddons.size());
        for (auto* addon : source.armorAddons) {
            if (addon) {
                target.armorAddons.push_back(RuntimeFormRef(addon));
            }
        }
        CaptureKeywords(source, target.keywords);
        return true;
    }

    bool CaptureArmorTypeTemplate(DynamicForms::DynamicForm& target, RE::TESObjectARMA& source) {
        target.race = RuntimeFormRef(source.race);
        target.bipedSlots = source.bipedModelData.bipedObjectSlots.underlying();
        target.armorType = source.bipedModelData.armorType.underlying();
        target.maleWorldModel = source.bipedModels[RE::SEX::kMale].GetModel();
        target.femaleWorldModel = source.bipedModels[RE::SEX::kFemale].GetModel();
        target.maleFirstPersonModel = source.bipedModel1stPersons[RE::SEX::kMale].GetModel();
        target.femaleFirstPersonModel = source.bipedModel1stPersons[RE::SEX::kFemale].GetModel();
        target.maleSkinTexture = RuntimeFormRef(source.skinTextures[RE::SEX::kMale]);
        target.femaleSkinTexture = RuntimeFormRef(source.skinTextures[RE::SEX::kFemale]);
        target.maleSkinTextureSwapList = RuntimeFormRef(source.skinTextureSwapLists[RE::SEX::kMale]);
        target.femaleSkinTextureSwapList = RuntimeFormRef(source.skinTextureSwapLists[RE::SEX::kFemale]);
        target.additionalRaces.clear();
        target.additionalRaces.reserve(source.additionalRaces.size());
        for (auto* race : source.additionalRaces) {
            if (race) {
                target.additionalRaces.push_back(RuntimeFormRef(race));
            }
        }
        target.footstepSet = RuntimeFormRef(source.footstepSet);
        target.armorArtObject = RuntimeFormRef(source.artObject);
        return true;
    }

    void CaptureItemPresentation(
        DynamicForms::DynamicForm& target,
        RE::TESFullName& fullName,
        RE::TESModel& model,
        RE::TESIcon& icon,
        RE::BGSMessageIcon* messageIcon,
        RE::BGSPickupPutdownSounds* sounds,
        RE::BGSKeywordForm* keywords)
    {
        target.fullName = fullName.fullName.c_str();
        target.modelPath = model.GetModel();
        target.inventoryIcon = icon.textureName.c_str();
        if (messageIcon) target.messageIcon = messageIcon->icon.textureName.c_str();
        if (sounds) {
            target.pickupSound = RuntimeFormRef(sounds->pickupSound);
            target.putdownSound = RuntimeFormRef(sounds->putdownSound);
        }
        if (keywords) CaptureKeywords(*keywords, target.keywords);
    }

    bool CaptureMiscTemplate(DynamicForms::DynamicForm& target, RE::TESObjectMISC& source) {
        CaptureItemPresentation(target, source, source, source, &source, &source, &source);
        target.itemValue = source.value;
        target.itemWeight = source.weight;
        return true;
    }

    bool CaptureKeyTemplate(DynamicForms::DynamicForm& target, RE::TESKey& source) {
        CaptureItemPresentation(target, source, source, source, &source, &source, &source);
        target.itemValue = source.value;
        target.itemWeight = source.weight;
        return true;
    }

    bool CaptureSoulGemTemplate(DynamicForms::DynamicForm& target, RE::TESSoulGem& source) {
        CaptureMiscTemplate(target, source);
        target.linkedSoulGem = RuntimeFormRef(source.linkedSoulGem);
        target.currentSoul = source.currentSoul.underlying();
        target.soulCapacity = source.soulCapacity.underlying();
        return true;
    }

    bool CaptureBookTemplate(DynamicForms::DynamicForm& target, RE::TESObjectBOOK& source) {
        CaptureItemPresentation(target, source, source, source, &source, &source, &source);
        target.itemValue = source.value;
        target.itemWeight = source.weight;
        target.bookFlags = source.data.flags.underlying();
        target.bookType = source.data.type.underlying();
        if (source.TeachesSpell()) {
            target.teachesSpell = RuntimeFormRef(source.GetSpell());
            target.teachesActorValue = -1;
        } else {
            target.teachesSpell = {};
            target.teachesActorValue = static_cast<std::int32_t>(source.GetSkill());
        }
        return true;
    }

    bool CaptureAmmoTemplate(DynamicForms::DynamicForm& target, RE::TESAmmo& source) {
        CaptureItemPresentation(
            target,
            source,
            source,
            source,
            &source,
            source.AsPickupPutdownSoundsForm(),
            source.AsKeywordForm());
        target.itemValue = source.value;
        const auto& data = source.GetRuntimeData().data;
        target.projectile = RuntimeFormRef(data.projectile);
        target.damage = data.damage;
        target.ammoFlags = data.flags.underlying();
        return true;
    }

    bool CaptureWeaponTemplate(DynamicForms::DynamicForm& target, RE::TESObjectWEAP& source) {
        CaptureItemPresentation(target, source, source, source, &source, &source, &source);
        target.itemValue = source.value;
        target.itemWeight = source.weight;
        target.damage = source.attackDamage;
        target.enchantment = RuntimeFormRef(source.formEnchanting);
        target.enchantmentAmount = source.amountofEnchantment;
        target.equipSlot = RuntimeFormRef(source.GetEquipSlot());
        target.templateWeapon = RuntimeFormRef(source.templateWeapon);
        target.blockBashImpactDataSet = RuntimeFormRef(source.blockBashImpactDataSet);
        target.altBlockMaterialType = RuntimeFormRef(source.altBlockMaterialType);
        target.impactDataSet = RuntimeFormRef(source.impactDataSet);
        target.firstPersonModelObject = RuntimeFormRef(source.firstPersonModelObject);
        target.attackSound = RuntimeFormRef(source.attackSound);
        target.attackSound2D = RuntimeFormRef(source.attackSound2D);
        target.attackLoopSound = RuntimeFormRef(source.attackLoopSound);
        target.attackFailSound = RuntimeFormRef(source.attackFailSound);
        target.idleSound = RuntimeFormRef(source.idleSound);
        target.equipSound = RuntimeFormRef(source.equipSound);
        target.unequipSound = RuntimeFormRef(source.unequipSound);
        target.weaponSpeed = source.weaponData.speed;
        target.weaponReach = source.weaponData.reach;
        target.weaponMinRange = source.weaponData.minRange;
        target.weaponMaxRange = source.weaponData.maxRange;
        target.weaponStagger = source.weaponData.staggerValue;
        target.weaponType = source.weaponData.animationType.underlying();
        target.weaponFlags = source.weaponData.flags.underlying();
        target.weaponFlags2 = source.weaponData.flags2.underlying();
        target.weaponSkill = source.weaponData.skill.underlying();
        target.weaponResist = source.weaponData.resistance.underlying();
        target.weaponCritMult = source.criticalData.prcntMult;
        target.weaponCritDamage = source.criticalData.damage;
        target.weaponCritFlags = source.criticalData.flags.underlying();
        target.critEffect = RuntimeFormRef(source.criticalData.effect);
        return true;
    }

    void CaptureMagicEffects(DynamicForms::DynamicForm& target, const RE::MagicItem& source) {
        target.magicEffectsOverride = true;
        target.magicEffects.clear();
        target.magicEffects.reserve(source.effects.size());
        for (const auto* effect : source.effects) {
            if (!effect || !effect->baseEffect) {
                continue;
            }
            DynamicForms::MagicEffectEntry entry;
            entry.effectSetting = RuntimeFormRef(effect->baseEffect);
            entry.magnitude = effect->effectItem.magnitude;
            entry.area = effect->effectItem.area;
            entry.duration = effect->effectItem.duration;
            entry.cost = effect->cost;
            CaptureConditions(effect->conditions, entry.conditions);
            target.magicEffects.push_back(std::move(entry));
        }
    }

    bool CaptureGlobalTemplate(DynamicForms::DynamicForm& target, RE::TESGlobal& source) {
        switch (source.type.get()) {
        case RE::TESGlobal::Type::kShort: target.globalType = DynamicForms::GlobalType::Short; break;
        case RE::TESGlobal::Type::kLong: target.globalType = DynamicForms::GlobalType::Long; break;
        default: target.globalType = DynamicForms::GlobalType::Float; break;
        }
        target.defaultValue = source.value;
        return true;
    }

    bool CaptureMaterialTypeTemplate(DynamicForms::DynamicForm& target, RE::BGSMaterialType& source) {
        target.materialParent = RuntimeFormRef(source.parentType);
        target.materialName = source.materialName.c_str();
        target.materialId = static_cast<std::uint32_t>(source.materialID);
        target.red = static_cast<std::uint8_t>(std::clamp(source.materialColor.red, 0.0F, 1.0F) * 255.0F);
        target.green = static_cast<std::uint8_t>(std::clamp(source.materialColor.green, 0.0F, 1.0F) * 255.0F);
        target.blue = static_cast<std::uint8_t>(std::clamp(source.materialColor.blue, 0.0F, 1.0F) * 255.0F);
        target.buoyancy = source.buoyancy;
        target.flags = source.flags.underlying();
        target.havokImpactDataSet = RuntimeFormRef(source.havokImpactDataSet);
        return true;
    }

    bool CaptureAlchemyTemplate(DynamicForms::DynamicForm& target, RE::AlchemyItem& source) {
        CaptureItemPresentation(target, source, source, source, &source, &source, &source);
        target.itemWeight = source.weight;
        target.equipSlot = RuntimeFormRef(source.GetEquipSlot());
        target.alchemyCostOverride = source.data.costOverride;
        target.alchemyFlags = source.data.flags.underlying();
        target.addictionItem = RuntimeFormRef(source.data.addictionItem);
        target.addictionChance = source.data.addictionChance;
        target.consumptionSound = RuntimeFormRef(source.data.consumptionSound);
        CaptureMagicEffects(target, source);
        return true;
    }

    bool CaptureIngredientTemplate(DynamicForms::DynamicForm& target, RE::IngredientItem& source) {
        CaptureItemPresentation(target, source, source, source, nullptr, &source, &source);
        target.itemValue = source.value;
        target.itemWeight = source.weight;
        target.equipSlot = RuntimeFormRef(source.GetEquipSlot());
        target.ingredientCostOverride = source.data.costOverride;
        target.ingredientFlags = source.data.flags.underlying();
        target.knownEffectFlags = source.gamedata.knownEffectFlags;
        target.playerUses = source.gamedata.playerUses;
        CaptureMagicEffects(target, source);
        return true;
    }

    bool CaptureSpellTemplate(DynamicForms::DynamicForm& target, RE::SpellItem& source) {
        target.fullName = source.fullName.c_str();
        target.spellCostOverride = source.data.costOverride;
        target.spellFlags = source.data.flags.underlying();
        target.spellType = static_cast<std::uint32_t>(source.data.spellType);
        target.spellChargeTime = source.data.chargeTime;
        target.spellCastingType = static_cast<std::uint32_t>(source.data.castingType);
        target.spellDelivery = static_cast<std::uint32_t>(source.data.delivery);
        target.spellCastDuration = source.data.castDuration;
        target.spellRange = source.data.range;
        target.castingPerk = RuntimeFormRef(source.data.castingPerk);
        target.equipSlot = RuntimeFormRef(source.GetEquipSlot());
        target.menuDisplayObject = RuntimeFormRef(source.menuDispObject);
        CaptureKeywords(source, target.keywords);
        CaptureMagicEffects(target, source);
        return true;
    }

    bool CaptureEnchantmentTemplate(DynamicForms::DynamicForm& target, RE::EnchantmentItem& source) {
        target.fullName = source.fullName.c_str();
        target.enchantmentCostOverride = source.data.costOverride;
        target.enchantmentFlags = source.data.flags.underlying();
        target.enchantmentCastingType = static_cast<std::uint32_t>(source.data.castingType);
        target.enchantmentChargeOverride = source.data.chargeOverride;
        target.enchantmentDelivery = static_cast<std::uint32_t>(source.data.delivery);
        target.enchantmentSpellType = static_cast<std::uint32_t>(source.data.spellType);
        target.enchantmentChargeTime = source.data.chargeTime;
        target.baseEnchantment = RuntimeFormRef(source.data.baseEnchantment);
        target.wornRestrictions = RuntimeFormRef(source.data.wornRestrictions);
        CaptureKeywords(source, target.keywords);
        CaptureMagicEffects(target, source);
        return true;
    }

    bool CaptureScrollTemplate(DynamicForms::DynamicForm& target, RE::ScrollItem& source) {
        target.fullName = source.fullName.c_str();
        target.modelPath = source.GetModel();
        target.pickupSound = RuntimeFormRef(source.pickupSound);
        target.putdownSound = RuntimeFormRef(source.putdownSound);
        CaptureKeywords(source, target.keywords);
        target.itemValue = source.value;
        target.itemWeight = source.weight;
        const auto& data = static_cast<const RE::SpellItem&>(source).data;
        target.scrollCostOverride = data.costOverride;
        target.scrollFlags = data.flags.underlying();
        target.scrollChargeTime = data.chargeTime;
        target.scrollDelivery = static_cast<std::uint32_t>(data.delivery);
        target.scrollCastDuration = data.castDuration;
        target.scrollRange = data.range;
        target.scrollCastingPerk = RuntimeFormRef(data.castingPerk);
        target.equipSlot = RuntimeFormRef(source.GetEquipSlot());
        target.menuDisplayObject = RuntimeFormRef(source.menuDispObject);
        CaptureMagicEffects(target, source);
        return true;
    }

    bool CaptureFormListTemplate(DynamicForms::DynamicForm& target, RE::BGSListForm& source) {
        target.formListItems.clear();
        target.formListItems.reserve(source.forms.size());
        for (auto* item : source.forms) if (item) target.formListItems.push_back(RuntimeFormRef(item));
        return true;
    }

    bool CaptureEquipSlotTemplate(DynamicForms::DynamicForm& target, RE::BGSEquipSlot& source) {
        target.equipSlotFlags = source.flags.underlying();
        target.equipSlotParents.clear();
        target.equipSlotParents.reserve(source.parentSlots.size());
        for (auto* parent : source.parentSlots) if (parent) target.equipSlotParents.push_back(RuntimeFormRef(parent));
        return true;
    }

    bool CaptureVoiceTypeTemplate(DynamicForms::DynamicForm& target, RE::BGSVoiceType& source) {
        target.voiceTypeAllowDefaultDialogue = source.data.flags.any(RE::VOICE_TYPE_DATA::Flag::kAllowDefaultDialogue);
        target.voiceTypeFemale = source.data.flags.any(RE::VOICE_TYPE_DATA::Flag::kFemale);
        return true;
    }

    bool CaptureOutfitTemplate(DynamicForms::DynamicForm& target, RE::BGSOutfit& source) {
        target.outfitPieces.clear();
        target.outfitPieces.reserve(source.outfitItems.size());
        for (auto* item : source.outfitItems) if (item) target.outfitPieces.push_back(RuntimeFormRef(item));
        return true;
    }

    void CaptureLeveledList(DynamicForms::DynamicForm& target, const RE::TESLeveledList& source) {
        target.leveledChanceNone = static_cast<std::uint8_t>(std::max(0, static_cast<int>(source.chanceNone)));
        target.leveledFlags = static_cast<std::uint8_t>(source.llFlags);
        target.leveledChanceGlobal = RuntimeFormRef(source.chanceGlobal);
        target.leveledEntries.clear();
        target.leveledEntries.reserve(source.entries.size());
        for (const auto& item : source.entries) {
            if (!item.form) continue;
            DynamicForms::LeveledEntry entry;
            entry.form = RuntimeFormRef(item.form);
            entry.level = item.level;
            entry.count = item.count;
            if (item.itemExtra) {
                entry.owner = RuntimeFormRef(item.itemExtra->owner);
                entry.healthMult = item.itemExtra->healthMult;
                const auto conditionalBits = reinterpret_cast<std::uintptr_t>(item.itemExtra->conditional.global);
                if (conditionalBits > std::numeric_limits<std::uint32_t>::max())
                    entry.conditionGlobal = RuntimeFormRef(item.itemExtra->conditional.global);
                else
                    entry.requiredRank = item.itemExtra->conditional.rank;
            }
            target.leveledEntries.push_back(std::move(entry));
        }
    }

    void CapturePackageEvent(const RE::PackageEventAction& source, DynamicForms::PackageEvent& target) {
        target.idle = RuntimeFormRef(source.idle);
        target.type = source.type.underlying();
        target.topicType = source.topic.type.underlying();
        target.topic = RuntimeFormRef(source.topic.topic);
    }

    DynamicForms::SceneAction CaptureSceneAction(RE::BGSSceneAction& source) {
        DynamicForms::SceneAction target;
        target.type = static_cast<std::uint32_t>(source.GetType());
        target.actorId = source.actorID;
        target.startPhase = source.startPhase;
        target.endPhase = source.endPhase;
        target.flags = source.flags.underlying();
        target.index = source.index;
        if (auto* dialogue = skyrim_cast<RE::BGSSceneActionDialogue*>(std::addressof(source))) {
            target.topic = RuntimeFormRef(dialogue->topic);
            target.headtrackActorId = dialogue->headtrackActorID;
            target.loopingMin = dialogue->loopingMin;
            target.loopingMax = dialogue->loopingMax;
            target.emotionType = static_cast<std::uint32_t>(dialogue->emotionType);
            target.emotionValue = dialogue->emotionValue;
        } else if (auto* package = skyrim_cast<RE::BGSSceneActionPackage*>(std::addressof(source))) {
            for (auto* item : package->packages) if (item) target.packages.push_back(RuntimeFormRef(item));
        } else if (auto* timer = skyrim_cast<RE::BGSSceneActionTimer*>(std::addressof(source))) {
            target.timerSeconds = timer->timerSeconds;
        }
        return target;
    }

    template <class Map, class Key, class Value>
    Value CaptureMapValue(const Map& map, const Key& key, const Value fallback) {
        const auto found = map.find(key);
        return found != map.end() ? found->second : fallback;
    }

    bool CaptureAdditionalSimpleTemplate(DynamicForms::DynamicForm& target, RE::TESForm& source) {
        using FK = DynamicForms::FormKind;
        switch (target.kind) {
        case FK::Perk: {
            auto* value = source.As<RE::BGSPerk>();
            if (!value) {
                return false;
            }
            target.fullName = value->fullName.c_str();
            target.trait = value->data.trait;
            target.level = value->data.level;
            target.numRanks = value->data.numRanks;
            target.playable = value->data.playable;
            target.hidden = value->data.hidden;
            target.nextPerk = RuntimeFormRef(value->nextPerk);
            CaptureConditions(value->perkConditions, target.conditions);
            target.entries.clear();

            for (auto* base : value->perkEntries) {
                if (!base) {
                    continue;
                }
                DynamicForms::PerkEntry out;
                out.rank = base->header.rank;
                out.priority = base->header.priority;
                switch (base->GetType()) {
                case RE::PERK_ENTRY_TYPE::kQuest: {
                    if (!questPerkEntryLayoutValid) {
                        logger::warn(
                            "Skipped quest perk entry while capturing '{}' because its runtime layout is unavailable.",
                            value->GetFormEditorID());
                        continue;
                    }
                    const auto* entry = reinterpret_cast<const QuestPerkEntryLayout*>(base);
                    if (!entry->quest || entry->quest->GetFormType() != RE::FormType::Quest) {
                        logger::warn(
                            "Skipped quest perk entry with an invalid runtime layout while capturing '{}'.",
                            value->GetFormEditorID());
                        continue;
                    }
                    out.kind = DynamicForms::PerkEntryKind::Quest;
                    out.quest = RuntimeFormRef(entry->quest);
                    out.questStage = entry->questStage;
                    break;
                }
                case RE::PERK_ENTRY_TYPE::kAbility: {
                    const auto* entry = static_cast<const RE::BGSAbilityPerkEntry*>(base);
                    out.kind = DynamicForms::PerkEntryKind::Ability;
                    out.ability = RuntimeFormRef(entry->ability);
                    break;
                }
                case RE::PERK_ENTRY_TYPE::kEntryPoint: {
                    const auto* entry = static_cast<const RE::BGSEntryPointPerkEntry*>(base);
                    out.kind = DynamicForms::PerkEntryKind::EntryPoint;
                    out.entryPoint = entry->entryData.entryPoint.underlying();
                    out.function = entry->entryData.function.underlying();
                    out.numArgs = entry->entryData.numArgs;
                    out.functionData.kind = ExpectedPerkFunctionDataKind(out.function);
                    if (entry->functionData) {
                        switch (entry->functionData->GetType()) {
                        case RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kOneValue:
                            out.functionData.value1 =
                                static_cast<const RE::BGSEntryPointFunctionDataOneValue*>(entry->functionData)->data;
                            break;
                        case RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kTwoValue: {
                            const auto* data =
                                reinterpret_cast<const EntryPointFunctionDataTwoValueLayout*>(entry->functionData);
                            if (out.functionData.kind == DynamicForms::PerkFunctionDataKind::ActorValueAndValue) {
                                out.functionData.actorValue = static_cast<std::uint32_t>(
                                    static_cast<std::int32_t>(std::lround(data->value1)));
                            } else {
                                out.functionData.value1 = data->value1;
                            }
                            out.functionData.value2 = data->value2;
                            break;
                        }
                        case RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kLeveledList:
                            out.functionData.form = RuntimeFormRef(
                                reinterpret_cast<const EntryPointFunctionDataLeveledListLayout*>(
                                    entry->functionData)->leveledList);
                            break;
                        case RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kActivateChoice: {
                            const auto* data =
                                static_cast<const RE::BGSEntryPointFunctionDataActivateChoice*>(entry->functionData);
                            out.functionData.buttonLabel = data->label.c_str();
                            out.functionData.form = RuntimeFormRef(data->appliedSpell);
                            out.functionData.flags = data->flags.underlying();
                            out.functionData.fragmentIndex = data->id;
                            break;
                        }
                        case RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kSpellItem:
                            out.functionData.form = RuntimeFormRef(
                                static_cast<const RE::BGSEntryPointFunctionDataSpellItem*>(
                                    entry->functionData)->spell);
                            break;
                        case RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kBooleanGraphVariable:
                            out.functionData.text =
                                reinterpret_cast<const EntryPointFunctionDataBooleanGraphVariableLayout*>(
                                    entry->functionData)->variableName.c_str();
                            break;
                        case RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kText:
                            out.functionData.text =
                                static_cast<const RE::BGSEntryPointFunctionDataText*>(
                                    entry->functionData)->text.c_str();
                            break;
                        default:
                            break;
                        }
                    }
                    for (std::uint32_t tabIndex = 0; tabIndex < entry->conditions.size(); ++tabIndex) {
                        DynamicForms::PerkConditionTab tab;
                        tab.index = tabIndex;
                        CaptureConditions(entry->conditions[tabIndex], tab.conditions);
                        out.conditionTabs.push_back(std::move(tab));
                    }
                    break;
                }
                default:
                    continue;
                }
                target.entries.push_back(std::move(out));
            }
            return true;
        }
        case FK::Color: { auto* value=source.As<RE::BGSColorForm>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.red=value->color.red; target.green=value->color.green; target.blue=value->color.blue; target.alpha=value->color.alpha; target.playable=value->flags.any(RE::BGSColorForm::Flag::kPlayable); return true; }
        case FK::ArtObject: { auto* value=source.As<RE::BGSArtObject>(); if(!value)return false; target.modelPath=value->GetModel(); target.artType=static_cast<DynamicForms::ArtObjectType>(value->data.artType.underlying()); target.boundX1=value->boundData.boundMin.x; target.boundY1=value->boundData.boundMin.y; target.boundZ1=value->boundData.boundMin.z; target.boundX2=value->boundData.boundMax.x; target.boundY2=value->boundData.boundMax.y; target.boundZ2=value->boundData.boundMax.z; return true; }
        case FK::HeadPart: { auto* value=source.As<RE::BGSHeadPart>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.modelPath=value->GetModel(); target.headPartType=static_cast<DynamicForms::HeadPartType>(value->type.underlying()); target.playable=value->flags.any(RE::BGSHeadPart::Flag::kPlayable); target.male=value->flags.any(RE::BGSHeadPart::Flag::kMale); target.female=value->flags.any(RE::BGSHeadPart::Flag::kFemale); target.isExtraPart=value->flags.any(RE::BGSHeadPart::Flag::kIsExtraPart); target.useSolidTint=value->flags.any(RE::BGSHeadPart::Flag::kUseSolidTint); target.raceMorphPath=value->morphs[RE::BGSHeadPart::MorphIndices::kRaceMorph].GetModel(); target.defaultMorphPath=value->morphs[RE::BGSHeadPart::MorphIndices::kDefaultMorph].GetModel(); target.chargenMorphPath=value->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].GetModel(); target.textureSet=RuntimeFormRef(value->textureSet); target.colorForm=RuntimeFormRef(value->color); target.validRaces=RuntimeFormRef(value->validRaces); target.extraParts.clear(); for(auto* p:value->extraParts)if(p)target.extraParts.push_back(RuntimeFormRef(p)); return true; }
        case FK::SoundDescriptor: { auto* value=source.As<RE::BGSSoundDescriptorForm>(); if(!value||!value->soundDescriptor)return false; auto* d=static_cast<RE::BGSStandardSoundDef*>(value->soundDescriptor); target.category=RuntimeFormRef(d->category); target.alternateSound=RuntimeFormRef(RE::TESForm::LookupByID(d->alternateSoundFormID)); target.outputModel=RuntimeFormRef(d->outputModel); target.soundFiles.clear(); target.frequencyShift=d->soundCharacteristics.frequencyShift; target.frequencyVariance=d->soundCharacteristics.frequencyVariance; target.priority=d->soundCharacteristics.priority; target.dbVariance=d->soundCharacteristics.dbVariance; target.staticAttenuation=static_cast<float>(d->soundCharacteristics.staticAttenuation)/100.0F; target.looping=d->lengthCharacteristics.looping.underlying(); target.rumbleSendValue=d->lengthCharacteristics.rumbleSendValue; return true; }
        case FK::Light: { auto* value=source.As<RE::TESObjectLIGH>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.modelPath=value->GetModel(); const auto& d=value->data; target.lightTime=d.time; target.lightRadius=d.radius; target.red=d.color.red; target.green=d.color.green; target.blue=d.color.blue; target.alpha=d.color.alpha; target.flags=d.flags.underlying(); target.falloffExponent=d.fallofExponent; target.fov=d.fov; target.nearClip=d.nearDistance; target.flickerPeriod=d.flickerPeriodRecip; target.flickerIntensityAmplitude=d.flickerIntensityAmplitude; target.flickerMovementAmplitude=d.flickerMovementAmplitude; target.fade=value->fade; target.sound=RuntimeFormRef(value->sound); target.lensFlare=RuntimeFormRef(value->lensFlare); return true; }
        case FK::Explosion: { auto* value=source.As<RE::BGSExplosion>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.modelPath=value->GetModel(); target.objectEffect=RuntimeFormRef(value->formEnchanting); target.imageSpaceModifier=RuntimeFormRef(value->imageSpaceModifying); const auto& d=value->data; target.light=RuntimeFormRef(d.light); target.sound1=RuntimeFormRef(d.sound1); target.sound2=RuntimeFormRef(d.sound2); target.impactDataSet=RuntimeFormRef(d.impactDataSet); target.placedObject=RuntimeFormRef(d.impactPlacedObject); target.spawnProjectile=RuntimeFormRef(d.spawnProjectile); target.force=d.force; target.damage=d.damage; target.radius=d.radius; target.imageSpaceRadius=d.imageSpaceRadius; target.verticalOffsetMult=d.verticalOffsetMult; target.flags=d.flags.underlying(); target.soundLevel=d.eSoundLevel.underlying(); return true; }
        case FK::Activator: { auto* value=source.As<RE::TESObjectACTI>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.modelPath=value->GetModel(); target.soundLoop=RuntimeFormRef(value->soundLoop); target.soundActivate=RuntimeFormRef(value->soundActivate); target.waterType=RuntimeFormRef(value->waterForm); target.flags=value->flags.underlying(); return true; }
        case FK::MagicEffect: { auto* value=source.As<RE::EffectSetting>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.menuDisplayObject=RuntimeFormRef(value->menuDispObject); CaptureKeywords(*value,target.keywords); const auto& d=value->data; target.magicEffectFlags=d.flags.underlying(); target.magicEffectBaseCost=d.baseCost; target.magicEffectAssociatedForm=RuntimeFormRef(d.associatedForm); target.magicEffectAssociatedSkill=static_cast<std::int32_t>(d.associatedSkill); target.magicEffectResistVariable=static_cast<std::int32_t>(d.resistVariable); target.magicEffectLight=RuntimeFormRef(d.light); target.magicEffectTaperWeight=d.taperWeight; target.magicEffectShader=RuntimeFormRef(d.effectShader); target.magicEffectEnchantShader=RuntimeFormRef(d.enchantShader); target.magicEffectMinimumSkill=d.minimumSkill; target.magicEffectSpellmakingArea=d.spellmakingArea; target.magicEffectSpellmakingChargeTime=d.spellmakingChargeTime; target.magicEffectTaperCurve=d.taperCurve; target.magicEffectTaperDuration=d.taperDuration; target.magicEffectSecondAVWeight=d.secondAVWeight; target.magicEffectArchetype=static_cast<std::int32_t>(d.archetype); target.magicEffectPrimaryAV=static_cast<std::int32_t>(d.primaryAV); target.magicEffectProjectile=RuntimeFormRef(d.projectileBase); target.magicEffectExplosion=RuntimeFormRef(d.explosion); target.magicEffectCastingType=static_cast<std::uint32_t>(d.castingType); target.magicEffectDelivery=static_cast<std::uint32_t>(d.delivery); target.magicEffectSecondaryAV=static_cast<std::int32_t>(d.secondaryAV); target.magicEffectCastingArt=RuntimeFormRef(d.castingArt); target.magicEffectHitEffectArt=RuntimeFormRef(d.hitEffectArt); target.magicEffectImpactDataSet=RuntimeFormRef(d.impactDataSet); target.magicEffectSkillUsageMult=d.skillUsageMult; target.magicEffectDualCastData=RuntimeFormRef(d.dualCastData); target.magicEffectDualCastScale=d.dualCastScale; target.magicEffectEnchantEffectArt=RuntimeFormRef(d.enchantEffectArt); target.magicEffectHitVisuals=RuntimeFormRef(d.hitVisuals); target.magicEffectEnchantVisuals=RuntimeFormRef(d.enchantVisuals); target.magicEffectEquipAbility=RuntimeFormRef(d.equipAbility); target.magicEffectImageSpaceMod=RuntimeFormRef(d.imageSpaceMod); target.magicEffectPerk=RuntimeFormRef(d.perk); target.magicEffectCastingSoundLevel=static_cast<std::uint32_t>(d.castingSoundLevel); target.magicEffectAIScore=d.aiScore; target.magicEffectAIDelayTime=d.aiDelayTimer; target.magicEffectCounterEffects.clear(); for(auto* e:value->counterEffects)if(e)target.magicEffectCounterEffects.push_back(RuntimeFormRef(e)); target.magicEffectSounds.fill({}); for(const auto& pair:value->effectSounds){const auto i=static_cast<std::size_t>(pair.id);if(i<target.magicEffectSounds.size())target.magicEffectSounds[i]=RuntimeFormRef(pair.sound);} target.magicItemDescription=value->magicItemDescription.c_str(); CaptureConditions(value->conditions,target.conditions); return true; }
        case FK::EffectShader: { auto* value=source.As<RE::TESEffectShader>(); if(!value)return false; target.fillTexturePath=value->fillTexture.textureName.c_str(); target.particleShaderTexturePath=value->particleShaderTexture.textureName.c_str(); target.holesTexturePath=value->holesTexture.textureName.c_str(); target.membranePaletteTexturePath=value->membranePaletteTexture.textureName.c_str(); target.particlePaletteTexturePath=value->particlePaletteTexture.textureName.c_str(); const auto& d=value->data; target.flags=d.flags.underlying(); const auto copyColor=[](const RE::Color& c,std::uint8_t& r,std::uint8_t& g,std::uint8_t& b,std::uint8_t& a){r=c.red;g=c.green;b=c.blue;a=c.alpha;}; copyColor(d.fillTextureEffectColorKey1,target.fillColor1Red,target.fillColor1Green,target.fillColor1Blue,target.fillColor1Alpha); copyColor(d.fillTextureEffectColorKey2,target.fillColor2Red,target.fillColor2Green,target.fillColor2Blue,target.fillColor2Alpha); copyColor(d.fillTextureEffectColorKey3,target.fillColor3Red,target.fillColor3Green,target.fillColor3Blue,target.fillColor3Alpha); copyColor(d.edgeEffectColor,target.edgeEffectRed,target.edgeEffectGreen,target.edgeEffectBlue,target.edgeEffectAlpha); copyColor(d.edgeColor,target.edgeColorRed,target.edgeColorGreen,target.edgeColorBlue,target.edgeColorAlpha); copyColor(d.colorKey1,target.particleColor1Red,target.particleColor1Green,target.particleColor1Blue,target.particleColor1Alpha); copyColor(d.colorKey2,target.particleColor2Red,target.particleColor2Green,target.particleColor2Blue,target.particleColor2Alpha); copyColor(d.colorKey3,target.particleColor3Red,target.particleColor3Green,target.particleColor3Blue,target.particleColor3Alpha); target.fillAlphaFadeIn=d.fillTextureEffectAlphaFadeInTime; target.fillFullAlphaTime=d.fillTextureEffectFullAlphaTime; target.fillAlphaFadeOut=d.fillTextureEffectAlphaFadeOutTime; target.fillPersistentAlphaRatio=d.fillTextureEffectPersistentAlphaRatio; target.fillAlphaPulseAmplitude=d.fillTextureEffectAlphaPulseAmplitude; target.fillAlphaPulseFrequency=d.fillTextureEffectAlphaPulseFrequency; target.fillTextureAnimationSpeedU=d.fillTextureEffectTextureAnimationSpeedU; target.fillTextureAnimationSpeedV=d.fillTextureEffectTextureAnimationSpeedV; target.fillTextureScaleU=d.fillTextureEffectTextureScaleU; target.fillTextureScaleV=d.fillTextureEffectTextureScaleV; target.fillFullAlphaRatio=d.fillTextureEffectFullAlphaRatio; target.edgeFalloff=d.edgeEffectFallOff; target.edgeAlphaFadeIn=d.edgeEffectAlphaFadeInTime; target.edgeFullAlphaTime=d.edgeEffectFullAlphaTime; target.edgeAlphaFadeOut=d.edgeEffectAlphaFadeOutTime; target.edgePersistentAlphaRatio=d.edgeEffectPersistentAlphaRatio; target.edgeAlphaPulseAmplitude=d.edgeEffectAlphaPulseAmplitude; target.edgeAlphaPulseFrequency=d.edgeEffectAlphaPulseFrequency; target.edgeFullAlphaRatio=d.edgeEffectFullAlphaRatio; target.edgeWidthAlphaUnits=d.edgeWidthAlphaUnits; target.particleBirthRampUpTime=d.particleShaderParticleBirthRampUpTime; target.particleFullBirthTime=d.particleShaderFullParticleBirthTime; target.particleBirthRampDownTime=d.particleShaderParticleBirthRampDownTime; target.particleFullBirthRatio=d.particleShaderFullParticleBirthRatio; target.particleCount=d.particleShaderPersistantParticleCount; target.particleLifetime=d.particleShaderParticleLifetime; target.particleLifetimeVariance=d.particleShaderParticleLifetimeVariance; target.particleInitialSpeedAlongNormal=d.particleShaderInitialSpeedAlongNormal; target.particleAccelerationAlongNormal=d.particleShaderAccelerationAlongNormal; target.particleScaleKey1=d.particleShaderScaleKey1; target.particleScaleKey2=d.particleShaderScaleKey2; target.particleScaleKey1Time=d.particleShaderScaleKey1Time; target.particleScaleKey2Time=d.particleShaderScaleKey2Time; target.particleColor1AlphaValue=d.colorKey1ColorAlpha; target.particleColor2AlphaValue=d.colorKey2ColorAlpha; target.particleColor3AlphaValue=d.colorKey3ColorAlpha; target.particleColor1Time=d.colorKey1ColorKeyTime; target.particleColor2Time=d.colorKey2ColorKeyTime; target.particleColor3Time=d.colorKey3ColorKeyTime; target.ambientSound=RuntimeFormRef(d.ambientSound); return true; }
        case FK::NPC: { auto* value=source.As<RE::TESNPC>();if(!value)return false;target.fullName=value->fullName.c_str();target.race=RuntimeFormRef(value->race);target.skin=RuntimeFormRef(value->farSkin);target.defaultOutfit=RuntimeFormRef(value->defaultOutfit);target.sleepOutfit=RuntimeFormRef(value->sleepOutfit);target.voice=RuntimeFormRef(value->voiceType);target.npcClass=RuntimeFormRef(value->npcClass);target.combatStyle=RuntimeFormRef(value->combatStyle);target.giftFilter=RuntimeFormRef(value->giftFilter);target.deathItem=RuntimeFormRef(value->deathItem);target.defaultPackageList=RuntimeFormRef(value->defaultPackList);target.crimeFaction=RuntimeFormRef(value->crimeFaction);if(value->headRelatedData){target.hairColor=RuntimeFormRef(value->headRelatedData->hairColor);target.faceTexture=RuntimeFormRef(value->headRelatedData->faceDetails);}const auto flags=value->actorData.actorBaseFlags;target.femaleNpc=flags.any(RE::ACTOR_BASE_DATA::Flag::kFemale);target.oppositeGenderAnim=flags.any(RE::ACTOR_BASE_DATA::Flag::kOppositeGenderAnims);target.essential=flags.any(RE::ACTOR_BASE_DATA::Flag::kEssential);target.protectedNpc=flags.any(RE::ACTOR_BASE_DATA::Flag::kProtected);target.unique=flags.any(RE::ACTOR_BASE_DATA::Flag::kUnique);target.calcStats=flags.any(RE::ACTOR_BASE_DATA::Flag::kPCLevelMult);target.respawn=flags.any(RE::ACTOR_BASE_DATA::Flag::kRespawn);target.doesntAffectStealthMeter=flags.any(RE::ACTOR_BASE_DATA::Flag::kDoesntAffectStealthMeter);target.doesntBleed=flags.any(RE::ACTOR_BASE_DATA::Flag::kDoesntBleed);target.bleedoutOverrideFlag=flags.any(RE::ACTOR_BASE_DATA::Flag::kBleedoutOverride);target.simpleActor=flags.any(RE::ACTOR_BASE_DATA::Flag::kSimpleActor);target.noActivation=flags.any(RE::ACTOR_BASE_DATA::Flag::kNoActivation);target.ghost=flags.any(RE::ACTOR_BASE_DATA::Flag::kIsGhost);target.invulnerable=flags.any(RE::ACTOR_BASE_DATA::Flag::kInvulnerable);target.height=value->height;target.weight=value->weight;target.red=value->bodyTintColor.red;target.green=value->bodyTintColor.green;target.blue=value->bodyTintColor.blue;target.alpha=value->bodyTintColor.alpha;target.health=value->playerSkills.health;target.magicka=value->playerSkills.magicka;target.stamina=value->playerSkills.stamina;target.healthOffset=value->actorData.healthOffset;target.magickaOffset=value->actorData.magickaOffset;target.staminaOffset=value->actorData.staminaOffset;target.calcMinLevel=value->actorData.calcLevelMin;target.calcMaxLevel=value->actorData.calcLevelMax;target.npcLevel=value->actorData.level;target.speedMult=value->actorData.speedMult;target.dispositionBase=value->actorData.baseDisposition;target.bleedoutOverride=value->actorData.bleedoutOverride;for(std::size_t i=0;i<target.skills.size();++i){target.skills[i]=value->playerSkills.values[i];target.skillOffsets[i]=value->playerSkills.offsets[i];}target.aiAggression=static_cast<std::int32_t>(value->GetAggressionLevel());target.aiConfidence=static_cast<std::int32_t>(value->GetConfidenceLevel());target.aiAssistance=static_cast<std::int32_t>(value->GetAssistanceLevel());target.aiEnergyLevel=value->GetEnergyLevel();target.aiMorality=static_cast<std::int32_t>(value->GetMoralityLevel());target.aiMood=static_cast<std::int32_t>(value->GetMoodLevel());target.aiAggroRadiusBehavior=value->aiData.aggroRadiusBehaviour;target.aiAggroRadiusWarn=value->aiData.aggroRadius[RE::ACTOR_AGGRO_RADIUS::kWarn];target.aiAggroRadiusWarnAndAttack=value->aiData.aggroRadius[RE::ACTOR_AGGRO_RADIUS::kWarnAndAttack];target.aiAggroRadiusAttack=value->aiData.aggroRadius[RE::ACTOR_AGGRO_RADIUS::kAttack];target.aiNoSlowApproach=value->aiData.noSlowApproach;target.packages.clear();for(auto* x:value->aiPackages.packages)if(x)target.packages.push_back(RuntimeFormRef(x));target.npcFactions.clear();for(const auto& x:value->factions)if(x.faction)target.npcFactions.push_back({RuntimeFormRef(x.faction),x.rank});target.npcPerks.clear();for(std::uint32_t i=0;i<value->perkCount;++i)if(value->perks&&value->perks[i].perk)target.npcPerks.push_back({RuntimeFormRef(value->perks[i].perk),value->perks[i].currentRank});target.spells.clear();if(auto* sd=static_cast<RE::TESSpellList*>(value)->actorEffects)for(std::uint32_t i=0;i<sd->numSpells;++i)if(sd->spells[i])target.spells.push_back(RuntimeFormRef(sd->spells[i]));target.headParts.clear();for(std::int32_t i=0;i<value->numHeadParts;++i)if(value->headParts&&value->headParts[i])target.headParts.push_back(RuntimeFormRef(value->headParts[i]));target.tintLayers.clear();if(value->tintLayers)for(auto* x:*value->tintLayers)if(x)target.tintLayers.push_back({x->tintIndex,x->preset,static_cast<float>(x->interpolationValue)/100.0F,x->tintColor.red,x->tintColor.green,x->tintColor.blue,x->tintColor.alpha});if(value->faceData){std::copy_n(value->faceData->morphs,target.faceMorphs.size(),target.faceMorphs.begin());std::copy_n(value->faceData->parts,target.faceParts.size(),target.faceParts.begin());}return true; }
        case FK::Projectile: {
            auto* value = source.As<RE::BGSProjectile>(); if (!value) return false; const auto& data = value->data;
            target.fullName = value->fullName.c_str(); target.modelPath = value->GetModel(); target.projectileMuzzleFlashModel = value->muzzleFlashModel.GetModel();
            target.projectileFlags = data.flags.underlying(); target.projectileTypes = data.types.underlying(); target.projectileGravity = data.gravity; target.projectileSpeed = data.speed; target.projectileRange = data.range;
            target.projectileLight = RuntimeFormRef(data.light); target.projectileMuzzleFlashLight = RuntimeFormRef(data.muzzleFlashLight); target.projectileTracerChance = data.tracerChance; target.projectileExplosionProximity = data.explosionProximity; target.projectileExplosionTimer = data.explosionTimer; target.projectileExplosionType = RuntimeFormRef(data.explosionType); target.projectileActiveSoundLoop = RuntimeFormRef(data.activeSoundLoop); target.projectileMuzzleFlashDuration = data.muzzleFlashDuration; target.projectileFadeOutTime = data.fadeOutTime; target.projectileForce = data.force; target.projectileCountdownSound = RuntimeFormRef(data.countdownSound); target.projectileDeactivateSound = RuntimeFormRef(data.deactivateSound); target.projectileDefaultWeaponSource = RuntimeFormRef(data.defaultWeaponSource); target.projectileConeSpread = data.coneSpread; target.projectileCollisionRadius = data.collisionRadius; target.projectileLifetime = data.lifetime; target.projectileRelaunchInterval = data.relaunchInterval; target.projectileDecalData = RuntimeFormRef(data.decalData); target.projectileCollisionLayer = RuntimeFormRef(data.collisionLayer); target.projectileSoundLevel = static_cast<std::uint32_t>(value->soundLevel); return true;
        }
        case FK::TextureSet: {
            auto* value = source.As<RE::BGSTextureSet>(); if (!value) return false; for (std::size_t i = 0; i < target.textureSetPaths.size(); ++i) target.textureSetPaths[i] = value->textures[i].textureName.c_str(); target.textureSetFlags = value->flags.underlying(); target.textureSetHasDecal = value->decalData != nullptr; if (value->decalData) { const auto& d = value->decalData->data; target.decalMinWidth=d.decalMinWidth; target.decalMaxWidth=d.decalMaxWidth; target.decalMinHeight=d.decalMinHeight; target.decalMaxHeight=d.decalMaxHeight; target.decalDepth=d.depth; target.decalShininess=d.shininess; target.decalParallaxScale=d.parallaxScale; target.decalParallaxPasses=d.parallaxPasses; target.decalFlags=static_cast<std::uint32_t>(d.flags); target.decalRed=d.color.red; target.decalGreen=d.color.green; target.decalBlue=d.color.blue; target.decalAlpha=d.color.alpha; } return true;
        }
        case FK::Hazard: { auto* value=source.As<RE::BGSHazard>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.modelPath=value->GetModel(); target.hazardImageSpaceModifier=RuntimeFormRef(value->imageSpaceModifying); target.hazardLimit=value->data.limit; target.hazardRadius=value->data.radius; target.hazardLifetime=value->data.lifetime; target.hazardImageSpaceRadius=value->data.imageSpaceRadius; target.hazardTargetInterval=value->data.targetInterval; target.hazardFlags=value->data.flags.underlying(); target.hazardSpell=RuntimeFormRef(value->data.spell); target.hazardLight=RuntimeFormRef(value->data.light); target.hazardImpactDataSet=RuntimeFormRef(value->data.impactDataSet); target.hazardSound=RuntimeFormRef(value->data.sound); return true; }
        case FK::ImpactData: { auto* value=source.As<RE::BGSImpactData>(); if(!value)return false; const auto& d=value->data; target.modelPath=value->GetModel(); target.impactEffectDuration=d.effectDuration; target.impactOrientation=d.orient.underlying(); target.impactAngleThreshold=d.angleThreshold; target.impactPlacementRadius=d.placementRadius; target.impactSoundLevel=static_cast<std::uint32_t>(d.soundLevel); target.impactFlags=d.flags.underlying(); target.impactResultOverride=d.resultOverride.underlying(); target.impactDecalTextureSet=RuntimeFormRef(value->decalTextureSet); target.impactDecalTextureSet2=RuntimeFormRef(value->decalTextureSet2); target.impactSound1=RuntimeFormRef(value->sound1); target.impactSound2=RuntimeFormRef(value->sound2); target.impactHazard=RuntimeFormRef(value->hazard); const auto& x=value->dData.data; target.decalMinWidth=x.decalMinWidth; target.decalMaxWidth=x.decalMaxWidth; target.decalMinHeight=x.decalMinHeight; target.decalMaxHeight=x.decalMaxHeight; target.decalDepth=x.depth; target.decalShininess=x.shininess; target.decalParallaxScale=x.parallaxScale; target.decalParallaxPasses=x.parallaxPasses; target.decalFlags=static_cast<std::uint32_t>(x.flags); target.decalRed=x.color.red; target.decalGreen=x.color.green; target.decalBlue=x.color.blue; target.decalAlpha=x.color.alpha; return true; }
        case FK::ReferenceEffect: { auto* value=source.As<RE::BGSReferenceEffect>(); if(!value)return false; target.referenceEffectArtObject=RuntimeFormRef(value->data.artObject); target.referenceEffectShader=RuntimeFormRef(value->data.effectShader); target.referenceEffectFlags=value->data.flags.underlying(); return true; }
        case FK::DualCastData: { auto* value=source.As<RE::BGSDualCastData>(); if(!value)return false; target.dualCastProjectile=RuntimeFormRef(value->data.pProjectile); target.dualCastExplosion=RuntimeFormRef(value->data.pExplosion); target.dualCastEffectShader=RuntimeFormRef(value->data.pEffectShader); target.dualCastHitEffectArt=RuntimeFormRef(value->data.pHitEffectArt); target.dualCastImpactDataSet=RuntimeFormRef(value->data.pImpactDataSet); target.dualCastFlags=value->data.flags.underlying(); return true; }
        case FK::Static: { auto* value=source.As<RE::TESObjectSTAT>(); if(!value)return false; target.modelPath=value->GetModel(); target.staticMaterialThresholdAngle=value->data.materialThresholdAngle; target.staticMaterialObject=RuntimeFormRef(value->data.materialObj); target.staticFlags=value->data.flags.underlying(); target.recordFlags=value->formFlags; return true; }
        case FK::MovableStatic: { auto* value=source.As<RE::BGSMovableStatic>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.modelPath=value->GetModel(); target.staticMaterialThresholdAngle=value->TESObjectSTAT::data.materialThresholdAngle; target.staticMaterialObject=RuntimeFormRef(value->TESObjectSTAT::data.materialObj); target.staticFlags=value->TESObjectSTAT::data.flags.underlying(); target.movableStaticSoundLoop=RuntimeFormRef(value->soundLoop); target.movableStaticFlags=value->data.flags.underlying(); target.recordFlags=value->formFlags; return true; }
        case FK::Door: { auto* value=source.As<RE::TESObjectDOOR>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.modelPath=value->GetModel(); target.doorOpenSound=RuntimeFormRef(value->openSound); target.doorCloseSound=RuntimeFormRef(value->closeSound); target.doorLoopSound=RuntimeFormRef(value->loopSound); target.doorFlags=value->flags.underlying(); target.recordFlags=value->formFlags; return true; }
        case FK::CombatStyle: { auto* value=source.As<RE::TESCombatStyle>(); if(!value)return false; std::copy_n(reinterpret_cast<const float*>(std::addressof(value->generalData)),target.combatGeneral.size(),target.combatGeneral.begin()); std::copy_n(reinterpret_cast<const float*>(std::addressof(value->meleeData)),target.combatMelee.size(),target.combatMelee.begin()); std::copy_n(reinterpret_cast<const float*>(std::addressof(value->closeRangeData)),target.combatCloseRange.size(),target.combatCloseRange.begin()); target.combatLongRangeStrafe=value->longRangeData.strafeMult; std::copy_n(reinterpret_cast<const float*>(std::addressof(value->flightData)),target.combatFlight.size(),target.combatFlight.begin()); target.combatStyleFlags=value->flags.underlying(); return true; }
        case FK::SoundCategory: { auto* value=source.As<RE::BGSSoundCategory>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.soundCategoryFlags=value->flags.underlying(); target.soundCategoryParent=RuntimeFormRef(value->parentCategory); target.soundCategoryAttenuation=value->attenuation; target.soundCategoryStaticMult=value->GetStaticVolumeMultiplier(); target.soundCategoryDefaultMenuValue=value->GetDefaultMenuValue(); target.soundCategoryVolumeMult=value->volumeMult; target.soundCategoryFrequencyMult=value->frequencyMult; return true; }
        case FK::Class: { auto* value=source.As<RE::TESClass>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.classTeachesSkill=value->data.teaches.underlying(); target.classMaximumTrainingLevel=value->data.maximumTrainingLevel; std::copy_n(std::addressof(value->data.skillWeights.oneHanded),target.classSkillWeights.size(),target.classSkillWeights.begin()); target.classBleedoutDefault=value->data.bleedoutDefault; target.classVoicePoints=value->data.voicePoints; std::copy_n(std::addressof(value->data.attributeWeights.health),target.classAttributeWeights.size(),target.classAttributeWeights.begin()); target.classIconPath=value->textureName.c_str(); return true; }
        case FK::ImpactDataSet: {
            auto* value = source.As<RE::BGSImpactDataSet>(); if (!value) return false;
            target.impactDataSetEntries.clear();
            for (const auto& [material, impact] : value->impactMap)
                if (material && impact) target.impactDataSetEntries.push_back({ RuntimeFormRef(const_cast<RE::BGSMaterialType*>(material)), RuntimeFormRef(impact) });
            return true;
        }
        case FK::CollisionLayer: {
            auto* value = source.As<RE::BGSCollisionLayer>(); if (!value) return false;
            target.collisionLayerIndex = value->collisionIdx;
            target.collisionLayerColor = (static_cast<std::uint32_t>(value->debugColor.red) << 24) |
                (static_cast<std::uint32_t>(value->debugColor.green) << 16) |
                (static_cast<std::uint32_t>(value->debugColor.blue) << 8) | value->debugColor.alpha;
            target.collisionLayerFlags = value->flags.underlying();
            target.collisionLayerName = value->name.c_str();
            target.collisionLayers.clear();
            for (auto* layer : value->collidesWith) if (layer) target.collisionLayers.push_back(RuntimeFormRef(layer));
            return true;
        }
        case FK::Footstep: { auto* value = source.As<RE::BGSFootstep>(); if (!value) return false; target.footstepTag = value->tag.c_str(); target.footstepImpactDataSet = RuntimeFormRef(value->impactSet); return true; }
        case FK::FootstepSet: {
            auto* value = source.As<RE::BGSFootstepSet>(); if (!value) return false;
            for (std::size_t i = 0; i < target.footstepSets.size(); ++i) { target.footstepSets[i].clear(); for (auto* step : value->entries[i]) if (step) target.footstepSets[i].push_back(RuntimeFormRef(step)); }
            return true;
        }
        case FK::ReverbParameters: { auto* value = source.As<RE::BGSReverbParameters>(); if (!value) return false; target.reverbDecayTime = value->data.decayTime; target.reverbHFReference = value->data.hfReference; std::copy_n(std::addressof(value->data.roomFilter), target.reverbValues.size(), target.reverbValues.begin()); return true; }
        case FK::AcousticSpace: { auto* value = source.As<RE::BGSAcousticSpace>(); if (!value) return false; target.acousticLoopingSound = RuntimeFormRef(value->loopingSound); target.acousticSoundRegion = RuntimeFormRef(value->soundRegion); target.acousticReverb = RuntimeFormRef(value->reverbType); return true; }
        case FK::Apparatus: { auto* value = source.As<RE::BGSApparatus>(); if (!value) return false; CaptureMiscTemplate(target, *value); target.apparatusQuality = static_cast<RE::TESQualityForm&>(*value).quality.underlying(); return true; }
        case FK::StaticCollection: { auto* value = source.As<RE::BGSStaticCollection>(); if (!value) return false; target.modelPath = value->GetModel(); target.recordFlags = value->formFlags; return true; }
        case FK::Grass: {
            auto* value = source.As<RE::TESGrass>(); if (!value) return false; target.modelPath = value->GetModel(); target.grassDensity = value->GetDensity(); target.grassMinSlope = value->GetMinSlopeDegrees(); target.grassMaxSlope = value->GetMaxSlopeDegrees(); target.grassDistanceFromWater = value->GetDistanceFromWaterLevel(); target.grassWaterState = static_cast<std::uint32_t>(value->GetUnderwaterState()); target.grassPositionRange = value->data.positionRange; target.grassHeightRange = value->data.heightRange; target.grassColorRange = value->data.colorRange; target.grassWavePeriod = value->data.wavePeriod; target.grassFlags = value->data.flags.underlying(); return true;
        }
        case FK::Flora: { auto* value=source.As<RE::TESFlora>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.modelPath=value->GetModel(); target.floraSoundLoop=RuntimeFormRef(value->soundLoop); target.floraSoundActivate=RuntimeFormRef(value->soundActivate); target.floraWaterType=RuntimeFormRef(value->waterForm); target.floraFlags=value->flags.underlying(); CaptureKeywords(*value,target.keywords); target.harvestSound=RuntimeFormRef(value->harvestSound); target.produceItem=RuntimeFormRef(value->produceItem); std::copy_n(value->produceChance,target.produceChance.size(),target.produceChance.begin()); return true; }
        case FK::Tree: { auto* value=source.As<RE::TESObjectTREE>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.modelPath=value->GetModel(); std::copy_n(reinterpret_cast<const float*>(std::addressof(value->data)),target.treeAnimation.size(),target.treeAnimation.begin()); target.treeType=value->type.underlying(); target.harvestSound=RuntimeFormRef(value->harvestSound); target.produceItem=RuntimeFormRef(value->produceItem); std::copy_n(value->produceChance,target.produceChance.size(),target.produceChance.begin()); target.recordFlags=value->formFlags; return true; }
        case FK::ConstructibleObject: { auto* value=source.As<RE::BGSConstructibleObject>(); if(!value)return false; target.createdItem=RuntimeFormRef(value->createdItem); target.benchKeyword=RuntimeFormRef(value->benchKeyword); target.numConstructed=value->data.numConstructed; target.requiredItems.clear(); for(std::uint32_t i=0;i<value->requiredItems.numContainerObjects;++i){auto* item=value->requiredItems.containerObjects[i];if(item&&item->obj)target.requiredItems.push_back({RuntimeFormRef(item->obj),item->count});} CaptureConditions(value->conditions,target.conditions); return true; }
        case FK::Container: { auto* value=source.As<RE::TESObjectCONT>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.modelPath=value->GetModel(); target.itemWeight=value->weight; target.containerFlags=value->data.flags.underlying(); target.containerOpenSound=RuntimeFormRef(value->openSound); target.containerCloseSound=RuntimeFormRef(value->closeSound); target.containerAllowStolenItems=value->allowStolenItems; target.recordFlags=value->formFlags; target.containerItems.clear(); for(std::uint32_t i=0;i<value->numContainerObjects;++i){auto* item=value->containerObjects[i];if(!item||!item->obj)continue;DynamicForms::ContainerEntry e;e.item=RuntimeFormRef(item->obj);e.count=item->count;if(item->itemExtra){e.owner=RuntimeFormRef(item->itemExtra->owner);e.healthMult=item->itemExtra->healthMult;const auto bits=reinterpret_cast<std::uintptr_t>(item->itemExtra->conditional.global);if(bits>std::numeric_limits<std::uint32_t>::max())e.conditionGlobal=RuntimeFormRef(item->itemExtra->conditional.global);else e.requiredRank=item->itemExtra->conditional.rank;}target.containerItems.push_back(std::move(e));} return true; }
        case FK::IdleMarker: { auto* value=source.As<RE::BGSIdleMarker>(); if(!value)return false; target.modelPath=value->GetModel(); auto& c=static_cast<RE::BGSIdleCollection&>(*value); target.idleFlags=c.idleFlags.underlying(); target.idleTimer=c.timerCheckForIdle; target.idleAnimations.clear(); for(std::uint32_t i=0;i<c.idleCount;++i)if(c.idles[i])target.idleAnimations.push_back(RuntimeFormRef(c.idles[i])); target.recordFlags=value->formFlags; return true; }
        case FK::EncounterZone: { auto* value = source.As<RE::BGSEncounterZone>(); if (!value) return false; target.encounterOwner = RuntimeFormRef(value->data.zoneOwner); target.encounterLocation = RuntimeFormRef(value->data.location); target.encounterOwnerRank = value->data.ownerRank; target.encounterMinLevel = value->data.minLevel; target.encounterMaxLevel = value->data.maxLevel; target.encounterFlags = value->data.flags.underlying(); return true; }
        case FK::Relationship: { auto* value = source.As<RE::BGSRelationship>(); if (!value) return false; target.relationshipNpc1 = RuntimeFormRef(value->npc1); target.relationshipNpc2 = RuntimeFormRef(value->npc2); target.relationshipAssociation = RuntimeFormRef(value->assocType); target.relationshipLevel = value->level.underlying(); target.relationshipFlags = value->flags.underlying(); return true; }
        case FK::AssociationType: { auto* value = source.As<RE::BGSAssociationType>(); if (!value) return false; target.associationLabels = { value->associationLabels[0][0].c_str(), value->associationLabels[0][1].c_str(), value->associationLabels[1][0].c_str(), value->associationLabels[1][1].c_str() }; target.associationFlags = value->flags.underlying(); return true; }
        case FK::MovementType: { auto* value = source.As<RE::BGSMovementType>(); if (!value) return false; target.movementName = value->movementTypeData.typeName.c_str(); std::copy_n(std::addressof(value->movementTypeData.defaultData.speeds[0][0]), target.movementSpeeds.size(), target.movementSpeeds.begin()); target.movementRotateWhileMoving = value->movementTypeData.defaultData.rotateWhileMovingRun; target.movementDirectional = value->movementTypeData.directional; target.movementSpeed = value->movementTypeData.movementSpeed; target.movementRotationSpeed = value->movementTypeData.rotationSpeed; return true; }
        case FK::WordOfPower: { auto* value = source.As<RE::TESWordOfPower>(); if (!value) return false; target.fullName = value->fullName.c_str(); target.wordTranslation = value->translation.c_str(); return true; }
        case FK::Water: { auto* value=source.As<RE::TESWaterForm>(); if(!value)return false; target.fullName=value->fullName.c_str(); for(std::size_t i=0;i<target.waterNoiseTextures.size();++i)target.waterNoiseTextures[i]=value->noiseTextures[i].textureName.c_str(); target.waterAlpha=static_cast<std::uint8_t>(value->alpha); target.waterFlags=value->flags.underlying(); target.waterMaterial=RuntimeFormRef(value->materialType); target.waterSound=RuntimeFormRef(value->waterSound); target.waterContactSpell=RuntimeFormRef(value->contactSpell); target.waterImageSpace=RuntimeFormRef(value->imageSpace); std::copy_n(std::addressof(value->linearVelocity.x),target.waterLinearVelocity.size(),target.waterLinearVelocity.begin()); std::copy_n(std::addressof(value->angularVelocity.x),target.waterAngularVelocity.size(),target.waterAngularVelocity.begin()); return true; }
        case FK::ImageSpace: { auto* value=source.As<RE::TESImageSpace>(); if(!value)return false; std::copy_n(std::addressof(value->data.hdr.eyeAdaptSpeed),target.imageSpaceHDR.size(),target.imageSpaceHDR.begin()); std::copy_n(std::addressof(value->data.cinematic.saturation),target.imageSpaceCinematic.size(),target.imageSpaceCinematic.begin()); target.imageSpaceTintAmount=value->data.tint.amount; std::copy_n(std::addressof(value->data.tint.color.red),target.imageSpaceTintColor.size(),target.imageSpaceTintColor.begin()); std::copy_n(std::addressof(value->data.depthOfField.strength),target.imageSpaceDOF.size(),target.imageSpaceDOF.begin()); target.imageSpaceDOFFlags=value->data.depthOfField.flags; target.imageSpaceSkyBlur=value->data.depthOfField.skyBlurRadius.underlying(); return true; }
        case FK::LightingTemplate: { auto* value=source.As<RE::BGSLightingTemplate>(); if(!value)return false; const RE::Color* colors[]{&value->data.ambient,&value->data.directional,&value->data.fogColorNear,&value->data.fogColorFar}; for(std::size_t i=0;i<4;++i)target.lightingColors[i]=(static_cast<std::uint32_t>(colors[i]->red)<<24)|(static_cast<std::uint32_t>(colors[i]->green)<<16)|(static_cast<std::uint32_t>(colors[i]->blue)<<8)|colors[i]->alpha; target.lightingValues={value->data.fogNear,value->data.fogFar,value->data.directionalFade,value->data.clipDist,value->data.fogPower,value->data.fogClamp,value->data.lightFadeStart,value->data.lightFadeEnd}; target.lightingDirectionalXY=value->data.directionalXY; target.lightingDirectionalZ=value->data.directionalZ; target.lightingInheritanceFlags=value->data.lightingTemplateInheritanceFlags.underlying(); return true; }
        case FK::Shout: { auto* value = source.As<RE::TESShout>(); if (!value) return false; target.fullName = value->fullName.c_str(); target.menuDisplayObject = RuntimeFormRef(value->menuDispObject); target.equipSlot = RuntimeFormRef(value->equipSlot); for (std::size_t i = 0; i < target.shoutWords.size(); ++i) { target.shoutWords[i] = RuntimeFormRef(value->variations[i].word); target.shoutSpells[i] = RuntimeFormRef(value->variations[i].spell); target.shoutRecoveryTimes[i] = value->variations[i].recoveryTime; } target.recordFlags = value->formFlags; return true; }
        case FK::LeveledItem: { auto* value = source.As<RE::TESLevItem>(); if (!value) return false; CaptureLeveledList(target, *value); return true; }
        case FK::LeveledNPC: { auto* value = source.As<RE::TESLevCharacter>(); if (!value) return false; target.modelPath = value->GetModel(); CaptureLeveledList(target, *value); return true; }
        case FK::LeveledSpell: { auto* value = source.As<RE::TESLevSpell>(); if (!value) return false; CaptureLeveledList(target, *value); return true; }
        case FK::LocationRefType: return source.As<RE::BGSLocationRefType>() != nullptr;
        case FK::Action: { auto* value = source.As<RE::BGSAction>(); if (!value) return false; target.actionIndex = value->index; return true; }
        case FK::MenuIcon: { auto* value = source.As<RE::BGSMenuIcon>(); if (!value) return false; target.inventoryIcon = value->textureName.c_str(); return true; }
        case FK::Eyes: { auto* value = source.As<RE::TESEyes>(); if (!value) return false; target.fullName = value->fullName.c_str(); target.eyesTexture = value->textureName.c_str(); target.eyesFlags = value->flags.underlying(); target.recordFlags = value->formFlags; return true; }
        case FK::Note: { auto* value = source.As<RE::BGSNote>(); if (!value) return false; target.modelPath = value->GetModel(); target.fullName = value->fullName.c_str(); target.inventoryIcon = value->textureName.c_str(); target.pickupSound = RuntimeFormRef(value->pickupSound); target.putdownSound = RuntimeFormRef(value->putdownSound); return true; }
        case FK::AnimatedObject: { auto* value = source.As<RE::TESObjectANIO>(); if (!value) return false; target.modelPath = value->GetModel(); target.animatedUnloadEvent = value->unloadEventName.c_str(); return true; }
        case FK::LoadScreen: { auto* value=source.As<RE::TESLoadScreen>(); if(!value)return false; target.loadScreenText=value->loadingText.c_str(); target.recordFlags=value->formFlags; CaptureConditions(value->conditions,target.conditions); if(value->loadNIFData){const auto& d=*value->loadNIFData;target.loadScreenObject=RuntimeFormRef(d.loadNIF);target.loadScreenInitialScale=d.initialScale;std::copy_n(d.rotationConstraints,target.loadScreenRotationConstraints.size(),target.loadScreenRotationConstraints.begin());std::copy_n(d.rotationOffsetConstraints,target.loadScreenRotationOffsetConstraints.size(),target.loadScreenRotationOffsetConstraints.begin());std::copy_n(d.initialTranslationOffset,target.loadScreenTranslationOffset.size(),target.loadScreenTranslationOffset.begin());target.loadScreenCameraPath=d.cameraPath.model.c_str();} return true; }
        case FK::ShaderParticleGeometry: { auto* value=source.As<RE::BGSShaderParticleGeometryData>(); if(!value)return false; for(std::size_t i=0;i<target.shaderParticleSettings.size();++i){const auto& s=value->GetSettingRef(static_cast<RE::BGSShaderParticleGeometryData::DataID>(i));target.shaderParticleSettings[i]=(i==7||i==8||i==9)?static_cast<float>(s.i):s.f;} target.shaderParticleTexture=REL::Module::IsVR()?value->GetVRRuntimeData().particleTexture.textureName.c_str():value->GetRuntimeData().particleTexture.textureName.c_str(); return true; }
        case FK::AddonNode: { auto* value = source.As<RE::BGSAddonNode>(); if (!value) return false; target.modelPath = value->GetModel(); target.addonIndex = value->index; target.addonSound = RuntimeFormRef(value->sound); target.addonMasterParticleCap = value->data.masterParticleCap; target.addonFlags = value->data.flags.underlying(); return true; }
        case FK::Faction: {
            auto* value=source.As<RE::TESFaction>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.factionFlags=static_cast<std::uint32_t>(value->data.flags); target.factionReactions.clear(); for(auto* r:value->reactions) if(r&&r->form) target.factionReactions.push_back({RuntimeFormRef(r->form),r->reaction,static_cast<std::uint32_t>(r->fightReaction)}); target.factionRanks.clear(); for(auto* r:value->rankData) if(r) target.factionRanks.push_back({r->maleRankTitle.c_str(),r->femaleRankTitle.c_str(),r->textureInsignia.textureName.c_str()}); const auto& c=value->crimeData; target.factionJailMarker=RuntimeFormRef(c.factionJailMarker); target.factionWaitMarker=RuntimeFormRef(c.factionWaitMarker); target.factionStolenContainer=RuntimeFormRef(c.factionStolenContainer); target.factionPlayerInventoryContainer=RuntimeFormRef(c.factionPlayerInventoryContainer); target.factionCrimeGroup=RuntimeFormRef(c.crimeGroup); target.factionJailOutfit=RuntimeFormRef(c.jailOutfit); target.factionArrest=c.crimevalues.arrest; target.factionAttackOnSight=c.crimevalues.attackOnSight; target.factionMurderCrimeGold=c.crimevalues.murderCrimeGold; target.factionAssaultCrimeGold=c.crimevalues.assaultCrimeGold; target.factionTrespassCrimeGold=c.crimevalues.trespassCrimeGold; target.factionPickpocketCrimeGold=c.crimevalues.pickpocketCrimeGold; target.factionStealCrimeGoldMult=c.crimevalues.stealCrimeGoldMult; target.factionEscapeCrimeGold=c.crimevalues.escapeCrimeGold; target.factionWerewolfCrimeGold=c.crimevalues.werewolfCrimeGold; const auto& v=value->vendorData; target.factionVendorStartHour=v.vendorValues.startHour; target.factionVendorEndHour=v.vendorValues.endHour; target.factionVendorRadius=v.vendorValues.locationRadius; target.factionVendorBuysStolen=v.vendorValues.buysStolen; target.factionVendorNotBuySell=v.vendorValues.notBuySell; target.factionVendorBuysNonStolen=v.vendorValues.buysNonStolen; target.factionVendorSellBuyList=RuntimeFormRef(v.vendorSellBuyList); target.factionMerchantContainer=RuntimeFormRef(v.merchantContainer); return true;
        }
        case FK::IdleAnimation: { auto* value=source.As<RE::TESIdleForm>(); if(!value)return false; CaptureConditions(value->conditions,target.conditions); target.idleLoopMin=value->data.loopMin; target.idleLoopMax=value->data.loopMax; target.idleAnimationFlags=value->data.flags.underlying(); target.idleAnimationGroupSelection=value->data.animationGroupSelection; target.idleReplayDelay=value->data.replayDelay; target.idleParent=RuntimeFormRef(value->parentIdle); target.idlePrevious=RuntimeFormRef(value->prevIdle); target.idleAnimationFile=value->animFileName.c_str(); target.idleAnimationEvent=value->animEventName.c_str(); return true; }
        case FK::MaterialObject: { auto* value=source.As<RE::BGSMaterialObject>(); if(!value)return false; target.modelPath=value->GetModel(); const auto& d=value->directionalData; target.materialDirectionalData={d.falloffScale,d.falloffBias,d.noiseUVScale,d.materialUVScale,d.ProjectionDir.x,d.ProjectionDir.y,d.ProjectionDir.z,d.normalDampener,d.singlePassColor.red,d.singlePassColor.green,d.singlePassColor.blue}; target.materialSinglePass=d.singlePass; target.materialObjectFlags=d.flags.underlying(); return true; }
        case FK::Message: { auto* value=source.As<RE::BGSMessage>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.messageMenuIcon=RuntimeFormRef(value->icon); target.messageOwnerQuest=RuntimeFormRef(value->ownerQuest); target.messageFlags=value->flags.underlying(); target.messageDisplayTime=value->displayTime; target.messageButtons.clear(); for(auto* b:value->menuButtons) if(b) { DynamicForms::MessageButton x; x.text=b->text.c_str(); CaptureConditions(b->conditions,x.conditions); target.messageButtons.push_back(std::move(x)); } return true; }
        case FK::LandTexture: { auto* value=source.As<RE::TESLandTexture>(); if(!value)return false; target.landTextureSet=RuntimeFormRef(value->textureSet); target.landFriction=value->havokData.friction; target.landRestitution=value->havokData.restitution; target.landMaterialType=RuntimeFormRef(value->materialType); target.landSpecularExponent=value->specularExponent; target.landShaderTextureIndex=value->shaderTextureIndex; target.landGrasses.clear(); for(auto* g:value->textureGrassList) if(g)target.landGrasses.push_back(RuntimeFormRef(g)); return true; }
        case FK::SoundOutputModel: { auto* value=source.As<RE::BGSSoundOutput>(); if(!value)return false; target.soundOutputType=value->type.underlying(); target.soundOutputFlags=value->data.flags.underlying(); target.soundOutputReverbSend=value->data.reverbSendPct; if(value->attenuation){target.soundOutputMinDistance=value->attenuation->data.minDistance;target.soundOutputMaxDistance=value->attenuation->data.maxDistance;std::copy_n(value->attenuation->data.curve,target.soundOutputCurve.size(),target.soundOutputCurve.begin());} if(value->speakerOutputs)std::copy_n(std::addressof(value->speakerOutputs->channels[0].l),target.soundOutputSpeakers.size(),target.soundOutputSpeakers.begin()); return true; }
        case FK::LensFlare: { auto* value=source.As<RE::BGSLensFlare>(); if(!value)return false; target.lensFlareFadeDistanceRadiusScale=value->fadeDistRadiusScale; target.lensFlareColorInfluence=value->colorInfluence; return true; }
        case FK::Debris: { auto* value=source.As<RE::BGSDebris>(); if(!value)return false; target.debrisEntries.clear(); for(auto* e:value->data) if(e) target.debrisEntries.push_back({e->percentage,e->flags.underlying(),e->fileName?e->fileName:""}); return true; }
        case FK::ImageSpaceModifier: { auto* value=source.As<RE::TESImageSpaceModifier>(); if(!value)return false; const auto& d=value->data; target.imageModifierAnimatable=d.animatable; target.imageModifierDuration=d.duration; std::copy_n(std::addressof(d.hdr.eyeAdaptSpeed.mult),target.imageModifierHDR.size(),target.imageModifierHDR.begin()); std::copy_n(std::addressof(d.cinematic.saturation.mult),target.imageModifierCinematic.size(),target.imageModifierCinematic.begin()); target.imageModifierTintColor=d.tintColor; target.imageModifierBlurRadius=d.blurRadius; target.imageModifierDoubleVisionStrength=d.doubleVisionStrength; target.imageModifierRadialBlurStrength=d.radialBlurStrength; target.imageModifierRadialBlurRampUp=d.radialBlurRampUp; target.imageModifierRadialBlurStart=d.radialBlurStart; target.imageModifierUseTargetForRadialBlur=d.useTargetForRadialBlur; target.imageModifierRadialBlurCenter={d.radialBlurCenter.x,d.radialBlurCenter.y}; target.imageModifierDofStrength=d.dof.strength; target.imageModifierDofDistance=d.dof.distance; target.imageModifierDofRange=d.dof.range; target.imageModifierDofUseTarget=d.dof.useTarget; target.imageModifierDofFlags=d.dof.flags.underlying(); target.imageModifierRadialBlurRampDown=d.radialBlurRampDown; target.imageModifierRadialBlurDownStart=d.radialBlurDownStart; target.imageModifierFadeColor=d.fadeColor; target.imageModifierMotionBlurStrength=d.motionBlurStrength; return true; }
        case FK::CameraShot: { auto* value=source.As<RE::BGSCameraShot>(); if(!value)return false; target.modelPath=value->GetModel(); target.cameraImageSpaceModifier=RuntimeFormRef(value->imageSpaceModifying); target.cameraAction=value->data.cameraAction.underlying(); target.cameraLocation=value->data.location.underlying(); target.cameraTarget=value->data.target.underlying(); target.cameraFlags=value->data.flags.underlying(); std::copy_n(std::addressof(value->data.playerTimeMult),target.cameraTiming.size(),target.cameraTiming.begin()); return true; }
        case FK::CameraPath: { auto* value=source.As<RE::BGSCameraPath>(); if(!value)return false; CaptureConditions(value->conditions,target.conditions); target.cameraPathShots.clear(); for(auto* shot:value->shots)if(shot)target.cameraPathShots.push_back(RuntimeFormRef(shot)); target.cameraPathFlags=value->data.flags.underlying(); target.cameraPathParent=RuntimeFormRef(value->parentPath); target.cameraPathPrevious=RuntimeFormRef(value->prevPath); return true; }
        case FK::TalkingActivator: { auto* value=source.As<RE::BGSTalkingActivator>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.modelPath=value->GetModel(); target.talkingVoiceType=RuntimeFormRef(value->voiceType); return true; }
        case FK::Furniture: { auto* value=source.As<RE::TESFurniture>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.modelPath=value->GetModel(); target.furnitureFlags=value->furnFlags.underlying(); target.furnitureWorkbenchType=value->workBenchData.benchType.underlying(); target.furnitureWorkbenchSkill=value->workBenchData.usesSkill.underlying(); target.furnitureAssociatedSpell=RuntimeFormRef(value->associatedForm); return true; }
        case FK::Weather: { auto* value=source.As<RE::TESWeather>(); if(!value)return false; target.weatherFlags=value->data.flags.underlying(); target.weatherWindSpeed=value->data.windSpeed; target.weatherTransitionDelta=value->data.transDelta; target.weatherSunGlare=value->data.sunGlare; target.weatherSunDamage=value->data.sunDamage; std::copy_n(std::addressof(value->fogData.dayNear),target.weatherFogData.size(),target.weatherFogData.begin()); target.weatherPrecipitation=RuntimeFormRef(value->precipitationData); target.weatherReferenceEffect=RuntimeFormRef(value->referenceEffect); target.weatherLensFlare=RuntimeFormRef(value->sunGlareLensFlare); for(std::size_t i=0;i<4;++i){target.weatherImageSpaces[i]=RuntimeFormRef(value->imageSpaces[i]);target.weatherVolumetricLighting[i]=RuntimeFormRef(value->volumetricLighting[i]);} return true; }
        case FK::Climate: { auto* value=source.As<RE::TESClimate>(); if(!value)return false; target.climateNightSkyModel=value->nightSky.GetModel(); target.climateSunTexture=value->skyObjects[0].textureName.c_str(); target.climateSunGlareTexture=value->skyObjects[1].textureName.c_str(); target.climateWeatherEntries.clear(); for(auto* e:value->weatherList)if(e&&e->weather)target.climateWeatherEntries.push_back({RuntimeFormRef(e->weather),e->chance,RuntimeFormRef(e->global)}); target.climateTimes={value->timing.sunrise.begin,value->timing.sunrise.end,value->timing.sunset.begin,value->timing.sunset.end}; target.climateVolatility=value->timing.volatility; target.climateMoonPhaseLength=value->timing.moonPhaseLength.underlying(); return true; }
        case FK::Location: { auto* value=source.As<RE::BGSLocation>(); if(!value)return false; target.fullName=value->fullName.c_str(); CaptureKeywords(*value,target.keywords); target.locationParent=RuntimeFormRef(value->parentLoc); target.locationCrimeFaction=RuntimeFormRef(value->unreportedCrimeFaction); target.locationMusicType=RuntimeFormRef(value->musicType); target.locationWorldRadius=value->worldLocRadius; return true; }
        case FK::MusicType: { auto* value=source.As<RE::BGSMusicType>(); if(!value)return false; target.musicTypeFlags=value->flags.underlying(); target.musicTypePriority=value->priority; target.musicTypeDucking=value->ducksOtherMusicBy; target.musicTypeFadeTime=value->fadeTime; target.musicTypeTracks.clear(); return true; }
        case FK::MusicTrack: { auto* value=source.As<RE::BGSMusicTrackFormWrapper>(); if(!value||!value->track)return false; auto* track=skyrim_cast<RE::BGSMusicSingleTrack*>(value->track); if(!track)return true; CaptureConditions(track->conditions,target.conditions); target.musicTrackPath.clear(); target.musicTrackFinalePath.clear(); target.musicTrackCuePoints.assign(track->cuePoints.begin(),track->cuePoints.end()); if(track->loopData){target.musicTrackLoopBegin=track->loopData->loopBegin;target.musicTrackLoopEnd=track->loopData->loopEnd;target.musicTrackLoopCount=track->loopData->loopCount;} return true; }
        case FK::BodyPartData: { auto* value=source.As<RE::BGSBodyPartData>(); if(!value)return false; target.modelPath=value->GetModel(); target.bodyPartRagdoll=RuntimeFormRef(value->ragdoll); return true; }
        case FK::VolumetricLighting: { auto* value=source.As<RE::BGSVolumetricLighting>(); if(!value)return false; target.volumetricLightingData={value->intensity,value->customColor.contribution,value->color.red,value->color.green,value->color.blue,value->density.contribution,value->density.size,value->density.windSpeed,value->density.fallingSpeed,value->phaseFunction.contribution}; return true; }
        case FK::Sound: { auto* value=source.As<RE::TESSound>(); if(!value)return false; target.legacySoundDescriptor=RuntimeFormRef(value->descriptor); return true; }
        case FK::ActorValueInfo: { auto* value=source.As<RE::ActorValueInfo>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.inventoryIcon=value->textureName.c_str(); target.actorValueAbbreviation=value->abbreviation.c_str(); target.actorValueEnumName=value->enumName?value->enumName:""; target.actorValueFlags=value->flags.underlying(); target.actorValueType=static_cast<std::uint32_t>(value->type); target.actorValueEnumValues.clear(); for(std::size_t i=0;i<value->enumValueCount&&i<10;++i)target.actorValueEnumValues.emplace_back(value->enumValues[i]?value->enumValues[i]:""); target.actorValueHasSkillData=value->skill!=nullptr; if(value->skill)std::copy_n(std::addressof(value->skill->useMult),target.actorValueSkillData.size(),target.actorValueSkillData.begin()); return true; }
        case FK::DialogueBranch: { auto* value=source.As<RE::BGSDialogueBranch>(); if(!value)return false; target.dialogueBranchFlags=value->flags.underlying(); target.dialogueBranchQuest=RuntimeFormRef(value->quest); target.dialogueBranchStartingTopic=RuntimeFormRef(value->startingTopic); target.dialogueBranchType=static_cast<std::uint32_t>(value->type); return true; }
        case FK::DialogueTopic: { auto* value=source.As<RE::TESTopic>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.dialogueTopicFlags=value->data.topicFlags.underlying(); target.dialogueTopicType=value->data.type.underlying(); target.dialogueTopicSubtype=value->data.subtype.underlying(); target.dialogueTopicPriority=static_cast<std::uint8_t>(value->priorityAndJournalIndex>>24); target.dialogueTopicJournalIndex=value->priorityAndJournalIndex&0x00FFFFFFu; target.dialogueTopicBranch=RuntimeFormRef(value->ownerBranch); target.dialogueTopicQuest=RuntimeFormRef(value->ownerQuest); target.dialogueTopicInfos.clear(); for(std::uint32_t i=0;i<value->numTopicInfos;++i)if(value->topicInfos[i])target.dialogueTopicInfos.push_back(RuntimeFormRef(value->topicInfos[i])); return true; }
        case FK::DialogueInfo: { auto* value=source.As<RE::TESTopicInfo>(); if(!value)return false; target.dialogueInfoTopic=RuntimeFormRef(value->parentTopic); target.dialogueInfoSharedInfo=RuntimeFormRef(value->dataInfo); CaptureConditions(value->objConditions,target.conditions); target.dialogueInfoIndex=value->infoIndex; target.dialogueInfoFavorLevel=value->favorLevel.underlying(); target.dialogueInfoFlags=value->data.flags.underlying(); target.dialogueInfoResetHours=value->data.timeUntilReset; return true; }
        case FK::Quest: { auto* value=source.As<RE::TESQuest>(); if(!value)return false; target.fullName=value->fullName.c_str(); target.questDelayTime=value->data.questDelayTime; target.questFlags=value->data.flags.underlying(); target.questPriority=value->data.priority; target.questType=value->data.questType.underlying(); CaptureConditions(value->objConditions,target.conditions); CaptureConditions(value->storyManagerConditions,target.questStoryConditions); target.questTextGlobals.clear(); if(value->textGlobals)for(auto* x:*value->textGlobals)if(x)target.questTextGlobals.push_back(RuntimeFormRef(x)); target.questStages.clear(); if(value->waitingStages)for(auto* x:*value->waitingStages)if(x)target.questStages.push_back({x->data.index,x->data.flags.underlying()}); target.questAliases.clear(); for(auto* base:value->aliases){auto* x=skyrim_cast<RE::BGSRefAlias*>(base);if(!x)continue;DynamicForms::QuestAlias a;a.name=x->aliasName.c_str();a.id=x->aliasID;a.flags=x->flags.underlying();a.fillType=x->fillType.underlying();if(x->conditions)CaptureConditions(*x->conditions,a.conditions);switch(a.fillType){case 1:if(auto ref=x->fillData.forced.forcedRef.get())a.forcedReference=RuntimeFormRef(ref.get());break;case 2:a.sourceAliasId=x->fillData.fromAlias.forcedFromAlias;a.sourceRefType=RuntimeFormRef(x->fillData.fromAlias.forcedRefType);break;case 5:a.externalQuest=RuntimeFormRef(x->fillData.fromExternal.externalQuest);a.externalAliasId=x->fillData.fromExternal.externalAlias;break;case 6:a.uniqueActor=RuntimeFormRef(x->fillData.uniqueActor.uniqueActor);break;case 7:a.sourceAliasId=x->fillData.nearAlias.nearAlias;break;default:break;}target.questAliases.push_back(std::move(a));} target.questObjectives.clear(); for(auto* x:value->objectives){if(!x)continue;DynamicForms::QuestObjective o;o.text=x->displayText.c_str();o.index=x->index;o.flags=x->flags.underlying();for(std::uint32_t i=0;i<x->numTargets;++i)if(auto* q=x->targets[i]){DynamicForms::QuestTarget t;t.aliasId=q->alias;t.flags=q->flags.underlying();CaptureConditions(q->conditions,t.conditions);o.targets.push_back(std::move(t));}target.questObjectives.push_back(std::move(o));} return true; }
        case FK::StoryManagerBranchNode: case FK::StoryManagerEventNode: { auto* value=source.As<RE::BGSStoryManagerBranchNode>();if(!value)return false;target.storyParent=RuntimeFormRef(value->parent);target.storyPreviousSibling=RuntimeFormRef(value->previousSibling);target.storyMaxQuests=value->maxQuests;target.storyNodeFlags=value->flags.nodeFlags.underlying();target.storyQuestFlags=value->flags.questFags.underlying();CaptureConditions(value->conditions,target.conditions);target.storyChildren.clear();for(auto* x:value->children)if(x)target.storyChildren.push_back(RuntimeFormRef(x));if(auto* eventNode=source.As<RE::BGSStoryManagerEventNode>();eventNode&&eventNode->event)target.storyEventId=std::string(eventNode->event->uniqueID,4);return true; }
        case FK::StoryManagerQuestNode: { auto* value=source.As<RE::BGSStoryManagerQuestNode>();if(!value)return false;target.storyParent=RuntimeFormRef(value->parent);target.storyPreviousSibling=RuntimeFormRef(value->previousSibling);target.storyMaxQuests=value->maxQuests;target.storyNodeFlags=value->flags.nodeFlags.underlying();target.storyQuestFlags=value->flags.questFags.underlying();CaptureConditions(value->conditions,target.conditions);target.storyNumQuestsToStart=value->numQuestsToStart;target.storyQuests.clear();for(auto* q:value->quests)if(q)target.storyQuests.push_back({RuntimeFormRef(q),CaptureMapValue(value->perQuestFlags,q,0u),CaptureMapValue(value->perQuestHoursUntilReset,q,0.0F)});return true; }
        case FK::Scene: { auto* value=source.As<RE::BGSScene>();if(!value)return false;target.sceneParentQuest=RuntimeFormRef(value->parentQuest);target.sceneFlags=value->flags.underlying();CaptureConditions(value->conditions,target.conditions);target.sceneActors.assign(value->actors.begin(),value->actors.end());target.sceneActorFlags.clear();for(auto x:value->actorFlags)target.sceneActorFlags.push_back(x.underlying());target.sceneActorBehaviorFlags.clear();for(auto x:value->actorProgressionFlags)target.sceneActorBehaviorFlags.push_back(x.underlying());target.scenePhases.clear();for(auto* p:value->phases)if(p){DynamicForms::ScenePhase phase;CaptureConditions(p->startConditions,phase.startConditions);CaptureConditions(p->completionConditions,phase.completionConditions);phase.questNode=RuntimeFormRef(p->questNode);target.scenePhases.push_back(std::move(phase));}target.sceneActions.clear();for(auto* a:value->actions)if(a)target.sceneActions.push_back(CaptureSceneAction(*a));return true; }
        case FK::Package: { auto* value=source.As<RE::TESPackage>(); if(!value)return false; target.packageProcedureType=value->procedureType.underlying(); target.packageTemplate={}; target.packageFlags=value->packData.packFlags.underlying(); target.packageType=value->packData.packType.underlying(); target.packageInterruptType=value->packData.interruptOverrideType.underlying(); target.packagePreferredSpeed=value->packData.maxSpeed.underlying(); target.packageInterruptFlags=value->packData.foBehaviorFlags.underlying(); target.packageSpecificFlags=value->packData.packageSpecificFlags; target.packageIdles.clear(); if(value->idleCollection){target.packageIdleFlags=value->idleCollection->idleFlags.underlying();target.packageIdleTimer=value->idleCollection->timerCheckForIdle;for(std::uint32_t i=0;i<value->idleCollection->idleCount;++i)if(value->idleCollection->idles[i])target.packageIdles.push_back(RuntimeFormRef(value->idleCollection->idles[i]));} target.packageMonth=value->packSched.psData.month; target.packageDayOfWeek=value->packSched.psData.dayOfWeek.underlying(); target.packageDate=value->packSched.psData.date; target.packageHour=value->packSched.psData.hour; target.packageMinute=value->packSched.psData.minute; target.packageDuration=value->packSched.psData.duration; CaptureConditions(value->packConditions,target.conditions); target.packageCombatStyle=RuntimeFormRef(value->combatStyle); target.packageOwnerQuest=RuntimeFormRef(value->ownerQuest);CapturePackageEvent(value->onBegin,target.packageOnBegin);CapturePackageEvent(value->onEnd,target.packageOnEnd);CapturePackageEvent(value->onChange,target.packageOnChange); if(value->packLoc){target.packageLocationType=value->packLoc->locType.underlying();target.packageLocationRadius=value->packLoc->rad;if(target.packageLocationType==0||target.packageLocationType==1||target.packageLocationType==4||target.packageLocationType==6)target.packageLocationObject=RuntimeFormRef(value->packLoc->data.object);else target.packageLocationValue=*reinterpret_cast<const std::uint32_t*>(std::addressof(value->packLoc->data));} if(value->packTarg){target.packageTargetType=value->packTarg->targType;target.packageTargetValue=value->packTarg->value;if(target.packageTargetType==4||target.packageTargetType==5)target.packageTargetAlias=value->packTarg->target.aliasID;else if(target.packageTargetType==2)target.packageTargetAlias=value->packTarg->target.objType.underlying();else target.packageTargetForm=RuntimeFormRef(value->packTarg->target.object);} return true; }
        case FK::Race: { auto* value=source.As<RE::TESRace>(); if(!value)return false; target.fullName=value->fullName.c_str(); CaptureKeywords(*value,target.keywords); target.skin=RuntimeFormRef(value->skin); target.raceFlags=value->data.flags.underlying(); target.raceFlags2=value->data.flags2.underlying(); target.raceSize=value->data.raceSize.underlying(); for(std::size_t i=0;i<7;++i){target.raceSkillBoostSkills[i]=static_cast<std::int32_t>(value->data.skillBoosts[i].skill.underlying());target.raceSkillBoostBonuses[i]=value->data.skillBoosts[i].bonus;} for(std::size_t i=0;i<2;++i){target.raceHeight[i]=value->data.height[i];target.raceWeight[i]=value->data.weight[i];target.raceSkeletonModels[i]=value->skeletonModels[i].GetModel();target.raceBehaviorGraphs[i]=value->behaviorGraphs[i].GetModel();target.raceBodyTextureModels[i]=value->bodyTextureModels[i].GetModel();target.raceVoiceTypes[i]=RuntimeFormRef(value->defaultVoiceTypes[i]);target.raceDecapitateArmors[i]=RuntimeFormRef(value->decapitateArmors[i]);} target.raceStats={value->data.startingHealth,value->data.startingMagicka,value->data.startingStamina,value->data.baseCarryWeight,value->data.baseMass,value->data.accelerate,value->data.decelerate,value->data.injuredHealthPercent,value->data.healthRegen,value->data.magickaRegen,value->data.staminaRegen,value->data.unarmedDamage,value->data.unarmedReach,value->data.aimAngleTolerance,value->data.flightRadius}; target.raceBodyPartData=RuntimeFormRef(value->bodyPartData);target.raceBloodMaterial=RuntimeFormRef(value->bloodImpactMaterial);target.raceImpactDataSet=RuntimeFormRef(value->impactDataSet);target.raceDismemberBlood=RuntimeFormRef(value->dismemberBlood);target.raceCorpseOpenSound=RuntimeFormRef(value->corpseOpenSound);target.raceCorpseCloseSound=RuntimeFormRef(value->corpseCloseSound);target.raceEquipSlots.clear();for(auto* x:value->equipSlots)if(x)target.raceEquipSlots.push_back(RuntimeFormRef(x));target.raceValidEquipTypes=value->validEquipTypes.underlying();target.raceUnarmedEquipSlot=RuntimeFormRef(value->unarmedEquipSlot);target.raceMorphRace=RuntimeFormRef(value->morphRace);target.raceArmorParentRace=RuntimeFormRef(value->armorParentRace);for(std::size_t i=0;i<6;++i)target.raceMovementTypes[i]=RuntimeFormRef(value->baseMoveTypes[i]);target.raceFaceClamp=value->clampFaceGeoValue;target.raceFaceClamp2=value->clampFaceGeoValue2;target.raceMountData={value->data.mountOffset.x,value->data.mountOffset.y,value->data.mountOffset.z,value->data.dismountOffset.x,value->data.dismountOffset.y,value->data.dismountOffset.z,value->data.mountCameraOffset.x,value->data.mountCameraOffset.y,value->data.mountCameraOffset.z};target.raceAngularData={value->data.angleAccelerate,value->data.angleTolerance};for(std::size_t i=0;i<target.raceBipedObjectNames.size();++i)target.raceBipedObjectNames[i]=value->bipedObjectNameA[i].c_str();target.racePhonemeTargets.clear();for(const auto& x:value->phonemeTargets)target.racePhonemeTargets.emplace_back(x.c_str());for(std::size_t sex=0;sex<2;++sex)if(auto* face=value->faceRelatedData[sex]){target.raceHeadParts[sex].clear();if(face->headParts)for(auto* x:*face->headParts)if(x)target.raceHeadParts[sex].push_back(RuntimeFormRef(x));target.racePresetNPCs[sex].clear();if(face->presetNPCs)for(auto* x:*face->presetNPCs)if(x)target.racePresetNPCs[sex].push_back(RuntimeFormRef(x));target.raceHairColors[sex].clear();if(face->availableHairColors)for(auto* x:*face->availableHairColors)if(x)target.raceHairColors[sex].push_back(RuntimeFormRef(x));target.raceFaceDetailTextures[sex].clear();if(face->faceDetailsTextureSets)for(auto* x:*face->faceDetailsTextureSets)if(x)target.raceFaceDetailTextures[sex].push_back(RuntimeFormRef(x));target.raceDefaultFaceDetails[sex]=RuntimeFormRef(face->defaultFaceDetailsTextureSet);target.raceDefaultHairColors[sex]=RuntimeFormRef(face->defaultHairColor);for(std::size_t m=0;m<4;++m)target.raceMorphFlags[sex*4+m]=face->availableMorphs[m].morphFlags;} target.raceAttackRace=value->attackDataMap?RuntimeFormRef(value->attackDataMap->defaultDataRace):DynamicForms::FormRef{}; target.raceAttacks.clear(); if(value->attackDataMap)for(const auto& [event,attack]:value->attackDataMap->attackDataMap)if(attack){DynamicForms::RaceAttack x;x.event=event.c_str();x.damageMult=attack->data.damageMult;x.attackChance=attack->data.attackChance;x.attackSpell=RuntimeFormRef(attack->data.attackSpell);x.flags=attack->data.flags.underlying();x.attackAngle=attack->data.attackAngle;x.strikeAngle=attack->data.strikeAngle;x.staggerOffset=attack->data.staggerOffset;x.attackType=RuntimeFormRef(attack->data.attackType);x.knockDown=attack->data.knockDown;x.recoveryTime=attack->data.recoveryTime;x.staminaMult=attack->data.staminaMult;target.raceAttacks.push_back(std::move(x));} target.spells.clear();if(auto* sd=static_cast<RE::TESSpellList*>(value)->actorEffects)for(std::uint32_t i=0;i<sd->numSpells;++i)if(sd->spells[i])target.spells.push_back(RuntimeFormRef(sd->spells[i]));return true; }
        default: return false;
        }
    }

    std::string EditorIdOrFormId(const RE::TESForm* form) {
        if (!form) {
            return "<null>";
        }
        auto editorId = FormUtil::GetEditorIDSafe(form);
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
                templateNpc->race ? FormUtil::GetEditorIDSafe(templateNpc->race) : "<null>",
                npc->race ? FormUtil::GetEditorIDSafe(npc->race) : "<null>");
        } else if (rawFactoryNpc) {
            logger::warn("Using fallback InitializeData/InitItemImpl for NPC '{}' because the DPF NPC template was not available.", form.editorId);
            npc->InitializeData();
            npc->InitItemImpl();
        }

        npc->SetFormEditorID(form.editorId.c_str());
        npc->fullName = form.fullName.c_str();
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
            npc->race ? FormUtil::GetEditorIDSafe(npc->race) : "<null>",
            npc->npcClass ? FormUtil::GetEditorIDSafe(npc->npcClass) : "<null>",
            npc->voiceType ? FormUtil::GetEditorIDSafe(npc->voiceType) : "<null>",
            npc->farSkin ? FormUtil::GetEditorIDSafe(npc->farSkin) : "<null>",
            npc->defaultOutfit ? FormUtil::GetEditorIDSafe(npc->defaultOutfit) : "<null>",
            npc->sleepOutfit ? FormUtil::GetEditorIDSafe(npc->sleepOutfit) : "<null>",
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

    void ApplyLeveledList(RE::TESLeveledList& target, const DynamicForms::DynamicForm& form) {
        target.ClearDataComponent();
        std::vector<DynamicForms::LeveledEntry> valid;
        valid.reserve(form.leveledEntries.size());
        for (const auto& entry : form.leveledEntries) {
            if (valid.size() >= 255) {
                logger::warn("Leveled list '{}' has more than 255 entries; extra entries are ignored by the record format.", form.editorId);
                break;
            }
            auto* resolved = ResolveConfigForm(entry.form);
            if (!resolved || !target.GetCanContainFormsOfType(resolved->GetFormType())) {
                logger::warn("Leveled list '{}' entry '{}' has an unsupported or unresolved form type.", form.editorId, entry.form.Display());
                continue;
            }
            valid.push_back(entry);
        }
        target.entries.resize(valid.size());
        for (std::size_t i = 0; i < valid.size(); ++i) {
            const auto& source = valid[i]; auto& destination = target.entries[i];
            destination.form = ResolveConfigForm(source.form); destination.level = source.level; destination.count = source.count; destination.itemExtra = nullptr;
            auto* owner = ResolveConfigForm(source.owner); auto* global = ResolveAs<RE::TESGlobal>(source.conditionGlobal);
            if (owner || global || source.requiredRank != 0 || source.healthMult != 100.0F) {
                destination.itemExtra = new RE::ContainerItemExtra(owner);
                if (global) destination.itemExtra->conditional.global = global; else destination.itemExtra->conditional.rank = source.requiredRank;
                destination.itemExtra->healthMult = source.healthMult;
            }
        }
        target.numEntries = static_cast<std::uint8_t>(valid.size());
        target.chanceNone = static_cast<std::int8_t>(std::min(form.leveledChanceNone, static_cast<std::uint8_t>(100)));
        target.llFlags = static_cast<RE::TESLeveledList::Flag>(form.leveledFlags);
        target.chanceGlobal = ResolveAs<RE::TESGlobal>(form.leveledChanceGlobal);
    }

    void AppendDialogueInfo(RE::TESTopic& topic, RE::TESTopicInfo& info) {
        for (std::uint32_t i = 0; i < topic.numTopicInfos; ++i) if (topic.topicInfos[i] == std::addressof(info)) { info.infoIndex = static_cast<std::uint16_t>(i); return; }
        auto** updated = RE::calloc<RE::TESTopicInfo*>(static_cast<std::size_t>(topic.numTopicInfos) + 1);
        for (std::uint32_t i = 0; i < topic.numTopicInfos; ++i) updated[i] = topic.topicInfos[i];
        updated[topic.numTopicInfos] = std::addressof(info); info.infoIndex = static_cast<std::uint16_t>(topic.numTopicInfos);
        topic.topicInfos = updated; ++topic.numTopicInfos;
    }

    void RegisterDialogueTopicWithQuest(RE::TESTopic& topic) {
        if (!topic.ownerQuest) return;
        const auto type = static_cast<std::uint32_t>(topic.data.type.get());
        if (type < RE::DIALOGUE_TYPES::kBranchedTotal) {
            if (!topic.ownerBranch) return;
            auto& registry = topic.ownerQuest->branchedDialogue[type];
            auto found = registry.find(topic.ownerBranch);
            RE::BSTArray<RE::TESTopic*>* topics = nullptr;
            if (found == registry.end()) { topics = new RE::BSTArray<RE::TESTopic*>(); registry.emplace(topic.ownerBranch, topics); } else topics = found->second;
            if (std::ranges::find(*topics, std::addressof(topic)) == topics->end()) topics->push_back(std::addressof(topic));
            return;
        }
        const auto index = type - RE::DIALOGUE_TYPES::kBranchedTotal;
        if (index >= std::size(topic.ownerQuest->topics)) return;
        auto& topics = topic.ownerQuest->topics[index];
        if (std::ranges::find(topics, std::addressof(topic)) == topics.end()) topics.push_back(std::addressof(topic));
    }

    RE::TESTopicInfo::TESResponse* BuildDialogueResponses(const DynamicForms::DynamicForm& form) {
        RE::TESTopicInfo::TESResponse* head = nullptr;
        RE::TESTopicInfo::TESResponse* tail = nullptr;
        for (const auto& source : form.dialogueResponses) {
            auto* response = new RE::TESTopicInfo::TESResponse{};
            response->emotionType = static_cast<RE::TESTopicInfo::TESResponse::EmotionType>(std::min(source.emotionType, 7u));
            response->emotionValue = source.emotionValue; response->responseNumber = source.responseNumber;
            response->sound = ResolveAs<RE::BGSSoundDescriptorForm>(source.sound); response->flags = static_cast<RE::TESTopicInfo::TESResponse::Flag>(source.flags);
            response->responseText = source.text.c_str(); response->speakerIdle = ResolveAs<RE::TESIdleForm>(source.speakerIdle); response->listenerIdle = ResolveAs<RE::TESIdleForm>(source.listenerIdle);
            if (!head) head = response; else tail->next = response; tail = response;
        }
        return head;
    }

    void ApplyQuestData(RE::TESQuest& quest, const DynamicForms::DynamicForm& form) {
        quest.fullName = form.fullName.c_str();
        quest.data.questDelayTime = form.questDelayTime;
        quest.data.flags = static_cast<RE::QuestFlag>(form.questFlags);
        quest.data.priority = form.questPriority;
        quest.data.questType = static_cast<RE::QUEST_DATA::Type>(std::min(form.questType, 11u));
        ApplyConditions(quest.objConditions, form.conditions);
        ApplyConditions(quest.storyManagerConditions, form.questStoryConditions);

        if (!quest.textGlobals) quest.textGlobals = new RE::BSTArray<RE::TESGlobal*>();
        quest.textGlobals->clear();
        for (const auto& ref : form.questTextGlobals) if (auto* global = ResolveAs<RE::TESGlobal>(ref)) quest.textGlobals->push_back(global);

        if (!quest.waitingStages) quest.waitingStages = new RE::BSSimpleList<RE::TESQuestStage*>();
        quest.waitingStages->clear();
        for (const auto& source : form.questStages) { auto* stage = new RE::TESQuestStage(); stage->data.index = source.index; stage->data.flags = static_cast<RE::QUEST_STAGE_DATA::Flag>(source.flags); quest.waitingStages->insert_at(quest.waitingStages->size(), stage); }

        quest.aliases.clear();
        for (const auto& source : form.questAliases) {
            auto* alias = RE::calloc<RE::BGSRefAlias>(1);
            SetRuntimeVTable(alias, RE::VTABLE_BGSRefAlias[0]);
            alias->aliasName = source.name.c_str(); alias->owningQuest = std::addressof(quest); alias->aliasID = source.id;
            alias->flags = static_cast<RE::BGSBaseAlias::FLAGS>(source.flags);
            alias->fillType = static_cast<RE::BGSBaseAlias::FILL_TYPE>(std::min(source.fillType, 7u));
            alias->conditions = new RE::TESCondition(); ApplyConditions(*alias->conditions, source.conditions);
            switch (source.fillType) {
            case 1: if (auto* ref = ResolveAs<RE::TESObjectREFR>(source.forcedReference)) alias->fillData.forced.forcedRef = ref->CreateRefHandle(); break;
            case 2: alias->fillData.fromAlias.forcedFromAlias = source.sourceAliasId; alias->fillData.fromAlias.forcedRefType = ResolveAs<RE::BGSLocationRefType>(source.sourceRefType); break;
            case 5: alias->fillData.fromExternal.externalQuest = ResolveAs<RE::TESQuest>(source.externalQuest); alias->fillData.fromExternal.externalAlias = source.externalAliasId; break;
            case 6: alias->fillData.uniqueActor.uniqueActor = ResolveAs<RE::TESNPC>(source.uniqueActor); break;
            case 7: alias->fillData.nearAlias.nearAlias = source.sourceAliasId; break;
            default: break;
            }
            quest.aliases.push_back(alias);
        }

        quest.objectives.clear();
        for (auto it = form.questObjectives.rbegin(); it != form.questObjectives.rend(); ++it) {
            auto* objective = new RE::BGSQuestObjective();
            objective->displayText = it->text.c_str(); objective->ownerQuest = std::addressof(quest); objective->index = it->index;
            objective->flags = static_cast<RE::QUEST_OBJECTIVE_FLAGS>(it->flags); objective->numTargets = static_cast<std::uint32_t>(it->targets.size());
            objective->targets = objective->numTargets ? RE::calloc<RE::TESQuestTarget*>(objective->numTargets) : nullptr;
            for (std::size_t i = 0; i < it->targets.size(); ++i) { auto* target = new RE::TESQuestTarget(); target->alias = it->targets[i].aliasId; target->flags = static_cast<RE::TESQuestTarget::Flag>(it->targets[i].flags); ApplyConditions(target->conditions, it->targets[i].conditions); objective->targets[i] = target; }
            quest.objectives.push_front(objective);
        }
    }

    void ApplyStoryNodeBase(RE::BGSStoryManagerNodeBase& node, const DynamicForms::DynamicForm& form) {
        node.parent = ResolveAs<RE::BGSStoryManagerBranchNode>(form.storyParent); node.previousSibling = ResolveAs<RE::BGSStoryManagerNodeBase>(form.storyPreviousSibling);
        node.maxQuests = form.storyMaxQuests; node.flags.nodeFlags = static_cast<RE::BGSStoryManagerNodeBase::Flags::NodeFlag>(form.storyNodeFlags); node.flags.questFags = static_cast<RE::BGSStoryManagerNodeBase::Flags::QuestFlag>(form.storyQuestFlags); ApplyConditions(node.conditions, form.conditions);
    }

    const RE::BGSRegisteredStoryEvent* FindStoryEvent(const std::string_view id) {
        const auto* manager = RE::BGSStoryEventManager::GetSingleton(); if (!manager || id.empty()) return nullptr;
        for (const auto& event : manager->registeredEvents) {
            const std::string_view uniqueId(event.uniqueID, 4);
            if (uniqueId == id || std::string_view(event.name.c_str()) == id) return std::addressof(event);
        }
        return nullptr;
    }

    RE::BGSSceneAction* BuildSceneAction(const DynamicForms::SceneAction& source) {
        RE::BGSSceneAction* result = nullptr;
        if (source.type == 0) {
            auto* action = RE::calloc<RE::BGSSceneActionDialogue>(1); SetRuntimeVTable(action, RE::VTABLE_BGSSceneActionDialogue[0]); action->topic = ResolveAs<RE::TESTopic>(source.topic); action->headtrackActorID = source.headtrackActorId; action->loopingMin = source.loopingMin; action->loopingMax = source.loopingMax; action->emotionType = static_cast<RE::EmotionType>(std::min(source.emotionType, 7u)); action->emotionValue = source.emotionValue; result = action;
        } else if (source.type == 1) {
            auto* action = RE::calloc<RE::BGSSceneActionPackage>(1); SetRuntimeVTable(action, RE::VTABLE_BGSSceneActionPackage[0]); std::construct_at(std::addressof(action->packages)); for (const auto& ref : source.packages) if (auto* package = ResolveAs<RE::TESPackage>(ref)) action->packages.push_back(package); result = action;
        } else {
            auto* action = RE::calloc<RE::BGSSceneActionTimer>(1); SetRuntimeVTable(action, RE::VTABLE_BGSSceneActionTimer[0]); action->timerSeconds = source.timerSeconds; result = action;
        }
        result->actorID = source.actorId; result->startPhase = source.startPhase; result->endPhase = source.endPhase; result->flags = static_cast<RE::BGSSceneAction::Flag>(source.flags); result->index = source.index; return result;
    }

    void ApplyPackageEvent(RE::PackageEventAction& target, const DynamicForms::PackageEvent& source) {
        target.idle = ResolveAs<RE::TESIdleForm>(source.idle); target.type = static_cast<RE::PACK_EVENT_ACTION_TYPE>(std::min(source.type, 3u)); target.topic.type = static_cast<RE::PackageEventAction::TopicData::Type>(std::min(source.topicType, 1u)); target.topic.topic = ResolveAs<RE::TESTopic>(source.topic);
    }

    bool ConfigureAdditionalReadyForm(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        using FK = DynamicForms::FormKind;
        switch (form.kind) {
        case FK::ImpactDataSet: {
            auto* value = tesForm->As<RE::BGSImpactDataSet>(); if (!value) return false;
            value->impactMap.clear();
            for (const auto& entry : form.impactDataSetEntries) {
                auto* material = ResolveAs<RE::BGSMaterialType>(entry.key);
                auto* impact = ResolveAs<RE::BGSImpactData>(entry.value);
                if (material && impact) value->impactMap.emplace(material, impact);
            }
            return true;
        }
        case FK::CollisionLayer: {
            auto* value = tesForm->As<RE::BGSCollisionLayer>(); if (!value) return false;
            value->collisionIdx = form.collisionLayerIndex;
            value->debugColor = RE::Color(static_cast<std::uint8_t>((form.collisionLayerColor >> 24) & 0xFF), static_cast<std::uint8_t>((form.collisionLayerColor >> 16) & 0xFF), static_cast<std::uint8_t>((form.collisionLayerColor >> 8) & 0xFF), static_cast<std::uint8_t>(form.collisionLayerColor & 0xFF));
            value->flags = static_cast<RE::BGSCollisionLayer::FLAG>(form.collisionLayerFlags);
            value->name = form.collisionLayerName.c_str(); value->collidesWith.clear();
            for (const auto& ref : form.collisionLayers) if (auto* layer = ResolveAs<RE::BGSCollisionLayer>(ref)) value->collidesWith.push_back(layer);
            return true;
        }
        case FK::Footstep: {
            auto* value = tesForm->As<RE::BGSFootstep>(); if (!value) return false;
            value->tag = form.footstepTag.c_str(); value->impactSet = ResolveAs<RE::BGSImpactDataSet>(form.footstepImpactDataSet); return true;
        }
        case FK::FootstepSet: {
            auto* value = tesForm->As<RE::BGSFootstepSet>(); if (!value) return false;
            for (std::size_t i = 0; i < form.footstepSets.size(); ++i) {
                value->entries[i].clear();
                for (const auto& ref : form.footstepSets[i]) if (auto* step = ResolveAs<RE::BGSFootstep>(ref)) value->entries[i].push_back(step);
            }
            return true;
        }
        case FK::ReverbParameters: {
            auto* value = tesForm->As<RE::BGSReverbParameters>(); if (!value) return false;
            value->data.decayTime = form.reverbDecayTime; value->data.hfReference = form.reverbHFReference;
            auto* bytes = std::addressof(value->data.roomFilter); std::copy(form.reverbValues.begin(), form.reverbValues.end(), bytes); return true;
        }
        case FK::AcousticSpace: {
            auto* value = tesForm->As<RE::BGSAcousticSpace>(); if (!value) return false;
            value->loopingSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.acousticLoopingSound);
            value->soundRegion = ResolveAs<RE::TESRegion>(form.acousticSoundRegion);
            value->reverbType = ResolveAs<RE::BGSReverbParameters>(form.acousticReverb); return true;
        }
        case FK::Apparatus: {
            auto* value = tesForm->As<RE::BGSApparatus>(); if (!value) return false;
            ApplyMiscLikeItem(static_cast<RE::TESObjectMISC&>(*value), form);
            static_cast<RE::TESQualityForm&>(*value).quality = static_cast<RE::TESQualityForm::Quality>(std::min(form.apparatusQuality, 4u)); return true;
        }
        case FK::StaticCollection: {
            auto* value = tesForm->As<RE::BGSStaticCollection>(); if (!value) return false;
            value->SetModel(form.modelPath.c_str()); ApplyRecordFlags(*value, form.recordFlags, 1u << 19); return true;
        }
        case FK::Grass: {
            auto* value = tesForm->As<RE::TESGrass>(); if (!value) return false;
            value->SetModel(form.modelPath.c_str()); value->SetDensity(std::min(form.grassDensity, static_cast<std::uint8_t>(100)));
            value->data.minSlopeDegrees = static_cast<std::int8_t>(std::min(form.grassMinSlope, static_cast<std::uint8_t>(90))); value->data.maxSlopeDegrees = static_cast<std::int8_t>(std::clamp<int>(form.grassMaxSlope, value->data.minSlopeDegrees, 90));
            value->SetDistanceFromWaterLevel(form.grassDistanceFromWater); value->SetUnderwaterState(static_cast<RE::TESGrass::GRASS_WATER_STATE>(std::min(form.grassWaterState, 7u)));
            value->data.positionRange = std::clamp(form.grassPositionRange, 0.0F, 512.0F); value->data.heightRange = std::clamp(form.grassHeightRange, 0.0F, 1.0F);
            value->data.colorRange = std::clamp(form.grassColorRange, 0.0F, 1.0F); value->data.wavePeriod = std::max(form.grassWavePeriod, 0.001F);
            value->data.flags = static_cast<RE::TESGrass::GRASS_DATA::Flag>(form.grassFlags); return true;
        }
        case FK::IdleMarker: {
            auto* value = tesForm->As<RE::BGSIdleMarker>(); if (!value) return false;
            value->SetModel(form.modelPath.c_str()); auto& collection = static_cast<RE::BGSIdleCollection&>(*value); collection.ClearDataComponent();
            collection.idleFlags = static_cast<RE::BGSIdleCollection::IdleFlags>(form.idleFlags); collection.timerCheckForIdle = form.idleTimer;
            for (const auto& ref : form.idleAnimations) if (auto* idle = ResolveAs<RE::TESIdleForm>(ref)) collection.AddIdle(idle);
            ApplyRecordFlags(*value, form.recordFlags, 1u << 29); return true;
        }
        case FK::EncounterZone: {
            auto* value = tesForm->As<RE::BGSEncounterZone>(); if (!value) return false;
            value->data.zoneOwner = ResolveAs<RE::TESFaction>(form.encounterOwner); value->data.location = ResolveAs<RE::BGSLocation>(form.encounterLocation);
            value->data.ownerRank = form.encounterOwnerRank; value->data.minLevel = form.encounterMinLevel; value->data.maxLevel = form.encounterMaxLevel;
            value->data.flags = static_cast<RE::ENCOUNTER_ZONE_DATA::Flag>(form.encounterFlags); return true;
        }
        case FK::Relationship: {
            auto* value = tesForm->As<RE::BGSRelationship>(); if (!value) return false;
            value->npc1 = ResolveAs<RE::TESNPC>(form.relationshipNpc1); value->npc2 = ResolveAs<RE::TESNPC>(form.relationshipNpc2);
            value->assocType = ResolveAs<RE::BGSAssociationType>(form.relationshipAssociation);
            value->level = static_cast<RE::BGSRelationship::RELATIONSHIP_LEVEL>(std::min(form.relationshipLevel, 8u));
            value->flags = static_cast<RE::BGSRelationship::Flag>(form.relationshipFlags); return true;
        }
        case FK::AssociationType: {
            auto* value = tesForm->As<RE::BGSAssociationType>(); if (!value) return false;
            value->associationLabels[0][0] = form.associationLabels[0].c_str(); value->associationLabels[0][1] = form.associationLabels[1].c_str();
            value->associationLabels[1][0] = form.associationLabels[2].c_str(); value->associationLabels[1][1] = form.associationLabels[3].c_str();
            value->flags = static_cast<RE::BGSAssociationType::FLAGS>(form.associationFlags); return true;
        }
        case FK::MovementType: {
            auto* value = tesForm->As<RE::BGSMovementType>(); if (!value) return false;
            value->movementTypeData.typeName = form.movementName.c_str();
            std::copy(form.movementSpeeds.begin(), form.movementSpeeds.end(), std::addressof(value->movementTypeData.defaultData.speeds[0][0]));
            value->movementTypeData.defaultData.rotateWhileMovingRun = form.movementRotateWhileMoving;
            value->movementTypeData.directional = form.movementDirectional; value->movementTypeData.movementSpeed = form.movementSpeed; value->movementTypeData.rotationSpeed = form.movementRotationSpeed; return true;
        }
        case FK::WordOfPower: {
            auto* value = tesForm->As<RE::TESWordOfPower>(); if (!value) return false;
            value->fullName = form.fullName.c_str(); value->translation = form.wordTranslation.c_str(); return true;
        }
        case FK::Water: {
            auto* value = tesForm->As<RE::TESWaterForm>(); if (!value) return false;
            value->fullName = form.fullName.c_str();
            for (std::size_t i = 0; i < form.waterNoiseTextures.size(); ++i) value->noiseTextures[i].textureName = form.waterNoiseTextures[i].c_str();
            value->alpha = static_cast<std::int8_t>(form.waterAlpha); value->flags = static_cast<RE::TESWaterForm::Flag>(form.waterFlags);
            value->materialType = ResolveAs<RE::BGSMaterialType>(form.waterMaterial); value->waterSound = ResolveAs<RE::BGSSoundDescriptorForm>(form.waterSound);
            value->contactSpell = ResolveAs<RE::SpellItem>(form.waterContactSpell); value->imageSpace = ResolveAs<RE::TESImageSpace>(form.waterImageSpace);
            std::copy(form.waterLinearVelocity.begin(), form.waterLinearVelocity.end(), std::addressof(value->linearVelocity.x));
            std::copy(form.waterAngularVelocity.begin(), form.waterAngularVelocity.end(), std::addressof(value->angularVelocity.x)); return true;
        }
        case FK::ImageSpace: {
            auto* value = tesForm->As<RE::TESImageSpace>(); if (!value) return false;
            std::copy(form.imageSpaceHDR.begin(), form.imageSpaceHDR.end(), std::addressof(value->data.hdr.eyeAdaptSpeed));
            std::copy(form.imageSpaceCinematic.begin(), form.imageSpaceCinematic.end(), std::addressof(value->data.cinematic.saturation));
            value->data.tint.amount = form.imageSpaceTintAmount; std::copy(form.imageSpaceTintColor.begin(), form.imageSpaceTintColor.end(), std::addressof(value->data.tint.color.red));
            std::copy(form.imageSpaceDOF.begin(), form.imageSpaceDOF.end(), std::addressof(value->data.depthOfField.strength));
            value->data.depthOfField.flags = form.imageSpaceDOFFlags; value->data.depthOfField.skyBlurRadius = static_cast<RE::ImageSpaceBaseData::DepthOfField::SkyBlurRadius>(form.imageSpaceSkyBlur); return true;
        }
        case FK::LightingTemplate: {
            auto* value = tesForm->As<RE::BGSLightingTemplate>(); if (!value) return false;
            RE::Color* colors[]{ &value->data.ambient, &value->data.directional, &value->data.fogColorNear, &value->data.fogColorFar };
            for (std::size_t i = 0; i < 4; ++i) *colors[i] = RE::Color(form.lightingColors[i]);
            value->data.fogNear = form.lightingValues[0]; value->data.fogFar = form.lightingValues[1]; value->data.directionalFade = form.lightingValues[2]; value->data.clipDist = form.lightingValues[3];
            value->data.fogPower = form.lightingValues[4]; value->data.fogClamp = form.lightingValues[5]; value->data.lightFadeStart = form.lightingValues[6]; value->data.lightFadeEnd = form.lightingValues[7];
            value->data.directionalXY = form.lightingDirectionalXY; value->data.directionalZ = form.lightingDirectionalZ;
            value->data.lightingTemplateInheritanceFlags = static_cast<RE::INTERIOR_DATA::Inherit>(form.lightingInheritanceFlags); return true;
        }
        case FK::Shout: {
            auto* value = tesForm->As<RE::TESShout>(); if (!value) return false;
            value->fullName = form.fullName.c_str();
            static_cast<RE::BGSMenuDisplayObject&>(*value).menuDispObject = ResolveAs<RE::TESBoundObject>(form.menuDisplayObject);
            static_cast<RE::BGSEquipType&>(*value).equipSlot = ResolveAs<RE::BGSEquipSlot>(form.equipSlot);
            for (std::size_t i = 0; i < form.shoutWords.size(); ++i) {
                value->variations[i].word = ResolveAs<RE::TESWordOfPower>(form.shoutWords[i]);
                value->variations[i].spell = ResolveAs<RE::SpellItem>(form.shoutSpells[i]); value->variations[i].recoveryTime = form.shoutRecoveryTimes[i];
            }
            ApplyRecordFlags(*value, form.recordFlags, 1u << 7);
            if (!form.description.empty()) logger::debug("Shout '{}' description persisted but not assigned to TESDescription.", form.editorId);
            return true;
        }
        case FK::LeveledItem: { auto* value = tesForm->As<RE::TESLevItem>(); if (!value) return false; ApplyLeveledList(static_cast<RE::TESLeveledList&>(*value), form); return true; }
        case FK::LeveledNPC: { auto* value = tesForm->As<RE::TESLevCharacter>(); if (!value) return false; value->SetModel(form.modelPath.c_str()); ApplyLeveledList(static_cast<RE::TESLeveledList&>(*value), form); return true; }
        case FK::LeveledSpell: { auto* value = tesForm->As<RE::TESLevSpell>(); if (!value) return false; ApplyLeveledList(static_cast<RE::TESLeveledList&>(*value), form); return true; }
        case FK::LocationRefType: return tesForm->As<RE::BGSLocationRefType>() != nullptr;
        case FK::Action: { auto* value = tesForm->As<RE::BGSAction>(); if (!value) return false; value->index = form.actionIndex; return true; }
        case FK::MenuIcon: { auto* value = tesForm->As<RE::BGSMenuIcon>(); if (!value) return false; static_cast<RE::TESIcon&>(*value).textureName = form.inventoryIcon.c_str(); return true; }
        case FK::Eyes: {
            auto* value = tesForm->As<RE::TESEyes>(); if (!value) return false;
            value->fullName = form.fullName.c_str(); static_cast<RE::TESTexture&>(*value).textureName = form.eyesTexture.c_str(); value->flags = static_cast<RE::TESEyes::Flag>(form.eyesFlags); ApplyRecordFlags(*value, form.recordFlags, 1u << 2); return true;
        }
        case FK::Note: {
            auto* value = tesForm->As<RE::BGSNote>(); if (!value) return false;
            value->SetModel(form.modelPath.c_str()); value->fullName = form.fullName.c_str(); static_cast<RE::TESIcon&>(*value).textureName = form.inventoryIcon.c_str(); ApplyPickupPutdownSounds(static_cast<RE::BGSPickupPutdownSounds&>(*value), form); return true;
        }
        case FK::AnimatedObject: {
            auto* value = tesForm->As<RE::TESObjectANIO>(); if (!value) return false; value->SetModel(form.modelPath.c_str()); value->unloadEventName = form.animatedUnloadEvent.c_str(); return true;
        }
        case FK::LoadScreen: {
            auto* value = tesForm->As<RE::TESLoadScreen>(); if (!value) return false;
            ApplyConditions(value->conditions, form.conditions); value->loadingText = form.loadScreenText.c_str();
            if (!value->loadNIFData) {
                value->loadNIFData = RE::calloc<RE::TESLoadScreen::LoadNIFData>(1);
                SetRuntimeVTable(std::addressof(value->loadNIFData->cameraPath), RE::VTABLE_TESModel[0]);
            }
            auto& data = *value->loadNIFData; data.loadNIF = ResolveAs<RE::TESBoundObject>(form.loadScreenObject); data.initialScale = form.loadScreenInitialScale;
            std::copy(form.loadScreenRotationConstraints.begin(), form.loadScreenRotationConstraints.end(), data.rotationConstraints); std::copy(form.loadScreenRotationOffsetConstraints.begin(), form.loadScreenRotationOffsetConstraints.end(), data.rotationOffsetConstraints); std::copy(form.loadScreenTranslationOffset.begin(), form.loadScreenTranslationOffset.end(), data.initialTranslationOffset); data.cameraPath.model = form.loadScreenCameraPath.c_str();
            ApplyRecordFlags(*value, form.recordFlags, 1u << 10); return true;
        }
        case FK::ShaderParticleGeometry: {
            auto* value = tesForm->As<RE::BGSShaderParticleGeometryData>(); if (!value) return false;
            if (REL::Module::IsVR()) value->GetVRRuntimeData().data.resize(static_cast<std::size_t>(RE::BGSShaderParticleGeometryData::DataID::kTotal)); else value->GetRuntimeData().data.resize(static_cast<std::size_t>(RE::BGSShaderParticleGeometryData::DataID::kTotal));
            for (std::size_t i = 0; i < form.shaderParticleSettings.size(); ++i) {
                auto& setting = value->GetSettingRef(static_cast<RE::BGSShaderParticleGeometryData::DataID>(i));
                if (i == 7 || i == 8 || i == 9) setting.i = static_cast<std::uint32_t>(std::max(0.0F, std::round(form.shaderParticleSettings[i]))); else setting.f = form.shaderParticleSettings[i];
            }
            if (REL::Module::IsVR()) value->GetVRRuntimeData().particleTexture.textureName = form.shaderParticleTexture.c_str(); else value->GetRuntimeData().particleTexture.textureName = form.shaderParticleTexture.c_str(); return true;
        }
        case FK::AddonNode: {
            auto* value = tesForm->As<RE::BGSAddonNode>(); if (!value) return false; value->SetModel(form.modelPath.c_str()); value->index = form.addonIndex; value->sound = ResolveAs<RE::BGSSoundDescriptorForm>(form.addonSound); value->data.masterParticleCap = form.addonMasterParticleCap; value->data.flags = static_cast<RE::ADDON_DATA::Flag>(form.addonFlags); return true;
        }
        case FK::Faction: {
            auto* value = tesForm->As<RE::TESFaction>(); if (!value) return false;
            value->fullName = form.fullName.c_str();
            value->data.flags = static_cast<RE::FACTION_DATA::Flag>(form.factionFlags);
            value->groupFormType = RE::FormType::Faction;
            value->reactions.clear();
            for (const auto& source : form.factionReactions) {
                auto* faction = ResolveAs<RE::TESFaction>(source.faction); if (!faction) continue;
                auto* reaction = new RE::GROUP_REACTION{ faction, source.reaction, static_cast<RE::FIGHT_REACTION>(std::min(source.fightReaction, 3u)) };
                value->reactions.insert_at(value->reactions.size(), reaction);
            }
            value->rankData.clear();
            for (const auto& source : form.factionRanks) {
                auto* rank = RE::calloc<RE::RANK_DATA>(1);
                SetRuntimeVTable(std::addressof(rank->textureInsignia), RE::VTABLE_TESTexture[0]);
                rank->maleRankTitle = source.maleTitle.c_str(); rank->femaleRankTitle = source.femaleTitle.c_str(); rank->textureInsignia.textureName = source.insigniaPath.c_str();
                value->rankData.insert_at(value->rankData.size(), rank);
            }
            auto& crime = value->crimeData;
            crime.factionJailMarker = ResolveAs<RE::TESObjectREFR>(form.factionJailMarker); crime.factionWaitMarker = ResolveAs<RE::TESObjectREFR>(form.factionWaitMarker);
            crime.factionStolenContainer = ResolveAs<RE::TESObjectREFR>(form.factionStolenContainer); crime.factionPlayerInventoryContainer = ResolveAs<RE::TESObjectREFR>(form.factionPlayerInventoryContainer);
            crime.crimeGroup = ResolveAs<RE::BGSListForm>(form.factionCrimeGroup); crime.jailOutfit = ResolveAs<RE::BGSOutfit>(form.factionJailOutfit);
            crime.crimevalues.arrest = form.factionArrest; crime.crimevalues.attackOnSight = form.factionAttackOnSight;
            crime.crimevalues.murderCrimeGold = form.factionMurderCrimeGold; crime.crimevalues.assaultCrimeGold = form.factionAssaultCrimeGold; crime.crimevalues.trespassCrimeGold = form.factionTrespassCrimeGold; crime.crimevalues.pickpocketCrimeGold = form.factionPickpocketCrimeGold;
            crime.crimevalues.stealCrimeGoldMult = form.factionStealCrimeGoldMult; crime.crimevalues.escapeCrimeGold = form.factionEscapeCrimeGold; crime.crimevalues.werewolfCrimeGold = form.factionWerewolfCrimeGold;
            auto& vendor = value->vendorData;
            vendor.vendorValues.startHour = form.factionVendorStartHour; vendor.vendorValues.endHour = form.factionVendorEndHour; vendor.vendorValues.locationRadius = form.factionVendorRadius;
            vendor.vendorValues.buysStolen = form.factionVendorBuysStolen; vendor.vendorValues.notBuySell = form.factionVendorNotBuySell; vendor.vendorValues.buysNonStolen = form.factionVendorBuysNonStolen;
            vendor.vendorSellBuyList = ResolveAs<RE::BGSListForm>(form.factionVendorSellBuyList); vendor.merchantContainer = ResolveAs<RE::TESObjectREFR>(form.factionMerchantContainer);
            if (!form.factionVendorConditions.empty()) { if (!vendor.vendorConditions) vendor.vendorConditions = new RE::TESCondition(); ApplyConditions(*vendor.vendorConditions, form.factionVendorConditions); }
            return true;
        }
        case FK::IdleAnimation: {
            auto* value = tesForm->As<RE::TESIdleForm>(); if (!value) return false;
            ApplyConditions(value->conditions, form.conditions);
            value->data.loopMin = form.idleLoopMin; value->data.loopMax = form.idleLoopMax;
            value->data.flags = static_cast<RE::IDLE_DATA::Flag>(form.idleAnimationFlags); value->data.animationGroupSelection = form.idleAnimationGroupSelection; value->data.replayDelay = form.idleReplayDelay;
            value->parentIdle = ResolveAs<RE::TESIdleForm>(form.idleParent); value->prevIdle = ResolveAs<RE::TESIdleForm>(form.idlePrevious);
            value->animFileName = form.idleAnimationFile.c_str(); value->animEventName = form.idleAnimationEvent.c_str();
            return true;
        }
        case FK::MaterialObject: {
            auto* value = tesForm->As<RE::BGSMaterialObject>(); if (!value) return false; value->SetModel(form.modelPath.c_str()); auto& data = value->directionalData;
            data.falloffScale = form.materialDirectionalData[0]; data.falloffBias = form.materialDirectionalData[1]; data.noiseUVScale = form.materialDirectionalData[2]; data.materialUVScale = form.materialDirectionalData[3]; data.ProjectionDir = { form.materialDirectionalData[4], form.materialDirectionalData[5], form.materialDirectionalData[6] }; data.normalDampener = form.materialDirectionalData[7]; data.singlePassColor = { form.materialDirectionalData[8], form.materialDirectionalData[9], form.materialDirectionalData[10] }; data.singlePass = form.materialSinglePass; data.flags = static_cast<RE::BSMaterialObject::DIRECTIONAL_DATA::Flag>(form.materialObjectFlags); return true;
        }
        case FK::Message: {
            auto* value = tesForm->As<RE::BGSMessage>(); if (!value) return false; value->fullName = form.fullName.c_str(); value->icon = ResolveAs<RE::BGSMenuIcon>(form.messageMenuIcon); value->ownerQuest = ResolveAs<RE::TESQuest>(form.messageOwnerQuest); value->flags = static_cast<RE::BGSMessage::MessageFlag>(form.messageFlags); value->displayTime = form.messageDisplayTime; value->menuButtons.clear();
            for (const auto& source : form.messageButtons) { auto* button = new RE::BGSMessage::MESSAGEBOX_BUTTON(); button->text = source.text.c_str(); ApplyConditions(button->conditions, source.conditions); value->menuButtons.insert_at(value->menuButtons.size(), button); }
            if (!form.description.empty()) logger::debug("Message '{}' description persisted but not assigned to TESDescription.", form.editorId); return true;
        }
        case FK::LandTexture: {
            auto* value = tesForm->As<RE::TESLandTexture>(); if (!value) return false; value->textureSet = ResolveAs<RE::BGSTextureSet>(form.landTextureSet); value->havokData.friction = form.landFriction; value->havokData.restitution = form.landRestitution; value->materialType = ResolveAs<RE::BGSMaterialType>(form.landMaterialType); value->specularExponent = form.landSpecularExponent; value->shaderTextureIndex = form.landShaderTextureIndex; value->textureGrassList.clear(); for (const auto& ref : form.landGrasses) if (auto* grass = ResolveAs<RE::TESGrass>(ref)) value->textureGrassList.insert_at(value->textureGrassList.size(), grass); return true;
        }
        case FK::SoundOutputModel: {
            auto* value = tesForm->As<RE::BGSSoundOutput>(); if (!value) return false; value->type = static_cast<RE::BGSSoundOutput::Type>(std::min(form.soundOutputType, 1u)); value->data.flags = static_cast<RE::BGSSoundOutput::Data::Flag>(form.soundOutputFlags); value->data.reverbSendPct = form.soundOutputReverbSend;
            if (!value->attenuation) { value->attenuation = RE::calloc<RE::BGSSoundOutput::DynamicAttenuationCharacteristics>(1); SetRuntimeVTable(value->attenuation, RE::VTABLE_BGSSoundOutput__DynamicAttenuationCharacteristics[0]); }
            value->attenuation->data.minDistance = form.soundOutputMinDistance; value->attenuation->data.maxDistance = form.soundOutputMaxDistance; std::copy(form.soundOutputCurve.begin(), form.soundOutputCurve.end(), value->attenuation->data.curve);
            if (!value->speakerOutputs) value->speakerOutputs = RE::calloc<RE::BGSSoundOutput::SpeakerArrays>(1); std::copy(form.soundOutputSpeakers.begin(), form.soundOutputSpeakers.end(), std::addressof(value->speakerOutputs->channels[0].l)); return true;
        }
        case FK::LensFlare: { auto* value = tesForm->As<RE::BGSLensFlare>(); if (!value) return false; value->fadeDistRadiusScale = form.lensFlareFadeDistanceRadiusScale; value->colorInfluence = form.lensFlareColorInfluence; return true; }
        case FK::Debris: {
            auto* value = tesForm->As<RE::BGSDebris>(); if (!value) return false; value->data.clear(); for (const auto& source : form.debrisEntries) { auto* entry = RE::calloc<RE::BGSDebrisData>(1); entry->percentage = source.percentage; entry->flags = static_cast<RE::BGSDebrisData::BGSDebrisDataFlags>(source.flags); if (!source.modelPath.empty()) { auto* path = RE::malloc<char>(source.modelPath.size() + 1); std::memcpy(path, source.modelPath.c_str(), source.modelPath.size() + 1); entry->fileName = path; } value->data.insert_at(value->data.size(), entry); } return true;
        }
        case FK::ImageSpaceModifier: {
            auto* value = tesForm->As<RE::TESImageSpaceModifier>(); if (!value) return false; auto& data = value->data; data.animatable = form.imageModifierAnimatable; data.duration = form.imageModifierDuration;
            std::copy(form.imageModifierHDR.begin(), form.imageModifierHDR.end(), std::addressof(data.hdr.eyeAdaptSpeed.mult)); std::copy(form.imageModifierCinematic.begin(), form.imageModifierCinematic.end(), std::addressof(data.cinematic.saturation.mult)); data.tintColor = form.imageModifierTintColor; data.blurRadius = form.imageModifierBlurRadius; data.doubleVisionStrength = form.imageModifierDoubleVisionStrength; data.radialBlurStrength = form.imageModifierRadialBlurStrength; data.radialBlurRampUp = form.imageModifierRadialBlurRampUp; data.radialBlurStart = form.imageModifierRadialBlurStart; data.useTargetForRadialBlur = form.imageModifierUseTargetForRadialBlur; data.radialBlurCenter = { form.imageModifierRadialBlurCenter[0], form.imageModifierRadialBlurCenter[1] }; data.dof.strength = form.imageModifierDofStrength; data.dof.distance = form.imageModifierDofDistance; data.dof.range = form.imageModifierDofRange; data.dof.useTarget = form.imageModifierDofUseTarget; data.dof.flags = static_cast<RE::ImageSpaceModifierData::DOF::Mode>(form.imageModifierDofFlags); data.radialBlurRampDown = form.imageModifierRadialBlurRampDown; data.radialBlurDownStart = form.imageModifierRadialBlurDownStart; data.fadeColor = form.imageModifierFadeColor; data.motionBlurStrength = form.imageModifierMotionBlurStrength; return true;
        }
        case FK::CameraShot: {
            auto* value = tesForm->As<RE::BGSCameraShot>(); if (!value) return false; value->SetModel(form.modelPath.c_str()); static_cast<RE::TESImageSpaceModifiableForm&>(*value).imageSpaceModifying = ResolveAs<RE::TESImageSpaceModifier>(form.cameraImageSpaceModifier); value->data.cameraAction = static_cast<RE::BGSCameraShot::CAM_ACTION>(std::min(form.cameraAction, 3u)); value->data.location = static_cast<RE::BGSCameraShot::CAM_OBJECT>(std::min(form.cameraLocation, 3u)); value->data.target = static_cast<RE::BGSCameraShot::CAM_OBJECT>(std::min(form.cameraTarget, 3u)); value->data.flags = static_cast<RE::BGSCameraShot::CAMERA_SHOT_DATA::Flag>(form.cameraFlags); std::copy(form.cameraTiming.begin(), form.cameraTiming.end(), std::addressof(value->data.playerTimeMult)); return true;
        }
        case FK::CameraPath: {
            auto* value = tesForm->As<RE::BGSCameraPath>(); if (!value) return false; ApplyConditions(value->conditions, form.conditions); value->shots.clear(); for (const auto& ref : form.cameraPathShots) if (auto* shot = ResolveAs<RE::BGSCameraShot>(ref)) value->shots.insert_at(value->shots.size(), shot); value->data.flags = static_cast<RE::PATH_DATA::PathFlags>(form.cameraPathFlags); value->parentPath = ResolveAs<RE::BGSCameraPath>(form.cameraPathParent); value->prevPath = ResolveAs<RE::BGSCameraPath>(form.cameraPathPrevious); return true;
        }
        case FK::TalkingActivator: {
            if (!ConfigureActivator(tesForm, form)) return false;
            auto* value = tesForm->As<RE::BGSTalkingActivator>(); if (!value) return false;
            value->voiceType = ResolveAs<RE::BGSVoiceType>(form.talkingVoiceType); return true;
        }
        case FK::Furniture: {
            if (!ConfigureActivator(tesForm, form)) return false;
            auto* value = tesForm->As<RE::TESFurniture>(); if (!value) return false;
            value->furnFlags = static_cast<RE::TESFurniture::ActiveMarker>(form.furnitureFlags);
            value->workBenchData.benchType = static_cast<RE::TESFurniture::WorkBenchData::BenchType>(std::min(form.furnitureWorkbenchType, 7u));
            value->workBenchData.usesSkill = static_cast<RE::ActorValue>(form.furnitureWorkbenchSkill);
            value->associatedForm = ResolveAs<RE::SpellItem>(form.furnitureAssociatedSpell); return true;
        }
        case FK::Weather: {
            auto* value = tesForm->As<RE::TESWeather>(); if (!value) return false;
            value->data.flags = static_cast<RE::TESWeather::WeatherDataFlag>(form.weatherFlags);
            value->data.windSpeed = form.weatherWindSpeed; value->data.transDelta = form.weatherTransitionDelta;
            value->data.sunGlare = form.weatherSunGlare; value->data.sunDamage = form.weatherSunDamage;
            std::copy(form.weatherFogData.begin(), form.weatherFogData.end(), std::addressof(value->fogData.dayNear));
            value->precipitationData = ResolveAs<RE::BGSShaderParticleGeometryData>(form.weatherPrecipitation);
            value->referenceEffect = ResolveAs<RE::BGSReferenceEffect>(form.weatherReferenceEffect);
            value->sunGlareLensFlare = ResolveAs<RE::BGSLensFlare>(form.weatherLensFlare);
            for (std::size_t i = 0; i < 4; ++i) { value->imageSpaces[i] = ResolveAs<RE::TESImageSpace>(form.weatherImageSpaces[i]); value->volumetricLighting[i] = ResolveAs<RE::BGSVolumetricLighting>(form.weatherVolumetricLighting[i]); }
            return true;
        }
        case FK::Climate: {
            auto* value = tesForm->As<RE::TESClimate>(); if (!value) return false;
            value->nightSky.SetModel(form.climateNightSkyModel.c_str()); value->skyObjects[0].textureName = form.climateSunTexture.c_str(); value->skyObjects[1].textureName = form.climateSunGlareTexture.c_str();
            value->weatherList.clear();
            for (auto it = form.climateWeatherEntries.rbegin(); it != form.climateWeatherEntries.rend(); ++it) { auto* weather = ResolveAs<RE::TESWeather>(it->weather); if (!weather) continue; auto* entry = new RE::WeatherType{ weather, it->chance, 0, ResolveAs<RE::TESGlobal>(it->global) }; value->weatherList.push_front(entry); }
            value->timing.sunrise.begin = form.climateTimes[0]; value->timing.sunrise.end = form.climateTimes[1]; value->timing.sunset.begin = form.climateTimes[2]; value->timing.sunset.end = form.climateTimes[3]; value->timing.volatility = form.climateVolatility; value->timing.moonPhaseLength = static_cast<RE::TESClimate::Timing::MoonPhaseLength>(form.climateMoonPhaseLength); return true;
        }
        case FK::Location: {
            auto* value = tesForm->As<RE::BGSLocation>(); if (!value) return false;
            value->fullName = form.fullName.c_str(); ApplyKeywords(static_cast<RE::BGSKeywordForm&>(*value), form.keywords);
            value->parentLoc = ResolveAs<RE::BGSLocation>(form.locationParent); value->unreportedCrimeFaction = ResolveAs<RE::TESFaction>(form.locationCrimeFaction); value->musicType = ResolveAs<RE::BGSMusicType>(form.locationMusicType); value->worldLocRadius = form.locationWorldRadius; return true;
        }
        case FK::MusicType: {
            auto* value = tesForm->As<RE::BGSMusicType>(); if (!value) return false;
            value->flags = static_cast<RE::BSIMusicType::MST>(form.musicTypeFlags); value->priority = form.musicTypePriority; value->ducksOtherMusicBy = form.musicTypeDucking; value->fadeTime = form.musicTypeFadeTime; value->tracks.clear();
            for (const auto& ref : form.musicTypeTracks) if (auto* track = ResolveAs<RE::BGSMusicTrackFormWrapper>(ref); track && track->track) value->tracks.push_back(track->track); return true;
        }
        case FK::MusicTrack: {
            auto* value = tesForm->As<RE::BGSMusicTrackFormWrapper>(); if (!value) return false;
            auto* track = RE::calloc<RE::BGSMusicSingleTrack>(1); SetRuntimeVTable(track, RE::VTABLE_BGSMusicSingleTrack[0]);
            track->trackID.GenerateFromPath(form.musicTrackPath.c_str()); if (!form.musicTrackFinalePath.empty()) track->finaleID.GenerateFromPath(form.musicTrackFinalePath.c_str()); const auto cueCount = static_cast<RE::BSTArray<float>::size_type>(form.musicTrackCuePoints.size()); track->cuePoints.resize(cueCount); for (RE::BSTArray<float>::size_type i = 0; i < cueCount; ++i) track->cuePoints[i] = form.musicTrackCuePoints[i]; ApplyConditions(track->conditions, form.conditions);
            if (form.musicTrackLoopCount != 0 || form.musicTrackLoopEnd > form.musicTrackLoopBegin) { track->loopData = RE::calloc<RE::BGSMusicSingleTrack::LoopData>(1); track->loopData->loopBegin = form.musicTrackLoopBegin; track->loopData->loopEnd = form.musicTrackLoopEnd; track->loopData->loopCount = form.musicTrackLoopCount; }
            value->track = track; return true;
        }
        case FK::BodyPartData: { auto* value = tesForm->As<RE::BGSBodyPartData>(); if (!value) return false; value->SetModel(form.modelPath.c_str()); value->ragdoll = ResolveAs<RE::BGSRagdoll>(form.bodyPartRagdoll); return true; }
        case FK::VolumetricLighting: {
            auto* value = tesForm->As<RE::BGSVolumetricLighting>(); if (!value) return false; value->intensity = form.volumetricLightingData[0]; value->customColor.contribution = form.volumetricLightingData[1]; value->color = { form.volumetricLightingData[2], form.volumetricLightingData[3], form.volumetricLightingData[4] }; value->density = { form.volumetricLightingData[5], form.volumetricLightingData[6], form.volumetricLightingData[7], form.volumetricLightingData[8] }; value->phaseFunction.contribution = form.volumetricLightingData[9]; return true;
        }
        case FK::Sound: { auto* value = tesForm->As<RE::TESSound>(); if (!value) return false; value->descriptor = ResolveAs<RE::BGSSoundDescriptorForm>(form.legacySoundDescriptor); return true; }
        case FK::ActorValueInfo: {
            auto* value = tesForm->As<RE::ActorValueInfo>(); if (!value) return false;
            const auto copyString = [](const std::string& source) -> const char* { if (source.empty()) return nullptr; auto* result = RE::malloc<char>(source.size() + 1); std::memcpy(result, source.c_str(), source.size() + 1); return result; };
            value->fullName = form.fullName.c_str(); static_cast<RE::TESIcon&>(*value).textureName = form.inventoryIcon.c_str(); value->abbreviation = form.actorValueAbbreviation.c_str(); value->enumName = copyString(form.actorValueEnumName); value->flags = static_cast<RE::ActorValueInfo::ActorValueFlag>(form.actorValueFlags); value->type = static_cast<RE::ActorValueInfo::ActorValueType>(std::min(form.actorValueType, 6u));
            value->enumValueCount = std::min<std::size_t>(form.actorValueEnumValues.size(), 10); for (std::size_t i = 0; i < value->enumValueCount; ++i) value->enumValues[i] = copyString(form.actorValueEnumValues[i]);
            if (form.actorValueHasSkillData) { if (!value->skill) value->skill = RE::calloc<RE::ActorValueInfo::Skill>(1); std::copy(form.actorValueSkillData.begin(), form.actorValueSkillData.end(), std::addressof(value->skill->useMult)); }
            if (!form.description.empty()) logger::debug("Actor value info '{}' description persisted but not assigned to TESDescription.", form.editorId); return true;
        }
        case FK::DialogueBranch: {
            auto* value = tesForm->As<RE::BGSDialogueBranch>(); if (!value) return false;
            value->flags = static_cast<RE::BGSDialogueBranch::Flag>(form.dialogueBranchFlags); value->quest = ResolveAs<RE::TESQuest>(form.dialogueBranchQuest); value->startingTopic = ResolveAs<RE::TESTopic>(form.dialogueBranchStartingTopic); value->type = static_cast<RE::DIALOGUE_TYPE>(std::min(form.dialogueBranchType, 7u)); return true;
        }
        case FK::DialogueTopic: {
            auto* value = tesForm->As<RE::TESTopic>(); if (!value) return false;
            value->fullName = form.fullName.c_str(); value->data.topicFlags = static_cast<RE::DIALOGUE_DATA::TopicFlag>(form.dialogueTopicFlags); value->data.type = static_cast<RE::DIALOGUE_TYPE>(std::min(form.dialogueTopicType, 7u)); value->data.subtype = static_cast<RE::DIALOGUE_DATA::Subtype>(std::min(form.dialogueTopicSubtype, 102u)); value->priorityAndJournalIndex = (static_cast<std::uint32_t>(form.dialogueTopicPriority) << 24) | (form.dialogueTopicJournalIndex & 0x00FFFFFFu); value->ownerBranch = ResolveAs<RE::BGSDialogueBranch>(form.dialogueTopicBranch); value->ownerQuest = ResolveAs<RE::TESQuest>(form.dialogueTopicQuest);
            if (!form.dialogueTopicInfos.empty()) { std::vector<RE::TESTopicInfo*> valid; for (const auto& ref : form.dialogueTopicInfos) if (auto* info = ResolveAs<RE::TESTopicInfo>(ref)) valid.push_back(info); value->topicInfos = RE::calloc<RE::TESTopicInfo*>(valid.size()); value->numTopicInfos = static_cast<std::uint32_t>(valid.size()); for (std::size_t i = 0; i < valid.size(); ++i) { value->topicInfos[i] = valid[i]; valid[i]->parentTopic = value; valid[i]->infoIndex = static_cast<std::uint16_t>(i); } }
            RegisterDialogueTopicWithQuest(*value); return true;
        }
        case FK::DialogueInfo: {
            auto* value = tesForm->As<RE::TESTopicInfo>(); if (!value) return false;
            if (!responseListHookInstalled.load(std::memory_order_acquire)) {
                logger::error("Dialogue info '{}' was not applied because the TESTopicInfo::GetResponseList hook is unavailable.", form.editorId);
                return false;
            }
            value->parentTopic = ResolveAs<RE::TESTopic>(form.dialogueInfoTopic); value->dataInfo = ResolveAs<RE::TESTopicInfo>(form.dialogueInfoSharedInfo); ApplyConditions(value->objConditions, form.conditions); value->infoIndex = form.dialogueInfoIndex; value->favorLevel = static_cast<RE::TESTopicInfo::FavorLevel>(std::min(form.dialogueInfoFavorLevel, 3u)); value->data.flags = static_cast<RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS>(form.dialogueInfoFlags); value->data.timeUntilReset = form.dialogueInfoResetHours; dynamicDialogueResponses[value] = BuildDialogueResponses(form); if (value->parentTopic) AppendDialogueInfo(*value->parentTopic, *value); return true;
        }
        case FK::Quest: { auto* value = tesForm->As<RE::TESQuest>(); if (!value) return false; ApplyQuestData(*value, form); return true; }
        case FK::Scene: {
            auto* value = tesForm->As<RE::BGSScene>(); if (!value) return false; value->parentQuest = ResolveAs<RE::TESQuest>(form.sceneParentQuest); value->flags = static_cast<RE::BGSScene::Flag>(form.sceneFlags); ApplyConditions(value->conditions, form.conditions);
            value->actors.clear(); value->actorFlags.clear(); value->actorProgressionFlags.clear(); for (std::size_t i = 0; i < form.sceneActors.size(); ++i) { value->actors.push_back(form.sceneActors[i]); value->actorFlags.push_back(static_cast<RE::SCENE_ACTOR_FLAG>(i < form.sceneActorFlags.size() ? form.sceneActorFlags[i] : 0)); value->actorProgressionFlags.push_back(static_cast<RE::BGSScene::BehaviourFlag>(i < form.sceneActorBehaviorFlags.size() ? form.sceneActorBehaviorFlags[i] : 0)); }
            value->phases.clear(); for (const auto& source : form.scenePhases) { auto* phase = new RE::BGSScenePhase(); ApplyConditions(phase->startConditions, source.startConditions); ApplyConditions(phase->completionConditions, source.completionConditions); phase->questNode = ResolveAs<RE::BGSStoryManagerQuestNode>(source.questNode); value->phases.push_back(phase); }
            value->actions.clear(); for (const auto& source : form.sceneActions) value->actions.push_back(BuildSceneAction(source)); if (value->parentQuest && std::ranges::find(value->parentQuest->scenes, value) == value->parentQuest->scenes.end()) value->parentQuest->scenes.push_back(value); return true;
        }
        case FK::StoryManagerBranchNode: case FK::StoryManagerEventNode: {
            auto* value = tesForm->As<RE::BGSStoryManagerBranchNode>(); if (!value) return false; ApplyStoryNodeBase(*value, form); value->children.clear(); for (const auto& ref : form.storyChildren) if (auto* child = ResolveAs<RE::BGSStoryManagerNodeBase>(ref)) { value->children.push_back(child); child->parent = value; } if (form.kind == FK::StoryManagerEventNode) if (auto* eventNode = tesForm->As<RE::BGSStoryManagerEventNode>()) eventNode->event = FindStoryEvent(form.storyEventId); return true;
        }
        case FK::StoryManagerQuestNode: {
            auto* value = tesForm->As<RE::BGSStoryManagerQuestNode>();
            if (!value) return false;
            ApplyStoryNodeBase(*value, form);
            value->quests.clear();
            value->perQuestFlags.clear();
            value->perQuestHoursUntilReset.clear();
            for (const auto& source : form.storyQuests)
                if (auto* quest = ResolveAs<RE::TESQuest>(source.quest)) {
                    value->quests.push_back(quest);
                    value->perQuestFlags.emplace(quest, source.flags);
                    value->perQuestHoursUntilReset.emplace(quest, source.hoursUntilReset);
                }
            value->numQuestsToStart = form.storyNumQuestsToStart;
            return true;
        }
        case FK::Package: {
            auto* value = tesForm->As<RE::TESPackage>();
            if (!value) return false;
            value->SetPackType(static_cast<RE::PACKAGE_PROCEDURE_TYPE>(std::min(form.packageProcedureType, 48u)));
            if (auto* packageTemplate = ResolveAs<RE::TESPackage>(form.packageTemplate); packageTemplate && packageTemplate->data && value->data)
                value->data->Copy(packageTemplate->data, value);
            value->packData.packFlags = static_cast<RE::PACKAGE_DATA::GeneralFlag>(form.packageFlags);
            value->packData.packType = static_cast<RE::PACKAGE_TYPE>(std::min(form.packageType, 43u));
            value->packData.interruptOverrideType = static_cast<RE::PACK_INTERRUPT_TARGET>(form.packageInterruptType);
            value->packData.maxSpeed =
                static_cast<RE::PACKAGE_DATA::PreferredSpeed>(std::min(form.packagePreferredSpeed, 3u));
            value->packData.foBehaviorFlags = static_cast<RE::PACKAGE_DATA::InterruptFlag>(form.packageInterruptFlags);
            value->packData.packageSpecificFlags = static_cast<std::uint16_t>(form.packageSpecificFlags);
            if (!value->idleCollection) value->idleCollection = RE::BGSIdleCollection::Create();
            if (value->idleCollection) {
                while (value->idleCollection->idleCount > 0 && value->idleCollection->idles)
                    value->idleCollection->RemoveIdle(value->idleCollection->idles[0]);
                value->idleCollection->idleFlags = static_cast<RE::BGSIdleCollection::IdleFlags>(form.packageIdleFlags);
                value->idleCollection->timerCheckForIdle = form.packageIdleTimer;
                for (const auto& ref : form.packageIdles)
                    if (auto* idle = ResolveAs<RE::TESIdleForm>(ref)) value->idleCollection->AddIdle(idle);
            }
            value->packSched.psData.month = form.packageMonth;
            value->packSched.psData.dayOfWeek = static_cast<RE::PACK_SCHED_DATA::DayOfWeek>(form.packageDayOfWeek);
            value->packSched.psData.date = form.packageDate;
            value->packSched.psData.hour = form.packageHour;
            value->packSched.psData.minute = form.packageMinute;
            value->packSched.psData.duration = form.packageDuration;
            ApplyConditions(value->packConditions, form.conditions);
            value->combatStyle = ResolveAs<RE::TESCombatStyle>(form.packageCombatStyle);
            value->ownerQuest = ResolveAs<RE::TESQuest>(form.packageOwnerQuest);
            ApplyPackageEvent(value->onBegin, form.packageOnBegin);
            ApplyPackageEvent(value->onEnd, form.packageOnEnd);
            ApplyPackageEvent(value->onChange, form.packageOnChange);
            if (form.packageLocationType != 0xFFFFFFFFu) {
                if (!value->packLoc) {
                    value->packLoc = RE::calloc<RE::PackageLocation>(1);
                    SetRuntimeVTable(value->packLoc, RE::VTABLE_PackageLocation[0]);
                }
                value->packLoc->locType = static_cast<RE::PackageLocation::Type>(form.packageLocationType);
                value->packLoc->rad = form.packageLocationRadius;
                switch (form.packageLocationType) {
                    case 5:
                    case 8:
                    case 9:
                        value->packLoc->data.object = nullptr;
                        *reinterpret_cast<std::uint32_t*>(std::addressof(value->packLoc->data)) =
                            form.packageLocationValue;
                        break;
                    case 0:
                    case 1:
                    case 4:
                    case 6:
                        value->packLoc->data.object = ResolveConfigForm(form.packageLocationObject);
                        break;
                    default:
                        break;
                }
            }
            if (form.packageTargetType >= 0) {
                if (!value->packTarg) value->packTarg = new RE::PackageTarget();
                value->packTarg->targType = static_cast<std::int8_t>(form.packageTargetType);
                switch (form.packageTargetType) {
                    case 2:
                        value->packTarg->target.objType = static_cast<RE::PACKAGE_OBJECT_TYPE>(form.packageTargetAlias);
                        break;
                    case 4:
                    case 5:
                        value->packTarg->target.aliasID = form.packageTargetAlias;
                        break;
                    case 6:
                        break;
                    default:
                        value->packTarg->target.object = ResolveConfigForm(form.packageTargetForm);
                        break;
                }
                value->packTarg->value = form.packageTargetValue;
            }
            return true;
        }
        case FK::Race: {
            auto* value = tesForm->As<RE::TESRace>();
            if (!value) return false;
            value->fullName = form.fullName.c_str();
            ApplyKeywords(static_cast<RE::BGSKeywordForm&>(*value), form.keywords);
            static_cast<RE::BGSSkinForm&>(*value).skin = ResolveAs<RE::TESObjectARMO>(form.skin);
            value->data.flags = static_cast<RE::RACE_DATA::Flag>(form.raceFlags);
            value->data.flags2 = static_cast<RE::RACE_DATA::Flag2>(form.raceFlags2);
            value->data.raceSize = static_cast<RE::RACE_SIZE>(std::min(form.raceSize, 3u));
            for (std::size_t i = 0; i < 7; ++i) {
                value->data.skillBoosts[i].skill = static_cast<RE::ActorValue>(form.raceSkillBoostSkills[i]);
                value->data.skillBoosts[i].bonus = form.raceSkillBoostBonuses[i];
            }
            for (std::size_t i = 0; i < 2; ++i) {
                value->data.height[i] = form.raceHeight[i];
                value->data.weight[i] = form.raceWeight[i];
                value->skeletonModels[i].SetModel(form.raceSkeletonModels[i].c_str());
                value->behaviorGraphs[i].SetModel(form.raceBehaviorGraphs[i].c_str());
                value->bodyTextureModels[i].SetModel(form.raceBodyTextureModels[i].c_str());
                value->defaultVoiceTypes[i] = ResolveAs<RE::BGSVoiceType>(form.raceVoiceTypes[i]);
                value->decapitateArmors[i] = ResolveAs<RE::TESObjectARMO>(form.raceDecapitateArmors[i]);
            }
            value->data.startingHealth=form.raceStats[0]; value->data.startingMagicka=form.raceStats[1]; value->data.startingStamina=form.raceStats[2]; value->data.baseCarryWeight=form.raceStats[3]; value->data.baseMass=form.raceStats[4]; value->data.accelerate=form.raceStats[5]; value->data.decelerate=form.raceStats[6]; value->data.injuredHealthPercent=form.raceStats[7]; value->data.healthRegen=form.raceStats[8]; value->data.magickaRegen=form.raceStats[9]; value->data.staminaRegen=form.raceStats[10]; value->data.unarmedDamage=form.raceStats[11]; value->data.unarmedReach=form.raceStats[12]; value->data.aimAngleTolerance=form.raceStats[13]; value->data.flightRadius=form.raceStats[14];
            value->bodyPartData=ResolveAs<RE::BGSBodyPartData>(form.raceBodyPartData); value->bloodImpactMaterial=ResolveAs<RE::BGSMaterialType>(form.raceBloodMaterial); value->impactDataSet=ResolveAs<RE::BGSImpactDataSet>(form.raceImpactDataSet); value->dismemberBlood=ResolveAs<RE::BGSArtObject>(form.raceDismemberBlood); value->corpseOpenSound=ResolveAs<RE::BGSSoundDescriptorForm>(form.raceCorpseOpenSound); value->corpseCloseSound=ResolveAs<RE::BGSSoundDescriptorForm>(form.raceCorpseCloseSound); value->equipSlots.clear(); for (const auto& ref : form.raceEquipSlots) if (auto* slot=ResolveAs<RE::BGSEquipSlot>(ref)) value->equipSlots.push_back(slot); value->validEquipTypes=static_cast<RE::TESRace::EquipmentFlag>(form.raceValidEquipTypes); value->unarmedEquipSlot=ResolveAs<RE::BGSEquipSlot>(form.raceUnarmedEquipSlot); value->morphRace=ResolveAs<RE::TESRace>(form.raceMorphRace); value->armorParentRace=ResolveAs<RE::TESRace>(form.raceArmorParentRace); for (std::size_t i=0;i<6;++i) value->baseMoveTypes[i]=ResolveAs<RE::BGSMovementType>(form.raceMovementTypes[i]);
            value->clampFaceGeoValue = form.raceFaceClamp; value->clampFaceGeoValue2 = form.raceFaceClamp2;
            value->data.mountOffset = { form.raceMountData[0], form.raceMountData[1], form.raceMountData[2] }; value->data.dismountOffset = { form.raceMountData[3], form.raceMountData[4], form.raceMountData[5] }; value->data.mountCameraOffset = { form.raceMountData[6], form.raceMountData[7], form.raceMountData[8] }; value->data.angleAccelerate = form.raceAngularData[0]; value->data.angleTolerance = form.raceAngularData[1];
            for (std::size_t i=0;i<form.raceBipedObjectNames.size();++i) value->bipedObjectNameA[i]=form.raceBipedObjectNames[i].c_str(); value->phonemeTargets.clear(); for(const auto& name:form.racePhonemeTargets)value->phonemeTargets.push_back(name.c_str());
            for (std::size_t sex=0; sex<2; ++sex) { if (!value->faceRelatedData[sex]) value->faceRelatedData[sex]=new RE::TESRace::FaceRelatedData{}; auto& face=*value->faceRelatedData[sex]; if(!face.headParts)face.headParts=new RE::BSTArray<RE::BGSHeadPart*>();face.headParts->clear();for(const auto& ref:form.raceHeadParts[sex])if(auto* item=ResolveAs<RE::BGSHeadPart>(ref))face.headParts->push_back(item);if(!face.presetNPCs)face.presetNPCs=new RE::BSTArray<RE::TESNPC*>();face.presetNPCs->clear();for(const auto& ref:form.racePresetNPCs[sex])if(auto* item=ResolveAs<RE::TESNPC>(ref))face.presetNPCs->push_back(item);if(!face.availableHairColors)face.availableHairColors=new RE::BSTArray<RE::BGSColorForm*>();face.availableHairColors->clear();for(const auto& ref:form.raceHairColors[sex])if(auto* item=ResolveAs<RE::BGSColorForm>(ref))face.availableHairColors->push_back(item);if(!face.faceDetailsTextureSets)face.faceDetailsTextureSets=new RE::BSTArray<RE::BGSTextureSet*>();face.faceDetailsTextureSets->clear();for(const auto& ref:form.raceFaceDetailTextures[sex])if(auto* item=ResolveAs<RE::BGSTextureSet>(ref))face.faceDetailsTextureSets->push_back(item);face.defaultFaceDetailsTextureSet=ResolveAs<RE::BGSTextureSet>(form.raceDefaultFaceDetails[sex]);face.defaultHairColor=ResolveAs<RE::BGSColorForm>(form.raceDefaultHairColors[sex]);for(std::size_t morph=0;morph<4;++morph)face.availableMorphs[morph].morphFlags=form.raceMorphFlags[sex*4+morph]; }
            if (!value->attackDataMap) { auto* map=RE::calloc<RE::BGSAttackDataMap>(1); SetRuntimeVTable(map,RE::VTABLE_BGSAttackDataMap[0]); std::construct_at(std::addressof(map->attackDataMap)); value->attackDataMap=RE::NiPointer<RE::BGSAttackDataMap>(map); } if(value->attackDataMap){value->attackDataMap->attackDataMap.clear();value->attackDataMap->defaultDataRace=ResolveAs<RE::TESRace>(form.raceAttackRace);for(const auto& source:form.raceAttacks){if(source.event.empty())continue;auto* attack=RE::BGSAttackData::Create();if(!attack)continue;attack->event=source.event.c_str();attack->data.damageMult=source.damageMult;attack->data.attackChance=source.attackChance;attack->data.attackSpell=ResolveAs<RE::SpellItem>(source.attackSpell);attack->data.flags=static_cast<RE::AttackData::AttackFlag>(source.flags);attack->data.attackAngle=source.attackAngle;attack->data.strikeAngle=source.strikeAngle;attack->data.staggerOffset=source.staggerOffset;attack->data.attackType=ResolveAs<RE::BGSKeyword>(source.attackType);attack->data.knockDown=source.knockDown;attack->data.recoveryTime=source.recoveryTime;attack->data.staminaMult=source.staminaMult;value->attackDataMap->attackDataMap.emplace(attack->event,RE::NiPointer<RE::BGSAttackData>(attack));}}
            auto* spellList=static_cast<RE::TESSpellList*>(value); if (!form.spells.empty() && !spellList->actorEffects) spellList->actorEffects=new RE::TESSpellList::SpellData(); if (spellList->actorEffects) for (const auto& ref:form.spells) if (auto* spell=ResolveAs<RE::SpellItem>(ref)) spellList->actorEffects->AddSpell(spell); return true;
        }
        default: return false;
        }
    }

    bool ConfigureForm(RE::TESForm* tesForm, const DynamicForms::DynamicForm& form) {
        if (!tesForm) {
            return false;
        }

        tesForm->SetFormEditorID(form.editorId.c_str());
        if (form.kind >= DynamicForms::FormKind::ImpactDataSet) return ConfigureAdditionalReadyForm(tesForm, form);
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
        if (form.kind == DynamicForms::FormKind::Spell) {
            return ConfigureSpell(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::Enchantment) {
            return ConfigureEnchantment(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::Scroll) {
            return ConfigureScroll(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::Projectile) {
            return ConfigureProjectile(tesForm, form);
        }
        if (form.kind == DynamicForms::FormKind::TextureSet) return ConfigureTextureSet(tesForm, form);
        if (form.kind == DynamicForms::FormKind::Hazard) return ConfigureHazard(tesForm, form);
        if (form.kind == DynamicForms::FormKind::ImpactData) return ConfigureImpactData(tesForm, form);
        if (form.kind == DynamicForms::FormKind::ReferenceEffect) return ConfigureReferenceEffect(tesForm, form);
        if (form.kind == DynamicForms::FormKind::DualCastData) return ConfigureDualCastData(tesForm, form);
        if (form.kind == DynamicForms::FormKind::Static) return ConfigureStatic(tesForm, form);
        if (form.kind == DynamicForms::FormKind::MovableStatic) return ConfigureMovableStatic(tesForm, form);
        if (form.kind == DynamicForms::FormKind::Door) return ConfigureDoor(tesForm, form);
        if (form.kind == DynamicForms::FormKind::CombatStyle) return ConfigureCombatStyle(tesForm, form);
        if (form.kind == DynamicForms::FormKind::SoundCategory) return ConfigureSoundCategory(tesForm, form);
        if (form.kind == DynamicForms::FormKind::Class) return ConfigureClass(tesForm, form);
        if (form.kind == DynamicForms::FormKind::Flora) return ConfigureFlora(tesForm, form);
        if (form.kind == DynamicForms::FormKind::Tree) return ConfigureTree(tesForm, form);
        if (form.kind == DynamicForms::FormKind::ConstructibleObject) return ConfigureConstructibleObject(tesForm, form);
        if (form.kind == DynamicForms::FormKind::Container) return ConfigureContainer(tesForm, form);
        if (form.kind == DynamicForms::FormKind::MagicEffect) {
            return ConfigureMagicEffect(tesForm, form);
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

    enum class ResolveDPFFailure
    {
        None,
        Unavailable,
        Create,
        Configure
    };

    RE::TESForm* ResolveDPFFormObject(
        DynamicForms::DynamicForm& form,
        const bool configure = true,
        bool* recoveredExistingSlot = nullptr,
        ResolveDPFFailure* failure = nullptr)
    {
        if (recoveredExistingSlot) {
            *recoveredExistingSlot = false;
        }
        if (failure) {
            *failure = ResolveDPFFailure::None;
        }

        auto* api = DPF::GetAPI();
        if (!api) {
            if (failure) {
                *failure = ResolveDPFFailure::Unavailable;
            }
            logger::warn("Dynamic Persistent Forms API is not available yet.");
            return nullptr;
        }

        bool existed = false;
        auto pluginNumber = form.pluginNumber;
        auto localId = form.localId;
        const auto formType = FormTypeForKind(form.kind);
        auto* tesForm = api->GetOrCreateByOwnerKey(
            Manager::DPF_OWNER,
            form.editorId.c_str(),
            formType,
            &pluginNumber,
            &localId,
            &existed);
        if (!tesForm && (existed || form.localId != 0)) {
            logger::warn("DPF returned null for dynamic {} '{}'. Releasing stale owner/key and retrying once.",
                ToString(form.kind),
                form.editorId);
            if (api->ReleaseByOwnerKey(Manager::DPF_OWNER, form.editorId.c_str())) {
                pluginNumber = 0;
                localId = 0;
                existed = false;
                tesForm = api->GetOrCreateByOwnerKey(
                    Manager::DPF_OWNER,
                    form.editorId.c_str(),
                    formType,
                    &pluginNumber,
                    &localId,
                    &existed);
            } else {
                logger::warn("Could not release stale DPF owner/key for dynamic form '{}'.", form.editorId);
            }
        }
        if (!tesForm) {
            if (failure) {
                *failure = ResolveDPFFailure::Create;
            }
            logger::warn("DPF returned null for dynamic form '{}'", form.editorId);
            return nullptr;
        }

        if (pluginNumber == 0) {
            pluginNumber = api->GetPluginNumberForFormId(tesForm->GetFormID());
        }
        form.pluginNumber = pluginNumber;
        form.localId = localId;
        if (recoveredExistingSlot) {
            *recoveredExistingSlot = existed;
        }
        if (configure && !ConfigureForm(tesForm, form)) {
            if (failure) {
                *failure = ResolveDPFFailure::Configure;
            }
            return nullptr;
        }

        logger::info("DPF {} dynamic {} '{}' owner '{}' slot {}:{:06X} plugin '{}' FormID {:08X}.",
            existed ? "recovered" : "created",
            ToString(form.kind),
            form.editorId,
            Manager::DPF_OWNER,
            form.pluginNumber,
            form.localId,
            DPF::PluginNameForNumber(form.pluginNumber),
            tesForm->GetFormID());
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

    const char* PerkEntryKindName(const DynamicForms::PerkEntryKind kind) {
        switch (kind) {
        case DynamicForms::PerkEntryKind::Quest:
            return "Quest";
        case DynamicForms::PerkEntryKind::Ability:
            return "Ability";
        case DynamicForms::PerkEntryKind::EntryPoint:
        default:
            return "EntryPoint";
        }
    }

    DynamicForms::PerkEntryKind ReadPerkEntryKind(const rapidjson::Value& value) {
        if (!value.IsString()) {
            return DynamicForms::PerkEntryKind::EntryPoint;
        }
        const auto normalized = NormalizeKindName(value.GetString());
        if (normalized == "quest") {
            return DynamicForms::PerkEntryKind::Quest;
        }
        if (normalized == "ability") {
            return DynamicForms::PerkEntryKind::Ability;
        }
        return DynamicForms::PerkEntryKind::EntryPoint;
    }

    const char* PerkFunctionDataKindName(const DynamicForms::PerkFunctionDataKind kind) {
        switch (kind) {
        case DynamicForms::PerkFunctionDataKind::None:
            return "None";
        case DynamicForms::PerkFunctionDataKind::OneValue:
            return "OneValue";
        case DynamicForms::PerkFunctionDataKind::TwoValue:
            return "TwoValue";
        case DynamicForms::PerkFunctionDataKind::ActorValueAndValue:
            return "ActorValueAndValue";
        case DynamicForms::PerkFunctionDataKind::LeveledList:
            return "LeveledList";
        case DynamicForms::PerkFunctionDataKind::ActivateChoice:
            return "ActivateChoice";
        case DynamicForms::PerkFunctionDataKind::Spell:
            return "Spell";
        case DynamicForms::PerkFunctionDataKind::BooleanGraphVariable:
            return "BooleanGraphVariable";
        case DynamicForms::PerkFunctionDataKind::Text:
            return "Text";
        default:
            return "None";
        }
    }

    DynamicForms::PerkEntry ReadPerkEntry(const rapidjson::Value& value) {
        DynamicForms::PerkEntry entry;
        if (!value.IsObject()) {
            return entry;
        }
        if (value.HasMember("type")) {
            entry.kind = ReadPerkEntryKind(value["type"]);
        }
        if (value.HasMember("rank") && value["rank"].IsUint()) {
            entry.rank = value["rank"].GetUint();
        }
        if (value.HasMember("priority") && value["priority"].IsUint()) {
            entry.priority = value["priority"].GetUint();
        }
        if (value.HasMember("quest")) {
            entry.quest = ReadFormRefValue(value["quest"]);
        }
        if (value.HasMember("questStage") && value["questStage"].IsUint()) {
            entry.questStage = value["questStage"].GetUint();
        }
        if (value.HasMember("ability")) {
            entry.ability = ReadFormRefValue(value["ability"]);
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

        entry.functionData.kind = ExpectedPerkFunctionDataKind(entry.function);
        if (value.HasMember("functionData") && value["functionData"].IsObject()) {
            const auto& data = value["functionData"];
            entry.functionData.value1 = ReadFloat(data, "value1", entry.functionData.value1);
            entry.functionData.value2 = ReadFloat(data, "value2", entry.functionData.value2);
            entry.functionData.actorValue = ReadUInt32(data, "actorValue", entry.functionData.actorValue);
            if (data.HasMember("form")) {
                entry.functionData.form = ReadFormRefValue(data["form"]);
            }
            ReadString(data, "text", entry.functionData.text);
            ReadString(data, "buttonLabel", entry.functionData.buttonLabel);
            entry.functionData.flags = ReadUInt32(data, "flags", entry.functionData.flags);
            entry.functionData.fragmentIndex = ReadUInt32(data, "fragmentIndex", entry.functionData.fragmentIndex);
        } else if (value.HasMember("value") && value["value"].IsNumber()) {
            entry.functionData.value1 = value["value"].GetFloat();
        }

        if (value.HasMember("conditionTabs") && value["conditionTabs"].IsArray()) {
            for (const auto& item : value["conditionTabs"].GetArray()) {
                if (!item.IsObject()) {
                    continue;
                }
                DynamicForms::PerkConditionTab tab;
                tab.index = ReadUInt32(item, "index", static_cast<std::uint32_t>(entry.conditionTabs.size()));
                tab.conditions = ReadConditionArray(item, "conditions");
                entry.conditionTabs.push_back(std::move(tab));
            }
        } else if (value.HasMember("conditions") && value["conditions"].IsArray()) {
            DynamicForms::PerkConditionTab tab;
            tab.conditions = ReadConditionArray(value, "conditions");
            entry.conditionTabs.push_back(std::move(tab));
        }
        return entry;
    }

    void WritePerkEntry(rapidjson::Value& array, rapidjson::Document::AllocatorType& allocator, const DynamicForms::PerkEntry& entry) {
        rapidjson::Value item(rapidjson::kObjectType);
        item.AddMember(
            "type",
            rapidjson::Value(PerkEntryKindName(entry.kind), allocator),
            allocator);
        item.AddMember("rank", entry.rank, allocator);
        item.AddMember("priority", entry.priority, allocator);
        if (entry.kind == DynamicForms::PerkEntryKind::Quest) {
            AddFormRef(item, allocator, "quest", entry.quest);
            item.AddMember("questStage", entry.questStage, allocator);
            array.PushBack(item, allocator);
            return;
        }
        if (entry.kind == DynamicForms::PerkEntryKind::Ability) {
            AddFormRef(item, allocator, "ability", entry.ability);
            array.PushBack(item, allocator);
            return;
        }

        item.AddMember("entryPoint", entry.entryPoint, allocator);
        item.AddMember("function", entry.function, allocator);
        item.AddMember("numArgs", entry.numArgs, allocator);

        const auto dataKind = ExpectedPerkFunctionDataKind(entry.function);
        rapidjson::Value data(rapidjson::kObjectType);
        data.AddMember(
            "type",
            rapidjson::Value(PerkFunctionDataKindName(dataKind), allocator),
            allocator);
        switch (dataKind) {
        case DynamicForms::PerkFunctionDataKind::OneValue:
            data.AddMember("value1", entry.functionData.value1, allocator);
            break;
        case DynamicForms::PerkFunctionDataKind::TwoValue:
            data.AddMember("value1", entry.functionData.value1, allocator);
            data.AddMember("value2", entry.functionData.value2, allocator);
            break;
        case DynamicForms::PerkFunctionDataKind::ActorValueAndValue:
            data.AddMember("actorValue", entry.functionData.actorValue, allocator);
            data.AddMember("value2", entry.functionData.value2, allocator);
            break;
        case DynamicForms::PerkFunctionDataKind::LeveledList:
        case DynamicForms::PerkFunctionDataKind::Spell:
            AddFormRef(data, allocator, "form", entry.functionData.form);
            break;
        case DynamicForms::PerkFunctionDataKind::ActivateChoice:
            AddFormRef(data, allocator, "form", entry.functionData.form);
            data.AddMember("buttonLabel", rapidjson::Value(entry.functionData.buttonLabel.c_str(), allocator), allocator);
            data.AddMember("flags", entry.functionData.flags, allocator);
            data.AddMember("fragmentIndex", entry.functionData.fragmentIndex, allocator);
            break;
        case DynamicForms::PerkFunctionDataKind::BooleanGraphVariable:
        case DynamicForms::PerkFunctionDataKind::Text:
            data.AddMember("text", rapidjson::Value(entry.functionData.text.c_str(), allocator), allocator);
            break;
        case DynamicForms::PerkFunctionDataKind::None:
        default:
            break;
        }
        item.AddMember("functionData", data, allocator);

        rapidjson::Value tabs(rapidjson::kArrayType);
        for (const auto& sourceTab : entry.conditionTabs) {
            rapidjson::Value tab(rapidjson::kObjectType);
            tab.AddMember("index", sourceTab.index, allocator);
            AddConditionArray(tab, allocator, "conditions", sourceTab.conditions);
            tabs.PushBack(tab, allocator);
        }
        item.AddMember("conditionTabs", tabs, allocator);
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
        if (doc.HasMember("pluginNumber") && doc["pluginNumber"].IsUint()) {
            out.pluginNumber = doc["pluginNumber"].GetUint();
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
        out.spellFlags = ReadUInt32(doc, "spellFlags", out.spellFlags);
        out.spellType = ReadUInt32(doc, "spellType", out.spellType);
        if (doc.HasMember("spellCostOverride") && doc["spellCostOverride"].IsInt()) {
            out.spellCostOverride = doc["spellCostOverride"].GetInt();
        }
        out.spellChargeTime = ReadFloat(doc, "spellChargeTime", out.spellChargeTime);
        out.spellCastingType = ReadUInt32(doc, "spellCastingType", out.spellCastingType);
        out.spellDelivery = ReadUInt32(doc, "spellDelivery", out.spellDelivery);
        out.spellCastDuration = ReadFloat(doc, "spellCastDuration", out.spellCastDuration);
        out.spellRange = ReadFloat(doc, "spellRange", out.spellRange);
        ReadFormRef(doc, "castingPerk", out.castingPerk);
        ReadFormRef(doc, "menuDisplayObject", out.menuDisplayObject);
        out.enchantmentFlags = ReadUInt32(doc, "enchantmentFlags", out.enchantmentFlags);
        out.enchantmentCostOverride = ReadInt32(doc, "enchantmentCostOverride", out.enchantmentCostOverride);
        out.enchantmentCastingType = ReadUInt32(doc, "enchantmentCastingType", out.enchantmentCastingType);
        out.enchantmentChargeOverride = ReadInt32(doc, "enchantmentChargeOverride", out.enchantmentChargeOverride);
        out.enchantmentDelivery = ReadUInt32(doc, "enchantmentDelivery", out.enchantmentDelivery);
        out.enchantmentSpellType = ReadUInt32(doc, "enchantmentSpellType", out.enchantmentSpellType);
        out.enchantmentChargeTime = ReadFloat(doc, "enchantmentChargeTime", out.enchantmentChargeTime);
        ReadFormRef(doc, "baseEnchantment", out.baseEnchantment);
        ReadFormRef(doc, "wornRestrictions", out.wornRestrictions);
        out.scrollFlags = ReadUInt32(doc, "scrollFlags", out.scrollFlags);
        out.scrollCostOverride = ReadInt32(doc, "scrollCostOverride", out.scrollCostOverride);
        out.scrollChargeTime = ReadFloat(doc, "scrollChargeTime", out.scrollChargeTime);
        out.scrollDelivery = ReadUInt32(doc, "scrollDelivery", out.scrollDelivery);
        out.scrollCastDuration = ReadFloat(doc, "scrollCastDuration", out.scrollCastDuration);
        out.scrollRange = ReadFloat(doc, "scrollRange", out.scrollRange);
        ReadFormRef(doc, "scrollCastingPerk", out.scrollCastingPerk);
        out.projectileFlags = ReadUInt32(doc, "projectileFlags", out.projectileFlags);
        out.projectileTypes = ReadUInt32(doc, "projectileTypes", out.projectileTypes);
        out.projectileGravity = ReadFloat(doc, "projectileGravity", out.projectileGravity);
        out.projectileSpeed = ReadFloat(doc, "projectileSpeed", out.projectileSpeed);
        out.projectileRange = ReadFloat(doc, "projectileRange", out.projectileRange);
        ReadFormRef(doc, "projectileLight", out.projectileLight);
        ReadFormRef(doc, "projectileMuzzleFlashLight", out.projectileMuzzleFlashLight);
        out.projectileTracerChance = ReadFloat(doc, "projectileTracerChance", out.projectileTracerChance);
        out.projectileExplosionProximity = ReadFloat(doc, "projectileExplosionProximity", out.projectileExplosionProximity);
        out.projectileExplosionTimer = ReadFloat(doc, "projectileExplosionTimer", out.projectileExplosionTimer);
        ReadFormRef(doc, "projectileExplosionType", out.projectileExplosionType);
        ReadFormRef(doc, "projectileActiveSoundLoop", out.projectileActiveSoundLoop);
        out.projectileMuzzleFlashDuration = ReadFloat(doc, "projectileMuzzleFlashDuration", out.projectileMuzzleFlashDuration);
        out.projectileFadeOutTime = ReadFloat(doc, "projectileFadeOutTime", out.projectileFadeOutTime);
        out.projectileForce = ReadFloat(doc, "projectileForce", out.projectileForce);
        ReadFormRef(doc, "projectileCountdownSound", out.projectileCountdownSound);
        ReadFormRef(doc, "projectileDeactivateSound", out.projectileDeactivateSound);
        ReadFormRef(doc, "projectileDefaultWeaponSource", out.projectileDefaultWeaponSource);
        out.projectileConeSpread = ReadFloat(doc, "projectileConeSpread", out.projectileConeSpread);
        out.projectileCollisionRadius = ReadFloat(doc, "projectileCollisionRadius", out.projectileCollisionRadius);
        out.projectileLifetime = ReadFloat(doc, "projectileLifetime", out.projectileLifetime);
        out.projectileRelaunchInterval = ReadFloat(doc, "projectileRelaunchInterval", out.projectileRelaunchInterval);
        ReadFormRef(doc, "projectileDecalData", out.projectileDecalData);
        ReadFormRef(doc, "projectileCollisionLayer", out.projectileCollisionLayer);
        ReadString(doc, "projectileMuzzleFlashModel", out.projectileMuzzleFlashModel);
        out.projectileSoundLevel = ReadUInt32(doc, "projectileSoundLevel", out.projectileSoundLevel);
        ReadStringArray(doc, "textureSetPaths", out.textureSetPaths);
        out.textureSetFlags = ReadUInt32(doc, "textureSetFlags", out.textureSetFlags);
        if (doc.HasMember("textureSetHasDecal") && doc["textureSetHasDecal"].IsBool()) out.textureSetHasDecal = doc["textureSetHasDecal"].GetBool();
        out.decalMinWidth = ReadFloat(doc, "decalMinWidth", out.decalMinWidth);
        out.decalMaxWidth = ReadFloat(doc, "decalMaxWidth", out.decalMaxWidth);
        out.decalMinHeight = ReadFloat(doc, "decalMinHeight", out.decalMinHeight);
        out.decalMaxHeight = ReadFloat(doc, "decalMaxHeight", out.decalMaxHeight);
        out.decalDepth = ReadFloat(doc, "decalDepth", out.decalDepth);
        out.decalShininess = ReadFloat(doc, "decalShininess", out.decalShininess);
        out.decalParallaxScale = ReadFloat(doc, "decalParallaxScale", out.decalParallaxScale);
        out.decalParallaxPasses = ReadInt32(doc, "decalParallaxPasses", out.decalParallaxPasses);
        out.decalFlags = ReadUInt32(doc, "decalFlags", out.decalFlags);
        out.decalRed = static_cast<std::uint8_t>(ReadUInt32(doc, "decalRed", out.decalRed));
        out.decalGreen = static_cast<std::uint8_t>(ReadUInt32(doc, "decalGreen", out.decalGreen));
        out.decalBlue = static_cast<std::uint8_t>(ReadUInt32(doc, "decalBlue", out.decalBlue));
        out.decalAlpha = static_cast<std::uint8_t>(ReadUInt32(doc, "decalAlpha", out.decalAlpha));
        out.hazardLimit = ReadUInt32(doc, "hazardLimit", out.hazardLimit);
        out.hazardRadius = ReadFloat(doc, "hazardRadius", out.hazardRadius);
        out.hazardLifetime = ReadFloat(doc, "hazardLifetime", out.hazardLifetime);
        out.hazardImageSpaceRadius = ReadFloat(doc, "hazardImageSpaceRadius", out.hazardImageSpaceRadius);
        out.hazardTargetInterval = ReadFloat(doc, "hazardTargetInterval", out.hazardTargetInterval);
        out.hazardFlags = ReadUInt32(doc, "hazardFlags", out.hazardFlags);
        ReadFormRef(doc, "hazardSpell", out.hazardSpell); ReadFormRef(doc, "hazardLight", out.hazardLight);
        ReadFormRef(doc, "hazardImpactDataSet", out.hazardImpactDataSet); ReadFormRef(doc, "hazardSound", out.hazardSound);
        ReadFormRef(doc, "hazardImageSpaceModifier", out.hazardImageSpaceModifier);
        out.impactEffectDuration = ReadFloat(doc, "impactEffectDuration", out.impactEffectDuration);
        out.impactOrientation = ReadUInt32(doc, "impactOrientation", out.impactOrientation);
        out.impactAngleThreshold = ReadFloat(doc, "impactAngleThreshold", out.impactAngleThreshold);
        out.impactPlacementRadius = ReadFloat(doc, "impactPlacementRadius", out.impactPlacementRadius);
        out.impactSoundLevel = ReadUInt32(doc, "impactSoundLevel", out.impactSoundLevel);
        out.impactFlags = ReadUInt32(doc, "impactFlags", out.impactFlags);
        out.impactResultOverride = ReadUInt32(doc, "impactResultOverride", out.impactResultOverride);
        ReadFormRef(doc, "impactDecalTextureSet", out.impactDecalTextureSet); ReadFormRef(doc, "impactDecalTextureSet2", out.impactDecalTextureSet2);
        ReadFormRef(doc, "impactSound1", out.impactSound1); ReadFormRef(doc, "impactSound2", out.impactSound2); ReadFormRef(doc, "impactHazard", out.impactHazard);
        ReadFormRef(doc, "referenceEffectArtObject", out.referenceEffectArtObject); ReadFormRef(doc, "referenceEffectShader", out.referenceEffectShader);
        out.referenceEffectFlags = ReadUInt32(doc, "referenceEffectFlags", out.referenceEffectFlags);
        ReadFormRef(doc, "dualCastProjectile", out.dualCastProjectile); ReadFormRef(doc, "dualCastExplosion", out.dualCastExplosion);
        ReadFormRef(doc, "dualCastEffectShader", out.dualCastEffectShader); ReadFormRef(doc, "dualCastHitEffectArt", out.dualCastHitEffectArt);
        ReadFormRef(doc, "dualCastImpactDataSet", out.dualCastImpactDataSet); out.dualCastFlags = ReadUInt32(doc, "dualCastFlags", out.dualCastFlags);
        out.staticMaterialThresholdAngle = ReadFloat(doc, "staticMaterialThresholdAngle", out.staticMaterialThresholdAngle);
        ReadFormRef(doc, "staticMaterialObject", out.staticMaterialObject); out.staticFlags = ReadUInt32(doc, "staticFlags", out.staticFlags);
        out.recordFlags = ReadUInt32(doc, "recordFlags", out.recordFlags);
        ReadFormRef(doc, "movableStaticSoundLoop", out.movableStaticSoundLoop); out.movableStaticFlags = ReadUInt32(doc, "movableStaticFlags", out.movableStaticFlags);
        ReadFormRef(doc, "doorOpenSound", out.doorOpenSound); ReadFormRef(doc, "doorCloseSound", out.doorCloseSound); ReadFormRef(doc, "doorLoopSound", out.doorLoopSound);
        out.doorFlags = ReadUInt32(doc, "doorFlags", out.doorFlags);
        ReadNumberArray(doc, "combatGeneral", out.combatGeneral); ReadNumberArray(doc, "combatMelee", out.combatMelee);
        ReadNumberArray(doc, "combatCloseRange", out.combatCloseRange); out.combatLongRangeStrafe = ReadFloat(doc, "combatLongRangeStrafe", out.combatLongRangeStrafe);
        ReadNumberArray(doc, "combatFlight", out.combatFlight); out.combatStyleFlags = ReadUInt32(doc, "combatStyleFlags", out.combatStyleFlags);
        out.soundCategoryFlags = ReadUInt32(doc, "soundCategoryFlags", out.soundCategoryFlags); ReadFormRef(doc, "soundCategoryParent", out.soundCategoryParent);
        out.soundCategoryAttenuation = static_cast<std::uint16_t>(ReadUInt32(doc, "soundCategoryAttenuation", out.soundCategoryAttenuation));
        out.soundCategoryStaticMult = ReadFloat(doc, "soundCategoryStaticMult", out.soundCategoryStaticMult);
        out.soundCategoryDefaultMenuValue = ReadFloat(doc, "soundCategoryDefaultMenuValue", out.soundCategoryDefaultMenuValue);
        out.soundCategoryVolumeMult = ReadFloat(doc, "soundCategoryVolumeMult", out.soundCategoryVolumeMult);
        out.soundCategoryFrequencyMult = ReadFloat(doc, "soundCategoryFrequencyMult", out.soundCategoryFrequencyMult);
        out.classTeachesSkill = ReadUInt32(doc, "classTeachesSkill", out.classTeachesSkill);
        out.classMaximumTrainingLevel = static_cast<std::uint8_t>(ReadUInt32(doc, "classMaximumTrainingLevel", out.classMaximumTrainingLevel));
        ReadNumberArray(doc, "classSkillWeights", out.classSkillWeights); out.classBleedoutDefault = ReadFloat(doc, "classBleedoutDefault", out.classBleedoutDefault);
        out.classVoicePoints = ReadUInt32(doc, "classVoicePoints", out.classVoicePoints); ReadNumberArray(doc, "classAttributeWeights", out.classAttributeWeights);
        ReadString(doc, "classIconPath", out.classIconPath); ReadFormRef(doc, "produceItem", out.produceItem); ReadFormRef(doc, "harvestSound", out.harvestSound);
        ReadNumberArray(doc, "produceChance", out.produceChance); out.floraFlags = ReadUInt32(doc, "floraFlags", out.floraFlags);
        ReadFormRef(doc, "floraSoundLoop", out.floraSoundLoop); ReadFormRef(doc, "floraSoundActivate", out.floraSoundActivate); ReadFormRef(doc, "floraWaterType", out.floraWaterType);
        ReadNumberArray(doc, "treeAnimation", out.treeAnimation); out.treeType = ReadUInt32(doc, "treeType", out.treeType);
        ReadFormRef(doc, "createdItem", out.createdItem);
        ReadFormRef(doc, "benchKeyword", out.benchKeyword);
        out.numConstructed = static_cast<std::uint16_t>(std::clamp(ReadUInt32(doc, "numConstructed", out.numConstructed), 1u, 65535u));
        ReadContainerEntries(doc, "requiredItems", out.requiredItems);
        ReadContainerEntries(doc, "containerItems", out.containerItems);
        out.containerFlags = ReadUInt32(doc, "containerFlags", out.containerFlags);
        if (doc.HasMember("containerAllowStolenItems") && doc["containerAllowStolenItems"].IsBool()) out.containerAllowStolenItems = doc["containerAllowStolenItems"].GetBool();
        ReadFormRef(doc, "containerOpenSound", out.containerOpenSound);
        ReadFormRef(doc, "containerCloseSound", out.containerCloseSound);
        ReadFormRefPairs(doc, "impactDataSetEntries", out.impactDataSetEntries);
        out.collisionLayerIndex = ReadUInt32(doc, "collisionLayerIndex", out.collisionLayerIndex); out.collisionLayerColor = ReadUInt32(doc, "collisionLayerColor", out.collisionLayerColor);
        out.collisionLayerFlags = ReadUInt32(doc, "collisionLayerFlags", out.collisionLayerFlags); ReadString(doc, "collisionLayerName", out.collisionLayerName); ReadFormRefArray(doc, "collisionLayers", out.collisionLayers);
        ReadString(doc, "footstepTag", out.footstepTag); ReadFormRef(doc, "footstepImpactDataSet", out.footstepImpactDataSet);
        constexpr std::array footstepKeys{ "footstepWalk", "footstepRun", "footstepSneak", "footstepBleedout", "footstepSwim" };
        for (std::size_t i = 0; i < footstepKeys.size(); ++i) ReadFormRefArray(doc, footstepKeys[i], out.footstepSets[i]);
        out.reverbDecayTime = static_cast<std::uint16_t>(ReadUInt32(doc, "reverbDecayTime", out.reverbDecayTime)); out.reverbHFReference = static_cast<std::uint16_t>(ReadUInt32(doc, "reverbHFReference", out.reverbHFReference)); ReadNumberArray(doc, "reverbValues", out.reverbValues);
        ReadFormRef(doc, "acousticLoopingSound", out.acousticLoopingSound); ReadFormRef(doc, "acousticSoundRegion", out.acousticSoundRegion); ReadFormRef(doc, "acousticReverb", out.acousticReverb);
        out.apparatusQuality = ReadUInt32(doc, "apparatusQuality", out.apparatusQuality);
        out.grassDensity = static_cast<std::uint8_t>(ReadUInt32(doc, "grassDensity", out.grassDensity)); out.grassMinSlope = static_cast<std::uint8_t>(ReadUInt32(doc, "grassMinSlope", out.grassMinSlope)); out.grassMaxSlope = static_cast<std::uint8_t>(ReadUInt32(doc, "grassMaxSlope", out.grassMaxSlope));
        out.grassDistanceFromWater = static_cast<std::uint16_t>(ReadUInt32(doc, "grassDistanceFromWater", out.grassDistanceFromWater)); out.grassWaterState = ReadUInt32(doc, "grassWaterState", out.grassWaterState);
        out.grassPositionRange = ReadFloat(doc, "grassPositionRange", out.grassPositionRange); out.grassHeightRange = ReadFloat(doc, "grassHeightRange", out.grassHeightRange); out.grassColorRange = ReadFloat(doc, "grassColorRange", out.grassColorRange); out.grassWavePeriod = ReadFloat(doc, "grassWavePeriod", out.grassWavePeriod); out.grassFlags = ReadUInt32(doc, "grassFlags", out.grassFlags);
        out.idleFlags = ReadUInt32(doc, "idleFlags", out.idleFlags); out.idleTimer = ReadFloat(doc, "idleTimer", out.idleTimer); ReadFormRefArray(doc, "idleAnimations", out.idleAnimations);
        ReadFormRef(doc, "encounterOwner", out.encounterOwner); ReadFormRef(doc, "encounterLocation", out.encounterLocation); out.encounterOwnerRank = static_cast<std::int8_t>(ReadInt32(doc, "encounterOwnerRank", out.encounterOwnerRank)); out.encounterMinLevel = static_cast<std::int8_t>(ReadInt32(doc, "encounterMinLevel", out.encounterMinLevel)); out.encounterMaxLevel = static_cast<std::int8_t>(ReadInt32(doc, "encounterMaxLevel", out.encounterMaxLevel)); out.encounterFlags = ReadUInt32(doc, "encounterFlags", out.encounterFlags);
        ReadFormRef(doc, "relationshipNpc1", out.relationshipNpc1); ReadFormRef(doc, "relationshipNpc2", out.relationshipNpc2); ReadFormRef(doc, "relationshipAssociation", out.relationshipAssociation); out.relationshipLevel = ReadUInt32(doc, "relationshipLevel", out.relationshipLevel); out.relationshipFlags = ReadUInt32(doc, "relationshipFlags", out.relationshipFlags);
        ReadStringArray(doc, "associationLabels", out.associationLabels); out.associationFlags = ReadUInt32(doc, "associationFlags", out.associationFlags);
        ReadString(doc, "movementName", out.movementName); ReadNumberArray(doc, "movementSpeeds", out.movementSpeeds); out.movementRotateWhileMoving = ReadFloat(doc, "movementRotateWhileMoving", out.movementRotateWhileMoving); out.movementDirectional = ReadFloat(doc, "movementDirectional", out.movementDirectional); out.movementSpeed = ReadFloat(doc, "movementSpeed", out.movementSpeed); out.movementRotationSpeed = ReadFloat(doc, "movementRotationSpeed", out.movementRotationSpeed);
        ReadString(doc, "wordTranslation", out.wordTranslation); ReadStringArray(doc, "waterNoiseTextures", out.waterNoiseTextures); out.waterAlpha = static_cast<std::uint8_t>(ReadUInt32(doc, "waterAlpha", out.waterAlpha)); out.waterFlags = ReadUInt32(doc, "waterFlags", out.waterFlags);
        ReadFormRef(doc, "waterMaterial", out.waterMaterial); ReadFormRef(doc, "waterSound", out.waterSound); ReadFormRef(doc, "waterContactSpell", out.waterContactSpell); ReadFormRef(doc, "waterImageSpace", out.waterImageSpace); ReadNumberArray(doc, "waterLinearVelocity", out.waterLinearVelocity); ReadNumberArray(doc, "waterAngularVelocity", out.waterAngularVelocity);
        ReadNumberArray(doc, "imageSpaceHDR", out.imageSpaceHDR); ReadNumberArray(doc, "imageSpaceCinematic", out.imageSpaceCinematic); out.imageSpaceTintAmount = ReadFloat(doc, "imageSpaceTintAmount", out.imageSpaceTintAmount); ReadNumberArray(doc, "imageSpaceTintColor", out.imageSpaceTintColor); ReadNumberArray(doc, "imageSpaceDOF", out.imageSpaceDOF); out.imageSpaceDOFFlags = static_cast<std::uint16_t>(ReadUInt32(doc, "imageSpaceDOFFlags", out.imageSpaceDOFFlags)); out.imageSpaceSkyBlur = static_cast<std::uint16_t>(ReadUInt32(doc, "imageSpaceSkyBlur", out.imageSpaceSkyBlur));
        ReadNumberArray(doc, "lightingColors", out.lightingColors); ReadNumberArray(doc, "lightingValues", out.lightingValues); out.lightingDirectionalXY = ReadUInt32(doc, "lightingDirectionalXY", out.lightingDirectionalXY); out.lightingDirectionalZ = ReadUInt32(doc, "lightingDirectionalZ", out.lightingDirectionalZ); out.lightingInheritanceFlags = ReadUInt32(doc, "lightingInheritanceFlags", out.lightingInheritanceFlags);
        constexpr std::array shoutWordKeys{ "shoutWord1", "shoutWord2", "shoutWord3" }; constexpr std::array shoutSpellKeys{ "shoutSpell1", "shoutSpell2", "shoutSpell3" };
        for (std::size_t i = 0; i < 3; ++i) { ReadFormRef(doc, shoutWordKeys[i], out.shoutWords[i]); ReadFormRef(doc, shoutSpellKeys[i], out.shoutSpells[i]); }
        ReadNumberArray(doc, "shoutRecoveryTimes", out.shoutRecoveryTimes);
        ReadLeveledEntries(doc, "leveledEntries", out.leveledEntries); out.leveledChanceNone = static_cast<std::uint8_t>(std::clamp(ReadUInt32(doc, "leveledChanceNone", out.leveledChanceNone), 0u, 100u)); out.leveledFlags = ReadUInt32(doc, "leveledFlags", out.leveledFlags); ReadFormRef(doc, "leveledChanceGlobal", out.leveledChanceGlobal);
        out.actionIndex = ReadUInt32(doc, "actionIndex", out.actionIndex); ReadString(doc, "eyesTexture", out.eyesTexture); out.eyesFlags = ReadUInt32(doc, "eyesFlags", out.eyesFlags); ReadString(doc, "animatedUnloadEvent", out.animatedUnloadEvent);
        ReadString(doc, "loadScreenText", out.loadScreenText); ReadFormRef(doc, "loadScreenObject", out.loadScreenObject); out.loadScreenInitialScale = ReadFloat(doc, "loadScreenInitialScale", out.loadScreenInitialScale); ReadNumberArray(doc, "loadScreenRotationConstraints", out.loadScreenRotationConstraints); ReadNumberArray(doc, "loadScreenRotationOffsetConstraints", out.loadScreenRotationOffsetConstraints); ReadNumberArray(doc, "loadScreenTranslationOffset", out.loadScreenTranslationOffset); ReadString(doc, "loadScreenCameraPath", out.loadScreenCameraPath);
        ReadNumberArray(doc, "shaderParticleSettings", out.shaderParticleSettings); ReadString(doc, "shaderParticleTexture", out.shaderParticleTexture); out.addonIndex = ReadUInt32(doc, "addonIndex", out.addonIndex); ReadFormRef(doc, "addonSound", out.addonSound); out.addonMasterParticleCap = static_cast<std::uint16_t>(ReadUInt32(doc, "addonMasterParticleCap", out.addonMasterParticleCap)); out.addonFlags = ReadUInt32(doc, "addonFlags", out.addonFlags);
        out.factionFlags = ReadUInt32(doc, "factionFlags", out.factionFlags); ReadFactionReactions(doc, out.factionReactions); ReadFactionRanks(doc, out.factionRanks);
        ReadFormRef(doc, "factionJailMarker", out.factionJailMarker); ReadFormRef(doc, "factionWaitMarker", out.factionWaitMarker); ReadFormRef(doc, "factionStolenContainer", out.factionStolenContainer); ReadFormRef(doc, "factionPlayerInventoryContainer", out.factionPlayerInventoryContainer); ReadFormRef(doc, "factionCrimeGroup", out.factionCrimeGroup); ReadFormRef(doc, "factionJailOutfit", out.factionJailOutfit);
        if (doc.HasMember("factionArrest") && doc["factionArrest"].IsBool()) out.factionArrest = doc["factionArrest"].GetBool(); if (doc.HasMember("factionAttackOnSight") && doc["factionAttackOnSight"].IsBool()) out.factionAttackOnSight = doc["factionAttackOnSight"].GetBool();
        out.factionMurderCrimeGold = ReadUInt16(doc, "factionMurderCrimeGold", out.factionMurderCrimeGold); out.factionAssaultCrimeGold = ReadUInt16(doc, "factionAssaultCrimeGold", out.factionAssaultCrimeGold); out.factionTrespassCrimeGold = ReadUInt16(doc, "factionTrespassCrimeGold", out.factionTrespassCrimeGold); out.factionPickpocketCrimeGold = ReadUInt16(doc, "factionPickpocketCrimeGold", out.factionPickpocketCrimeGold); out.factionStealCrimeGoldMult = ReadFloat(doc, "factionStealCrimeGoldMult", out.factionStealCrimeGoldMult); out.factionEscapeCrimeGold = ReadUInt16(doc, "factionEscapeCrimeGold", out.factionEscapeCrimeGold); out.factionWerewolfCrimeGold = ReadUInt16(doc, "factionWerewolfCrimeGold", out.factionWerewolfCrimeGold);
        out.factionVendorStartHour = ReadUInt16(doc, "factionVendorStartHour", out.factionVendorStartHour); out.factionVendorEndHour = ReadUInt16(doc, "factionVendorEndHour", out.factionVendorEndHour); out.factionVendorRadius = ReadUInt32(doc, "factionVendorRadius", out.factionVendorRadius);
        if (doc.HasMember("factionVendorBuysStolen") && doc["factionVendorBuysStolen"].IsBool()) out.factionVendorBuysStolen = doc["factionVendorBuysStolen"].GetBool(); if (doc.HasMember("factionVendorNotBuySell") && doc["factionVendorNotBuySell"].IsBool()) out.factionVendorNotBuySell = doc["factionVendorNotBuySell"].GetBool(); if (doc.HasMember("factionVendorBuysNonStolen") && doc["factionVendorBuysNonStolen"].IsBool()) out.factionVendorBuysNonStolen = doc["factionVendorBuysNonStolen"].GetBool();
        ReadFormRef(doc, "factionVendorSellBuyList", out.factionVendorSellBuyList); ReadFormRef(doc, "factionMerchantContainer", out.factionMerchantContainer);
        if (doc.HasMember("factionVendorConditions") && doc["factionVendorConditions"].IsArray()) { out.factionVendorConditions.clear(); for (const auto& condition : doc["factionVendorConditions"].GetArray()) out.factionVendorConditions.push_back(ReadCondition(condition)); }
        out.idleLoopMin = ReadInt8(doc, "idleLoopMin", out.idleLoopMin); out.idleLoopMax = ReadInt8(doc, "idleLoopMax", out.idleLoopMax); out.idleAnimationFlags = ReadUInt32(doc, "idleAnimationFlags", out.idleAnimationFlags); out.idleAnimationGroupSelection = ReadUInt8(doc, "idleAnimationGroupSelection", out.idleAnimationGroupSelection); out.idleReplayDelay = ReadUInt16(doc, "idleReplayDelay", out.idleReplayDelay); ReadFormRef(doc, "idleParent", out.idleParent); ReadFormRef(doc, "idlePrevious", out.idlePrevious); ReadString(doc, "idleAnimationFile", out.idleAnimationFile); ReadString(doc, "idleAnimationEvent", out.idleAnimationEvent);
        ReadNumberArray(doc, "materialDirectionalData", out.materialDirectionalData); out.materialSinglePass = ReadInt32(doc, "materialSinglePass", out.materialSinglePass); out.materialObjectFlags = ReadUInt32(doc, "materialObjectFlags", out.materialObjectFlags);
        ReadFormRef(doc, "messageMenuIcon", out.messageMenuIcon); ReadFormRef(doc, "messageOwnerQuest", out.messageOwnerQuest); ReadMessageButtons(doc, out.messageButtons); out.messageFlags = ReadUInt32(doc, "messageFlags", out.messageFlags); out.messageDisplayTime = ReadUInt32(doc, "messageDisplayTime", out.messageDisplayTime);
        ReadFormRef(doc, "landTextureSet", out.landTextureSet); out.landFriction = ReadInt32(doc, "landFriction", out.landFriction); out.landRestitution = ReadInt32(doc, "landRestitution", out.landRestitution); ReadFormRef(doc, "landMaterialType", out.landMaterialType); out.landSpecularExponent = ReadInt8(doc, "landSpecularExponent", out.landSpecularExponent); out.landShaderTextureIndex = ReadInt32(doc, "landShaderTextureIndex", out.landShaderTextureIndex); ReadFormRefArray(doc, "landGrasses", out.landGrasses);
        out.soundOutputType = ReadUInt32(doc, "soundOutputType", out.soundOutputType); out.soundOutputFlags = ReadUInt32(doc, "soundOutputFlags", out.soundOutputFlags); out.soundOutputReverbSend = ReadUInt8(doc, "soundOutputReverbSend", out.soundOutputReverbSend); out.soundOutputMinDistance = ReadFloat(doc, "soundOutputMinDistance", out.soundOutputMinDistance); out.soundOutputMaxDistance = ReadFloat(doc, "soundOutputMaxDistance", out.soundOutputMaxDistance); ReadNumberArray(doc, "soundOutputCurve", out.soundOutputCurve); ReadNumberArray(doc, "soundOutputSpeakers", out.soundOutputSpeakers);
        out.lensFlareFadeDistanceRadiusScale = ReadFloat(doc, "lensFlareFadeDistanceRadiusScale", out.lensFlareFadeDistanceRadiusScale); out.lensFlareColorInfluence = ReadFloat(doc, "lensFlareColorInfluence", out.lensFlareColorInfluence); ReadDebrisEntries(doc, out.debrisEntries);
        if (doc.HasMember("imageModifierAnimatable") && doc["imageModifierAnimatable"].IsBool()) out.imageModifierAnimatable = doc["imageModifierAnimatable"].GetBool(); out.imageModifierDuration = ReadFloat(doc, "imageModifierDuration", out.imageModifierDuration); ReadNumberArray(doc, "imageModifierHDR", out.imageModifierHDR); ReadNumberArray(doc, "imageModifierCinematic", out.imageModifierCinematic); out.imageModifierTintColor = ReadUInt32(doc, "imageModifierTintColor", out.imageModifierTintColor); out.imageModifierBlurRadius = ReadUInt32(doc, "imageModifierBlurRadius", out.imageModifierBlurRadius); out.imageModifierDoubleVisionStrength = ReadUInt32(doc, "imageModifierDoubleVisionStrength", out.imageModifierDoubleVisionStrength); out.imageModifierRadialBlurStrength = ReadUInt32(doc, "imageModifierRadialBlurStrength", out.imageModifierRadialBlurStrength); out.imageModifierRadialBlurRampUp = ReadUInt32(doc, "imageModifierRadialBlurRampUp", out.imageModifierRadialBlurRampUp); out.imageModifierRadialBlurStart = ReadUInt32(doc, "imageModifierRadialBlurStart", out.imageModifierRadialBlurStart); if (doc.HasMember("imageModifierUseTargetForRadialBlur") && doc["imageModifierUseTargetForRadialBlur"].IsBool()) out.imageModifierUseTargetForRadialBlur = doc["imageModifierUseTargetForRadialBlur"].GetBool(); ReadNumberArray(doc, "imageModifierRadialBlurCenter", out.imageModifierRadialBlurCenter); out.imageModifierDofStrength = ReadUInt32(doc, "imageModifierDofStrength", out.imageModifierDofStrength); out.imageModifierDofDistance = ReadUInt32(doc, "imageModifierDofDistance", out.imageModifierDofDistance); out.imageModifierDofRange = ReadUInt32(doc, "imageModifierDofRange", out.imageModifierDofRange); if (doc.HasMember("imageModifierDofUseTarget") && doc["imageModifierDofUseTarget"].IsBool()) out.imageModifierDofUseTarget = doc["imageModifierDofUseTarget"].GetBool(); out.imageModifierDofFlags = ReadUInt32(doc, "imageModifierDofFlags", out.imageModifierDofFlags); out.imageModifierRadialBlurRampDown = ReadUInt32(doc, "imageModifierRadialBlurRampDown", out.imageModifierRadialBlurRampDown); out.imageModifierRadialBlurDownStart = ReadUInt32(doc, "imageModifierRadialBlurDownStart", out.imageModifierRadialBlurDownStart); out.imageModifierFadeColor = ReadUInt32(doc, "imageModifierFadeColor", out.imageModifierFadeColor); out.imageModifierMotionBlurStrength = ReadUInt32(doc, "imageModifierMotionBlurStrength", out.imageModifierMotionBlurStrength);
        ReadFormRef(doc, "cameraImageSpaceModifier", out.cameraImageSpaceModifier); out.cameraAction = ReadUInt32(doc, "cameraAction", out.cameraAction); out.cameraLocation = ReadUInt32(doc, "cameraLocation", out.cameraLocation); out.cameraTarget = ReadUInt32(doc, "cameraTarget", out.cameraTarget); out.cameraFlags = ReadUInt32(doc, "cameraFlags", out.cameraFlags); ReadNumberArray(doc, "cameraTiming", out.cameraTiming); ReadFormRefArray(doc, "cameraPathShots", out.cameraPathShots); out.cameraPathFlags = ReadUInt32(doc, "cameraPathFlags", out.cameraPathFlags); ReadFormRef(doc, "cameraPathParent", out.cameraPathParent); ReadFormRef(doc, "cameraPathPrevious", out.cameraPathPrevious);
        ReadFormRef(doc, "talkingVoiceType", out.talkingVoiceType); out.furnitureFlags = ReadUInt32(doc, "furnitureFlags", out.furnitureFlags); out.furnitureWorkbenchType = ReadUInt32(doc, "furnitureWorkbenchType", out.furnitureWorkbenchType); out.furnitureWorkbenchSkill = ReadInt32(doc, "furnitureWorkbenchSkill", out.furnitureWorkbenchSkill); ReadFormRef(doc, "furnitureAssociatedSpell", out.furnitureAssociatedSpell);
        out.weatherFlags = ReadUInt32(doc, "weatherFlags", out.weatherFlags); out.weatherWindSpeed = static_cast<std::uint8_t>(ReadUInt32(doc, "weatherWindSpeed", out.weatherWindSpeed)); out.weatherTransitionDelta = static_cast<std::uint8_t>(ReadUInt32(doc, "weatherTransitionDelta", out.weatherTransitionDelta)); out.weatherSunGlare = static_cast<std::uint8_t>(ReadUInt32(doc, "weatherSunGlare", out.weatherSunGlare)); out.weatherSunDamage = static_cast<std::uint8_t>(ReadUInt32(doc, "weatherSunDamage", out.weatherSunDamage)); ReadNumberArray(doc, "weatherFogData", out.weatherFogData); ReadFormRef(doc, "weatherPrecipitation", out.weatherPrecipitation); ReadFormRef(doc, "weatherReferenceEffect", out.weatherReferenceEffect); ReadFormRef(doc, "weatherLensFlare", out.weatherLensFlare); constexpr std::array weatherImageKeys{ "weatherImageSpaceSunrise", "weatherImageSpaceDay", "weatherImageSpaceSunset", "weatherImageSpaceNight" }; constexpr std::array weatherVolumeKeys{ "weatherVolumetricSunrise", "weatherVolumetricDay", "weatherVolumetricSunset", "weatherVolumetricNight" }; for (std::size_t i = 0; i < 4; ++i) { ReadFormRef(doc, weatherImageKeys[i], out.weatherImageSpaces[i]); ReadFormRef(doc, weatherVolumeKeys[i], out.weatherVolumetricLighting[i]); }
        ReadString(doc, "climateNightSkyModel", out.climateNightSkyModel); ReadString(doc, "climateSunTexture", out.climateSunTexture); ReadString(doc, "climateSunGlareTexture", out.climateSunGlareTexture); ReadNumberArray(doc, "climateTimes", out.climateTimes); out.climateVolatility = static_cast<std::uint8_t>(ReadUInt32(doc, "climateVolatility", out.climateVolatility)); out.climateMoonPhaseLength = static_cast<std::uint8_t>(ReadUInt32(doc, "climateMoonPhaseLength", out.climateMoonPhaseLength));
        if (doc.HasMember("climateWeatherEntries") && doc["climateWeatherEntries"].IsArray()) { out.climateWeatherEntries.clear(); for (const auto& item : doc["climateWeatherEntries"].GetArray()) { if (!item.IsObject()) continue; DynamicForms::ClimateWeatherEntry entry; if (item.HasMember("weather")) entry.weather = ReadFormRefValue(item["weather"]); if (item.HasMember("global")) entry.global = ReadFormRefValue(item["global"]); entry.chance = ReadUInt32(item, "chance", entry.chance); out.climateWeatherEntries.push_back(std::move(entry)); } }
        ReadFormRef(doc, "locationParent", out.locationParent); ReadFormRef(doc, "locationCrimeFaction", out.locationCrimeFaction); ReadFormRef(doc, "locationMusicType", out.locationMusicType); out.locationWorldRadius = ReadFloat(doc, "locationWorldRadius", out.locationWorldRadius);
        out.musicTypeFlags = ReadUInt32(doc, "musicTypeFlags", out.musicTypeFlags); out.musicTypePriority = static_cast<std::uint8_t>(ReadUInt32(doc, "musicTypePriority", out.musicTypePriority)); out.musicTypeDucking = static_cast<std::uint16_t>(ReadUInt32(doc, "musicTypeDucking", out.musicTypeDucking)); out.musicTypeFadeTime = ReadFloat(doc, "musicTypeFadeTime", out.musicTypeFadeTime); ReadFormRefArray(doc, "musicTypeTracks", out.musicTypeTracks);
        ReadString(doc, "musicTrackPath", out.musicTrackPath); ReadString(doc, "musicTrackFinalePath", out.musicTrackFinalePath); if (doc.HasMember("musicTrackCuePoints") && doc["musicTrackCuePoints"].IsArray()) { out.musicTrackCuePoints.clear(); for (const auto& item : doc["musicTrackCuePoints"].GetArray()) if (item.IsNumber()) out.musicTrackCuePoints.push_back(item.GetFloat()); } out.musicTrackLoopBegin = ReadFloat(doc, "musicTrackLoopBegin", out.musicTrackLoopBegin); out.musicTrackLoopEnd = ReadFloat(doc, "musicTrackLoopEnd", out.musicTrackLoopEnd); out.musicTrackLoopCount = ReadUInt32(doc, "musicTrackLoopCount", out.musicTrackLoopCount);
        ReadFormRef(doc, "bodyPartRagdoll", out.bodyPartRagdoll); ReadNumberArray(doc, "volumetricLightingData", out.volumetricLightingData); ReadFormRef(doc, "legacySoundDescriptor", out.legacySoundDescriptor); ReadString(doc, "actorValueAbbreviation", out.actorValueAbbreviation); ReadString(doc, "actorValueEnumName", out.actorValueEnumName); out.actorValueFlags = ReadUInt32(doc, "actorValueFlags", out.actorValueFlags); out.actorValueType = ReadUInt32(doc, "actorValueType", out.actorValueType); ReadStringArray(doc, "actorValueEnumValues", out.actorValueEnumValues); if (doc.HasMember("actorValueHasSkillData") && doc["actorValueHasSkillData"].IsBool()) out.actorValueHasSkillData = doc["actorValueHasSkillData"].GetBool(); ReadNumberArray(doc, "actorValueSkillData", out.actorValueSkillData);
        out.dialogueBranchFlags = ReadUInt32(doc, "dialogueBranchFlags", out.dialogueBranchFlags);
        out.dialogueBranchType = ReadUInt32(doc, "dialogueBranchType", out.dialogueBranchType);
        ReadFormRef(doc, "dialogueBranchQuest", out.dialogueBranchQuest);
        ReadFormRef(doc, "dialogueBranchStartingTopic", out.dialogueBranchStartingTopic);
        out.dialogueTopicFlags = ReadUInt32(doc, "dialogueTopicFlags", out.dialogueTopicFlags);
        out.dialogueTopicType = ReadUInt32(doc, "dialogueTopicType", out.dialogueTopicType);
        out.dialogueTopicSubtype = ReadUInt32(doc, "dialogueTopicSubtype", out.dialogueTopicSubtype);
        out.dialogueTopicPriority =
            static_cast<std::uint8_t>(ReadUInt32(doc, "dialogueTopicPriority", out.dialogueTopicPriority));
        out.dialogueTopicJournalIndex = ReadUInt32(doc, "dialogueTopicJournalIndex", out.dialogueTopicJournalIndex);
        ReadFormRef(doc, "dialogueTopicBranch", out.dialogueTopicBranch);
        ReadFormRef(doc, "dialogueTopicQuest", out.dialogueTopicQuest);
        ReadFormRefArray(doc, "dialogueTopicInfos", out.dialogueTopicInfos);
        ReadFormRef(doc, "dialogueInfoTopic", out.dialogueInfoTopic); ReadFormRef(doc, "dialogueInfoSharedInfo", out.dialogueInfoSharedInfo); out.dialogueInfoIndex = static_cast<std::uint16_t>(ReadUInt32(doc, "dialogueInfoIndex", out.dialogueInfoIndex)); out.dialogueInfoFavorLevel = ReadUInt32(doc, "dialogueInfoFavorLevel", out.dialogueInfoFavorLevel); out.dialogueInfoFlags = ReadUInt32(doc, "dialogueInfoFlags", out.dialogueInfoFlags); out.dialogueInfoResetHours = static_cast<std::uint16_t>(ReadUInt32(doc, "dialogueInfoResetHours", out.dialogueInfoResetHours)); ReadDialogueResponses(doc, out.dialogueResponses);
        out.questFlags=ReadUInt32(doc,"questFlags",out.questFlags);out.questType=ReadUInt32(doc,"questType",out.questType);out.questPriority=ReadInt8(doc,"questPriority",out.questPriority);out.questDelayTime=ReadFloat(doc,"questDelayTime",out.questDelayTime);out.questStoryConditions=ReadConditionArray(doc,"questStoryConditions");ReadFormRefArray(doc,"questTextGlobals",out.questTextGlobals);
        out.sceneFlags=ReadUInt32(doc,"sceneFlags",out.sceneFlags);ReadFormRef(doc,"sceneParentQuest",out.sceneParentQuest);if(doc.HasMember("sceneActors")&&doc["sceneActors"].IsArray()){out.sceneActors.clear();for(const auto& item:doc["sceneActors"].GetArray())if(item.IsUint())out.sceneActors.push_back(item.GetUint());}if(doc.HasMember("sceneActorFlags")&&doc["sceneActorFlags"].IsArray()){out.sceneActorFlags.clear();for(const auto& item:doc["sceneActorFlags"].GetArray())if(item.IsUint())out.sceneActorFlags.push_back(item.GetUint());}if(doc.HasMember("sceneActorBehaviorFlags")&&doc["sceneActorBehaviorFlags"].IsArray()){out.sceneActorBehaviorFlags.clear();for(const auto& item:doc["sceneActorBehaviorFlags"].GetArray())if(item.IsUint())out.sceneActorBehaviorFlags.push_back(item.GetUint());}
        ReadFormRef(doc,"storyParent",out.storyParent);ReadFormRef(doc,"storyPreviousSibling",out.storyPreviousSibling);out.storyMaxQuests=ReadUInt32(doc,"storyMaxQuests",out.storyMaxQuests);out.storyNodeFlags=ReadUInt32(doc,"storyNodeFlags",out.storyNodeFlags);out.storyQuestFlags=ReadUInt32(doc,"storyQuestFlags",out.storyQuestFlags);ReadFormRefArray(doc,"storyChildren",out.storyChildren);out.storyNumQuestsToStart=ReadUInt32(doc,"storyNumQuestsToStart",out.storyNumQuestsToStart);ReadString(doc,"storyEventId",out.storyEventId);
        out.packageFlags = ReadUInt32(doc, "packageFlags", out.packageFlags);
        out.packageType = ReadUInt32(doc, "packageType", out.packageType);
        out.packageProcedureType = ReadUInt32(doc, "packageProcedureType", out.packageProcedureType);
        out.packageInterruptType = ReadUInt32(doc, "packageInterruptType", out.packageInterruptType);
        out.packagePreferredSpeed = ReadUInt32(doc, "packagePreferredSpeed", out.packagePreferredSpeed);
        out.packageInterruptFlags = ReadUInt32(doc, "packageInterruptFlags", out.packageInterruptFlags);
        out.packageSpecificFlags = ReadUInt32(doc, "packageSpecificFlags", out.packageSpecificFlags);
        out.packageIdleFlags = ReadUInt32(doc, "packageIdleFlags", out.packageIdleFlags);
        out.packageIdleTimer = ReadFloat(doc, "packageIdleTimer", out.packageIdleTimer);
        ReadFormRefArray(doc, "packageIdles", out.packageIdles);
        ReadFormRef(doc, "packageTemplate", out.packageTemplate);
        out.packageMonth = ReadInt8(doc, "packageMonth", out.packageMonth);
        out.packageDayOfWeek = ReadInt8(doc, "packageDayOfWeek", out.packageDayOfWeek);
        out.packageDate = ReadInt8(doc, "packageDate", out.packageDate);
        out.packageHour = ReadInt8(doc, "packageHour", out.packageHour);
        out.packageMinute = ReadInt8(doc, "packageMinute", out.packageMinute);
        out.packageDuration = ReadInt32(doc, "packageDuration", out.packageDuration);
        ReadFormRef(doc, "packageCombatStyle", out.packageCombatStyle);
        ReadFormRef(doc, "packageOwnerQuest", out.packageOwnerQuest);
        out.packageLocationType = ReadUInt32(doc, "packageLocationType", out.packageLocationType);
        out.packageLocationRadius = ReadUInt32(doc, "packageLocationRadius", out.packageLocationRadius);
        ReadFormRef(doc, "packageLocationObject", out.packageLocationObject);
        out.packageLocationValue = ReadUInt32(doc, "packageLocationValue", out.packageLocationValue);
        out.packageTargetType = ReadInt32(doc, "packageTargetType", out.packageTargetType);
        ReadFormRef(doc, "packageTargetForm", out.packageTargetForm);
        out.packageTargetAlias = ReadUInt32(doc, "packageTargetAlias", out.packageTargetAlias);
        out.packageTargetValue = ReadInt32(doc, "packageTargetValue", out.packageTargetValue);
        out.raceFlags = ReadUInt32(doc, "raceFlags", out.raceFlags);
        out.raceFlags2 = ReadUInt32(doc, "raceFlags2", out.raceFlags2);
        out.raceSize = ReadUInt32(doc, "raceSize", out.raceSize);
        ReadNumberArray(doc, "raceSkillBoostSkills", out.raceSkillBoostSkills);
        ReadNumberArray(doc, "raceSkillBoostBonuses", out.raceSkillBoostBonuses);
        ReadNumberArray(doc, "raceHeight", out.raceHeight);
        ReadNumberArray(doc, "raceWeight", out.raceWeight);
        ReadNumberArray(doc, "raceStats", out.raceStats);
        ReadString(doc, "raceSkeletonMale", out.raceSkeletonModels[0]);
        ReadString(doc, "raceSkeletonFemale", out.raceSkeletonModels[1]);
        ReadString(doc, "raceBehaviorMale", out.raceBehaviorGraphs[0]);
        ReadString(doc, "raceBehaviorFemale", out.raceBehaviorGraphs[1]);
        ReadFormRef(doc, "raceVoiceMale", out.raceVoiceTypes[0]);
        ReadFormRef(doc, "raceVoiceFemale", out.raceVoiceTypes[1]);
        ReadFormRef(doc, "raceBodyPartData", out.raceBodyPartData);
        ReadFormRef(doc, "raceDecapitateMale", out.raceDecapitateArmors[0]);
        ReadFormRef(doc, "raceDecapitateFemale", out.raceDecapitateArmors[1]);
        ReadFormRef(doc, "raceBloodMaterial", out.raceBloodMaterial);
        ReadFormRef(doc, "raceImpactDataSet", out.raceImpactDataSet);
        ReadFormRef(doc, "raceDismemberBlood", out.raceDismemberBlood);
        ReadFormRef(doc, "raceCorpseOpenSound", out.raceCorpseOpenSound);
        ReadFormRef(doc, "raceCorpseCloseSound", out.raceCorpseCloseSound);
        ReadFormRefArray(doc, "raceEquipSlots", out.raceEquipSlots);
        out.raceValidEquipTypes = ReadUInt32(doc, "raceValidEquipTypes", out.raceValidEquipTypes);
        ReadFormRef(doc, "raceUnarmedEquipSlot", out.raceUnarmedEquipSlot);
        ReadFormRef(doc, "raceMorphRace", out.raceMorphRace);
        ReadFormRef(doc, "raceArmorParentRace", out.raceArmorParentRace);
        constexpr std::array raceMoveKeys{"raceMoveWalk", "raceMoveRun",   "raceMoveSwim",
                                          "raceMoveFly",  "raceMoveSneak", "raceMoveSprint"};
        for (std::size_t i = 0; i < raceMoveKeys.size(); ++i)
            ReadFormRef(doc, raceMoveKeys[i], out.raceMovementTypes[i]);
        ReadString(doc, "raceBodyTextureMale", out.raceBodyTextureModels[0]);
        ReadString(doc, "raceBodyTextureFemale", out.raceBodyTextureModels[1]);
        constexpr std::array raceHeadPartKeys{"raceHeadPartsMale", "raceHeadPartsFemale"};
        constexpr std::array racePresetKeys{"racePresetsMale", "racePresetsFemale"};
        constexpr std::array raceHairColorKeys{"raceHairColorsMale", "raceHairColorsFemale"};
        constexpr std::array raceFaceDetailKeys{"raceFaceDetailsMale", "raceFaceDetailsFemale"};
        constexpr std::array raceDefaultFaceKeys{"raceDefaultFaceMale", "raceDefaultFaceFemale"};
        constexpr std::array raceDefaultHairKeys{"raceDefaultHairMale", "raceDefaultHairFemale"};
        for (std::size_t i = 0; i < 2; ++i) {
            ReadFormRefArray(doc, raceHeadPartKeys[i], out.raceHeadParts[i]);
            ReadFormRefArray(doc, racePresetKeys[i], out.racePresetNPCs[i]);
            ReadFormRefArray(doc, raceHairColorKeys[i], out.raceHairColors[i]);
            ReadFormRefArray(doc, raceFaceDetailKeys[i], out.raceFaceDetailTextures[i]);
            ReadFormRef(doc, raceDefaultFaceKeys[i], out.raceDefaultFaceDetails[i]);
            ReadFormRef(doc, raceDefaultHairKeys[i], out.raceDefaultHairColors[i]);
        }
        ReadNumberArray(doc, "raceMorphFlags", out.raceMorphFlags);
        ReadStringArray(doc, "raceBipedObjectNames", out.raceBipedObjectNames);
        ReadStringArray(doc, "racePhonemeTargets", out.racePhonemeTargets);
        ReadFormRef(doc, "raceAttackRace", out.raceAttackRace);
        out.raceFaceClamp = ReadFloat(doc, "raceFaceClamp", out.raceFaceClamp);
        out.raceFaceClamp2 = ReadFloat(doc, "raceFaceClamp2", out.raceFaceClamp2);
        ReadNumberArray(doc, "raceMountData", out.raceMountData);
        ReadNumberArray(doc, "raceAngularData", out.raceAngularData);
        ReadAdvancedForms(doc, out);
        out.magicEffectFlags = ReadUInt32(doc, "magicEffectFlags", out.magicEffectFlags);
        out.magicEffectBaseCost = ReadFloat(doc, "magicEffectBaseCost", out.magicEffectBaseCost);
        ReadFormRef(doc, "magicEffectAssociatedForm", out.magicEffectAssociatedForm);
        out.magicEffectAssociatedSkill = ReadInt32(doc, "magicEffectAssociatedSkill", out.magicEffectAssociatedSkill);
        out.magicEffectResistVariable = ReadInt32(doc, "magicEffectResistVariable", out.magicEffectResistVariable);
        ReadFormRefArray(doc, "magicEffectCounterEffects", out.magicEffectCounterEffects);
        ReadFormRef(doc, "magicEffectLight", out.magicEffectLight);
        out.magicEffectTaperWeight = ReadFloat(doc, "magicEffectTaperWeight", out.magicEffectTaperWeight);
        ReadFormRef(doc, "magicEffectShader", out.magicEffectShader);
        ReadFormRef(doc, "magicEffectEnchantShader", out.magicEffectEnchantShader);
        out.magicEffectMinimumSkill = ReadInt32(doc, "magicEffectMinimumSkill", out.magicEffectMinimumSkill);
        out.magicEffectSpellmakingArea = ReadInt32(doc, "magicEffectSpellmakingArea", out.magicEffectSpellmakingArea);
        out.magicEffectSpellmakingChargeTime = ReadFloat(doc, "magicEffectSpellmakingChargeTime", out.magicEffectSpellmakingChargeTime);
        out.magicEffectTaperCurve = ReadFloat(doc, "magicEffectTaperCurve", out.magicEffectTaperCurve);
        out.magicEffectTaperDuration = ReadFloat(doc, "magicEffectTaperDuration", out.magicEffectTaperDuration);
        out.magicEffectSecondAVWeight = ReadFloat(doc, "magicEffectSecondAVWeight", out.magicEffectSecondAVWeight);
        out.magicEffectArchetype = ReadInt32(doc, "magicEffectArchetype", out.magicEffectArchetype);
        out.magicEffectPrimaryAV = ReadInt32(doc, "magicEffectPrimaryAV", out.magicEffectPrimaryAV);
        ReadFormRef(doc, "magicEffectProjectile", out.magicEffectProjectile);
        ReadFormRef(doc, "magicEffectExplosion", out.magicEffectExplosion);
        out.magicEffectCastingType = ReadUInt32(doc, "magicEffectCastingType", out.magicEffectCastingType);
        out.magicEffectDelivery = ReadUInt32(doc, "magicEffectDelivery", out.magicEffectDelivery);
        out.magicEffectSecondaryAV = ReadInt32(doc, "magicEffectSecondaryAV", out.magicEffectSecondaryAV);
        ReadFormRef(doc, "magicEffectCastingArt", out.magicEffectCastingArt);
        ReadFormRef(doc, "magicEffectHitEffectArt", out.magicEffectHitEffectArt);
        ReadFormRef(doc, "magicEffectImpactDataSet", out.magicEffectImpactDataSet);
        out.magicEffectSkillUsageMult = ReadFloat(doc, "magicEffectSkillUsageMult", out.magicEffectSkillUsageMult);
        ReadFormRef(doc, "magicEffectDualCastData", out.magicEffectDualCastData);
        out.magicEffectDualCastScale = ReadFloat(doc, "magicEffectDualCastScale", out.magicEffectDualCastScale);
        ReadFormRef(doc, "magicEffectEnchantEffectArt", out.magicEffectEnchantEffectArt);
        ReadFormRef(doc, "magicEffectHitVisuals", out.magicEffectHitVisuals);
        ReadFormRef(doc, "magicEffectEnchantVisuals", out.magicEffectEnchantVisuals);
        ReadFormRef(doc, "magicEffectEquipAbility", out.magicEffectEquipAbility);
        ReadFormRef(doc, "magicEffectImageSpaceMod", out.magicEffectImageSpaceMod);
        ReadFormRef(doc, "magicEffectPerk", out.magicEffectPerk);
        out.magicEffectCastingSoundLevel = ReadUInt32(doc, "magicEffectCastingSoundLevel", out.magicEffectCastingSoundLevel);
        out.magicEffectAIScore = ReadFloat(doc, "magicEffectAIScore", out.magicEffectAIScore);
        out.magicEffectAIDelayTime = ReadFloat(doc, "magicEffectAIDelayTime", out.magicEffectAIDelayTime);
        for (std::size_t i = 0; i < out.magicEffectSounds.size(); ++i) {
            const auto key = std::format("magicEffectSound{}", i);
            ReadFormRef(doc, key.c_str(), out.magicEffectSounds[i]);
        }
        ReadString(doc, "magicItemDescription", out.magicItemDescription);
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

        NormalizePerkForm(out);
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

    std::int32_t ReadInt32(const rapidjson::Value& doc, const char* key, const std::int32_t fallback) {
        if (!doc.HasMember(key) || !doc[key].IsInt()) {
            return fallback;
        }
        return doc[key].GetInt();
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

    std::string PackageIdFromName(const std::string_view name) {
        std::string result;
        result.reserve(name.size());
        bool pendingSeparator = false;
        for (const unsigned char ch : name) {
            if (std::isalnum(ch)) {
                if (pendingSeparator && !result.empty()) {
                    result.push_back('-');
                }
                result.push_back(static_cast<char>(std::tolower(ch)));
                pendingSeparator = false;
            } else {
                pendingSeparator = true;
            }
        }
        return result.empty() ? "dfg-package" : result;
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

    bool IsExternalPatchMetadataKey(const std::string_view key) {
        static constexpr std::array<std::string_view, 9> ignored{
            "schemaVersion",
            "formKind",
            "sourceSignature",
            "editorId",
            "packageName",
            "basePackageName",
            "patchPackageNames",
            "pluginNumber",
            "localId"
        };
        return std::ranges::find(ignored, key) != ignored.end();
    }

    bool BuildExternalChangesDocument(
        const rapidjson::Document& baseline,
        const rapidjson::Document& resolved,
        rapidjson::Document& changes)
    {
        if (!baseline.IsObject() || !resolved.IsObject()) {
            return false;
        }

        changes.SetObject();
        auto& allocator = changes.GetAllocator();
        changes.AddMember("schemaVersion", 1, allocator);
        rapidjson::Value fields(rapidjson::kObjectType);

        for (auto member = resolved.MemberBegin(); member != resolved.MemberEnd(); ++member) {
            const std::string_view key(member->name.GetString(), member->name.GetStringLength());
            if (IsExternalPatchMetadataKey(key)) {
                continue;
            }

            const auto baselineMember = baseline.FindMember(member->name);
            if (baselineMember != baseline.MemberEnd() && baselineMember->value == member->value) {
                continue;
            }

            rapidjson::Value operation(rapidjson::kObjectType);
            operation.AddMember(
                "operation",
                rapidjson::Value(member->value.IsArray() ? "replace" : "set", allocator),
                allocator);
            operation.AddMember("value", rapidjson::Value(member->value, allocator), allocator);
            fields.AddMember(rapidjson::Value(member->name, allocator), operation, allocator);
        }

        for (auto member = baseline.MemberBegin(); member != baseline.MemberEnd(); ++member) {
            const std::string_view key(member->name.GetString(), member->name.GetStringLength());
            if (IsExternalPatchMetadataKey(key) || resolved.HasMember(member->name)) {
                continue;
            }

            rapidjson::Value operation(rapidjson::kObjectType);
            operation.AddMember("operation", "clear", allocator);
            fields.AddMember(rapidjson::Value(member->name, allocator), operation, allocator);
        }

        changes.AddMember("fields", fields, allocator);
        return true;
    }

    std::vector<std::string> ExternalChangeFieldNames(const rapidjson::Document& changes) {
        std::vector<std::string> names;
        if (!changes.IsObject() || !changes.HasMember("fields") || !changes["fields"].IsObject()) {
            return names;
        }
        names.reserve(changes["fields"].MemberCount());
        for (auto member = changes["fields"].MemberBegin(); member != changes["fields"].MemberEnd(); ++member) {
            names.emplace_back(member->name.GetString(), member->name.GetStringLength());
        }
        return names;
    }

    void ApplyExternalArrayOperationChoices(
        const DynamicForms::DynamicForm& form,
        const rapidjson::Document& baseline,
        const rapidjson::Document& resolved,
        rapidjson::Document& changes)
    {
        if (!changes.HasMember("fields") || !changes["fields"].IsObject()) {
            return;
        }

        auto& allocator = changes.GetAllocator();
        for (const auto& [field, operation] : form.externalArrayOperations) {
            if (operation != "merge") {
                continue;
            }

            const auto baselineMember = baseline.FindMember(field.c_str());
            const auto resolvedMember = resolved.FindMember(field.c_str());
            auto changeMember = changes["fields"].FindMember(field.c_str());
            if (baselineMember == baseline.MemberEnd() ||
                resolvedMember == resolved.MemberEnd() ||
                changeMember == changes["fields"].MemberEnd() ||
                !baselineMember->value.IsArray() ||
                !resolvedMember->value.IsArray())
            {
                continue;
            }

            rapidjson::Value additions(rapidjson::kArrayType);
            for (const auto& value : resolvedMember->value.GetArray()) {
                const bool inherited = std::ranges::any_of(
                    baselineMember->value.GetArray(),
                    [&value](const rapidjson::Value& candidate) { return candidate == value; });
                if (!inherited) {
                    additions.PushBack(rapidjson::Value(value, allocator), allocator);
                }
            }

            rapidjson::Value removals(rapidjson::kArrayType);
            for (const auto& value : baselineMember->value.GetArray()) {
                const bool retained = std::ranges::any_of(
                    resolvedMember->value.GetArray(),
                    [&value](const rapidjson::Value& candidate) { return candidate == value; });
                if (!retained) {
                    removals.PushBack(rapidjson::Value(value, allocator), allocator);
                }
            }

            rapidjson::Value merge(rapidjson::kObjectType);
            merge.AddMember("operation", "merge", allocator);
            merge.AddMember("add", additions, allocator);
            merge.AddMember("remove", removals, allocator);
            changeMember->value = std::move(merge);
        }
    }

    void AppendUniqueFields(std::vector<std::string>& target, const std::vector<std::string>& source) {
        for (const auto& field : source) {
            if (std::ranges::find(target, field) == target.end()) {
                target.push_back(field);
            }
        }
    }

    bool RefreshExternalFieldProvenance(DynamicForms::DynamicForm& form) {
        if (!form.externalPatch || form.externalBaselinePayload.empty()) {
            return false;
        }

        rapidjson::Document baseline;
        baseline.Parse(form.externalBaselinePayload.c_str());
        rapidjson::Document resolved;
        rapidjson::Document changes;
        if (baseline.HasParseError() ||
            !Manager::BuildFormDocument(form, resolved) ||
            !BuildExternalChangesDocument(baseline, resolved, changes))
        {
            return false;
        }

        const auto layerFields = ExternalChangeFieldNames(changes);
        form.externalChangedFields = form.externalInheritedChangedFields;
        AppendUniqueFields(form.externalChangedFields, layerFields);
        form.externalConflictingFields = form.externalInheritedConflictingFields;
        for (const auto& field : layerFields) {
            if (std::ranges::find(form.externalInheritedChangedFields, field) !=
                    form.externalInheritedChangedFields.end() &&
                std::ranges::find(form.externalConflictingFields, field) ==
                    form.externalConflictingFields.end())
            {
                form.externalConflictingFields.push_back(field);
            }
        }
        return true;
    }

    void RemoveMatchingArrayValues(rapidjson::Value& target, const rapidjson::Value& removals) {
        if (!target.IsArray() || !removals.IsArray()) {
            return;
        }
        for (auto removal = removals.Begin(); removal != removals.End(); ++removal) {
            for (auto item = target.Begin(); item != target.End();) {
                if (*item == *removal) {
                    item = target.Erase(item);
                } else {
                    ++item;
                }
            }
        }
    }

    void AddUniqueArrayValues(
        rapidjson::Value& target,
        const rapidjson::Value& additions,
        rapidjson::Document::AllocatorType& allocator)
    {
        if (!target.IsArray() || !additions.IsArray()) {
            return;
        }
        for (auto addition = additions.Begin(); addition != additions.End(); ++addition) {
            const bool exists = std::ranges::any_of(target.GetArray(), [&addition](const rapidjson::Value& item) {
                return item == *addition;
            });
            if (!exists) {
                target.PushBack(rapidjson::Value(*addition, allocator), allocator);
            }
        }
    }

    bool ApplyExternalChangesDocument(
        rapidjson::Document& resolved,
        const rapidjson::Document& changes,
        const std::string_view context)
    {
        if (!resolved.IsObject() || !changes.IsObject() ||
            !changes.HasMember("fields") || !changes["fields"].IsObject())
        {
            logger::warn("External patch '{}' does not contain a valid fields object.", context);
            return false;
        }

        auto& allocator = resolved.GetAllocator();
        for (auto member = changes["fields"].MemberBegin(); member != changes["fields"].MemberEnd(); ++member) {
            if (!member->value.IsObject() ||
                !member->value.HasMember("operation") ||
                !member->value["operation"].IsString())
            {
                logger::warn("External patch '{}' field '{}' has no valid operation.", context, member->name.GetString());
                return false;
            }

            const std::string_view operation = member->value["operation"].GetString();
            auto target = resolved.FindMember(member->name);
            if (operation == "clear") {
                if (target != resolved.MemberEnd()) {
                    resolved.RemoveMember(target);
                }
                continue;
            }

            if (operation == "set" || operation == "replace") {
                if (!member->value.HasMember("value")) {
                    logger::warn("External patch '{}' field '{}' has no value.", context, member->name.GetString());
                    return false;
                }
                if (target == resolved.MemberEnd()) {
                    resolved.AddMember(
                        rapidjson::Value(member->name, allocator),
                        rapidjson::Value(member->value["value"], allocator),
                        allocator);
                } else {
                    target->value.CopyFrom(member->value["value"], allocator);
                }
                continue;
            }

            if (operation == "merge" || operation == "add" || operation == "remove") {
                if (target == resolved.MemberEnd()) {
                    resolved.AddMember(
                        rapidjson::Value(member->name, allocator),
                        rapidjson::Value(rapidjson::kArrayType),
                        allocator);
                    target = resolved.FindMember(member->name);
                }
                if (!target->value.IsArray()) {
                    logger::warn("External patch '{}' field '{}' cannot use '{}' on a non-array value.",
                        context, member->name.GetString(), operation);
                    return false;
                }
                if ((operation == "merge" || operation == "remove") &&
                    member->value.HasMember("remove"))
                {
                    RemoveMatchingArrayValues(target->value, member->value["remove"]);
                }
                if ((operation == "merge" || operation == "add") &&
                    member->value.HasMember("add"))
                {
                    AddUniqueArrayValues(target->value, member->value["add"], allocator);
                }
                continue;
            }

            logger::warn("External patch '{}' field '{}' uses unknown operation '{}'.",
                context, member->name.GetString(), operation);
            return false;
        }
        return true;
    }

    bool SupportsExternalRuntimePatch(const DynamicForms::FormKind kind) {
        using FK = DynamicForms::FormKind;
        switch (kind) {
        case FK::Global:
        case FK::FormList:
        case FK::EquipSlot:
        case FK::VoiceType:
        case FK::Outfit:
        case FK::ArmorType:
        case FK::Armor:
        case FK::Book:
        case FK::Misc:
        case FK::Key:
        case FK::SoulGem:
        case FK::MaterialType:
        case FK::Ammo:
        case FK::Weapon:
        case FK::AlchemyItem:
        case FK::Ingredient:
        case FK::Spell:
        case FK::MagicEffect:
        case FK::Enchantment:
        case FK::Scroll:
        case FK::Projectile:
        case FK::Color:
        case FK::ArtObject:
        case FK::Perk:
        case FK::HeadPart:
        case FK::SoundDescriptor:
        case FK::Light:
        case FK::Explosion:
        case FK::Activator:
        case FK::EffectShader:
        case FK::NPC:
        case FK::TextureSet:
        case FK::Hazard:
        case FK::ImpactData:
        case FK::ReferenceEffect:
        case FK::DualCastData:
        case FK::Static:
        case FK::MovableStatic:
        case FK::Door:
        case FK::CombatStyle:
        case FK::SoundCategory:
        case FK::Class:
        case FK::Flora:
        case FK::Tree:
        case FK::ConstructibleObject:
        case FK::Container:
        case FK::ImpactDataSet:
        case FK::Footstep:
        case FK::FootstepSet:
        case FK::ReverbParameters:
        case FK::AcousticSpace:
        case FK::Apparatus:
        case FK::StaticCollection:
        case FK::Grass:
        case FK::IdleMarker:
        case FK::EncounterZone:
        case FK::Relationship:
        case FK::AssociationType:
        case FK::MovementType:
        case FK::WordOfPower:
        case FK::Water:
        case FK::ImageSpace:
        case FK::LightingTemplate:
        case FK::Shout:
        case FK::LeveledItem:
        case FK::LeveledNPC:
        case FK::LeveledSpell:
        case FK::Action:
        case FK::MenuIcon:
        case FK::Eyes:
        case FK::Note:
        case FK::AnimatedObject:
        case FK::LoadScreen:
        case FK::ShaderParticleGeometry:
        case FK::AddonNode:
        case FK::Faction:
        case FK::IdleAnimation:
        case FK::MaterialObject:
        case FK::Message:
        case FK::LandTexture:
        case FK::SoundOutputModel:
        case FK::LensFlare:
        case FK::Debris:
        case FK::ImageSpaceModifier:
        case FK::CameraShot:
        case FK::CameraPath:
        case FK::TalkingActivator:
        case FK::Furniture:
        case FK::Weather:
        case FK::Climate:
        case FK::Location:
        case FK::MusicType:
        case FK::MusicTrack:
        case FK::BodyPartData:
        case FK::VolumetricLighting:
        case FK::Sound:
            return true;
        default:
            return false;
        }
    }

    RE::TESForm* LookupExternalPatchTarget(
        const std::string_view sourcePlugin,
        const std::uint32_t localFormId)
    {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler || sourcePlugin.empty() || localFormId == 0) {
            return nullptr;
        }
        return dataHandler->LookupForm(localFormId, sourcePlugin);
    }

    std::string ExternalPatchIdentity(
        const std::string_view sourcePlugin,
        const std::uint32_t localFormId,
        const DynamicForms::FormKind kind)
    {
        return std::format("{}|{:X}|{}", NormalizeEditorId(sourcePlugin), localFormId, ToString(kind));
    }

    auto FindExternalPatchForm(
        const std::string_view sourcePlugin,
        const std::uint32_t localFormId,
        const DynamicForms::FormKind kind)
    {
        const auto identity = ExternalPatchIdentity(sourcePlugin, localFormId, kind);
        return std::ranges::find_if(forms, [&identity](const DynamicForms::DynamicForm& form) {
            return form.externalPatch &&
                ExternalPatchIdentity(form.externalSourcePlugin, form.externalLocalId, form.kind) == identity;
        });
    }

    bool HasExternalField(
        const std::vector<std::string>& fields,
        const std::string_view field)
    {
        return std::ranges::find(fields, field) != fields.end();
    }

    bool HasAnyExternalField(
        const std::vector<std::string>& fields,
        const std::initializer_list<std::string_view> candidates)
    {
        return std::ranges::any_of(candidates, [&fields](const std::string_view field) {
            return HasExternalField(fields, field);
        });
    }

    bool ConfigureExternalPerk(
        RE::TESForm* tesForm,
        const DynamicForms::DynamicForm& form,
        const std::vector<std::string>& fields)
    {
        auto* perk = tesForm ? tesForm->As<RE::BGSPerk>() : nullptr;
        if (!perk) {
            return false;
        }

        std::vector<std::string> validationErrors;
        if (!ValidatePerkForm(form, validationErrors)) {
            logger::warn(
                "External perk patch '{}' was not applied: {}",
                form.editorId,
                JoinValidationErrors(validationErrors));
            return false;
        }

        if (HasExternalField(fields, "fullName")) perk->fullName = form.fullName.c_str();
        if (HasExternalField(fields, "trait")) perk->data.trait = form.trait;
        if (HasExternalField(fields, "level")) perk->data.level = form.level;
        if (HasExternalField(fields, "numRanks")) perk->data.numRanks = form.numRanks;
        if (HasExternalField(fields, "playable")) perk->data.playable = form.playable;
        if (HasExternalField(fields, "hidden")) perk->data.hidden = form.hidden;
        if (HasExternalField(fields, "nextPerk")) {
            perk->nextPerk = ResolveAs<RE::BGSPerk>(form.nextPerk);
        }
        if (HasExternalField(fields, "conditions")) {
            ApplyConditions(perk->perkConditions, form.conditions);
        }
        if (HasExternalField(fields, "entries")) {
            const auto loadedActorSnapshots = RemoveAppliedAbilityEntries(*perk);
            ClearPerkEntries(*perk);
            for (const auto& entry : form.entries) {
                if (auto* perkEntry = CreatePerkEntry(*perk, entry)) {
                    perk->perkEntries.push_back(perkEntry);
                }
            }
            ApplyUpdatedAbilityAndQuestEntries(*perk, loadedActorSnapshots);
        }

        logger::info(
            "Applied {} sparse external PERK field(s) to '{}'.",
            fields.size(),
            form.editorId);
        return true;
    }

    bool ConfigureExternalSoundDescriptor(
        RE::TESForm* tesForm,
        const DynamicForms::DynamicForm& form,
        const std::vector<std::string>& fields)
    {
        auto* soundForm = tesForm ? tesForm->As<RE::BGSSoundDescriptorForm>() : nullptr;
        if (!soundForm) {
            return false;
        }
        auto* soundDef = soundForm->soundDescriptor ?
            static_cast<RE::BGSStandardSoundDef*>(soundForm->soundDescriptor) :
            CreateStandardSoundDefObject();
        if (!soundDef) {
            return false;
        }
        soundForm->soundDescriptor = soundDef;

        if (HasExternalField(fields, "category")) {
            soundDef->category = ResolveAs<RE::BGSSoundCategory>(form.category);
        }
        if (HasExternalField(fields, "alternateSound")) {
            auto* alternate = ResolveConfigForm(form.alternateSound);
            soundDef->alternateSoundFormID = alternate ? alternate->GetFormID() : 0;
        }
        if (HasExternalField(fields, "outputModel")) {
            soundDef->outputModel = ResolveAs<RE::BGSSoundOutput>(form.outputModel);
        }
        if (HasExternalField(fields, "soundFiles")) {
            soundDef->soundFiles.clear();
            for (const auto& file : form.soundFiles) {
                if (file.empty()) {
                    continue;
                }
                RE::BSResource::ID fileId;
                fileId.GenerateFromPath(file.c_str());
                soundDef->soundFiles.push_back(fileId);
            }
        }
        if (HasExternalField(fields, "frequencyShift")) {
            soundDef->soundCharacteristics.frequencyShift = form.frequencyShift;
        }
        if (HasExternalField(fields, "frequencyVariance")) {
            soundDef->soundCharacteristics.frequencyVariance = form.frequencyVariance;
        }
        if (HasExternalField(fields, "priority")) {
            soundDef->soundCharacteristics.priority = form.priority;
        }
        if (HasExternalField(fields, "dbVariance")) {
            soundDef->soundCharacteristics.dbVariance = form.dbVariance;
        }
        if (HasExternalField(fields, "staticAttenuation")) {
            soundDef->soundCharacteristics.staticAttenuation =
                static_cast<std::uint16_t>(
                    std::clamp(form.staticAttenuation * 100.0F, 0.0F, 65535.0F));
        }
        if (HasExternalField(fields, "looping")) {
            soundDef->lengthCharacteristics.looping =
                static_cast<RE::BGSStandardSoundDef::LengthCharacteristics::Looping>(
                    form.looping);
        }
        if (HasExternalField(fields, "rumbleSendValue")) {
            soundDef->lengthCharacteristics.rumbleSendValue = form.rumbleSendValue;
        }
        if (HasExternalField(fields, "conditions")) {
            if (!form.conditions.empty()) {
                if (!soundDef->conditions) {
                    soundDef->conditions = new RE::TESCondition();
                }
                ApplyConditions(*soundDef->conditions, form.conditions);
            } else if (soundDef->conditions) {
                ClearCondition(*soundDef->conditions);
            }
        }

        logger::info(
            "Applied {} sparse external SNDR field(s) to '{}'.",
            fields.size(),
            form.editorId);
        return true;
    }

    bool ConfigureExternalNPC(
        RE::TESForm* tesForm,
        const DynamicForms::DynamicForm& form,
        const std::vector<std::string>& fields)
    {
        auto* npc = tesForm ? tesForm->As<RE::TESNPC>() : nullptr;
        if (!npc) {
            return false;
        }

        if (HasExternalField(fields, "fullName")) npc->fullName = form.fullName.c_str();
        if (HasExternalField(fields, "height")) npc->height = form.height;
        if (HasExternalField(fields, "weight")) npc->weight = form.weight;
        if (HasAnyExternalField(fields, { "red", "green", "blue", "alpha" })) {
            auto color = npc->bodyTintColor;
            if (HasExternalField(fields, "red")) color.red = form.red;
            if (HasExternalField(fields, "green")) color.green = form.green;
            if (HasExternalField(fields, "blue")) color.blue = form.blue;
            if (HasExternalField(fields, "alpha")) color.alpha = form.alpha;
            npc->bodyTintColor = color;
        }

        const std::array flagFields{
            std::pair{ std::string_view("female"), RE::ACTOR_BASE_DATA::Flag::kFemale },
            std::pair{ std::string_view("oppositeGenderAnim"), RE::ACTOR_BASE_DATA::Flag::kOppositeGenderAnims },
            std::pair{ std::string_view("essential"), RE::ACTOR_BASE_DATA::Flag::kEssential },
            std::pair{ std::string_view("protected"), RE::ACTOR_BASE_DATA::Flag::kProtected },
            std::pair{ std::string_view("unique"), RE::ACTOR_BASE_DATA::Flag::kUnique },
            std::pair{ std::string_view("calcStats"), RE::ACTOR_BASE_DATA::Flag::kPCLevelMult },
            std::pair{ std::string_view("respawn"), RE::ACTOR_BASE_DATA::Flag::kRespawn },
            std::pair{ std::string_view("doesntAffectStealthMeter"), RE::ACTOR_BASE_DATA::Flag::kDoesntAffectStealthMeter },
            std::pair{ std::string_view("doesntBleed"), RE::ACTOR_BASE_DATA::Flag::kDoesntBleed },
            std::pair{ std::string_view("bleedoutOverrideFlag"), RE::ACTOR_BASE_DATA::Flag::kBleedoutOverride },
            std::pair{ std::string_view("simpleActor"), RE::ACTOR_BASE_DATA::Flag::kSimpleActor },
            std::pair{ std::string_view("noActivation"), RE::ACTOR_BASE_DATA::Flag::kNoActivation },
            std::pair{ std::string_view("ghost"), RE::ACTOR_BASE_DATA::Flag::kIsGhost },
            std::pair{ std::string_view("invulnerable"), RE::ACTOR_BASE_DATA::Flag::kInvulnerable }
        };
        const std::array flagValues{
            form.femaleNpc,
            form.oppositeGenderAnim,
            form.essential,
            form.protectedNpc,
            form.unique,
            form.calcStats,
            form.respawn,
            form.doesntAffectStealthMeter,
            form.doesntBleed,
            form.bleedoutOverrideFlag,
            form.simpleActor,
            form.noActivation,
            form.ghost,
            form.invulnerable
        };
        for (std::size_t index = 0; index < flagFields.size(); ++index) {
            if (HasExternalField(fields, flagFields[index].first)) {
                SetActorBaseFlag(*npc, flagFields[index].second, flagValues[index]);
            }
        }

        if (HasExternalField(fields, "health")) npc->playerSkills.health = form.health;
        if (HasExternalField(fields, "magicka")) npc->playerSkills.magicka = form.magicka;
        if (HasExternalField(fields, "stamina")) npc->playerSkills.stamina = form.stamina;
        if (HasExternalField(fields, "healthOffset")) npc->actorData.healthOffset = form.healthOffset;
        if (HasExternalField(fields, "magickaOffset")) npc->actorData.magickaOffset = form.magickaOffset;
        if (HasExternalField(fields, "staminaOffset")) npc->actorData.staminaOffset = form.staminaOffset;
        if (HasExternalField(fields, "calcMinLevel")) npc->actorData.calcLevelMin = form.calcMinLevel;
        if (HasExternalField(fields, "calcMaxLevel")) npc->actorData.calcLevelMax = form.calcMaxLevel;
        if (HasExternalField(fields, "npcLevel")) npc->actorData.level = form.npcLevel;
        if (HasExternalField(fields, "speedMult")) npc->actorData.speedMult = form.speedMult;
        if (HasExternalField(fields, "dispositionBase")) npc->actorData.baseDisposition = form.dispositionBase;
        if (HasExternalField(fields, "bleedoutOverride")) npc->actorData.bleedoutOverride = form.bleedoutOverride;
        if (HasExternalField(fields, "soundLevel")) npc->soundLevel = static_cast<RE::SOUND_LEVEL>(form.soundLevel);
        if (HasExternalField(fields, "skills")) {
            std::copy(form.skills.begin(), form.skills.end(), npc->playerSkills.values);
        }
        if (HasExternalField(fields, "skillOffsets")) {
            std::copy(form.skillOffsets.begin(), form.skillOffsets.end(), npc->playerSkills.offsets);
        }

        if (HasExternalField(fields, "race")) {
            npc->race = ResolveAs<RE::TESRace>(form.race);
            npc->originalRace = npc->race;
        }
        if (HasExternalField(fields, "skin")) npc->farSkin = ResolveAs<RE::TESObjectARMO>(form.skin);
        if (HasExternalField(fields, "defaultOutfit")) npc->defaultOutfit = ResolveAs<RE::BGSOutfit>(form.defaultOutfit);
        if (HasExternalField(fields, "sleepOutfit")) npc->sleepOutfit = ResolveAs<RE::BGSOutfit>(form.sleepOutfit);
        if (HasExternalField(fields, "voice")) npc->voiceType = ResolveAs<RE::BGSVoiceType>(form.voice);
        if (HasExternalField(fields, "class")) npc->npcClass = ResolveAs<RE::TESClass>(form.npcClass);
        if (HasExternalField(fields, "combatStyle")) npc->combatStyle = ResolveAs<RE::TESCombatStyle>(form.combatStyle);
        if (HasExternalField(fields, "giftFilter")) npc->giftFilter = ResolveAs<RE::BGSListForm>(form.giftFilter);
        if (HasExternalField(fields, "deathItem")) npc->deathItem = ResolveAs<RE::TESLevItem>(form.deathItem);
        if (HasExternalField(fields, "defaultPackageList")) npc->defaultPackList = ResolveAs<RE::BGSListForm>(form.defaultPackageList);
        if (HasExternalField(fields, "crimeFaction")) npc->crimeFaction = ResolveAs<RE::TESFaction>(form.crimeFaction);

        if (HasAnyExternalField(fields, {
                "aiAggression", "aiConfidence", "aiEnergyLevel", "aiMorality", "aiMood",
                "aiAssistance", "aiAggroRadiusBehavior", "aiAggroRadiusWarn",
                "aiAggroRadiusWarnAndAttack", "aiAggroRadiusAttack", "aiNoSlowApproach" }))
        {
            SetAIDataBits(*npc, form);
        }

        if (HasAnyExternalField(fields, { "hairColor", "faceTexture" })) {
            if (!npc->headRelatedData) {
                npc->headRelatedData = new RE::TESNPC::HeadRelatedData();
            }
            if (HasExternalField(fields, "hairColor")) {
                npc->headRelatedData->hairColor = ResolveAs<RE::BGSColorForm>(form.hairColor);
            }
            if (HasExternalField(fields, "faceTexture")) {
                npc->headRelatedData->faceDetails = ResolveAs<RE::BGSTextureSet>(form.faceTexture);
            }
        }

        if (HasExternalField(fields, "packages")) {
            npc->aiPackages.packages.clear();
            RE::BSSimpleList<RE::TESPackage*>::size_type index = 0;
            for (const auto& ref : form.packages) {
                if (auto* package = ResolveAs<RE::TESPackage>(ref)) {
                    npc->aiPackages.packages.insert_at(index++, package);
                }
            }
        }

        if (HasExternalField(fields, "factions")) {
            npc->factions.clear();
            for (const auto& source : form.npcFactions) {
                if (auto* faction = ResolveAs<RE::TESFaction>(source.form)) {
                    RE::FACTION_RANK rank;
                    rank.faction = faction;
                    rank.rank = static_cast<std::int8_t>(std::clamp(source.rank, -128, 127));
                    npc->factions.push_back(rank);
                }
            }
        }

        if (HasExternalField(fields, "perks")) {
            std::vector<RE::BGSPerk*> oldPerks;
            for (std::uint32_t index = 0; index < npc->perkCount; ++index) {
                if (npc->perks && npc->perks[index].perk) {
                    oldPerks.push_back(npc->perks[index].perk);
                }
            }
            if (!oldPerks.empty()) {
                npc->RemovePerks(oldPerks);
            }
            for (const auto& source : form.npcPerks) {
                if (auto* perk = ResolveAs<RE::BGSPerk>(source.form)) {
                    npc->AddPerk(
                        perk,
                        static_cast<std::int8_t>(std::clamp(source.rank, -128, 127)));
                }
            }
        }

        if (HasExternalField(fields, "spells")) {
            auto* spellList = static_cast<RE::TESSpellList*>(npc);
            if (spellList->actorEffects) {
                std::vector<RE::SpellItem*> oldSpells;
                for (std::uint32_t index = 0; index < spellList->actorEffects->numSpells; ++index) {
                    if (spellList->actorEffects->spells &&
                        spellList->actorEffects->spells[index])
                    {
                        oldSpells.push_back(spellList->actorEffects->spells[index]);
                    }
                }
                if (!oldSpells.empty()) {
                    spellList->actorEffects->RemoveSpells(oldSpells);
                }
            }
            if (!form.spells.empty() && !spellList->actorEffects) {
                spellList->actorEffects = new RE::TESSpellList::SpellData();
            }
            if (spellList->actorEffects) {
                for (const auto& ref : form.spells) {
                    if (auto* spell = ResolveAs<RE::SpellItem>(ref)) {
                        spellList->actorEffects->AddSpell(spell);
                    }
                }
            }
        }

        if (HasExternalField(fields, "headParts")) {
            std::vector<RE::BGSHeadPart*> parts;
            std::set<RE::BGSHeadPart*> visited;
            std::function<void(RE::BGSHeadPart*)> addPart = [&](RE::BGSHeadPart* part) {
                if (!part || !visited.insert(part).second) {
                    return;
                }
                parts.push_back(part);
                for (auto* extra : part->extraParts) {
                    addPart(extra);
                }
            };
            for (const auto& ref : form.headParts) {
                addPart(ResolveAs<RE::BGSHeadPart>(ref));
            }
            const auto count = std::min<std::size_t>(parts.size(), 127);
            auto** values = count > 0 ? RE::calloc<RE::BGSHeadPart*>(count) : nullptr;
            for (std::size_t index = 0; index < count; ++index) {
                values[index] = parts[index];
            }
            npc->headParts = values;
            npc->numHeadParts = static_cast<std::int8_t>(count);
        }

        if (HasExternalField(fields, "tintLayers")) {
            if (!npc->tintLayers) {
                npc->tintLayers = new RE::BSTArray<RE::TESNPC::Layer*>();
            } else {
                npc->tintLayers->clear();
            }
            for (const auto& source : form.tintLayers) {
                auto* layer = new RE::TESNPC::Layer();
                layer->tintIndex = source.index;
                layer->preset = source.preset;
                layer->interpolationValue = static_cast<std::uint16_t>(
                    std::clamp(source.interpolation * 100.0F, 0.0F, 65535.0F));
                layer->tintColor =
                    RE::Color(source.red, source.green, source.blue, source.alpha);
                npc->tintLayers->push_back(layer);
            }
        }

        if (HasAnyExternalField(fields, { "faceMorphs", "faceParts" })) {
            auto* faceData = new RE::TESNPC::FaceData();
            if (npc->faceData) {
                *faceData = *npc->faceData;
            }
            npc->faceData = faceData;
            if (HasExternalField(fields, "faceMorphs")) {
                std::copy(form.faceMorphs.begin(), form.faceMorphs.end(), npc->faceData->morphs);
            }
            if (HasExternalField(fields, "faceParts")) {
                std::copy(form.faceParts.begin(), form.faceParts.end(), npc->faceData->parts);
            }
        }

        logger::info(
            "Applied {} sparse external NPC_ field(s) to '{}'.",
            fields.size(),
            form.editorId);
        return true;
    }

    bool ParseNormalizedExternalFormId(
        const std::string_view normalized,
        std::string& sourcePlugin,
        std::uint32_t& localFormId)
    {
        const auto separator = normalized.rfind('|');
        if (separator == std::string_view::npos || separator == 0 || separator + 1 >= normalized.size()) {
            return false;
        }

        sourcePlugin.assign(normalized.substr(0, separator));
        try {
            localFormId = static_cast<std::uint32_t>(
                std::stoul(std::string(normalized.substr(separator + 1)), nullptr, 16));
        } catch (...) {
            localFormId = 0;
        }
        return !sourcePlugin.empty() && localFormId != 0;
    }

    bool BuildExternalBaseline(
        RE::TESForm& target,
        const DynamicForms::FormKind kind,
        const std::string_view packageName,
        DynamicForms::DynamicForm& form,
        std::string& baselinePayload)
    {
        if (!SupportsExternalRuntimePatch(kind) ||
            target.GetFormType() != static_cast<RE::FormType>(FormTypeForKind(kind)))
        {
            return false;
        }

        const auto normalizedId = FormUtil::NormalizeFormID(std::addressof(target));
        std::string sourcePlugin;
        std::uint32_t localFormId = 0;
        if (!ParseNormalizedExternalFormId(normalizedId, sourcePlugin, localFormId)) {
            logger::warn("External patch target {:08X} has no stable plugin-local identity.", target.GetFormID());
            return false;
        }

        auto editorId = FormUtil::GetEditorIDSafe(std::addressof(target));
        if (editorId.empty()) {
            editorId = std::format("{}_{:X}", ToSignature(kind), localFormId);
        }

        DynamicForms::DynamicForm captured;
        captured.kind = kind;
        captured.editorId = editorId;
        DynamicForms::FormRef targetRef;
        targetRef.editorID = FormUtil::GetEditorIDSafe(std::addressof(target));
        targetRef.formID = normalizedId;
        if (!Manager::PopulateFormFromGameTemplate(captured, targetRef)) {
            return false;
        }

        captured.externalPatch = true;
        captured.externalSourcePlugin = sourcePlugin;
        captured.externalLocalId = localFormId;
        captured.externalWinningPlugin =
            target.GetFile() ? std::string(target.GetFile()->GetFilename()) : sourcePlugin;
        captured.externalEditPackage = std::string(packageName);
        captured.packageName = std::string(packageName);
        captured.basePackageName = sourcePlugin;
        captured.patchPackageNames = { std::string(packageName) };
        captured.pluginNumber = 0;
        captured.localId = 0;
        captured.dirty = false;

        rapidjson::Document baseline;
        if (!Manager::BuildFormDocument(captured, baseline)) {
            return false;
        }
        baselinePayload = JsonString(baseline);
        captured.externalBaselinePayload = baselinePayload;
        form = std::move(captured);
        return true;
    }

    bool ApplyExternalResolvedForm(DynamicForms::DynamicForm& form) {
        if (!form.externalPatch || !SupportsExternalRuntimePatch(form.kind)) {
            return false;
        }

        auto* target = LookupExternalPatchTarget(form.externalSourcePlugin, form.externalLocalId);
        if (!target ||
            target->GetFormType() != static_cast<RE::FormType>(FormTypeForKind(form.kind)))
        {
            logger::warn(
                "External patch target '{}|{:X}' is missing or no longer has type {}.",
                form.externalSourcePlugin,
                form.externalLocalId,
                ToString(form.kind));
            return false;
        }

        const auto originalEditorId = FormUtil::GetEditorIDSafe(target);
        const auto& fieldsToApply = form.externalPendingApplyFields.empty() ?
            form.externalChangedFields :
            form.externalPendingApplyFields;
        if (fieldsToApply.empty()) {
            logger::debug(
                "External patch '{}|{:X}' has no field changes to apply.",
                form.externalSourcePlugin,
                form.externalLocalId);
            return true;
        }

        DynamicForms::DynamicForm currentValues;
        currentValues.kind = form.kind;
        currentValues.editorId = originalEditorId.empty() ?
            std::format("{}_{:X}", ToSignature(form.kind), form.externalLocalId) :
            originalEditorId;
        DynamicForms::FormRef targetRef;
        targetRef.editorID = originalEditorId;
        targetRef.formID = std::format("{}|{:X}", form.externalSourcePlugin, form.externalLocalId);
        if (!Manager::PopulateFormFromGameTemplate(currentValues, targetRef)) {
            logger::warn(
                "Could not recapture external patch target '{}|{:X}' before applying sparse fields.",
                form.externalSourcePlugin,
                form.externalLocalId);
            return false;
        }

        rapidjson::Document currentDocument;
        rapidjson::Document resolvedDocument;
        if (!Manager::BuildFormDocument(currentValues, currentDocument) ||
            !Manager::BuildFormDocument(form, resolvedDocument)) {
            return false;
        }

        auto& allocator = currentDocument.GetAllocator();
        for (const auto& field : fieldsToApply) {
            const auto resolvedMember = resolvedDocument.FindMember(field.c_str());
            auto currentMember = currentDocument.FindMember(field.c_str());
            if (resolvedMember == resolvedDocument.MemberEnd()) {
                if (currentMember != currentDocument.MemberEnd()) {
                    currentDocument.RemoveMember(currentMember);
                }
                continue;
            }
            if (currentMember == currentDocument.MemberEnd()) {
                currentDocument.AddMember(
                    rapidjson::Value(field.c_str(), allocator),
                    rapidjson::Value(resolvedMember->value, allocator),
                    allocator);
            } else {
                currentMember->value.CopyFrom(resolvedMember->value, allocator);
            }
        }

        DynamicForms::DynamicForm runtimeValues;
        if (!ReadFormDocument(
                currentDocument,
                std::format("external runtime apply {}|{:X}", form.externalSourcePlugin, form.externalLocalId),
                currentValues.editorId,
                runtimeValues))
        {
            return false;
        }
        runtimeValues.editorId = originalEditorId;
        bool configured = false;
        if (runtimeValues.kind == DynamicForms::FormKind::Perk) {
            configured = ConfigureExternalPerk(target, runtimeValues, fieldsToApply);
        } else if (runtimeValues.kind == DynamicForms::FormKind::NPC) {
            configured = ConfigureExternalNPC(target, runtimeValues, fieldsToApply);
        } else if (runtimeValues.kind == DynamicForms::FormKind::SoundDescriptor) {
            configured =
                ConfigureExternalSoundDescriptor(target, runtimeValues, fieldsToApply);
        } else {
            configured = ConfigureForm(target, runtimeValues);
        }
        if (!configured) {
            logger::warn(
                "Could not apply external runtime patch '{}' to {}|{:X}.",
                form.editorId,
                form.externalSourcePlugin,
                form.externalLocalId);
            return false;
        }
        form.externalPendingApplyFields.clear();

        std::string layers;
        for (const auto& package : form.patchPackageNames) {
            if (!layers.empty()) {
                layers += ", ";
            }
            layers += package;
        }
        logger::info(
            "Applied external runtime patch '{}' to {}|{:X} ({}) through packages [{}].",
            form.editorId,
            form.externalSourcePlugin,
            form.externalLocalId,
            ToString(form.kind),
            layers);
        return true;
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

    RE::TESTopicInfo::TESResponseList* GetDynamicResponseList(RE::TESTopicInfo* info, RE::TESTopicInfo::TESResponseList* list) {
        const auto found = dynamicDialogueResponses.find(info);
        if (found == dynamicDialogueResponses.end()) return originalGetResponseList(info, list);
        if (info->dataInfo) {
            if (const auto shared = dynamicDialogueResponses.find(info->dataInfo); shared != dynamicDialogueResponses.end()) {
                if (!list) list = new RE::TESTopicInfo::TESResponseList{};
                list->head = shared->second;
                return list;
            }
            return originalGetResponseList(info->dataInfo, list);
        }
        if (!list) list = new RE::TESTopicInfo::TESResponseList{};
        list->head = found->second;
        return list;
    }

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

    bool EnsureTableColumn(
        sqlite3* db,
        const std::string_view table,
        const std::string_view column,
        const std::string_view declaration,
        const std::string_view context)
    {
        SqliteStatement statement;
        const auto pragma = std::format("PRAGMA table_info({});", table);
        if (!PrepareSql(db, pragma.c_str(), statement, context)) {
            return false;
        }
        while (sqlite3_step(statement.handle) == SQLITE_ROW) {
            const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 1));
            if (name && column == name) {
                return true;
            }
        }
        sqlite3_finalize(statement.handle);
        statement.handle = nullptr;
        const auto alter = std::format("ALTER TABLE {} ADD COLUMN {} {};", table, column, declaration);
        return ExecSql(db, alter.c_str(), context);
    }

    bool WritePackageManifest(const std::string_view packageName) {
        std::error_code ec;
        std::filesystem::create_directories(PackageDirectory(packageName), ec);
        if (ec) {
            logger::warn("Could not create package directory '{}': {}", PackageDirectory(packageName).string(), ec.message());
            return false;
        }

        const auto manifestPath = PackageManifestPath(packageName);
        rapidjson::Document doc;
        bool changed = false;
        if (std::filesystem::exists(manifestPath)) {
            std::ifstream stream(manifestPath);
            if (!stream.is_open()) {
                logger::warn("Could not read package manifest '{}'.", manifestPath.string());
                return false;
            }
            rapidjson::IStreamWrapper wrapper(stream);
            doc.ParseStream(wrapper);
            if (doc.HasParseError() || !doc.IsObject()) {
                logger::warn("Invalid package manifest '{}'.", manifestPath.string());
                return false;
            }
        } else {
            doc.SetObject();
            changed = true;
        }

        auto& allocator = doc.GetAllocator();
        const auto ensureString = [&doc, &allocator, &changed](
                                      const char* key,
                                      const std::string_view value) {
            if (doc.HasMember(key)) {
                return;
            }
            doc.AddMember(
                rapidjson::Value(key, allocator),
                rapidjson::Value(
                    value.data(),
                    static_cast<rapidjson::SizeType>(value.size()),
                    allocator),
                allocator);
            changed = true;
        };
        const auto ensureArray = [&doc, &allocator, &changed](const char* key) {
            if (doc.HasMember(key)) {
                return;
            }
            doc.AddMember(
                rapidjson::Value(key, allocator),
                rapidjson::Value(rapidjson::kArrayType),
                allocator);
            changed = true;
        };

        if (!doc.HasMember("schemaVersion")) {
            doc.AddMember("schemaVersion", 1, allocator);
            changed = true;
        }
        const auto packageId = PackageIdFromName(packageName);
        ensureString("packageId", packageId);
        ensureString("displayName", packageName);
        ensureString("version", "1.0.0");
        if (!doc.HasMember("enabled")) {
            doc.AddMember("enabled", true, allocator);
            changed = true;
        }
        if (!doc.HasMember("priority")) {
            doc.AddMember("priority", 0, allocator);
            changed = true;
        }
        ensureString("database", "package.db");
        ensureArray("dependencies");
        ensureArray("pluginDependencies");

        if (!changed) {
            return true;
        }

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

        if (!ExecSql(db.handle, "PRAGMA journal_mode=WAL;", packageName) ||
            !ExecSql(db.handle, "PRAGMA synchronous=NORMAL;", packageName) ||
            !ExecSql(db.handle,
                "CREATE TABLE IF NOT EXISTS forms ("
                "editor_id TEXT PRIMARY KEY NOT NULL,"
                "form_kind TEXT NOT NULL,"
                "plugin_number INTEGER NOT NULL DEFAULT 0,"
                "local_id INTEGER NOT NULL DEFAULT 0,"
                "payload TEXT NOT NULL,"
                "updated_at INTEGER NOT NULL DEFAULT (unixepoch())"
                ");",
                packageName) ||
            !ExecSql(db.handle,
                "CREATE TABLE IF NOT EXISTS patches ("
                "target_editor_id TEXT PRIMARY KEY NOT NULL,"
                "target_package TEXT NOT NULL,"
                "form_kind TEXT NOT NULL,"
                "payload TEXT NOT NULL,"
                "updated_at INTEGER NOT NULL DEFAULT (unixepoch())"
                ");",
                packageName) ||
            !ExecSql(db.handle,
                "CREATE TABLE IF NOT EXISTS external_patches ("
                "source_plugin TEXT NOT NULL COLLATE NOCASE,"
                "local_form_id INTEGER NOT NULL,"
                "form_kind TEXT NOT NULL,"
                "editor_id TEXT NOT NULL DEFAULT '',"
                "winning_plugin TEXT NOT NULL DEFAULT '',"
                "changes_json TEXT NOT NULL,"
                "updated_at INTEGER NOT NULL DEFAULT (unixepoch()),"
                "PRIMARY KEY(source_plugin, local_form_id, form_kind)"
                ");",
                packageName)) {
            return false;
        }

        return EnsureTableColumn(db.handle, "forms", "plugin_number", "INTEGER NOT NULL DEFAULT 0", packageName);
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
                "INSERT INTO forms(editor_id, form_kind, plugin_number, local_id, payload, updated_at) "
                "VALUES(?1, ?2, ?3, ?4, ?5, unixepoch()) "
                "ON CONFLICT(editor_id) DO UPDATE SET "
                "form_kind=excluded.form_kind, plugin_number=excluded.plugin_number, local_id=excluded.local_id, payload=excluded.payload, updated_at=excluded.updated_at;",
                statement,
                packageName)) {
            return false;
        }

        sqlite3_bind_text(statement.handle, 1, form.editorId.c_str(), -1, SQLITE_TRANSIENT);
        const auto formKind = ToString(form.kind);
        sqlite3_bind_text(statement.handle, 2, formKind.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(statement.handle, 3, form.pluginNumber);
        sqlite3_bind_int64(statement.handle, 4, form.localId);
        sqlite3_bind_text(statement.handle, 5, payload.c_str(), -1, SQLITE_TRANSIENT);

        const int rc = sqlite3_step(statement.handle);
        if (rc == SQLITE_DONE) {
            return true;
        }

        logger::warn("Could not import dynamic form '{}' in package '{}': {}", form.editorId, packageName, sqlite3_errmsg(db));
        return false;
    }

    struct PackageImportStats
    {
        std::size_t discovered{ 0 };
        std::size_t imported{ 0 };
        std::size_t invalid{ 0 };
        std::size_t persistenceFailures{ 0 };
    };

    PackageImportStats ImportPackageJsonQueue(const std::string& packageName, sqlite3* db) {
        PackageImportStats stats;
        const auto importDir = PackageImportDirectory(packageName);
        if (!std::filesystem::exists(importDir)) {
            return stats;
        }

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(importDir, ec)) {
            if (ec) {
                logger::warn("Could not enumerate package import directory '{}': {}", importDir.string(), ec.message());
                return stats;
            }
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }
            ++stats.discovered;

            std::string payload;
            {
                std::ifstream stream(entry.path());
                if (!stream.is_open()) {
                    logger::warn("Could not open package import file '{}'.", entry.path().string());
                    ++stats.invalid;
                    continue;
                }

                payload.assign(
                    std::istreambuf_iterator<char>(stream),
                    std::istreambuf_iterator<char>());
            }

            rapidjson::Document doc;
            doc.Parse(payload.c_str());
            if (doc.HasParseError()) {
                logger::warn(
                    "Invalid package import JSON '{}': {} at offset {}. The file was left in imports for correction.",
                    entry.path().string(),
                    rapidjson::GetParseError_En(doc.GetParseError()),
                    doc.GetErrorOffset());
                ++stats.invalid;
                continue;
            }
            if (!doc.IsObject()) {
                logger::warn(
                    "Invalid package import JSON '{}': the root value must be an object. The file was left in imports for correction.",
                    entry.path().string());
                ++stats.invalid;
                continue;
            }

            DynamicForms::DynamicForm form;
            if (!ReadFormDocument(doc, entry.path().string(), entry.path().stem().string(), form)) {
                ++stats.invalid;
                continue;
            }

            form.packageName = packageName;
            auto& allocator = doc.GetAllocator();
            SetStringMember(doc, allocator, "editorId", form.editorId);
            SetStringMember(doc, allocator, "packageName", packageName);
            const auto normalizedPayload = JsonString(doc);

            if (!UpsertPackageFormPayload(db, packageName, form, normalizedPayload)) {
                ++stats.persistenceFailures;
                continue;
            }
            ++stats.imported;

            std::error_code removeEc;
            std::filesystem::remove(entry.path(), removeEc);
            if (removeEc) {
                logger::warn("Imported '{}' but could not remove import file: {}", entry.path().string(), removeEc.message());
            } else {
                logger::info("Imported dynamic form '{}' into package '{}'.", form.editorId, packageName);
            }
        }

        if (stats.discovered > 0) {
            logger::info(
                "Package '{}' import queue: {} discovered, {} stored in package.db, {} invalid, {} persistence failures.",
                packageName,
                stats.discovered,
                stats.imported,
                stats.invalid,
                stats.persistenceFailures);
        }
        return stats;
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
                    "INSERT INTO forms(editor_id, form_kind, plugin_number, local_id, payload, updated_at) "
                    "VALUES(?1, ?2, ?3, ?4, ?5, unixepoch()) "
                    "ON CONFLICT(editor_id) DO UPDATE SET "
                    "form_kind=excluded.form_kind, plugin_number=excluded.plugin_number, local_id=excluded.local_id, payload=excluded.payload, updated_at=excluded.updated_at;",
                    statement,
                    packageName)) {
                return false;
            }
            sqlite3_bind_text(statement.handle, 1, form.editorId.c_str(), -1, SQLITE_TRANSIENT);
            const auto formKind = ToString(form.kind);
            sqlite3_bind_text(statement.handle, 2, formKind.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(statement.handle, 3, form.pluginNumber);
            sqlite3_bind_int64(statement.handle, 4, form.localId);
            sqlite3_bind_text(statement.handle, 5, payload.c_str(), -1, SQLITE_TRANSIENT);
        }

        const int rc = sqlite3_step(statement.handle);
        if (rc == SQLITE_DONE) {
            return true;
        }

        logger::warn("Could not persist dynamic form '{}' in package '{}': {}", form.editorId, packageName, sqlite3_errmsg(db.handle));
        return false;
    }

    bool PersistExternalPatch(const DynamicForms::DynamicForm& form) {
        if (!form.externalPatch ||
            form.externalSourcePlugin.empty() ||
            form.externalLocalId == 0 ||
            form.externalEditPackage.empty() ||
            form.externalBaselinePayload.empty())
        {
            logger::warn("External patch '{}' has incomplete target or baseline metadata.", form.editorId);
            return false;
        }

        rapidjson::Document baseline;
        baseline.Parse(form.externalBaselinePayload.c_str());
        rapidjson::Document resolved;
        if (baseline.HasParseError() || !Manager::BuildFormDocument(form, resolved)) {
            logger::warn("Could not build sparse external patch '{}'.", form.editorId);
            return false;
        }

        rapidjson::Document changes;
        if (!BuildExternalChangesDocument(baseline, resolved, changes)) {
            return false;
        }
        ApplyExternalArrayOperationChoices(form, baseline, resolved, changes);

        SqliteDb db;
        if (!OpenPackageDb(form.externalEditPackage, db)) {
            return false;
        }

        SqliteStatement statement;
        if (!PrepareSql(db.handle,
                "INSERT INTO external_patches("
                "source_plugin, local_form_id, form_kind, editor_id, winning_plugin, changes_json, updated_at"
                ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, unixepoch()) "
                "ON CONFLICT(source_plugin, local_form_id, form_kind) DO UPDATE SET "
                "editor_id=excluded.editor_id, winning_plugin=excluded.winning_plugin, "
                "changes_json=excluded.changes_json, updated_at=excluded.updated_at;",
                statement,
                form.externalEditPackage)) {
            return false;
        }

        const auto formKind = ToString(form.kind);
        const auto changesPayload = JsonString(changes);
        sqlite3_bind_text(statement.handle, 1, form.externalSourcePlugin.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(statement.handle, 2, form.externalLocalId);
        sqlite3_bind_text(statement.handle, 3, formKind.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement.handle, 4, form.editorId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement.handle, 5, form.externalWinningPlugin.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement.handle, 6, changesPayload.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(statement.handle) != SQLITE_DONE) {
            logger::warn(
                "Could not save external patch '{}|{:X}' in package '{}': {}",
                form.externalSourcePlugin,
                form.externalLocalId,
                form.externalEditPackage,
                sqlite3_errmsg(db.handle));
            return false;
        }

        logger::info(
            "Saved sparse external patch '{}|{:X}' ({}) in package '{}'.",
            form.externalSourcePlugin,
            form.externalLocalId,
            formKind,
            form.externalEditPackage);
        return true;
    }

    bool DeleteStoredExternalPatch(const DynamicForms::DynamicForm& form) {
        if (!form.externalPatch || form.externalEditPackage.empty()) {
            return false;
        }

        SqliteDb db;
        if (!OpenPackageDb(form.externalEditPackage, db)) {
            return false;
        }

        SqliteStatement statement;
        if (!PrepareSql(db.handle,
                "DELETE FROM external_patches "
                "WHERE source_plugin=?1 COLLATE NOCASE AND local_form_id=?2 AND form_kind=?3;",
                statement,
                form.externalEditPackage)) {
            return false;
        }

        const auto kind = ToString(form.kind);
        sqlite3_bind_text(statement.handle, 1, form.externalSourcePlugin.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(statement.handle, 2, form.externalLocalId);
        sqlite3_bind_text(statement.handle, 3, kind.c_str(), -1, SQLITE_TRANSIENT);
        return sqlite3_step(statement.handle) == SQLITE_DONE && sqlite3_changes(db.handle) > 0;
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
        struct PackageOrderEntry
        {
            std::string name;
            std::int32_t priority{ 0 };
        };

        std::vector<PackageOrderEntry> discovered;
        discovered.push_back({ Manager::DEFAULT_PACKAGE_NAME, std::numeric_limits<std::int32_t>::min() });

        std::error_code ec;
        std::filesystem::create_directories(Manager::PACKAGES_DIR, ec);
        if (ec) {
            logger::warn("Could not create packages directory '{}': {}", Manager::PACKAGES_DIR, ec.message());
            return { Manager::DEFAULT_PACKAGE_NAME };
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
                if (!displayName.empty() &&
                    std::ranges::none_of(discovered, [&displayName](const PackageOrderEntry& package) {
                        return package.name == displayName;
                    }))
                {
                    std::int32_t priority = 0;
                    if (doc.HasMember("priority") && doc["priority"].IsInt()) {
                        priority = doc["priority"].GetInt();
                    }
                    discovered.push_back({ displayName, priority });
                }
            }
        }

        std::ranges::stable_sort(discovered, [](const PackageOrderEntry& left, const PackageOrderEntry& right) {
            if (left.priority != right.priority) {
                return left.priority < right.priority;
            }
            return left.name < right.name;
        });

        std::vector<std::string> packages;
        packages.reserve(discovered.size());
        for (auto& package : discovered) {
            packages.push_back(std::move(package.name));
        }
        return packages;
    }

    bool IsEditorIdReservedForStoredForm(const DynamicForms::DynamicForm& form) {
        if (const auto* indexed = FindExternalFormsByEditorId(form.editorId); indexed && !indexed->empty()) {
            return true;
        }

        auto* existing = RE::TESForm::LookupByEditorID(form.editorId);
        if (!existing) {
            return false;
        }

        if (form.pluginNumber != 0 && form.localId != 0) {
            if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
                const auto pluginName = DPF::PluginNameForNumber(form.pluginNumber);
                if (!pluginName.empty() &&
                    dataHandler->LookupForm(form.localId, pluginName) == existing)
                {
                    return false;
                }
            }
        }
        return true;
    }

    enum class BaseFormLoadResult
    {
        Added,
        Replaced,
        Deferred
    };

    BaseFormLoadResult AddOrReplaceBaseForm(DynamicForms::DynamicForm form, const std::string& sourcePackage) {
        if (form.packageName.empty()) {
            form.packageName = sourcePackage;
        }
        form.basePackageName.clear();
        form.patchPackageNames.clear();

        const auto existing = std::ranges::find_if(forms, [&form](const DynamicForms::DynamicForm& current) {
            return !current.externalPatch && current.editorId == form.editorId;
        });
        if (existing == forms.end()) {
            if (IsEditorIdReservedForStoredForm(form)) {
                logger::warn(
                    "Dynamic form '{}' from package '{}' was stored but deferred because CLibUtil found that EditorID in a loaded non-DPF form. "
                    "Disable the source plugin or export under a unique EditorID before the next load.",
                    form.editorId,
                    sourcePackage);
                return BaseFormLoadResult::Deferred;
            }
            forms.push_back(std::move(form));
            return BaseFormLoadResult::Added;
        }

        const auto pluginNumber = existing->pluginNumber;
        const auto localId = existing->localId;
        const auto previousPackage = EffectivePackageName(*existing);
        if (previousPackage != sourcePackage) {
            logger::warn(
                "Base form '{}' exists in packages '{}' and '{}'; '{}' owns the resolved form. This is not a patch layer.",
                form.editorId,
                previousPackage,
                sourcePackage,
                sourcePackage);
        }

        form.pluginNumber = pluginNumber;
        form.localId = localId;
        form.packageName = sourcePackage;
        form.basePackageName.clear();
        form.patchPackageNames.clear();
        *existing = std::move(form);
        return BaseFormLoadResult::Replaced;
    }

    void ApplyResolvedPatch(DynamicForms::DynamicForm form, const std::string& sourcePackage) {
        const auto existing = std::ranges::find_if(forms, [&form](const DynamicForms::DynamicForm& current) {
            return !current.externalPatch && current.editorId == form.editorId;
        });
        if (existing == forms.end()) {
            logger::warn("Patch '{}' in package '{}' was skipped because the target form does not exist.", form.editorId, sourcePackage);
            return;
        }

        const auto pluginNumber = existing->pluginNumber;
        const auto localId = existing->localId;
        const auto packageName = EffectivePackageName(*existing);
        const auto basePackageName = existing->basePackageName.empty() ? packageName : existing->basePackageName;
        auto patchPackageNames = existing->patchPackageNames;
        if (!sourcePackage.empty() && sourcePackage != packageName &&
            std::ranges::find(patchPackageNames, sourcePackage) == patchPackageNames.end())
        {
            patchPackageNames.push_back(sourcePackage);
        }

        form.pluginNumber = pluginNumber;
        form.localId = localId;
        form.packageName = packageName;
        form.basePackageName = basePackageName;
        form.patchPackageNames = std::move(patchPackageNames);
        *existing = std::move(form);
    }

    void LoadExternalPackagePatches(const std::string& packageName, sqlite3* db) {
        SqliteStatement statement;
        if (!PrepareSql(db,
                "SELECT source_plugin, local_form_id, form_kind, editor_id, winning_plugin, changes_json "
                "FROM external_patches ORDER BY source_plugin, local_form_id, form_kind;",
                statement,
                packageName)) {
            return;
        }

        while (sqlite3_step(statement.handle) == SQLITE_ROW) {
            const auto* pluginText = reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 0));
            const auto localIdValue = sqlite3_column_int64(statement.handle, 1);
            const auto* kindText = reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 2));
            const auto* editorText = reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 3));
            const auto* winningText = reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 4));
            const auto* changesText = reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 5));
            if (!pluginText || !kindText || !changesText || localIdValue <= 0) {
                continue;
            }

            const auto parsedKind = TryFormKindFromString(kindText);
            if (!parsedKind || !SupportsExternalRuntimePatch(*parsedKind)) {
                logger::warn(
                    "External patch '{}|{:X}' in package '{}' uses unsupported kind '{}'.",
                    pluginText,
                    static_cast<std::uint32_t>(localIdValue),
                    packageName,
                    kindText);
                continue;
            }

            const auto localFormId = static_cast<std::uint32_t>(localIdValue);
            auto existing = FindExternalPatchForm(pluginText, localFormId, *parsedKind);
            DynamicForms::DynamicForm previousResolved;
            std::string layerBaseline;
            if (existing == forms.end()) {
                auto* target = LookupExternalPatchTarget(pluginText, localFormId);
                if (!target ||
                    !BuildExternalBaseline(*target, *parsedKind, packageName, previousResolved, layerBaseline))
                {
                    logger::warn(
                        "External patch '{}|{:X}' in package '{}' was skipped because its target is unavailable.",
                        pluginText,
                        localFormId,
                        packageName);
                    continue;
                }
            } else {
                previousResolved = *existing;
                rapidjson::Document baseline;
                if (!Manager::BuildFormDocument(previousResolved, baseline)) {
                    continue;
                }
                layerBaseline = JsonString(baseline);
            }

            rapidjson::Document resolvedDocument;
            resolvedDocument.Parse(layerBaseline.c_str());
            rapidjson::Document changesDocument;
            changesDocument.Parse(changesText);
            const auto context = std::format("{}:{}|{:X}", packageName, pluginText, localFormId);
            if (resolvedDocument.HasParseError() || changesDocument.HasParseError() ||
                !ApplyExternalChangesDocument(resolvedDocument, changesDocument, context))
            {
                continue;
            }

            DynamicForms::DynamicForm resolved;
            const auto fallbackEditorId =
                editorText && editorText[0] != '\0' ? editorText : previousResolved.editorId.c_str();
            if (!ReadFormDocument(resolvedDocument, context, fallbackEditorId, resolved)) {
                continue;
            }

            auto layers = previousResolved.patchPackageNames;
            if (std::ranges::find(layers, packageName) == layers.end()) {
                layers.push_back(packageName);
            }
            resolved.externalPatch = true;
            resolved.externalSourcePlugin = pluginText;
            resolved.externalLocalId = localFormId;
            resolved.externalWinningPlugin =
                !previousResolved.externalWinningPlugin.empty() ?
                    previousResolved.externalWinningPlugin :
                    (winningText && winningText[0] != '\0' ? winningText : pluginText);
            resolved.externalEditPackage = packageName;
            resolved.externalBaselinePayload = std::move(layerBaseline);
            resolved.externalInheritedChangedFields = previousResolved.externalChangedFields;
            resolved.externalInheritedConflictingFields = previousResolved.externalConflictingFields;
            resolved.externalArrayOperations.clear();
            for (auto member = changesDocument["fields"].MemberBegin();
                 member != changesDocument["fields"].MemberEnd();
                 ++member)
            {
                if (!member->value.IsObject() ||
                    !member->value.HasMember("operation") ||
                    !member->value["operation"].IsString()) {
                    continue;
                }
                const std::string_view operation = member->value["operation"].GetString();
                if (operation == "merge" || operation == "add" || operation == "remove") {
                    resolved.externalArrayOperations[member->name.GetString()] = "merge";
                } else if (operation == "replace") {
                    resolved.externalArrayOperations[member->name.GetString()] = "replace";
                }
            }
            resolved.packageName = previousResolved.packageName.empty() ? packageName : previousResolved.packageName;
            resolved.basePackageName = pluginText;
            resolved.patchPackageNames = std::move(layers);
            resolved.pluginNumber = 0;
            resolved.localId = 0;
            resolved.externalPersisted = true;
            resolved.dirty = false;
            RefreshExternalFieldProvenance(resolved);

            if (existing == forms.end()) {
                forms.push_back(std::move(resolved));
            } else {
                *existing = std::move(resolved);
            }
        }
    }

    void LoadPackageForms(const std::string& packageName) {
        SqliteDb db;
        if (!OpenPackageDb(packageName, db)) {
            return;
        }

        const auto importStats = ImportPackageJsonQueue(packageName, db.handle);
        static_cast<void>(importStats);

        std::size_t storedRows = 0;
        std::size_t acceptedRows = 0;
        std::size_t deferredRows = 0;
        std::size_t invalidRows = 0;

        SqliteStatement formsStatement;
        if (PrepareSql(db.handle, "SELECT editor_id, plugin_number, local_id, payload FROM forms ORDER BY editor_id;", formsStatement, packageName)) {
            while (sqlite3_step(formsStatement.handle) == SQLITE_ROW) {
                ++storedRows;
                const auto* editorText = reinterpret_cast<const char*>(sqlite3_column_text(formsStatement.handle, 0));
                const auto* payloadText = reinterpret_cast<const char*>(sqlite3_column_text(formsStatement.handle, 3));
                if (!editorText || !payloadText) {
                    ++invalidRows;
                    continue;
                }

                DynamicForms::DynamicForm form;
                if (ReadFormPayload(payloadText, std::format("{}:{}", packageName, editorText), editorText, form)) {
                    const auto storedPluginNumber = sqlite3_column_int64(formsStatement.handle, 1);
                    const auto storedLocalId = sqlite3_column_int64(formsStatement.handle, 2);
                    if (storedPluginNumber > 0) form.pluginNumber = static_cast<std::uint32_t>(storedPluginNumber);
                    if (storedLocalId > 0) form.localId = static_cast<std::uint32_t>(storedLocalId);
                    form.packageName = form.packageName.empty() ? packageName : form.packageName;
                    const auto loadResult = AddOrReplaceBaseForm(std::move(form), packageName);
                    if (loadResult == BaseFormLoadResult::Deferred) {
                        ++deferredRows;
                    } else {
                        ++acceptedRows;
                    }
                } else {
                    ++invalidRows;
                }
            }
        }

        logger::info(
            "Package '{}' database: {} stored forms, {} accepted for runtime activation, {} deferred by EditorID collision, {} invalid rows.",
            packageName,
            storedRows,
            acceptedRows,
            deferredRows,
            invalidRows);

        if (!ExecSql(
                db.handle,
                "DELETE FROM patches WHERE target_editor_id IN (SELECT editor_id FROM forms);",
                std::format("{} base/patch cleanup", packageName)))
        {
            logger::warn("Could not remove patch rows shadowed by base forms in package '{}'.", packageName);
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
                    return !current.externalPatch && current.editorId == editorText;
                });
                if (existing == forms.end()) {
                    logger::warn("Patch '{}' in package '{}' was skipped because the target form does not exist.", editorText, packageName);
                    continue;
                }

                DynamicForms::DynamicForm patch;
                if (ReadFormPayload(payloadText, std::format("{} patch:{}", packageName, editorText), editorText, patch)) {
                    patch.packageName = targetPackageText && targetPackageText[0] != '\0' ? targetPackageText : existing->packageName;
                    ApplyResolvedPatch(std::move(patch), packageName);
                }
            }
        }

        LoadExternalPackagePatches(packageName, db.handle);
    }
}

namespace Manager {
    void InstallHooks() {
        bool expected = false;
        if (!responseListHookInstallationAttempted.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel)) {
            logger::debug("TESTopicInfo::GetResponseList hook installation was already attempted (installed={}).",
                responseListHookInstalled.load(std::memory_order_acquire));
            return;
        }

        REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(25083, 25626) };
        originalGetResponseList = reinterpret_cast<GetResponseListFn>(target.address());
        const auto* targetBytes = reinterpret_cast<const std::uint8_t*>(target.address());
        const bool dynamicStringDistributorLoaded = GetModuleHandleW(L"DynamicStringDistributor.dll") != nullptr;
        logger::info(
            "Preparing late TESTopicInfo::GetResponseList detour: DSD loaded={}, target={}, bytes={:02X} {:02X} {:02X} {:02X} {:02X} {:02X}.",
            dynamicStringDistributorLoaded,
            fmt::ptr(reinterpret_cast<const void*>(target.address())),
            targetBytes[0],
            targetBytes[1],
            targetBytes[2],
            targetBytes[3],
            targetBytes[4],
            targetBytes[5]);

        if (const auto error = DetourTransactionBegin(); error != NO_ERROR) {
            logger::error("Could not begin TESTopicInfo::GetResponseList detour: {}.", error);
            originalGetResponseList = nullptr;
            return;
        }
        if (const auto error = DetourUpdateThread(GetCurrentThread()); error != NO_ERROR) {
            logger::error("Could not enlist the current thread for TESTopicInfo::GetResponseList detour: {}.", error);
            DetourTransactionAbort();
            originalGetResponseList = nullptr;
            return;
        }

        PDETOUR_TRAMPOLINE realTrampoline = nullptr;
        PVOID realTarget = nullptr;
        PVOID realDetour = nullptr;
        if (const auto error = DetourAttachEx(
                reinterpret_cast<PVOID*>(&originalGetResponseList),
                reinterpret_cast<PVOID>(GetDynamicResponseList),
                &realTrampoline,
                &realTarget,
                &realDetour);
            error != NO_ERROR) {
            logger::error("Could not attach TESTopicInfo::GetResponseList detour: {}.", error);
            DetourTransactionAbort();
            originalGetResponseList = nullptr;
            return;
        }
        if (const auto error = DetourTransactionCommit(); error != NO_ERROR) {
            logger::error("Could not commit TESTopicInfo::GetResponseList detour: {}.", error);
            originalGetResponseList = nullptr;
            return;
        }
        responseListHookInstalled.store(true, std::memory_order_release);
        logger::info(
            "Installed late TESTopicInfo::GetResponseList detour: target={}, trampoline={}, detour={}, chained entry={}.",
            fmt::ptr(realTarget),
            fmt::ptr(reinterpret_cast<void*>(realTrampoline)),
            fmt::ptr(realDetour),
            fmt::ptr(reinterpret_cast<void*>(originalGetResponseList)));
    }

    std::vector<DynamicForms::DynamicForm>& GetForms() {
        return forms;
    }

    bool ValidateForm(
        const DynamicForms::DynamicForm& form,
        std::vector<std::string>& errors)
    {
        errors.clear();
        if (form.kind == DynamicForms::FormKind::Perk) {
            return ValidatePerkForm(form, errors);
        }
        return true;
    }

    void LoadForms() {
        apiReady.store(false, std::memory_order_release);
        forms.clear();
        dynamicDialogueResponses.clear();
        BuildExternalEditorIdIndex();
        ValidatePerkEntryRuntimeLayouts();

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
            if (ReadFormFile(entry.path(), form) &&
                !HasEditorId(form.editorId) &&
                !IsEditorIdReservedForStoredForm(form))
            {
                form.packageName = Manager::DEFAULT_PACKAGE_NAME;
                forms.push_back(std::move(form));
            }
        }

        for (const auto& form : forms) {
            SaveForm(form);
        }
    }

    bool BuildFormDocument(const DynamicForms::DynamicForm& form, rapidjson::Document& doc) {
        doc.SetObject();
        auto& allocator = doc.GetAllocator();
        doc.AddMember("schemaVersion", 1, allocator);
        const auto formKind = ToString(form.kind);
        doc.AddMember("formKind", rapidjson::Value(formKind.c_str(), allocator), allocator);
        const auto sourceSignature = ToSignature(form.kind);
        doc.AddMember("sourceSignature", rapidjson::Value(sourceSignature.c_str(), allocator), allocator);
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
        if (form.kind == DynamicForms::FormKind::Spell) {
            AddString(doc, allocator, "fullName", form.fullName);
            AddString(doc, allocator, "description", form.description);
            AddFormRefArray(doc, allocator, "keywords", form.keywords);
            AddFormRef(doc, allocator, "equipSlot", form.equipSlot);
            AddFormRef(doc, allocator, "castingPerk", form.castingPerk);
            AddFormRef(doc, allocator, "menuDisplayObject", form.menuDisplayObject);
            doc.AddMember("spellFlags", form.spellFlags, allocator);
            doc.AddMember("spellType", form.spellType, allocator);
            doc.AddMember("spellCostOverride", form.spellCostOverride, allocator);
            doc.AddMember("spellChargeTime", form.spellChargeTime, allocator);
            doc.AddMember("spellCastingType", form.spellCastingType, allocator);
            doc.AddMember("spellDelivery", form.spellDelivery, allocator);
            doc.AddMember("spellCastDuration", form.spellCastDuration, allocator);
            doc.AddMember("spellRange", form.spellRange, allocator);
        }
        if (form.kind == DynamicForms::FormKind::Enchantment) {
            AddString(doc, allocator, "fullName", form.fullName);
            AddFormRefArray(doc, allocator, "keywords", form.keywords);
            doc.AddMember("enchantmentFlags", form.enchantmentFlags, allocator);
            doc.AddMember("enchantmentCostOverride", form.enchantmentCostOverride, allocator);
            doc.AddMember("enchantmentCastingType", form.enchantmentCastingType, allocator);
            doc.AddMember("enchantmentChargeOverride", form.enchantmentChargeOverride, allocator);
            doc.AddMember("enchantmentDelivery", form.enchantmentDelivery, allocator);
            doc.AddMember("enchantmentSpellType", form.enchantmentSpellType, allocator);
            doc.AddMember("enchantmentChargeTime", form.enchantmentChargeTime, allocator);
            AddFormRef(doc, allocator, "baseEnchantment", form.baseEnchantment);
            AddFormRef(doc, allocator, "wornRestrictions", form.wornRestrictions);
        }
        if (form.kind == DynamicForms::FormKind::Scroll) {
            AddString(doc, allocator, "fullName", form.fullName);
            AddString(doc, allocator, "description", form.description);
            AddString(doc, allocator, "modelPath", form.modelPath);
            AddFormRefArray(doc, allocator, "keywords", form.keywords);
            AddFormRef(doc, allocator, "equipSlot", form.equipSlot);
            AddFormRef(doc, allocator, "menuDisplayObject", form.menuDisplayObject);
            AddFormRef(doc, allocator, "pickupSound", form.pickupSound);
            AddFormRef(doc, allocator, "putdownSound", form.putdownSound);
            doc.AddMember("itemValue", form.itemValue, allocator);
            doc.AddMember("itemWeight", form.itemWeight, allocator);
            doc.AddMember("scrollFlags", form.scrollFlags, allocator);
            doc.AddMember("scrollCostOverride", form.scrollCostOverride, allocator);
            doc.AddMember("scrollChargeTime", form.scrollChargeTime, allocator);
            doc.AddMember("scrollDelivery", form.scrollDelivery, allocator);
            doc.AddMember("scrollCastDuration", form.scrollCastDuration, allocator);
            doc.AddMember("scrollRange", form.scrollRange, allocator);
            AddFormRef(doc, allocator, "scrollCastingPerk", form.scrollCastingPerk);
        }
        if (form.kind == DynamicForms::FormKind::Projectile) {
            AddString(doc, allocator, "fullName", form.fullName);
            AddString(doc, allocator, "modelPath", form.modelPath);
            AddString(doc, allocator, "projectileMuzzleFlashModel", form.projectileMuzzleFlashModel);
            doc.AddMember("projectileFlags", form.projectileFlags, allocator);
            doc.AddMember("projectileTypes", form.projectileTypes, allocator);
            doc.AddMember("projectileGravity", form.projectileGravity, allocator);
            doc.AddMember("projectileSpeed", form.projectileSpeed, allocator);
            doc.AddMember("projectileRange", form.projectileRange, allocator);
            AddFormRef(doc, allocator, "projectileLight", form.projectileLight);
            AddFormRef(doc, allocator, "projectileMuzzleFlashLight", form.projectileMuzzleFlashLight);
            doc.AddMember("projectileTracerChance", form.projectileTracerChance, allocator);
            doc.AddMember("projectileExplosionProximity", form.projectileExplosionProximity, allocator);
            doc.AddMember("projectileExplosionTimer", form.projectileExplosionTimer, allocator);
            AddFormRef(doc, allocator, "projectileExplosionType", form.projectileExplosionType);
            AddFormRef(doc, allocator, "projectileActiveSoundLoop", form.projectileActiveSoundLoop);
            doc.AddMember("projectileMuzzleFlashDuration", form.projectileMuzzleFlashDuration, allocator);
            doc.AddMember("projectileFadeOutTime", form.projectileFadeOutTime, allocator);
            doc.AddMember("projectileForce", form.projectileForce, allocator);
            AddFormRef(doc, allocator, "projectileCountdownSound", form.projectileCountdownSound);
            AddFormRef(doc, allocator, "projectileDeactivateSound", form.projectileDeactivateSound);
            AddFormRef(doc, allocator, "projectileDefaultWeaponSource", form.projectileDefaultWeaponSource);
            doc.AddMember("projectileConeSpread", form.projectileConeSpread, allocator);
            doc.AddMember("projectileCollisionRadius", form.projectileCollisionRadius, allocator);
            doc.AddMember("projectileLifetime", form.projectileLifetime, allocator);
            doc.AddMember("projectileRelaunchInterval", form.projectileRelaunchInterval, allocator);
            AddFormRef(doc, allocator, "projectileDecalData", form.projectileDecalData);
            AddFormRef(doc, allocator, "projectileCollisionLayer", form.projectileCollisionLayer);
            doc.AddMember("projectileSoundLevel", form.projectileSoundLevel, allocator);
        }
        if (form.kind == DynamicForms::FormKind::TextureSet) {
            AddStringArray(doc, allocator, "textureSetPaths", form.textureSetPaths);
            doc.AddMember("textureSetFlags", form.textureSetFlags, allocator);
            doc.AddMember("textureSetHasDecal", form.textureSetHasDecal, allocator);
        }
        if (form.kind == DynamicForms::FormKind::TextureSet || form.kind == DynamicForms::FormKind::ImpactData) {
            doc.AddMember("decalMinWidth", form.decalMinWidth, allocator); doc.AddMember("decalMaxWidth", form.decalMaxWidth, allocator);
            doc.AddMember("decalMinHeight", form.decalMinHeight, allocator); doc.AddMember("decalMaxHeight", form.decalMaxHeight, allocator);
            doc.AddMember("decalDepth", form.decalDepth, allocator); doc.AddMember("decalShininess", form.decalShininess, allocator);
            doc.AddMember("decalParallaxScale", form.decalParallaxScale, allocator); doc.AddMember("decalParallaxPasses", form.decalParallaxPasses, allocator);
            doc.AddMember("decalFlags", form.decalFlags, allocator); doc.AddMember("decalRed", form.decalRed, allocator);
            doc.AddMember("decalGreen", form.decalGreen, allocator); doc.AddMember("decalBlue", form.decalBlue, allocator); doc.AddMember("decalAlpha", form.decalAlpha, allocator);
        }
        if (form.kind == DynamicForms::FormKind::Hazard) {
            AddString(doc, allocator, "fullName", form.fullName); AddString(doc, allocator, "modelPath", form.modelPath);
            doc.AddMember("hazardLimit", form.hazardLimit, allocator); doc.AddMember("hazardRadius", form.hazardRadius, allocator);
            doc.AddMember("hazardLifetime", form.hazardLifetime, allocator); doc.AddMember("hazardImageSpaceRadius", form.hazardImageSpaceRadius, allocator);
            doc.AddMember("hazardTargetInterval", form.hazardTargetInterval, allocator); doc.AddMember("hazardFlags", form.hazardFlags, allocator);
            AddFormRef(doc, allocator, "hazardSpell", form.hazardSpell); AddFormRef(doc, allocator, "hazardLight", form.hazardLight);
            AddFormRef(doc, allocator, "hazardImpactDataSet", form.hazardImpactDataSet); AddFormRef(doc, allocator, "hazardSound", form.hazardSound);
            AddFormRef(doc, allocator, "hazardImageSpaceModifier", form.hazardImageSpaceModifier);
        }
        if (form.kind == DynamicForms::FormKind::ImpactData) {
            AddString(doc, allocator, "modelPath", form.modelPath); doc.AddMember("impactEffectDuration", form.impactEffectDuration, allocator);
            doc.AddMember("impactOrientation", form.impactOrientation, allocator); doc.AddMember("impactAngleThreshold", form.impactAngleThreshold, allocator);
            doc.AddMember("impactPlacementRadius", form.impactPlacementRadius, allocator); doc.AddMember("impactSoundLevel", form.impactSoundLevel, allocator);
            doc.AddMember("impactFlags", form.impactFlags, allocator); doc.AddMember("impactResultOverride", form.impactResultOverride, allocator);
            AddFormRef(doc, allocator, "impactDecalTextureSet", form.impactDecalTextureSet); AddFormRef(doc, allocator, "impactDecalTextureSet2", form.impactDecalTextureSet2);
            AddFormRef(doc, allocator, "impactSound1", form.impactSound1); AddFormRef(doc, allocator, "impactSound2", form.impactSound2);
            AddFormRef(doc, allocator, "impactHazard", form.impactHazard);
        }
        if (form.kind == DynamicForms::FormKind::ReferenceEffect) {
            AddFormRef(doc, allocator, "referenceEffectArtObject", form.referenceEffectArtObject); AddFormRef(doc, allocator, "referenceEffectShader", form.referenceEffectShader);
            doc.AddMember("referenceEffectFlags", form.referenceEffectFlags, allocator);
        }
        if (form.kind == DynamicForms::FormKind::DualCastData) {
            AddFormRef(doc, allocator, "dualCastProjectile", form.dualCastProjectile); AddFormRef(doc, allocator, "dualCastExplosion", form.dualCastExplosion);
            AddFormRef(doc, allocator, "dualCastEffectShader", form.dualCastEffectShader); AddFormRef(doc, allocator, "dualCastHitEffectArt", form.dualCastHitEffectArt);
            AddFormRef(doc, allocator, "dualCastImpactDataSet", form.dualCastImpactDataSet); doc.AddMember("dualCastFlags", form.dualCastFlags, allocator);
        }
        if (form.kind == DynamicForms::FormKind::Static || form.kind == DynamicForms::FormKind::MovableStatic) {
            AddString(doc, allocator, "modelPath", form.modelPath); doc.AddMember("staticMaterialThresholdAngle", form.staticMaterialThresholdAngle, allocator);
            AddFormRef(doc, allocator, "staticMaterialObject", form.staticMaterialObject); doc.AddMember("staticFlags", form.staticFlags, allocator);
            doc.AddMember("recordFlags", form.recordFlags, allocator);
        }
        if (form.kind == DynamicForms::FormKind::MovableStatic) {
            AddString(doc, allocator, "fullName", form.fullName); AddFormRef(doc, allocator, "movableStaticSoundLoop", form.movableStaticSoundLoop);
            doc.AddMember("movableStaticFlags", form.movableStaticFlags, allocator);
        }
        if (form.kind == DynamicForms::FormKind::Door) {
            AddString(doc, allocator, "fullName", form.fullName); AddString(doc, allocator, "modelPath", form.modelPath);
            AddFormRef(doc, allocator, "doorOpenSound", form.doorOpenSound); AddFormRef(doc, allocator, "doorCloseSound", form.doorCloseSound);
            AddFormRef(doc, allocator, "doorLoopSound", form.doorLoopSound); doc.AddMember("doorFlags", form.doorFlags, allocator);
            doc.AddMember("recordFlags", form.recordFlags, allocator);
        }
        if (form.kind == DynamicForms::FormKind::CombatStyle) {
            AddNumberArray(doc, allocator, "combatGeneral", form.combatGeneral); AddNumberArray(doc, allocator, "combatMelee", form.combatMelee);
            AddNumberArray(doc, allocator, "combatCloseRange", form.combatCloseRange); doc.AddMember("combatLongRangeStrafe", form.combatLongRangeStrafe, allocator);
            AddNumberArray(doc, allocator, "combatFlight", form.combatFlight); doc.AddMember("combatStyleFlags", form.combatStyleFlags, allocator);
        }
        if (form.kind == DynamicForms::FormKind::SoundCategory) {
            AddString(doc, allocator, "fullName", form.fullName); doc.AddMember("soundCategoryFlags", form.soundCategoryFlags, allocator);
            AddFormRef(doc, allocator, "soundCategoryParent", form.soundCategoryParent); doc.AddMember("soundCategoryAttenuation", form.soundCategoryAttenuation, allocator);
            doc.AddMember("soundCategoryStaticMult", form.soundCategoryStaticMult, allocator); doc.AddMember("soundCategoryDefaultMenuValue", form.soundCategoryDefaultMenuValue, allocator);
            doc.AddMember("soundCategoryVolumeMult", form.soundCategoryVolumeMult, allocator); doc.AddMember("soundCategoryFrequencyMult", form.soundCategoryFrequencyMult, allocator);
        }
        if (form.kind == DynamicForms::FormKind::Class) {
            AddString(doc, allocator, "fullName", form.fullName); AddString(doc, allocator, "description", form.description); AddString(doc, allocator, "classIconPath", form.classIconPath);
            doc.AddMember("classTeachesSkill", form.classTeachesSkill, allocator); doc.AddMember("classMaximumTrainingLevel", form.classMaximumTrainingLevel, allocator);
            AddNumberArray(doc, allocator, "classSkillWeights", form.classSkillWeights); doc.AddMember("classBleedoutDefault", form.classBleedoutDefault, allocator);
            doc.AddMember("classVoicePoints", form.classVoicePoints, allocator); AddNumberArray(doc, allocator, "classAttributeWeights", form.classAttributeWeights);
        }
        if (form.kind == DynamicForms::FormKind::Flora || form.kind == DynamicForms::FormKind::Tree) {
            AddString(doc, allocator, "fullName", form.fullName); AddString(doc, allocator, "modelPath", form.modelPath);
            AddFormRef(doc, allocator, "produceItem", form.produceItem); AddFormRef(doc, allocator, "harvestSound", form.harvestSound);
            AddNumberArray(doc, allocator, "produceChance", form.produceChance);
        }
        if (form.kind == DynamicForms::FormKind::Flora) {
            AddFormRefArray(doc, allocator, "keywords", form.keywords); doc.AddMember("floraFlags", form.floraFlags, allocator);
            AddFormRef(doc, allocator, "floraSoundLoop", form.floraSoundLoop); AddFormRef(doc, allocator, "floraSoundActivate", form.floraSoundActivate);
            AddFormRef(doc, allocator, "floraWaterType", form.floraWaterType);
        }
        if (form.kind == DynamicForms::FormKind::Tree) {
            AddNumberArray(doc, allocator, "treeAnimation", form.treeAnimation); doc.AddMember("treeType", form.treeType, allocator);
            doc.AddMember("recordFlags", form.recordFlags, allocator);
        }
        if (form.kind == DynamicForms::FormKind::ConstructibleObject) {
            AddFormRef(doc, allocator, "createdItem", form.createdItem);
            AddFormRef(doc, allocator, "benchKeyword", form.benchKeyword);
            doc.AddMember("numConstructed", std::max<std::uint16_t>(1, form.numConstructed), allocator);
            AddContainerEntries(doc, allocator, "requiredItems", form.requiredItems);
            rapidjson::Value conditions(rapidjson::kArrayType);
            for (const auto& condition : form.conditions) WriteCondition(conditions, allocator, condition);
            doc.AddMember("conditions", conditions, allocator);
        }
        if (form.kind == DynamicForms::FormKind::Container) {
            AddString(doc, allocator, "fullName", form.fullName);
            AddString(doc, allocator, "modelPath", form.modelPath);
            doc.AddMember("itemWeight", form.itemWeight, allocator);
            AddContainerEntries(doc, allocator, "containerItems", form.containerItems);
            doc.AddMember("containerFlags", form.containerFlags, allocator);
            doc.AddMember("containerAllowStolenItems", form.containerAllowStolenItems, allocator);
            AddFormRef(doc, allocator, "containerOpenSound", form.containerOpenSound);
            AddFormRef(doc, allocator, "containerCloseSound", form.containerCloseSound);
            doc.AddMember("recordFlags", form.recordFlags, allocator);
        }
        if (form.kind == DynamicForms::FormKind::ImpactDataSet) AddFormRefPairs(doc, allocator, "impactDataSetEntries", form.impactDataSetEntries);
        if (form.kind == DynamicForms::FormKind::CollisionLayer) {
            doc.AddMember("collisionLayerIndex", form.collisionLayerIndex, allocator); doc.AddMember("collisionLayerColor", form.collisionLayerColor, allocator); doc.AddMember("collisionLayerFlags", form.collisionLayerFlags, allocator);
            AddString(doc, allocator, "collisionLayerName", form.collisionLayerName); AddFormRefArray(doc, allocator, "collisionLayers", form.collisionLayers);
        }
        if (form.kind == DynamicForms::FormKind::Footstep) { AddString(doc, allocator, "footstepTag", form.footstepTag); AddFormRef(doc, allocator, "footstepImpactDataSet", form.footstepImpactDataSet); }
        if (form.kind == DynamicForms::FormKind::FootstepSet) {
            constexpr std::array footstepKeys{ "footstepWalk", "footstepRun", "footstepSneak", "footstepBleedout", "footstepSwim" };
            for (std::size_t i = 0; i < footstepKeys.size(); ++i) AddFormRefArray(doc, allocator, footstepKeys[i], form.footstepSets[i]);
        }
        if (form.kind == DynamicForms::FormKind::ReverbParameters) { doc.AddMember("reverbDecayTime", form.reverbDecayTime, allocator); doc.AddMember("reverbHFReference", form.reverbHFReference, allocator); AddNumberArray(doc, allocator, "reverbValues", form.reverbValues); }
        if (form.kind == DynamicForms::FormKind::AcousticSpace) { AddFormRef(doc, allocator, "acousticLoopingSound", form.acousticLoopingSound); AddFormRef(doc, allocator, "acousticSoundRegion", form.acousticSoundRegion); AddFormRef(doc, allocator, "acousticReverb", form.acousticReverb); }
        if (form.kind == DynamicForms::FormKind::Apparatus) {
            AddString(doc, allocator, "fullName", form.fullName); AddString(doc, allocator, "modelPath", form.modelPath); AddString(doc, allocator, "inventoryIcon", form.inventoryIcon); AddString(doc, allocator, "messageIcon", form.messageIcon);
            doc.AddMember("itemValue", form.itemValue, allocator); doc.AddMember("itemWeight", form.itemWeight, allocator); AddFormRefArray(doc, allocator, "keywords", form.keywords); AddFormRef(doc, allocator, "pickupSound", form.pickupSound); AddFormRef(doc, allocator, "putdownSound", form.putdownSound); doc.AddMember("apparatusQuality", form.apparatusQuality, allocator);
        }
        if (form.kind == DynamicForms::FormKind::StaticCollection) { AddString(doc, allocator, "modelPath", form.modelPath); doc.AddMember("recordFlags", form.recordFlags, allocator); }
        if (form.kind == DynamicForms::FormKind::Grass) {
            AddString(doc, allocator, "modelPath", form.modelPath); doc.AddMember("grassDensity", form.grassDensity, allocator); doc.AddMember("grassMinSlope", form.grassMinSlope, allocator); doc.AddMember("grassMaxSlope", form.grassMaxSlope, allocator); doc.AddMember("grassDistanceFromWater", form.grassDistanceFromWater, allocator); doc.AddMember("grassWaterState", form.grassWaterState, allocator);
            doc.AddMember("grassPositionRange", form.grassPositionRange, allocator); doc.AddMember("grassHeightRange", form.grassHeightRange, allocator); doc.AddMember("grassColorRange", form.grassColorRange, allocator); doc.AddMember("grassWavePeriod", form.grassWavePeriod, allocator); doc.AddMember("grassFlags", form.grassFlags, allocator);
        }
        if (form.kind == DynamicForms::FormKind::IdleMarker) { AddString(doc, allocator, "modelPath", form.modelPath); doc.AddMember("idleFlags", form.idleFlags, allocator); doc.AddMember("idleTimer", form.idleTimer, allocator); AddFormRefArray(doc, allocator, "idleAnimations", form.idleAnimations); doc.AddMember("recordFlags", form.recordFlags, allocator); }
        if (form.kind == DynamicForms::FormKind::EncounterZone) { AddFormRef(doc, allocator, "encounterOwner", form.encounterOwner); AddFormRef(doc, allocator, "encounterLocation", form.encounterLocation); doc.AddMember("encounterOwnerRank", form.encounterOwnerRank, allocator); doc.AddMember("encounterMinLevel", form.encounterMinLevel, allocator); doc.AddMember("encounterMaxLevel", form.encounterMaxLevel, allocator); doc.AddMember("encounterFlags", form.encounterFlags, allocator); }
        if (form.kind == DynamicForms::FormKind::Relationship) { AddFormRef(doc, allocator, "relationshipNpc1", form.relationshipNpc1); AddFormRef(doc, allocator, "relationshipNpc2", form.relationshipNpc2); AddFormRef(doc, allocator, "relationshipAssociation", form.relationshipAssociation); doc.AddMember("relationshipLevel", form.relationshipLevel, allocator); doc.AddMember("relationshipFlags", form.relationshipFlags, allocator); }
        if (form.kind == DynamicForms::FormKind::AssociationType) { AddStringArray(doc, allocator, "associationLabels", form.associationLabels); doc.AddMember("associationFlags", form.associationFlags, allocator); }
        if (form.kind == DynamicForms::FormKind::MovementType) { AddString(doc, allocator, "movementName", form.movementName); AddNumberArray(doc, allocator, "movementSpeeds", form.movementSpeeds); doc.AddMember("movementRotateWhileMoving", form.movementRotateWhileMoving, allocator); doc.AddMember("movementDirectional", form.movementDirectional, allocator); doc.AddMember("movementSpeed", form.movementSpeed, allocator); doc.AddMember("movementRotationSpeed", form.movementRotationSpeed, allocator); }
        if (form.kind == DynamicForms::FormKind::WordOfPower) { AddString(doc, allocator, "fullName", form.fullName); AddString(doc, allocator, "wordTranslation", form.wordTranslation); }
        if (form.kind == DynamicForms::FormKind::Water) { AddString(doc, allocator, "fullName", form.fullName); AddStringArray(doc, allocator, "waterNoiseTextures", form.waterNoiseTextures); doc.AddMember("waterAlpha", form.waterAlpha, allocator); doc.AddMember("waterFlags", form.waterFlags, allocator); AddFormRef(doc, allocator, "waterMaterial", form.waterMaterial); AddFormRef(doc, allocator, "waterSound", form.waterSound); AddFormRef(doc, allocator, "waterContactSpell", form.waterContactSpell); AddFormRef(doc, allocator, "waterImageSpace", form.waterImageSpace); AddNumberArray(doc, allocator, "waterLinearVelocity", form.waterLinearVelocity); AddNumberArray(doc, allocator, "waterAngularVelocity", form.waterAngularVelocity); }
        if (form.kind == DynamicForms::FormKind::ImageSpace) { AddNumberArray(doc, allocator, "imageSpaceHDR", form.imageSpaceHDR); AddNumberArray(doc, allocator, "imageSpaceCinematic", form.imageSpaceCinematic); doc.AddMember("imageSpaceTintAmount", form.imageSpaceTintAmount, allocator); AddNumberArray(doc, allocator, "imageSpaceTintColor", form.imageSpaceTintColor); AddNumberArray(doc, allocator, "imageSpaceDOF", form.imageSpaceDOF); doc.AddMember("imageSpaceDOFFlags", form.imageSpaceDOFFlags, allocator); doc.AddMember("imageSpaceSkyBlur", form.imageSpaceSkyBlur, allocator); }
        if (form.kind == DynamicForms::FormKind::LightingTemplate) { AddNumberArray(doc, allocator, "lightingColors", form.lightingColors); AddNumberArray(doc, allocator, "lightingValues", form.lightingValues); doc.AddMember("lightingDirectionalXY", form.lightingDirectionalXY, allocator); doc.AddMember("lightingDirectionalZ", form.lightingDirectionalZ, allocator); doc.AddMember("lightingInheritanceFlags", form.lightingInheritanceFlags, allocator); }
        if (form.kind == DynamicForms::FormKind::Shout) {
            AddString(doc, allocator, "fullName", form.fullName); AddString(doc, allocator, "description", form.description); AddFormRef(doc, allocator, "equipSlot", form.equipSlot); AddFormRef(doc, allocator, "menuDisplayObject", form.menuDisplayObject); doc.AddMember("recordFlags", form.recordFlags, allocator);
            constexpr std::array shoutWordKeys{ "shoutWord1", "shoutWord2", "shoutWord3" }; constexpr std::array shoutSpellKeys{ "shoutSpell1", "shoutSpell2", "shoutSpell3" };
            for (std::size_t i = 0; i < 3; ++i) { AddFormRef(doc, allocator, shoutWordKeys[i], form.shoutWords[i]); AddFormRef(doc, allocator, shoutSpellKeys[i], form.shoutSpells[i]); }
            AddNumberArray(doc, allocator, "shoutRecoveryTimes", form.shoutRecoveryTimes);
        }
        if (form.kind == DynamicForms::FormKind::LeveledItem || form.kind == DynamicForms::FormKind::LeveledNPC || form.kind == DynamicForms::FormKind::LeveledSpell) {
            AddLeveledEntries(doc, allocator, "leveledEntries", form.leveledEntries); doc.AddMember("leveledChanceNone", form.leveledChanceNone, allocator); doc.AddMember("leveledFlags", form.leveledFlags, allocator); AddFormRef(doc, allocator, "leveledChanceGlobal", form.leveledChanceGlobal);
            if (form.kind == DynamicForms::FormKind::LeveledNPC) AddString(doc, allocator, "modelPath", form.modelPath);
        }
        if (form.kind == DynamicForms::FormKind::Action) doc.AddMember("actionIndex", form.actionIndex, allocator);
        if (form.kind == DynamicForms::FormKind::MenuIcon) AddString(doc, allocator, "inventoryIcon", form.inventoryIcon);
        if (form.kind == DynamicForms::FormKind::Eyes) { AddString(doc, allocator, "fullName", form.fullName); AddString(doc, allocator, "eyesTexture", form.eyesTexture); doc.AddMember("eyesFlags", form.eyesFlags, allocator); doc.AddMember("recordFlags", form.recordFlags, allocator); }
        if (form.kind == DynamicForms::FormKind::Note) { AddString(doc, allocator, "fullName", form.fullName); AddString(doc, allocator, "modelPath", form.modelPath); AddString(doc, allocator, "inventoryIcon", form.inventoryIcon); AddFormRef(doc, allocator, "pickupSound", form.pickupSound); AddFormRef(doc, allocator, "putdownSound", form.putdownSound); }
        if (form.kind == DynamicForms::FormKind::AnimatedObject) { AddString(doc, allocator, "modelPath", form.modelPath); AddString(doc, allocator, "animatedUnloadEvent", form.animatedUnloadEvent); }
        if (form.kind == DynamicForms::FormKind::LoadScreen) {
            AddString(doc, allocator, "loadScreenText", form.loadScreenText); AddFormRef(doc, allocator, "loadScreenObject", form.loadScreenObject); doc.AddMember("loadScreenInitialScale", form.loadScreenInitialScale, allocator); AddNumberArray(doc, allocator, "loadScreenRotationConstraints", form.loadScreenRotationConstraints); AddNumberArray(doc, allocator, "loadScreenRotationOffsetConstraints", form.loadScreenRotationOffsetConstraints); AddNumberArray(doc, allocator, "loadScreenTranslationOffset", form.loadScreenTranslationOffset); AddString(doc, allocator, "loadScreenCameraPath", form.loadScreenCameraPath); doc.AddMember("recordFlags", form.recordFlags, allocator);
            rapidjson::Value conditions(rapidjson::kArrayType); for (const auto& condition : form.conditions) WriteCondition(conditions, allocator, condition); doc.AddMember("conditions", conditions, allocator);
        }
        if (form.kind == DynamicForms::FormKind::ShaderParticleGeometry) { AddNumberArray(doc, allocator, "shaderParticleSettings", form.shaderParticleSettings); AddString(doc, allocator, "shaderParticleTexture", form.shaderParticleTexture); }
        if (form.kind == DynamicForms::FormKind::AddonNode) { AddString(doc, allocator, "modelPath", form.modelPath); doc.AddMember("addonIndex", form.addonIndex, allocator); AddFormRef(doc, allocator, "addonSound", form.addonSound); doc.AddMember("addonMasterParticleCap", form.addonMasterParticleCap, allocator); doc.AddMember("addonFlags", form.addonFlags, allocator); }
        if (form.kind == DynamicForms::FormKind::Faction) {
            AddString(doc, allocator, "fullName", form.fullName); doc.AddMember("factionFlags", form.factionFlags, allocator); AddFactionReactions(doc, allocator, form.factionReactions); AddFactionRanks(doc, allocator, form.factionRanks);
            AddFormRef(doc, allocator, "factionJailMarker", form.factionJailMarker); AddFormRef(doc, allocator, "factionWaitMarker", form.factionWaitMarker); AddFormRef(doc, allocator, "factionStolenContainer", form.factionStolenContainer); AddFormRef(doc, allocator, "factionPlayerInventoryContainer", form.factionPlayerInventoryContainer); AddFormRef(doc, allocator, "factionCrimeGroup", form.factionCrimeGroup); AddFormRef(doc, allocator, "factionJailOutfit", form.factionJailOutfit);
            doc.AddMember("factionArrest", form.factionArrest, allocator); doc.AddMember("factionAttackOnSight", form.factionAttackOnSight, allocator); doc.AddMember("factionMurderCrimeGold", form.factionMurderCrimeGold, allocator); doc.AddMember("factionAssaultCrimeGold", form.factionAssaultCrimeGold, allocator); doc.AddMember("factionTrespassCrimeGold", form.factionTrespassCrimeGold, allocator); doc.AddMember("factionPickpocketCrimeGold", form.factionPickpocketCrimeGold, allocator); doc.AddMember("factionStealCrimeGoldMult", form.factionStealCrimeGoldMult, allocator); doc.AddMember("factionEscapeCrimeGold", form.factionEscapeCrimeGold, allocator); doc.AddMember("factionWerewolfCrimeGold", form.factionWerewolfCrimeGold, allocator);
            doc.AddMember("factionVendorStartHour", form.factionVendorStartHour, allocator); doc.AddMember("factionVendorEndHour", form.factionVendorEndHour, allocator); doc.AddMember("factionVendorRadius", form.factionVendorRadius, allocator); doc.AddMember("factionVendorBuysStolen", form.factionVendorBuysStolen, allocator); doc.AddMember("factionVendorNotBuySell", form.factionVendorNotBuySell, allocator); doc.AddMember("factionVendorBuysNonStolen", form.factionVendorBuysNonStolen, allocator); AddFormRef(doc, allocator, "factionVendorSellBuyList", form.factionVendorSellBuyList); AddFormRef(doc, allocator, "factionMerchantContainer", form.factionMerchantContainer);
            rapidjson::Value vendorConditions(rapidjson::kArrayType); for (const auto& condition : form.factionVendorConditions) WriteCondition(vendorConditions, allocator, condition); doc.AddMember("factionVendorConditions", vendorConditions, allocator);
        }
        if (form.kind == DynamicForms::FormKind::IdleAnimation) {
            doc.AddMember("idleLoopMin", form.idleLoopMin, allocator); doc.AddMember("idleLoopMax", form.idleLoopMax, allocator); doc.AddMember("idleAnimationFlags", form.idleAnimationFlags, allocator); doc.AddMember("idleAnimationGroupSelection", form.idleAnimationGroupSelection, allocator); doc.AddMember("idleReplayDelay", form.idleReplayDelay, allocator); AddFormRef(doc, allocator, "idleParent", form.idleParent); AddFormRef(doc, allocator, "idlePrevious", form.idlePrevious); AddString(doc, allocator, "idleAnimationFile", form.idleAnimationFile); AddString(doc, allocator, "idleAnimationEvent", form.idleAnimationEvent);
            rapidjson::Value conditions(rapidjson::kArrayType); for (const auto& condition : form.conditions) WriteCondition(conditions, allocator, condition); doc.AddMember("conditions", conditions, allocator);
        }
        if (form.kind == DynamicForms::FormKind::MaterialObject) { AddString(doc, allocator, "modelPath", form.modelPath); AddNumberArray(doc, allocator, "materialDirectionalData", form.materialDirectionalData); doc.AddMember("materialSinglePass", form.materialSinglePass, allocator); doc.AddMember("materialObjectFlags", form.materialObjectFlags, allocator); }
        if (form.kind == DynamicForms::FormKind::Message) { AddString(doc, allocator, "fullName", form.fullName); AddString(doc, allocator, "description", form.description); AddFormRef(doc, allocator, "messageMenuIcon", form.messageMenuIcon); AddFormRef(doc, allocator, "messageOwnerQuest", form.messageOwnerQuest); AddMessageButtons(doc, allocator, form.messageButtons); doc.AddMember("messageFlags", form.messageFlags, allocator); doc.AddMember("messageDisplayTime", form.messageDisplayTime, allocator); }
        if (form.kind == DynamicForms::FormKind::LandTexture) { AddFormRef(doc, allocator, "landTextureSet", form.landTextureSet); doc.AddMember("landFriction", form.landFriction, allocator); doc.AddMember("landRestitution", form.landRestitution, allocator); AddFormRef(doc, allocator, "landMaterialType", form.landMaterialType); doc.AddMember("landSpecularExponent", form.landSpecularExponent, allocator); doc.AddMember("landShaderTextureIndex", form.landShaderTextureIndex, allocator); AddFormRefArray(doc, allocator, "landGrasses", form.landGrasses); }
        if (form.kind == DynamicForms::FormKind::SoundOutputModel) { doc.AddMember("soundOutputType", form.soundOutputType, allocator); doc.AddMember("soundOutputFlags", form.soundOutputFlags, allocator); doc.AddMember("soundOutputReverbSend", form.soundOutputReverbSend, allocator); doc.AddMember("soundOutputMinDistance", form.soundOutputMinDistance, allocator); doc.AddMember("soundOutputMaxDistance", form.soundOutputMaxDistance, allocator); AddNumberArray(doc, allocator, "soundOutputCurve", form.soundOutputCurve); AddNumberArray(doc, allocator, "soundOutputSpeakers", form.soundOutputSpeakers); }
        if (form.kind == DynamicForms::FormKind::LensFlare) { doc.AddMember("lensFlareFadeDistanceRadiusScale", form.lensFlareFadeDistanceRadiusScale, allocator); doc.AddMember("lensFlareColorInfluence", form.lensFlareColorInfluence, allocator); }
        if (form.kind == DynamicForms::FormKind::Debris) AddDebrisEntries(doc, allocator, form.debrisEntries);
        if (form.kind == DynamicForms::FormKind::ImageSpaceModifier) { doc.AddMember("imageModifierAnimatable", form.imageModifierAnimatable, allocator); doc.AddMember("imageModifierDuration", form.imageModifierDuration, allocator); AddNumberArray(doc, allocator, "imageModifierHDR", form.imageModifierHDR); AddNumberArray(doc, allocator, "imageModifierCinematic", form.imageModifierCinematic); doc.AddMember("imageModifierTintColor", form.imageModifierTintColor, allocator); doc.AddMember("imageModifierBlurRadius", form.imageModifierBlurRadius, allocator); doc.AddMember("imageModifierDoubleVisionStrength", form.imageModifierDoubleVisionStrength, allocator); doc.AddMember("imageModifierRadialBlurStrength", form.imageModifierRadialBlurStrength, allocator); doc.AddMember("imageModifierRadialBlurRampUp", form.imageModifierRadialBlurRampUp, allocator); doc.AddMember("imageModifierRadialBlurStart", form.imageModifierRadialBlurStart, allocator); doc.AddMember("imageModifierUseTargetForRadialBlur", form.imageModifierUseTargetForRadialBlur, allocator); AddNumberArray(doc, allocator, "imageModifierRadialBlurCenter", form.imageModifierRadialBlurCenter); doc.AddMember("imageModifierDofStrength", form.imageModifierDofStrength, allocator); doc.AddMember("imageModifierDofDistance", form.imageModifierDofDistance, allocator); doc.AddMember("imageModifierDofRange", form.imageModifierDofRange, allocator); doc.AddMember("imageModifierDofUseTarget", form.imageModifierDofUseTarget, allocator); doc.AddMember("imageModifierDofFlags", form.imageModifierDofFlags, allocator); doc.AddMember("imageModifierRadialBlurRampDown", form.imageModifierRadialBlurRampDown, allocator); doc.AddMember("imageModifierRadialBlurDownStart", form.imageModifierRadialBlurDownStart, allocator); doc.AddMember("imageModifierFadeColor", form.imageModifierFadeColor, allocator); doc.AddMember("imageModifierMotionBlurStrength", form.imageModifierMotionBlurStrength, allocator); }
        if (form.kind == DynamicForms::FormKind::CameraShot) { AddString(doc, allocator, "modelPath", form.modelPath); AddFormRef(doc, allocator, "cameraImageSpaceModifier", form.cameraImageSpaceModifier); doc.AddMember("cameraAction", form.cameraAction, allocator); doc.AddMember("cameraLocation", form.cameraLocation, allocator); doc.AddMember("cameraTarget", form.cameraTarget, allocator); doc.AddMember("cameraFlags", form.cameraFlags, allocator); AddNumberArray(doc, allocator, "cameraTiming", form.cameraTiming); }
        if (form.kind == DynamicForms::FormKind::CameraPath) { AddFormRefArray(doc, allocator, "cameraPathShots", form.cameraPathShots); doc.AddMember("cameraPathFlags", form.cameraPathFlags, allocator); AddFormRef(doc, allocator, "cameraPathParent", form.cameraPathParent); AddFormRef(doc, allocator, "cameraPathPrevious", form.cameraPathPrevious); rapidjson::Value conditions(rapidjson::kArrayType); for (const auto& condition : form.conditions) WriteCondition(conditions, allocator, condition); doc.AddMember("conditions", conditions, allocator); }
        if (form.kind == DynamicForms::FormKind::TalkingActivator) { AddString(doc, allocator, "fullName", form.fullName); AddString(doc, allocator, "modelPath", form.modelPath); AddFormRefArray(doc, allocator, "keywords", form.keywords); AddFormRef(doc, allocator, "talkingVoiceType", form.talkingVoiceType); AddFormRef(doc, allocator, "soundLoop", form.soundLoop); AddFormRef(doc, allocator, "soundActivate", form.soundActivate); doc.AddMember("recordFlags", form.recordFlags, allocator); }
        if (form.kind == DynamicForms::FormKind::Furniture) { AddString(doc, allocator, "fullName", form.fullName); AddString(doc, allocator, "modelPath", form.modelPath); AddFormRefArray(doc, allocator, "keywords", form.keywords); AddFormRef(doc, allocator, "soundLoop", form.soundLoop); AddFormRef(doc, allocator, "soundActivate", form.soundActivate); doc.AddMember("furnitureFlags", form.furnitureFlags, allocator); doc.AddMember("furnitureWorkbenchType", form.furnitureWorkbenchType, allocator); doc.AddMember("furnitureWorkbenchSkill", form.furnitureWorkbenchSkill, allocator); AddFormRef(doc, allocator, "furnitureAssociatedSpell", form.furnitureAssociatedSpell); doc.AddMember("recordFlags", form.recordFlags, allocator); }
        if (form.kind == DynamicForms::FormKind::Weather) { doc.AddMember("weatherFlags", form.weatherFlags, allocator); doc.AddMember("weatherWindSpeed", form.weatherWindSpeed, allocator); doc.AddMember("weatherTransitionDelta", form.weatherTransitionDelta, allocator); doc.AddMember("weatherSunGlare", form.weatherSunGlare, allocator); doc.AddMember("weatherSunDamage", form.weatherSunDamage, allocator); AddNumberArray(doc, allocator, "weatherFogData", form.weatherFogData); AddFormRef(doc, allocator, "weatherPrecipitation", form.weatherPrecipitation); AddFormRef(doc, allocator, "weatherReferenceEffect", form.weatherReferenceEffect); AddFormRef(doc, allocator, "weatherLensFlare", form.weatherLensFlare); constexpr std::array imageKeys{ "weatherImageSpaceSunrise", "weatherImageSpaceDay", "weatherImageSpaceSunset", "weatherImageSpaceNight" }; constexpr std::array volumeKeys{ "weatherVolumetricSunrise", "weatherVolumetricDay", "weatherVolumetricSunset", "weatherVolumetricNight" }; for (std::size_t i = 0; i < 4; ++i) { AddFormRef(doc, allocator, imageKeys[i], form.weatherImageSpaces[i]); AddFormRef(doc, allocator, volumeKeys[i], form.weatherVolumetricLighting[i]); } }
        if (form.kind == DynamicForms::FormKind::Climate) { AddString(doc, allocator, "climateNightSkyModel", form.climateNightSkyModel); AddString(doc, allocator, "climateSunTexture", form.climateSunTexture); AddString(doc, allocator, "climateSunGlareTexture", form.climateSunGlareTexture); AddNumberArray(doc, allocator, "climateTimes", form.climateTimes); doc.AddMember("climateVolatility", form.climateVolatility, allocator); doc.AddMember("climateMoonPhaseLength", form.climateMoonPhaseLength, allocator); rapidjson::Value entries(rapidjson::kArrayType); for (const auto& source : form.climateWeatherEntries) { rapidjson::Value item(rapidjson::kObjectType); AddFormRef(item, allocator, "weather", source.weather); item.AddMember("chance", source.chance, allocator); AddFormRef(item, allocator, "global", source.global); entries.PushBack(item, allocator); } doc.AddMember("climateWeatherEntries", entries, allocator); }
        if (form.kind == DynamicForms::FormKind::Location) { AddString(doc, allocator, "fullName", form.fullName); AddFormRefArray(doc, allocator, "keywords", form.keywords); AddFormRef(doc, allocator, "locationParent", form.locationParent); AddFormRef(doc, allocator, "locationCrimeFaction", form.locationCrimeFaction); AddFormRef(doc, allocator, "locationMusicType", form.locationMusicType); doc.AddMember("locationWorldRadius", form.locationWorldRadius, allocator); }
        if (form.kind == DynamicForms::FormKind::MusicType) { doc.AddMember("musicTypeFlags", form.musicTypeFlags, allocator); doc.AddMember("musicTypePriority", form.musicTypePriority, allocator); doc.AddMember("musicTypeDucking", form.musicTypeDucking, allocator); doc.AddMember("musicTypeFadeTime", form.musicTypeFadeTime, allocator); AddFormRefArray(doc, allocator, "musicTypeTracks", form.musicTypeTracks); }
        if (form.kind == DynamicForms::FormKind::MusicTrack) { AddString(doc, allocator, "musicTrackPath", form.musicTrackPath); AddString(doc, allocator, "musicTrackFinalePath", form.musicTrackFinalePath); rapidjson::Value cues(rapidjson::kArrayType); for (const float cue : form.musicTrackCuePoints) cues.PushBack(cue, allocator); doc.AddMember("musicTrackCuePoints", cues, allocator); doc.AddMember("musicTrackLoopBegin", form.musicTrackLoopBegin, allocator); doc.AddMember("musicTrackLoopEnd", form.musicTrackLoopEnd, allocator); doc.AddMember("musicTrackLoopCount", form.musicTrackLoopCount, allocator); rapidjson::Value conditions(rapidjson::kArrayType); for (const auto& condition : form.conditions) WriteCondition(conditions, allocator, condition); doc.AddMember("conditions", conditions, allocator); }
        if (form.kind == DynamicForms::FormKind::BodyPartData) { AddString(doc, allocator, "modelPath", form.modelPath); AddFormRef(doc, allocator, "bodyPartRagdoll", form.bodyPartRagdoll); }
        if (form.kind == DynamicForms::FormKind::VolumetricLighting) AddNumberArray(doc, allocator, "volumetricLightingData", form.volumetricLightingData);
        if (form.kind == DynamicForms::FormKind::Sound) AddFormRef(doc, allocator, "legacySoundDescriptor", form.legacySoundDescriptor);
        if (form.kind == DynamicForms::FormKind::ActorValueInfo) { AddString(doc, allocator, "fullName", form.fullName); AddString(doc, allocator, "description", form.description); AddString(doc, allocator, "inventoryIcon", form.inventoryIcon); AddString(doc, allocator, "actorValueAbbreviation", form.actorValueAbbreviation); AddString(doc, allocator, "actorValueEnumName", form.actorValueEnumName); doc.AddMember("actorValueFlags", form.actorValueFlags, allocator); doc.AddMember("actorValueType", form.actorValueType, allocator); AddStringArray(doc, allocator, "actorValueEnumValues", form.actorValueEnumValues); doc.AddMember("actorValueHasSkillData", form.actorValueHasSkillData, allocator); AddNumberArray(doc, allocator, "actorValueSkillData", form.actorValueSkillData); }
        if (form.kind == DynamicForms::FormKind::DialogueBranch) {
            doc.AddMember("dialogueBranchFlags", form.dialogueBranchFlags, allocator);
            doc.AddMember("dialogueBranchType", form.dialogueBranchType, allocator);
            AddFormRef(doc, allocator, "dialogueBranchQuest", form.dialogueBranchQuest);
            AddFormRef(doc, allocator, "dialogueBranchStartingTopic", form.dialogueBranchStartingTopic);
        }
        if (form.kind == DynamicForms::FormKind::DialogueTopic) {
            AddString(doc, allocator, "fullName", form.fullName);
            doc.AddMember("dialogueTopicFlags", form.dialogueTopicFlags, allocator);
            doc.AddMember("dialogueTopicType", form.dialogueTopicType, allocator);
            doc.AddMember("dialogueTopicSubtype", form.dialogueTopicSubtype, allocator);
            doc.AddMember("dialogueTopicPriority", form.dialogueTopicPriority, allocator);
            doc.AddMember("dialogueTopicJournalIndex", form.dialogueTopicJournalIndex, allocator);
            AddFormRef(doc, allocator, "dialogueTopicBranch", form.dialogueTopicBranch);
            AddFormRef(doc, allocator, "dialogueTopicQuest", form.dialogueTopicQuest);
            AddFormRefArray(doc, allocator, "dialogueTopicInfos", form.dialogueTopicInfos);
        }
        if (form.kind == DynamicForms::FormKind::DialogueInfo) { AddFormRef(doc, allocator, "dialogueInfoTopic", form.dialogueInfoTopic); AddFormRef(doc, allocator, "dialogueInfoSharedInfo", form.dialogueInfoSharedInfo); doc.AddMember("dialogueInfoIndex", form.dialogueInfoIndex, allocator); doc.AddMember("dialogueInfoFavorLevel", form.dialogueInfoFavorLevel, allocator); doc.AddMember("dialogueInfoFlags", form.dialogueInfoFlags, allocator); doc.AddMember("dialogueInfoResetHours", form.dialogueInfoResetHours, allocator); AddDialogueResponses(doc, allocator, form.dialogueResponses); rapidjson::Value conditions(rapidjson::kArrayType); for (const auto& condition : form.conditions) WriteCondition(conditions, allocator, condition); doc.AddMember("conditions", conditions, allocator); }
        if(form.kind==DynamicForms::FormKind::Quest){AddString(doc,allocator,"fullName",form.fullName);doc.AddMember("questFlags",form.questFlags,allocator);doc.AddMember("questType",form.questType,allocator);doc.AddMember("questPriority",form.questPriority,allocator);doc.AddMember("questDelayTime",form.questDelayTime,allocator);AddConditionArray(doc,allocator,"conditions",form.conditions);AddConditionArray(doc,allocator,"questStoryConditions",form.questStoryConditions);AddFormRefArray(doc,allocator,"questTextGlobals",form.questTextGlobals);AddAdvancedForms(doc,allocator,form);}
        if(form.kind==DynamicForms::FormKind::Scene){doc.AddMember("sceneFlags",form.sceneFlags,allocator);AddFormRef(doc,allocator,"sceneParentQuest",form.sceneParentQuest);AddNumberVector(doc,allocator,"sceneActors",form.sceneActors);AddNumberVector(doc,allocator,"sceneActorFlags",form.sceneActorFlags);AddNumberVector(doc,allocator,"sceneActorBehaviorFlags",form.sceneActorBehaviorFlags);AddConditionArray(doc,allocator,"conditions",form.conditions);AddAdvancedForms(doc,allocator,form);}
        if(form.kind==DynamicForms::FormKind::StoryManagerBranchNode||form.kind==DynamicForms::FormKind::StoryManagerQuestNode||form.kind==DynamicForms::FormKind::StoryManagerEventNode){AddFormRef(doc,allocator,"storyParent",form.storyParent);AddFormRef(doc,allocator,"storyPreviousSibling",form.storyPreviousSibling);doc.AddMember("storyMaxQuests",form.storyMaxQuests,allocator);doc.AddMember("storyNodeFlags",form.storyNodeFlags,allocator);doc.AddMember("storyQuestFlags",form.storyQuestFlags,allocator);AddConditionArray(doc,allocator,"conditions",form.conditions);AddFormRefArray(doc,allocator,"storyChildren",form.storyChildren);doc.AddMember("storyNumQuestsToStart",form.storyNumQuestsToStart,allocator);AddString(doc,allocator,"storyEventId",form.storyEventId);AddAdvancedForms(doc,allocator,form);}
        if (form.kind == DynamicForms::FormKind::Package) {
            doc.AddMember("packageFlags", form.packageFlags, allocator);
            doc.AddMember("packageType", form.packageType, allocator);
            doc.AddMember("packageProcedureType", form.packageProcedureType, allocator);
            doc.AddMember("packageInterruptType", form.packageInterruptType, allocator);
            doc.AddMember("packagePreferredSpeed", form.packagePreferredSpeed, allocator);
            doc.AddMember("packageInterruptFlags", form.packageInterruptFlags, allocator);
            doc.AddMember("packageSpecificFlags", form.packageSpecificFlags, allocator);
            doc.AddMember("packageIdleFlags", form.packageIdleFlags, allocator);
            doc.AddMember("packageIdleTimer", form.packageIdleTimer, allocator);
            AddFormRefArray(doc, allocator, "packageIdles", form.packageIdles);
            AddFormRef(doc, allocator, "packageTemplate", form.packageTemplate);
            doc.AddMember("packageMonth", form.packageMonth, allocator);
            doc.AddMember("packageDayOfWeek", form.packageDayOfWeek, allocator);
            doc.AddMember("packageDate", form.packageDate, allocator);
            doc.AddMember("packageHour", form.packageHour, allocator);
            doc.AddMember("packageMinute", form.packageMinute, allocator);
            doc.AddMember("packageDuration", form.packageDuration, allocator);
            AddConditionArray(doc, allocator, "conditions", form.conditions);
            AddFormRef(doc, allocator, "packageCombatStyle", form.packageCombatStyle);
            AddFormRef(doc, allocator, "packageOwnerQuest", form.packageOwnerQuest);
            doc.AddMember("packageLocationType", form.packageLocationType, allocator);
            doc.AddMember("packageLocationRadius", form.packageLocationRadius, allocator);
            AddFormRef(doc, allocator, "packageLocationObject", form.packageLocationObject);
            doc.AddMember("packageLocationValue", form.packageLocationValue, allocator);
            doc.AddMember("packageTargetType", form.packageTargetType, allocator);
            AddFormRef(doc, allocator, "packageTargetForm", form.packageTargetForm);
            doc.AddMember("packageTargetAlias", form.packageTargetAlias, allocator);
            doc.AddMember("packageTargetValue", form.packageTargetValue, allocator);
            AddAdvancedForms(doc, allocator, form);
        }
        if (form.kind == DynamicForms::FormKind::Race) {
            AddString(doc, allocator, "fullName", form.fullName);
            AddFormRefArray(doc, allocator, "keywords", form.keywords);
            AddFormRefArray(doc, allocator, "spells", form.spells);
            AddFormRef(doc, allocator, "skin", form.skin);
            doc.AddMember("raceFlags", form.raceFlags, allocator);
            doc.AddMember("raceFlags2", form.raceFlags2, allocator);
            doc.AddMember("raceSize", form.raceSize, allocator);
            AddNumberArray(doc, allocator, "raceSkillBoostSkills", form.raceSkillBoostSkills);
            AddNumberArray(doc, allocator, "raceSkillBoostBonuses", form.raceSkillBoostBonuses);
            AddNumberArray(doc, allocator, "raceHeight", form.raceHeight);
            AddNumberArray(doc, allocator, "raceWeight", form.raceWeight);
            AddNumberArray(doc, allocator, "raceStats", form.raceStats);
            AddString(doc, allocator, "raceSkeletonMale", form.raceSkeletonModels[0]);
            AddString(doc, allocator, "raceSkeletonFemale", form.raceSkeletonModels[1]);
            AddString(doc, allocator, "raceBehaviorMale", form.raceBehaviorGraphs[0]);
            AddString(doc, allocator, "raceBehaviorFemale", form.raceBehaviorGraphs[1]);
            AddFormRef(doc, allocator, "raceVoiceMale", form.raceVoiceTypes[0]);
            AddFormRef(doc, allocator, "raceVoiceFemale", form.raceVoiceTypes[1]);
            AddFormRef(doc, allocator, "raceBodyPartData", form.raceBodyPartData);
            AddFormRef(doc, allocator, "raceDecapitateMale", form.raceDecapitateArmors[0]);
            AddFormRef(doc, allocator, "raceDecapitateFemale", form.raceDecapitateArmors[1]);
            AddFormRef(doc, allocator, "raceBloodMaterial", form.raceBloodMaterial);
            AddFormRef(doc, allocator, "raceImpactDataSet", form.raceImpactDataSet);
            AddFormRef(doc, allocator, "raceDismemberBlood", form.raceDismemberBlood);
            AddFormRef(doc, allocator, "raceCorpseOpenSound", form.raceCorpseOpenSound);
            AddFormRef(doc, allocator, "raceCorpseCloseSound", form.raceCorpseCloseSound);
            AddFormRefArray(doc, allocator, "raceEquipSlots", form.raceEquipSlots);
            doc.AddMember("raceValidEquipTypes", form.raceValidEquipTypes, allocator);
            AddFormRef(doc, allocator, "raceUnarmedEquipSlot", form.raceUnarmedEquipSlot);
            AddFormRef(doc, allocator, "raceMorphRace", form.raceMorphRace);
            AddFormRef(doc, allocator, "raceArmorParentRace", form.raceArmorParentRace);
            constexpr std::array raceMoveKeys{"raceMoveWalk", "raceMoveRun",   "raceMoveSwim",
                                              "raceMoveFly",  "raceMoveSneak", "raceMoveSprint"};
            for (std::size_t i = 0; i < raceMoveKeys.size(); ++i)
                AddFormRef(doc, allocator, raceMoveKeys[i], form.raceMovementTypes[i]);
            AddString(doc, allocator, "raceBodyTextureMale", form.raceBodyTextureModels[0]);
            AddString(doc, allocator, "raceBodyTextureFemale", form.raceBodyTextureModels[1]);
            constexpr std::array raceHeadPartKeys{"raceHeadPartsMale", "raceHeadPartsFemale"};
            constexpr std::array racePresetKeys{"racePresetsMale", "racePresetsFemale"};
            constexpr std::array raceHairColorKeys{"raceHairColorsMale", "raceHairColorsFemale"};
            constexpr std::array raceFaceDetailKeys{"raceFaceDetailsMale", "raceFaceDetailsFemale"};
            constexpr std::array raceDefaultFaceKeys{"raceDefaultFaceMale", "raceDefaultFaceFemale"};
            constexpr std::array raceDefaultHairKeys{"raceDefaultHairMale", "raceDefaultHairFemale"};
            for (std::size_t i = 0; i < 2; ++i) {
                AddFormRefArray(doc, allocator, raceHeadPartKeys[i], form.raceHeadParts[i]);
                AddFormRefArray(doc, allocator, racePresetKeys[i], form.racePresetNPCs[i]);
                AddFormRefArray(doc, allocator, raceHairColorKeys[i], form.raceHairColors[i]);
                AddFormRefArray(doc, allocator, raceFaceDetailKeys[i], form.raceFaceDetailTextures[i]);
                AddFormRef(doc, allocator, raceDefaultFaceKeys[i], form.raceDefaultFaceDetails[i]);
                AddFormRef(doc, allocator, raceDefaultHairKeys[i], form.raceDefaultHairColors[i]);
            }
            AddNumberArray(doc, allocator, "raceMorphFlags", form.raceMorphFlags);
            AddStringArray(doc, allocator, "raceBipedObjectNames", form.raceBipedObjectNames);
            AddStringArray(doc, allocator, "racePhonemeTargets", form.racePhonemeTargets);
            AddFormRef(doc, allocator, "raceAttackRace", form.raceAttackRace);
            doc.AddMember("raceFaceClamp", form.raceFaceClamp, allocator);
            doc.AddMember("raceFaceClamp2", form.raceFaceClamp2, allocator);
            AddNumberArray(doc, allocator, "raceMountData", form.raceMountData);
            AddNumberArray(doc, allocator, "raceAngularData", form.raceAngularData);
            AddAdvancedForms(doc, allocator, form);
        }
        if (form.kind == DynamicForms::FormKind::MagicEffect) {
            AddString(doc, allocator, "fullName", form.fullName);
            AddString(doc, allocator, "magicItemDescription", form.magicItemDescription);
            AddFormRefArray(doc, allocator, "keywords", form.keywords);
            AddFormRef(doc, allocator, "menuDisplayObject", form.menuDisplayObject);
            doc.AddMember("magicEffectFlags", form.magicEffectFlags, allocator);
            doc.AddMember("magicEffectBaseCost", form.magicEffectBaseCost, allocator);
            AddFormRef(doc, allocator, "magicEffectAssociatedForm", form.magicEffectAssociatedForm);
            doc.AddMember("magicEffectAssociatedSkill", form.magicEffectAssociatedSkill, allocator);
            doc.AddMember("magicEffectResistVariable", form.magicEffectResistVariable, allocator);
            AddFormRefArray(doc, allocator, "magicEffectCounterEffects", form.magicEffectCounterEffects);
            AddFormRef(doc, allocator, "magicEffectLight", form.magicEffectLight);
            doc.AddMember("magicEffectTaperWeight", form.magicEffectTaperWeight, allocator);
            AddFormRef(doc, allocator, "magicEffectShader", form.magicEffectShader);
            AddFormRef(doc, allocator, "magicEffectEnchantShader", form.magicEffectEnchantShader);
            doc.AddMember("magicEffectMinimumSkill", form.magicEffectMinimumSkill, allocator);
            doc.AddMember("magicEffectSpellmakingArea", form.magicEffectSpellmakingArea, allocator);
            doc.AddMember("magicEffectSpellmakingChargeTime", form.magicEffectSpellmakingChargeTime, allocator);
            doc.AddMember("magicEffectTaperCurve", form.magicEffectTaperCurve, allocator);
            doc.AddMember("magicEffectTaperDuration", form.magicEffectTaperDuration, allocator);
            doc.AddMember("magicEffectSecondAVWeight", form.magicEffectSecondAVWeight, allocator);
            doc.AddMember("magicEffectArchetype", form.magicEffectArchetype, allocator);
            doc.AddMember("magicEffectPrimaryAV", form.magicEffectPrimaryAV, allocator);
            AddFormRef(doc, allocator, "magicEffectProjectile", form.magicEffectProjectile);
            AddFormRef(doc, allocator, "magicEffectExplosion", form.magicEffectExplosion);
            doc.AddMember("magicEffectCastingType", form.magicEffectCastingType, allocator);
            doc.AddMember("magicEffectDelivery", form.magicEffectDelivery, allocator);
            doc.AddMember("magicEffectSecondaryAV", form.magicEffectSecondaryAV, allocator);
            AddFormRef(doc, allocator, "magicEffectCastingArt", form.magicEffectCastingArt);
            AddFormRef(doc, allocator, "magicEffectHitEffectArt", form.magicEffectHitEffectArt);
            AddFormRef(doc, allocator, "magicEffectImpactDataSet", form.magicEffectImpactDataSet);
            doc.AddMember("magicEffectSkillUsageMult", form.magicEffectSkillUsageMult, allocator);
            AddFormRef(doc, allocator, "magicEffectDualCastData", form.magicEffectDualCastData);
            doc.AddMember("magicEffectDualCastScale", form.magicEffectDualCastScale, allocator);
            AddFormRef(doc, allocator, "magicEffectEnchantEffectArt", form.magicEffectEnchantEffectArt);
            AddFormRef(doc, allocator, "magicEffectHitVisuals", form.magicEffectHitVisuals);
            AddFormRef(doc, allocator, "magicEffectEnchantVisuals", form.magicEffectEnchantVisuals);
            AddFormRef(doc, allocator, "magicEffectEquipAbility", form.magicEffectEquipAbility);
            AddFormRef(doc, allocator, "magicEffectImageSpaceMod", form.magicEffectImageSpaceMod);
            AddFormRef(doc, allocator, "magicEffectPerk", form.magicEffectPerk);
            doc.AddMember("magicEffectCastingSoundLevel", form.magicEffectCastingSoundLevel, allocator);
            doc.AddMember("magicEffectAIScore", form.magicEffectAIScore, allocator);
            doc.AddMember("magicEffectAIDelayTime", form.magicEffectAIDelayTime, allocator);
            for (std::size_t i = 0; i < form.magicEffectSounds.size(); ++i) {
                const auto key = std::format("magicEffectSound{}", i);
                AddFormRef(doc, allocator, key.c_str(), form.magicEffectSounds[i]);
            }
            rapidjson::Value conditions(rapidjson::kArrayType);
            for (const auto& condition : form.conditions) {
                WriteCondition(conditions, allocator, condition);
            }
            doc.AddMember("conditions", conditions, allocator);
        }
        if ((form.kind == DynamicForms::FormKind::AlchemyItem ||
                form.kind == DynamicForms::FormKind::Ingredient ||
                form.kind == DynamicForms::FormKind::Spell ||
                form.kind == DynamicForms::FormKind::Enchantment ||
                form.kind == DynamicForms::FormKind::Scroll) &&
            form.magicEffectsOverride) {
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
        if (form.pluginNumber != 0) {
            doc.AddMember("pluginNumber", form.pluginNumber, allocator);
        }
        if (form.localId != 0) {
            doc.AddMember("localId", form.localId, allocator);
        }

        return true;
    }

    bool SaveForm(const DynamicForms::DynamicForm& form) {
        if (form.externalPatch) {
            return PersistExternalPatch(form);
        }

        std::error_code ec;
        std::filesystem::create_directories(FORMS_DIR, ec);
        if (ec) {
            logger::warn("Could not create forms directory '{}': {}", FORMS_DIR, ec.message());
            return false;
        }

        rapidjson::Document doc;
        return BuildFormDocument(form, doc) && PersistFormDocument(form, doc);
    }

    std::string SerializeFormJson(const DynamicForms::DynamicForm& form) {
        rapidjson::Document doc;
        if (!BuildFormDocument(form, doc)) {
            return {};
        }
        return JsonString(doc);
    }

    bool SaveForm(const std::size_t index, const bool dispatchUpdate) {
        if (index >= forms.size()) {
            return false;
        }

        if (forms[index].externalPatch) {
            const auto previouslyAppliedFields = forms[index].externalChangedFields;
            if (!RefreshExternalFieldProvenance(forms[index])) {
                return false;
            }
            forms[index].externalPendingApplyFields = previouslyAppliedFields;
            AppendUniqueFields(forms[index].externalPendingApplyFields, forms[index].externalChangedFields);
            if (!PersistExternalPatch(forms[index])) {
                return false;
            }
            forms[index].externalPersisted = true;
            if (!ApplyExternalResolvedForm(forms[index])) {
                return false;
            }
            forms[index].dirty = false;
            ListManager::GetSingleton()->PopulateAllLists(true);
            if (dispatchUpdate) {
                DispatchEvent(
                    UPDATED_EVENT,
                    ToSignature(forms[index].kind),
                    static_cast<float>(forms[index].externalLocalId));
            }
            return true;
        }

        NormalizePerkForm(forms[index]);
        std::vector<std::string> validationErrors;
        if (!ValidateForm(forms[index], validationErrors)) {
            logger::warn(
                "Could not save '{}': {}",
                forms[index].editorId,
                JoinValidationErrors(validationErrors));
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
            if (forms[i].externalPatch) {
                if (!forms[i].dirty) {
                    continue;
                }
                const auto previouslyAppliedFields = forms[i].externalChangedFields;
                const bool refreshed = RefreshExternalFieldProvenance(forms[i]);
                if (refreshed) {
                    forms[i].externalPendingApplyFields = previouslyAppliedFields;
                    AppendUniqueFields(forms[i].externalPendingApplyFields, forms[i].externalChangedFields);
                }
                const bool persisted = refreshed && PersistExternalPatch(forms[i]);
                if (persisted) {
                    forms[i].externalPersisted = true;
                }
                if (persisted && ApplyExternalResolvedForm(forms[i])) {
                    forms[i].dirty = dispatchUpdate ? false : wasDirty;
                    updatedSignatures.insert(ToSignature(forms[i].kind));
                    ++savedCount;
                } else {
                    saved = false;
                }
                continue;
            }
            NormalizePerkForm(forms[i]);
            std::vector<std::string> validationErrors;
            if (!ValidateForm(forms[i], validationErrors)) {
                logger::warn(
                    "Could not save '{}': {}",
                    forms[i].editorId,
                    JoinValidationErrors(validationErrors));
                saved = false;
                continue;
            }
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

    const char* GetListTypeName(const DynamicForms::FormKind kind) {
        return ListTypeName(kind);
    }

    const char* GetStoredFormKindName(
        const DynamicForms::FormKind kind)
    {
        static thread_local std::string name;
        name = ToString(kind);
        return name.c_str();
    }

    bool SupportsExternalPatch(const DynamicForms::FormKind kind) {
        return SupportsExternalRuntimePatch(kind);
    }

    std::int32_t GetPackagePriority(const std::string_view packageName) {
        if (packageName.empty() || !WritePackageManifest(packageName)) {
            return 0;
        }

        std::ifstream stream(PackageManifestPath(packageName));
        rapidjson::IStreamWrapper wrapper(stream);
        rapidjson::Document doc;
        doc.ParseStream(wrapper);
        return !doc.HasParseError() && doc.IsObject() &&
                doc.HasMember("priority") && doc["priority"].IsInt() ?
            doc["priority"].GetInt() :
            0;
    }

    bool SetPackagePriority(const std::string_view packageName, const std::int32_t priority) {
        if (packageName.empty() || !WritePackageManifest(packageName)) {
            return false;
        }

        const auto path = PackageManifestPath(packageName);
        rapidjson::Document doc;
        {
            std::ifstream stream(path);
            rapidjson::IStreamWrapper wrapper(stream);
            doc.ParseStream(wrapper);
        }
        if (doc.HasParseError() || !doc.IsObject()) {
            return false;
        }

        if (doc.HasMember("priority")) {
            doc["priority"].SetInt(priority);
        } else {
            doc.AddMember("priority", priority, doc.GetAllocator());
        }

        std::ofstream stream(path, std::ios::trunc);
        if (!stream.is_open()) {
            return false;
        }
        rapidjson::OStreamWrapper wrapper(stream);
        rapidjson::PrettyWriter writer(wrapper);
        doc.Accept(writer);
        return true;
    }

    std::vector<ExternalPatchFieldView> GetExternalPatchFieldViews(const std::size_t index) {
        std::vector<ExternalPatchFieldView> result;
        if (index >= forms.size() || !forms[index].externalPatch ||
            forms[index].externalBaselinePayload.empty()) {
            return result;
        }

        rapidjson::Document baseline;
        baseline.Parse(forms[index].externalBaselinePayload.c_str());
        rapidjson::Document resolved;
        if (baseline.HasParseError() || !BuildFormDocument(forms[index], resolved)) {
            return result;
        }

        const auto stringify = [](const rapidjson::Value* value) {
            if (!value) {
                return std::string("<unset>");
            }
            if (value->IsString()) {
                return std::string(value->GetString(), value->GetStringLength());
            }
            rapidjson::StringBuffer buffer;
            rapidjson::Writer writer(buffer);
            value->Accept(writer);
            return std::string(buffer.GetString());
        };

        std::set<std::string> fields;
        for (auto member = baseline.MemberBegin(); member != baseline.MemberEnd(); ++member) {
            const std::string_view key(member->name.GetString(), member->name.GetStringLength());
            if (!IsExternalPatchMetadataKey(key)) {
                fields.emplace(key);
            }
        }
        for (auto member = resolved.MemberBegin(); member != resolved.MemberEnd(); ++member) {
            const std::string_view key(member->name.GetString(), member->name.GetStringLength());
            if (!IsExternalPatchMetadataKey(key)) {
                fields.emplace(key);
            }
        }

        for (const auto& field : fields) {
            const auto inherited = baseline.FindMember(field.c_str());
            const auto finalValue = resolved.FindMember(field.c_str());
            const rapidjson::Value* inheritedValue =
                inherited == baseline.MemberEnd() ? nullptr : std::addressof(inherited->value);
            const rapidjson::Value* resolvedValue =
                finalValue == resolved.MemberEnd() ? nullptr : std::addressof(finalValue->value);
            if (inheritedValue && resolvedValue && *inheritedValue == *resolvedValue) {
                continue;
            }
            result.push_back({
                field,
                stringify(inheritedValue),
                resolvedValue ? stringify(resolvedValue) : std::string("<cleared>"),
                std::ranges::find(forms[index].externalConflictingFields, field) !=
                    forms[index].externalConflictingFields.end(),
                (inheritedValue && inheritedValue->IsArray()) ||
                    (resolvedValue && resolvedValue->IsArray()),
                forms[index].externalArrayOperations.contains(field) ?
                    forms[index].externalArrayOperations.at(field) :
                    std::string("replace")
            });
        }
        return result;
    }

    bool SetExternalPatchArrayOperation(
        const std::size_t index,
        const std::string_view field,
        const std::string_view operation)
    {
        if (index >= forms.size() ||
            !forms[index].externalPatch ||
            field.empty() ||
            (operation != "replace" && operation != "merge"))
        {
            return false;
        }

        forms[index].externalArrayOperations[std::string(field)] = operation;
        forms[index].dirty = true;
        return true;
    }

    bool PopulateFormFromGameTemplate(DynamicForms::DynamicForm& form, const DynamicForms::FormRef& templateRef) {
        auto* source = ResolveConfigForm(templateRef);
        if (!source || source->GetFormType() != static_cast<RE::FormType>(FormTypeForKind(form.kind))) {
            logger::warn("Template '{}' does not resolve to the selected {} type.", templateRef.Display(), ToString(form.kind));
            return false;
        }

        bool captured = false;
        switch (form.kind) {
        case DynamicForms::FormKind::Global:
            if (auto* value = source->As<RE::TESGlobal>()) captured = CaptureGlobalTemplate(form, *value);
            break;
        case DynamicForms::FormKind::Keyword:
            captured = source->As<RE::BGSKeyword>() != nullptr;
            break;
        case DynamicForms::FormKind::FormList:
            if (auto* value = source->As<RE::BGSListForm>()) captured = CaptureFormListTemplate(form, *value);
            break;
        case DynamicForms::FormKind::EquipSlot:
            if (auto* value = source->As<RE::BGSEquipSlot>()) captured = CaptureEquipSlotTemplate(form, *value);
            break;
        case DynamicForms::FormKind::VoiceType:
            if (auto* value = source->As<RE::BGSVoiceType>()) captured = CaptureVoiceTypeTemplate(form, *value);
            break;
        case DynamicForms::FormKind::Outfit:
            if (auto* value = source->As<RE::BGSOutfit>()) captured = CaptureOutfitTemplate(form, *value);
            break;
        case DynamicForms::FormKind::Armor:
            if (auto* armor = source->As<RE::TESObjectARMO>()) {
                captured = CaptureArmorTemplate(form, *armor);
            }
            break;
        case DynamicForms::FormKind::ArmorType:
            if (auto* armorType = source->As<RE::TESObjectARMA>()) {
                captured = CaptureArmorTypeTemplate(form, *armorType);
            }
            break;
        case DynamicForms::FormKind::Book:
            if (auto* value = source->As<RE::TESObjectBOOK>()) captured = CaptureBookTemplate(form, *value);
            break;
        case DynamicForms::FormKind::Misc:
            if (auto* value = source->As<RE::TESObjectMISC>()) captured = CaptureMiscTemplate(form, *value);
            break;
        case DynamicForms::FormKind::Key:
            if (auto* value = source->As<RE::TESKey>()) captured = CaptureKeyTemplate(form, *value);
            break;
        case DynamicForms::FormKind::SoulGem:
            if (auto* value = source->As<RE::TESSoulGem>()) captured = CaptureSoulGemTemplate(form, *value);
            break;
        case DynamicForms::FormKind::MaterialType:
            if (auto* value = source->As<RE::BGSMaterialType>()) captured = CaptureMaterialTypeTemplate(form, *value);
            break;
        case DynamicForms::FormKind::Ammo:
            if (auto* value = source->As<RE::TESAmmo>()) captured = CaptureAmmoTemplate(form, *value);
            break;
        case DynamicForms::FormKind::Weapon:
            if (auto* value = source->As<RE::TESObjectWEAP>()) captured = CaptureWeaponTemplate(form, *value);
            break;
        case DynamicForms::FormKind::AlchemyItem:
            if (auto* value = source->As<RE::AlchemyItem>()) captured = CaptureAlchemyTemplate(form, *value);
            break;
        case DynamicForms::FormKind::Ingredient:
            if (auto* value = source->As<RE::IngredientItem>()) captured = CaptureIngredientTemplate(form, *value);
            break;
        case DynamicForms::FormKind::Spell:
            if (auto* value = source->As<RE::SpellItem>()) captured = CaptureSpellTemplate(form, *value);
            break;
        case DynamicForms::FormKind::Enchantment:
            if (auto* value = source->As<RE::EnchantmentItem>()) captured = CaptureEnchantmentTemplate(form, *value);
            break;
        case DynamicForms::FormKind::Scroll:
            if (auto* value = source->As<RE::ScrollItem>()) captured = CaptureScrollTemplate(form, *value);
            break;
        default:
            captured = CaptureAdditionalSimpleTemplate(form, *source);
            if (!captured) {
                logger::warn("Game template capture is not implemented yet for {}.", ToString(form.kind));
                return false;
            }
            break;
        }

        if (captured)
            logger::info("Copied {} fields from game template '{}' into '{}'.", ToString(form.kind), templateRef.Display(), form.editorId);
        return captured;
    }

    bool AddForm(const DynamicForms::DynamicForm& form) {
        const bool reservedEditorId = IsEditorIdReserved(form.editorId);
        if (form.editorId.empty() || HasEditorId(form.editorId) || reservedEditorId) {
            if (reservedEditorId) {
                logger::warn("Could not create '{}': the EditorID belongs to an existing game form.", form.editorId);
            }
            return false;
        }

        auto createdForm = form;
        NormalizePerkForm(createdForm);
        std::vector<std::string> validationErrors;
        if (!ValidateForm(createdForm, validationErrors)) {
            logger::warn(
                "Could not create '{}': {}",
                createdForm.editorId,
                JoinValidationErrors(validationErrors));
            return false;
        }
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

    bool AddExternalPatch(
        const DynamicForms::FormRef& targetRef,
        const DynamicForms::FormKind kind,
        const std::string_view packageName)
    {
        if (packageName.empty() || !SupportsExternalRuntimePatch(kind)) {
            logger::warn(
                "Could not create external patch: package is empty or {} is not enabled for safe runtime patching.",
                ToString(kind));
            return false;
        }

        auto* target = ResolveConfigForm(targetRef);
        if (!target ||
            target->GetFormType() != static_cast<RE::FormType>(FormTypeForKind(kind)))
        {
            logger::warn(
                "Could not create external {} patch: target '{}' is missing or has another type.",
                ToString(kind),
                targetRef.Display());
            return false;
        }

        const auto normalizedId = FormUtil::NormalizeFormID(target);
        std::string sourcePlugin;
        std::uint32_t localFormId = 0;
        if (!ParseNormalizedExternalFormId(normalizedId, sourcePlugin, localFormId)) {
            logger::warn(
                "Could not patch {:08X}: temporary/dynamic forms do not have a stable plugin-local identity.",
                target->GetFormID());
            return false;
        }

        const auto existing = FindExternalPatchForm(sourcePlugin, localFormId, kind);
        if (existing != forms.end()) {
            return AddExternalPatchLayer(
                static_cast<std::size_t>(std::distance(forms.begin(), existing)),
                packageName);
        }

        DynamicForms::DynamicForm patch;
        std::string baseline;
        if (!BuildExternalBaseline(*target, kind, packageName, patch, baseline)) {
            return false;
        }

        patch.externalBaselinePayload = std::move(baseline);
        patch.dirty = true;
        forms.push_back(std::move(patch));
        logger::info(
            "Created external patch draft for {}|{:X} ({}) in package '{}'.",
            sourcePlugin,
            localFormId,
            ToString(kind),
            packageName);
        return true;
    }

    bool AddExternalPatchLayer(const std::size_t index, const std::string_view packageName) {
        if (index >= forms.size() || !forms[index].externalPatch || packageName.empty()) {
            return false;
        }

        auto& form = forms[index];
        if (form.externalEditPackage == packageName) {
            return true;
        }
        if (std::ranges::find(form.patchPackageNames, packageName) != form.patchPackageNames.end()) {
            logger::warn(
                "External patch '{}|{:X}' package '{}' is not the final resolved layer and cannot be edited in place.",
                form.externalSourcePlugin,
                form.externalLocalId,
                packageName);
            return false;
        }

        rapidjson::Document baseline;
        if (!BuildFormDocument(form, baseline)) {
            return false;
        }

        form.externalBaselinePayload = JsonString(baseline);
        form.externalInheritedChangedFields = form.externalChangedFields;
        form.externalInheritedConflictingFields = form.externalConflictingFields;
        form.externalArrayOperations.clear();
        form.externalEditPackage = std::string(packageName);
        form.patchPackageNames.emplace_back(packageName);
        form.externalPersisted = false;
        form.dirty = true;
        return true;
    }

    bool UpdateForm(const std::size_t index, const DynamicForms::DynamicForm& form) {
        if (index >= forms.size() || form.editorId.empty() || forms[index].editorId != form.editorId) {
            return false;
        }

        forms[index] = form;
        NormalizePerkForm(forms[index]);
        forms[index].dirty = true;
        return true;
    }

    bool DeleteForm(const std::size_t index) {
        if (index >= forms.size()) {
            return false;
        }

        if (forms[index].externalPatch) {
            const auto removed = forms[index];
            DynamicForms::DynamicForm previous;
            if (!ReadFormPayload(
                    removed.externalBaselinePayload,
                    std::format("external patch rollback {}|{:X}", removed.externalSourcePlugin, removed.externalLocalId),
                    removed.editorId,
                    previous))
            {
                logger::warn("External patch baseline is invalid; the stored layer was not deleted.");
                return false;
            }

            if (removed.externalPersisted && !DeleteStoredExternalPatch(removed)) {
                logger::warn(
                    "Could not delete external patch '{}|{:X}' from package '{}'.",
                    removed.externalSourcePlugin,
                    removed.externalLocalId,
                    removed.externalEditPackage);
                return false;
            }

            previous.externalPatch = true;
            previous.externalSourcePlugin = removed.externalSourcePlugin;
            previous.externalLocalId = removed.externalLocalId;
            previous.externalWinningPlugin = removed.externalWinningPlugin;
            previous.packageName = removed.packageName;
            previous.basePackageName = removed.externalSourcePlugin;
            previous.patchPackageNames = removed.patchPackageNames;
            std::erase(previous.patchPackageNames, removed.externalEditPackage);
            previous.externalEditPackage.clear();
            previous.externalBaselinePayload.clear();
            previous.externalChangedFields = removed.externalChangedFields;
            previous.externalConflictingFields = removed.externalInheritedConflictingFields;
            previous.dirty = false;

            if (removed.externalPersisted && !ApplyExternalResolvedForm(previous)) {
                logger::warn("External patch was deleted but its previous runtime state could not be reapplied.");
            }
            previous.externalChangedFields = removed.externalInheritedChangedFields;
            previous.externalPersisted = true;

            const auto signature = ToSignature(removed.kind);
            if (previous.patchPackageNames.empty()) {
                forms.erase(forms.begin() + static_cast<std::ptrdiff_t>(index));
            } else {
                forms[index] = std::move(previous);
            }
            ListManager::GetSingleton()->PopulateAllLists(true);
            DispatchEvent(UPDATED_EVENT, signature, static_cast<float>(removed.externalLocalId));
            return true;
        }

        auto* api = DPF::GetAPI();
        if (!api) {
            logger::warn("Dynamic Persistent Forms API is not available. '{}' will not be deleted.", forms[index].editorId);
            return false;
        }

        const auto form = forms[index];
        auto runtimeSnapshot = form;
        bool recoveredExistingSlot = false;
        auto* runtimeForm =
            ResolveDPFFormObject(runtimeSnapshot, false, &recoveredExistingSlot);
        const auto& releasedForm = runtimeForm ? runtimeSnapshot : form;
        auto* runtimePerk = runtimeForm ? runtimeForm->As<RE::BGSPerk>() : nullptr;
        const auto actorSnapshots = runtimePerk ?
            RemovePerkFromLoadedActors(*runtimePerk) :
            std::vector<RemovedActorPerkSnapshot>{};

        bool released = false;
        if (releasedForm.pluginNumber != 0 && releasedForm.localId != 0) {
            released = api->ReleaseByPluginLocalId(
                releasedForm.pluginNumber,
                releasedForm.localId,
                DPF_OWNER);
        }
        if (!released) {
            released = api->ReleaseByOwnerKey(DPF_OWNER, form.editorId.c_str());
        }
        if (!released) {
            if (runtimePerk) {
                RestorePerkToLoadedActors(*runtimePerk, actorSnapshots);
            }
            logger::warn(
                "DPF release failed for dynamic form '{}' slot {}:{:06X}.",
                form.editorId,
                releasedForm.pluginNumber,
                releasedForm.localId);
            return false;
        }

        if (!DeleteStoredForm(form)) {
            logger::warn("Could not delete dynamic form '{}' from package storage.", form.editorId);
            auto restored = form;
            restored.pluginNumber = 0;
            restored.localId = 0;
            if (auto* restoredRuntime = ResolveDPFFormObject(restored, true)) {
                forms[index] = restored;
                SaveForm(forms[index]);
                if (auto* restoredPerk = restoredRuntime->As<RE::BGSPerk>()) {
                    RestorePerkToLoadedActors(*restoredPerk, actorSnapshots);
                }
            }
            return false;
        }

        const auto signature = ToSignature(form.kind);
        forms.erase(forms.begin() + static_cast<std::ptrdiff_t>(index));
        ListManager::GetSingleton()->PopulateAllLists(true);
        DispatchEvent(UPDATED_EVENT, signature, static_cast<float>(releasedForm.localId));
        logger::info(
            "Deleted dynamic form '{}' from DPF slot {}:{:06X}.",
            form.editorId,
            releasedForm.pluginNumber,
            releasedForm.localId);
        return true;
    }

    bool AssignFormToPackage(const std::string_view editorId, const std::string_view packageName, const bool save) {
        if (editorId.empty() || packageName.empty()) {
            return false;
        }

        const auto found = std::ranges::find_if(forms, [editorId](const DynamicForms::DynamicForm& form) {
            return !form.externalPatch && form.editorId == editorId;
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
            return !form.externalPatch && form.editorId == editorId;
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

    bool StopSoundPreview() {
        if (!soundPreviewHandle.IsValid()) {
            soundPreviewHandle = RE::BSSoundHandle{};
            soundPreviewEditorId.clear();
            return true;
        }

        const bool stopped = soundPreviewHandle.Stop();
        soundPreviewHandle = RE::BSSoundHandle{};
        soundPreviewEditorId.clear();
        return stopped;
    }

    bool IsSoundPreviewPlaying(const std::string_view editorId) {
        return !editorId.empty() &&
               soundPreviewEditorId == editorId &&
               soundPreviewHandle.IsValid() &&
               soundPreviewHandle.IsPlaying();
    }

    RE::BGSSoundDescriptorForm* PrepareSoundPreview(const std::size_t index) {
        if (index >= forms.size()) {
            return nullptr;
        }

        auto& form = forms[index];
        if (form.kind != DynamicForms::FormKind::SoundDescriptor &&
            form.kind != DynamicForms::FormKind::Sound) {
            logger::warn("Could not preview '{}': {} is not a playable sound form.", form.editorId, ToString(form.kind));
            return nullptr;
        }

        auto* runtimeForm = ResolveDPFFormObject(form, false);
        if (!runtimeForm || !ConfigureForm(runtimeForm, form)) {
            logger::warn("Could not preview '{}': the runtime sound form could not be configured.", form.editorId);
            return nullptr;
        }

        RE::BGSSoundDescriptorForm* descriptor = nullptr;
        if (form.kind == DynamicForms::FormKind::SoundDescriptor) {
            descriptor = runtimeForm->As<RE::BGSSoundDescriptorForm>();
        } else if (auto* sound = runtimeForm->As<RE::TESSound>()) {
            descriptor = sound->descriptor;
        }
        if (!descriptor) {
            logger::warn("Could not preview '{}': no sound descriptor is assigned.", form.editorId);
            return nullptr;
        }
        return descriptor;
    }

    bool PlaySoundPreview(const std::size_t index) {
        auto* descriptor = PrepareSoundPreview(index);
        if (!descriptor) {
            return false;
        }

        if (soundPreviewHandle.IsValid()) {
            soundPreviewHandle.Stop();
        }
        soundPreviewHandle = RE::BSSoundHandle{};
        soundPreviewEditorId.clear();

        std::string descriptorEditorId;
        if (forms[index].kind == DynamicForms::FormKind::SoundDescriptor) {
            descriptorEditorId = forms[index].editorId;
        } else if (!forms[index].legacySoundDescriptor.editorID.empty()) {
            descriptorEditorId = forms[index].legacySoundDescriptor.editorID;
        } else {
            descriptorEditorId = FormUtil::GetEditorIDSafe(descriptor);
        }
        if (descriptorEditorId.empty()) {
            logger::warn("Could not play universal preview for '{}': the descriptor has no EditorID.", forms[index].editorId);
            return false;
        }

        RE::PlaySound(descriptorEditorId.c_str());
        logger::info(
            "Requested universal sound preview for '{}' through RE::PlaySound('{}').",
            forms[index].editorId,
            descriptorEditorId);
        return true;
    }

    bool PlaySoundPreviewOnPlayer(const std::size_t index) {
        auto* descriptor = PrepareSoundPreview(index);
        if (!descriptor) {
            return false;
        }

        if (soundPreviewHandle.IsValid()) {
            soundPreviewHandle.Stop();
        }
        soundPreviewHandle = RE::BSSoundHandle{};
        soundPreviewEditorId.clear();

        auto* audio = RE::BSAudioManager::GetSingleton();
        if (!audio || !audio->GetSoundHandle(soundPreviewHandle, descriptor)) {
            logger::warn("Could not preview '{}': the audio manager could not resolve the descriptor.", forms[index].editorId);
            soundPreviewHandle = RE::BSSoundHandle{};
            return false;
        }

        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            if (auto* node = player->Get3D()) {
                soundPreviewHandle.SetObjectToFollow(node);
            } else {
                soundPreviewHandle.SetPosition(player->GetPosition());
            }
        }

        if (!soundPreviewHandle.Play()) {
            logger::warn("Could not preview '{}': the sound handle failed to play.", forms[index].editorId);
            soundPreviewHandle = RE::BSSoundHandle{};
            return false;
        }

        soundPreviewEditorId = forms[index].editorId;
        logger::info("Playing sound preview for '{}' on the player.", forms[index].editorId);
        return true;
    }

    bool AddFormToPlayerInventory(const std::size_t index) {
        if (index >= forms.size()) {
            return false;
        }
        if (forms[index].dirty) {
            logger::warn("Could not add '{}' to inventory: save the form before testing it.", forms[index].editorId);
            return false;
        }

        const auto oldPluginNumber = forms[index].pluginNumber;
        const auto oldLocalId = forms[index].localId;
        const bool configureBeforeTest = forms[index].kind == DynamicForms::FormKind::NPC;
        auto* runtimeForm = ResolveDPFFormObject(forms[index], configureBeforeTest);
        if (!runtimeForm) {
            return false;
        }
        if (forms[index].pluginNumber != oldPluginNumber || forms[index].localId != oldLocalId) {
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

        const auto oldPluginNumber = forms[index].pluginNumber;
        const auto oldLocalId = forms[index].localId;
        const bool configureBeforeTest = forms[index].kind == DynamicForms::FormKind::NPC;
        auto* runtimeForm = ResolveDPFFormObject(forms[index], configureBeforeTest);
        if (!runtimeForm) {
            return false;
        }
        if (forms[index].pluginNumber != oldPluginNumber || forms[index].localId != oldLocalId) {
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
                npc->race ? FormUtil::GetEditorIDSafe(npc->race) : "<null>",
                npc->npcClass ? FormUtil::GetEditorIDSafe(npc->npcClass) : "<null>",
                npc->voiceType ? FormUtil::GetEditorIDSafe(npc->voiceType) : "<null>",
                npc->farSkin ? FormUtil::GetEditorIDSafe(npc->farSkin) : "<null>",
                npc->defaultOutfit ? FormUtil::GetEditorIDSafe(npc->defaultOutfit) : "<null>",
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
        const auto key = NormalizeEditorId(editorId);
        return std::ranges::any_of(forms, [&key](const DynamicForms::DynamicForm& form) {
            return NormalizeEditorId(form.editorId) == key;
        });
    }

    bool IsEditorIdReserved(const std::string_view editorId) {
        if (editorId.empty()) {
            return false;
        }
        if (const auto* indexed = FindExternalFormsByEditorId(editorId); indexed && !indexed->empty()) {
            return true;
        }
        return RE::TESForm::LookupByEditorID(editorId) != nullptr;
    }

    bool IsDirty(const std::size_t index) {
        return index < forms.size() && forms[index].dirty;
    }

    bool HasDirtyForms() {
        return std::ranges::any_of(forms, [](const DynamicForms::DynamicForm& form) {
            return form.dirty;
        });
    }

    bool IsReady() noexcept {
        return apiReady.load(std::memory_order_acquire);
    }

    void ApplyAllForms() {
        apiReady.store(false, std::memory_order_release);
        bool changed = false;
        bool allApplied = true;

        // Create/register every dynamic form first so references between forms in the
        // same package do not depend on database or import ordering.
        for (auto& form : forms) {
            if (form.externalPatch) {
                continue;
            }
            const auto oldPluginNumber = form.pluginNumber;
            const auto oldLocalId = form.localId;
            if (ResolveDPFFormObject(form, false)) {
                if (form.pluginNumber != oldPluginNumber || form.localId != oldLocalId) {
                    changed = true;
                }
            } else {
                allApplied = false;
            }
        }

        if (allApplied) {
            for (auto& form : forms) {
                if (form.externalPatch) {
                    if (!ApplyExternalResolvedForm(form)) {
                        logger::warn(
                            "External patch '{}|{:X}' was skipped; DFG-owned forms will continue loading.",
                            form.externalSourcePlugin,
                            form.externalLocalId);
                    }
                    continue;
                }
                auto* runtimeForm = ResolveDPFFormObject(form, false);
                if (!runtimeForm || !ConfigureForm(runtimeForm, form)) {
                    allApplied = false;
                }
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
        apiReady.store(true, std::memory_order_release);
        DispatchEvent(LOADED_EVENT, "All", static_cast<float>(forms.size()));
    }

}

namespace
{
    template <std::size_t N>
    void CopyResultText(char (&target)[N], const std::string_view value)
    {
        const auto count = (std::min)(value.size(), N - 1);
        std::memcpy(target, value.data(), count);
        target[count] = '\0';
    }

    DFG::FormOperationResult MakeResult(const DFG::Operation operation)
    {
        DFG::FormOperationResult result;
        result.operation = operation;
        return result;
    }

    DFG::FormOperationResult& Fail(
        DFG::FormOperationResult& result,
        const DFG::Status status,
        const std::string_view message)
    {
        result.status = status;
        CopyResultText(result.error, message);
        return result;
    }

    void FillFormResult(
        DFG::FormOperationResult& result,
        const DynamicForms::DynamicForm& form,
        RE::TESForm* runtimeForm,
        const bool recoveredExistingSlot)
    {
        result.form = runtimeForm;
        result.formID = runtimeForm ? runtimeForm->GetFormID() : 0;
        result.pluginNumber = form.pluginNumber;
        result.localId = form.localId;
        result.recoveredExistingSlot = recoveredExistingSlot ? 1 : 0;
        CopyResultText(result.editorId, form.editorId);
        CopyResultText(result.packageName, EffectivePackageName(form));
        CopyResultText(result.pluginName, DPF::PluginNameForNumber(form.pluginNumber));
    }

    bool IsValidExternalEditorId(const std::string_view editorId)
    {
        if (editorId.empty() || editorId.size() >= sizeof(DFG::FormOperationResult{}.editorId)) {
            return false;
        }
        return std::ranges::all_of(editorId, [](const unsigned char ch) {
            return std::isalnum(ch) != 0 || ch == '_';
        });
    }

    bool IsValidExternalPackageName(const std::string_view packageName)
    {
        if (packageName.empty() || packageName.size() >= sizeof(DFG::FormOperationResult{}.packageName) ||
            packageName == "." || packageName == "..")
        {
            return false;
        }
        return std::ranges::all_of(packageName, [](const unsigned char ch) {
            return std::isalnum(ch) != 0 || ch == ' ' || ch == '_' || ch == '-' || ch == '.';
        });
    }

    const char* FindProtectedMember(const rapidjson::Document& doc, const bool update)
    {
        constexpr std::array alwaysProtected{
            "pluginNumber",
            "localId",
            "basePackageName",
            "patchPackageNames"
        };
        for (const auto* key : alwaysProtected) {
            if (doc.HasMember(key)) {
                return key;
            }
        }
        if (update && doc.HasMember("packageName")) {
            return "packageName";
        }
        return nullptr;
    }

    bool ParseJsonObject(
        const std::string& payload,
        rapidjson::Document& doc,
        DFG::FormOperationResult& result)
    {
        if (payload.empty()) {
            Fail(result, DFG::Status::InvalidJson, "JSON payload is empty.");
            return false;
        }

        doc.Parse(payload.c_str());
        if (doc.HasParseError()) {
            Fail(result,
                DFG::Status::InvalidJson,
                std::format(
                    "JSON parse error at offset {}: {}.",
                    doc.GetErrorOffset(),
                    rapidjson::GetParseError_En(doc.GetParseError())));
            return false;
        }
        if (!doc.IsObject()) {
            Fail(result, DFG::Status::InvalidJson, "JSON payload must be an object.");
            return false;
        }
        return true;
    }

    std::optional<DynamicForms::FormKind> ReadRequestedKind(
        const rapidjson::Document& doc,
        const bool required,
        DFG::FormOperationResult& result)
    {
        const auto kindMember = doc.FindMember("formKind");
        if (kindMember == doc.MemberEnd()) {
            if (required) {
                Fail(result, DFG::Status::MissingFormKind, "formKind is required.");
            }
            return std::nullopt;
        }
        if (!kindMember->value.IsString()) {
            Fail(result, DFG::Status::UnsupportedFormKind, "formKind must be a supported name or signature.");
            return std::nullopt;
        }

        const auto kind = TryFormKindFromString(kindMember->value.GetString());
        if (!kind) {
            Fail(result,
                DFG::Status::UnsupportedFormKind,
                std::format("Unsupported formKind '{}'.", kindMember->value.GetString()));
            return std::nullopt;
        }

        const auto signatureMember = doc.FindMember("sourceSignature");
        if (signatureMember != doc.MemberEnd()) {
            if (!signatureMember->value.IsString()) {
                Fail(result, DFG::Status::UnsupportedFormKind, "sourceSignature must be a supported signature.");
                return std::nullopt;
            }
            const auto signatureKind = TryFormKindFromString(signatureMember->value.GetString());
            if (!signatureKind) {
                Fail(result,
                    DFG::Status::UnsupportedFormKind,
                    std::format("Unsupported sourceSignature '{}'.", signatureMember->value.GetString()));
                return std::nullopt;
            }
            if (*signatureKind != *kind) {
                Fail(result, DFG::Status::FormKindMismatch, "formKind and sourceSignature identify different form types.");
                return std::nullopt;
            }
        }
        return kind;
    }

    DFG::Status StatusForResolveFailure(const ResolveDPFFailure failure)
    {
        switch (failure) {
        case ResolveDPFFailure::Unavailable:
            return DFG::Status::DPFUnavailable;
        case ResolveDPFFailure::Configure:
            return DFG::Status::ConfigureFailed;
        case ResolveDPFFailure::Create:
        case ResolveDPFFailure::None:
        default:
            return DFG::Status::DPFCreateFailed;
        }
    }

    std::string MessageForResolveFailure(const ResolveDPFFailure failure)
    {
        switch (failure) {
        case ResolveDPFFailure::Unavailable:
            return "Dynamic Persistent Forms API is unavailable.";
        case ResolveDPFFailure::Configure:
            return "The runtime form was allocated, but its DFG fields could not be configured.";
        case ResolveDPFFailure::Create:
        case ResolveDPFFailure::None:
        default:
            return "Dynamic Persistent Forms could not allocate or recover the requested form.";
        }
    }

    void ReleaseNewSlot(const DynamicForms::DynamicForm& form)
    {
        if (auto* api = DPF::GetAPI()) {
            if (!api->ReleaseByOwnerKey(Manager::DPF_OWNER, form.editorId.c_str())) {
                logger::warn("Could not roll back DPF owner/key for '{}'.", form.editorId);
            }
        }
    }

    void RestoreAfterFailedUpdate(
        const std::size_t index,
        const DynamicForms::DynamicForm& oldForm,
        const DynamicForms::DynamicForm& attemptedForm,
        RE::TESForm* attemptedRuntime,
        const bool recoveredExistingSlot)
    {
        if (recoveredExistingSlot) {
            if (attemptedRuntime && !ConfigureForm(attemptedRuntime, oldForm)) {
                logger::warn("Could not restore runtime values for '{}' after a failed API update.", oldForm.editorId);
            }
            return;
        }

        ReleaseNewSlot(attemptedForm);
        auto restored = oldForm;
        restored.pluginNumber = 0;
        restored.localId = 0;
        if (ResolveDPFFormObject(restored, true)) {
            forms[index] = restored;
            if (!Manager::SaveForm(forms[index])) {
                logger::warn("Could not persist restored DPF slot for '{}' after a failed API update.", oldForm.editorId);
            }
        } else {
            logger::warn("Could not restore a runtime DPF slot for '{}' after a failed API update.", oldForm.editorId);
        }
    }

    DFG::FormOperationResult ExecuteCreate(
        const std::string& requester,
        const std::string& packageName,
        const std::string& payload,
        const bool notify)
    {
        auto result = MakeResult(DFG::Operation::Create);
        if (!Manager::IsReady()) {
            return Fail(result, DFG::Status::NotReady, "Dynamic Forms Generator has not finished loading.");
        }
        if (packageName.empty()) {
            return Fail(result, DFG::Status::MissingPackageName, "packageName is required.");
        }
        if (!IsValidExternalPackageName(packageName)) {
            return Fail(result, DFG::Status::InvalidPackageName, "packageName contains unsupported characters or is too long.");
        }
        if (!DPF::GetAPI()) {
            return Fail(result, DFG::Status::DPFUnavailable, "Dynamic Persistent Forms API is unavailable.");
        }

        rapidjson::Document doc;
        if (!ParseJsonObject(payload, doc, result)) {
            return result;
        }
        if (const auto* protectedMember = FindProtectedMember(doc, false)) {
            return Fail(result,
                DFG::Status::ProtectedField,
                std::format("'{}' is managed by DFG and cannot be supplied during creation.", protectedMember));
        }

        const auto kind = ReadRequestedKind(doc, true, result);
        if (!kind) {
            return result;
        }

        const auto editorMember = doc.FindMember("editorId");
        if (editorMember == doc.MemberEnd() || !editorMember->value.IsString() ||
            editorMember->value.GetStringLength() == 0)
        {
            return Fail(result, DFG::Status::MissingEditorId, "editorId is required.");
        }
        const std::string editorId = editorMember->value.GetString();
        CopyResultText(result.editorId, editorId);
        CopyResultText(result.packageName, packageName);
        if (!IsValidExternalEditorId(editorId)) {
            return Fail(result, DFG::Status::InvalidEditorId, "editorId must contain only letters, numbers, and underscores.");
        }
        if (Manager::HasEditorId(editorId)) {
            return Fail(result,
                DFG::Status::EditorIdAlreadyExists,
                std::format("A DFG form with editorId '{}' already exists.", editorId));
        }
        if (Manager::IsEditorIdReserved(editorId)) {
            return Fail(result,
                DFG::Status::EditorIdReserved,
                std::format("EditorId '{}' belongs to an existing game form and is reserved.", editorId));
        }

        SetStringMember(doc, doc.GetAllocator(), "packageName", packageName);
        DynamicForms::DynamicForm form;
        if (!ReadFormDocument(doc, std::format("API create from {}", requester), editorId, form)) {
            return Fail(result, DFG::Status::InvalidJson, "The JSON fields could not be converted to a DFG form.");
        }
        form.editorId = editorId;
        form.kind = *kind;
        form.packageName = packageName;
        form.basePackageName.clear();
        form.patchPackageNames.clear();
        form.pluginNumber = 0;
        form.localId = 0;
        form.dirty = false;

        std::vector<std::string> validationErrors;
        if (!Manager::ValidateForm(form, validationErrors)) {
            return Fail(
                result,
                DFG::Status::InvalidArgument,
                JoinValidationErrors(validationErrors));
        }

        bool recoveredExistingSlot = false;
        ResolveDPFFailure resolveFailure = ResolveDPFFailure::None;
        auto* runtimeForm = ResolveDPFFormObject(form, false, &recoveredExistingSlot, &resolveFailure);
        if (!runtimeForm) {
            return Fail(result, StatusForResolveFailure(resolveFailure), MessageForResolveFailure(resolveFailure));
        }
        if (!ConfigureForm(runtimeForm, form)) {
            if (!recoveredExistingSlot) {
                ReleaseNewSlot(form);
            }
            return Fail(result, DFG::Status::ConfigureFailed, "The runtime form was allocated, but its DFG fields could not be configured.");
        }

        if (!Manager::SaveForm(form)) {
            if (!recoveredExistingSlot) {
                ReleaseNewSlot(form);
            }
            return Fail(result, DFG::Status::PersistenceFailed, "The form was configured but could not be saved to package.db.");
        }

        forms.push_back(form);
        if (notify) {
            ListManager::GetSingleton()->PopulateAllLists(true);
            DispatchEvent(UPDATED_EVENT, ToSignature(form.kind), static_cast<float>(form.localId));
        }

        result.status = DFG::Status::Success;
        FillFormResult(result, forms.back(), runtimeForm, recoveredExistingSlot);
        logger::info("API requester '{}' created '{}' in package '{}'.", requester, editorId, packageName);
        return result;
    }

    DFG::FormOperationResult ExecuteUpdate(
        const std::string& requester,
        const std::string& editorId,
        const std::string& payload,
        const bool notify)
    {
        auto result = MakeResult(DFG::Operation::Update);
        CopyResultText(result.editorId, editorId);
        if (!Manager::IsReady()) {
            return Fail(result, DFG::Status::NotReady, "Dynamic Forms Generator has not finished loading.");
        }
        if (editorId.empty()) {
            return Fail(result, DFG::Status::MissingEditorId, "editorId is required.");
        }
        if (!IsValidExternalEditorId(editorId)) {
            return Fail(result, DFG::Status::InvalidEditorId, "editorId must contain only letters, numbers, and underscores.");
        }
        if (!DPF::GetAPI()) {
            return Fail(result, DFG::Status::DPFUnavailable, "Dynamic Persistent Forms API is unavailable.");
        }

        const auto found = std::ranges::find_if(forms, [&editorId](const DynamicForms::DynamicForm& form) {
            return !form.externalPatch && form.editorId == editorId;
        });
        if (found == forms.end()) {
            return Fail(result,
                DFG::Status::EditorIdNotFound,
                std::format("No DFG form with editorId '{}' exists.", editorId));
        }
        const auto index = static_cast<std::size_t>(std::distance(forms.begin(), found));
        const auto oldForm = *found;
        CopyResultText(result.packageName, EffectivePackageName(oldForm));

        rapidjson::Document doc;
        if (!ParseJsonObject(payload, doc, result)) {
            return result;
        }
        if (const auto* protectedMember = FindProtectedMember(doc, true)) {
            return Fail(result,
                DFG::Status::ProtectedField,
                std::format("'{}' is managed by DFG and cannot be changed by UpdateForm.", protectedMember));
        }

        if (const auto editorMember = doc.FindMember("editorId"); editorMember != doc.MemberEnd()) {
            if (!editorMember->value.IsString() || editorMember->value.GetString() != editorId) {
                return Fail(result, DFG::Status::EditorIdMismatch, "The JSON editorId does not match the requested editorId.");
            }
        }

        if (doc.HasMember("formKind")) {
            const auto requestedKind = ReadRequestedKind(doc, false, result);
            if (!requestedKind) {
                return result;
            }
            if (*requestedKind != oldForm.kind) {
                return Fail(result, DFG::Status::FormKindMismatch, "A form's type cannot be changed during update.");
            }
        } else if (const auto signatureMember = doc.FindMember("sourceSignature");
                   signatureMember != doc.MemberEnd())
        {
            if (!signatureMember->value.IsString()) {
                return Fail(result, DFG::Status::UnsupportedFormKind, "sourceSignature must be a supported signature.");
            }
            const auto signatureKind = TryFormKindFromString(signatureMember->value.GetString());
            if (!signatureKind) {
                return Fail(result, DFG::Status::UnsupportedFormKind, "sourceSignature is not supported.");
            }
            if (*signatureKind != oldForm.kind) {
                return Fail(result, DFG::Status::FormKindMismatch, "A form's type cannot be changed during update.");
            }
        }

        auto& allocator = doc.GetAllocator();
        SetStringMember(doc, allocator, "editorId", oldForm.editorId);
        SetStringMember(doc, allocator, "formKind", ToString(oldForm.kind));

        auto updated = oldForm;
        if (!ReadFormDocument(doc, std::format("API update from {}", requester), editorId, updated)) {
            return Fail(result, DFG::Status::InvalidJson, "The JSON patch could not be applied to the DFG form.");
        }
        updated.editorId = oldForm.editorId;
        updated.kind = oldForm.kind;
        updated.packageName = oldForm.packageName;
        updated.basePackageName = oldForm.basePackageName;
        updated.patchPackageNames = oldForm.patchPackageNames;
        updated.pluginNumber = oldForm.pluginNumber;
        updated.localId = oldForm.localId;
        updated.dirty = false;

        std::vector<std::string> validationErrors;
        if (!Manager::ValidateForm(updated, validationErrors)) {
            return Fail(
                result,
                DFG::Status::InvalidArgument,
                JoinValidationErrors(validationErrors));
        }

        bool recoveredExistingSlot = false;
        ResolveDPFFailure resolveFailure = ResolveDPFFailure::None;
        auto* runtimeForm = ResolveDPFFormObject(updated, false, &recoveredExistingSlot, &resolveFailure);
        if (!runtimeForm) {
            return Fail(result, StatusForResolveFailure(resolveFailure), MessageForResolveFailure(resolveFailure));
        }
        if (!ConfigureForm(runtimeForm, updated)) {
            RestoreAfterFailedUpdate(index, oldForm, updated, runtimeForm, recoveredExistingSlot);
            return Fail(result, DFG::Status::ConfigureFailed, "The JSON was valid, but its values could not configure the runtime form.");
        }

        if (!Manager::SaveForm(updated)) {
            RestoreAfterFailedUpdate(index, oldForm, updated, runtimeForm, recoveredExistingSlot);
            return Fail(result, DFG::Status::PersistenceFailed, "The updated runtime values could not be saved to package.db.");
        }

        forms[index] = updated;
        if (notify) {
            ListManager::GetSingleton()->PopulateAllLists(true);
            DispatchEvent(UPDATED_EVENT, ToSignature(updated.kind), static_cast<float>(updated.localId));
        }

        result.status = DFG::Status::Success;
        FillFormResult(result, forms[index], runtimeForm, recoveredExistingSlot);
        logger::info("API requester '{}' updated '{}'.", requester, editorId);
        return result;
    }

    DFG::FormOperationResult ExecuteDelete(
        const std::string& requester,
        const std::string& editorId,
        const bool notify)
    {
        auto result = MakeResult(DFG::Operation::Delete);
        CopyResultText(result.editorId, editorId);
        if (!Manager::IsReady()) {
            return Fail(result, DFG::Status::NotReady, "Dynamic Forms Generator has not finished loading.");
        }
        if (editorId.empty()) {
            return Fail(result, DFG::Status::MissingEditorId, "editorId is required.");
        }
        if (!IsValidExternalEditorId(editorId)) {
            return Fail(result, DFG::Status::InvalidEditorId, "editorId must contain only letters, numbers, and underscores.");
        }

        const auto found = std::ranges::find_if(forms, [&editorId](const DynamicForms::DynamicForm& form) {
            return !form.externalPatch && form.editorId == editorId;
        });
        if (found == forms.end()) {
            return Fail(result,
                DFG::Status::EditorIdNotFound,
                std::format("No DFG form with editorId '{}' exists.", editorId));
        }
        const auto index = static_cast<std::size_t>(std::distance(forms.begin(), found));
        const auto form = *found;
        auto runtimeSnapshot = form;
        bool recoveredExistingSlot = false;
        auto* deletedRuntimeForm = ResolveDPFFormObject(runtimeSnapshot, false, &recoveredExistingSlot);
        const auto& releasedForm = deletedRuntimeForm ? runtimeSnapshot : form;
        FillFormResult(result, releasedForm, deletedRuntimeForm, recoveredExistingSlot);
        const auto deletedFormID = deletedRuntimeForm ? deletedRuntimeForm->GetFormID() : 0;
        auto* deletedRuntimePerk =
            deletedRuntimeForm ? deletedRuntimeForm->As<RE::BGSPerk>() : nullptr;
        const auto actorSnapshots = deletedRuntimePerk ?
            RemovePerkFromLoadedActors(*deletedRuntimePerk) :
            std::vector<RemovedActorPerkSnapshot>{};

        auto* api = DPF::GetAPI();
        if (!api) {
            if (deletedRuntimePerk) {
                RestorePerkToLoadedActors(*deletedRuntimePerk, actorSnapshots);
            }
            return Fail(result, DFG::Status::DPFUnavailable, "Dynamic Persistent Forms API is unavailable.");
        }

        bool released = false;
        if (releasedForm.pluginNumber != 0 && releasedForm.localId != 0) {
            released = api->ReleaseByPluginLocalId(releasedForm.pluginNumber, releasedForm.localId, Manager::DPF_OWNER);
        }
        if (!released) {
            released = api->ReleaseByOwnerKey(Manager::DPF_OWNER, form.editorId.c_str());
        }
        if (!released) {
            if (deletedRuntimePerk) {
                RestorePerkToLoadedActors(*deletedRuntimePerk, actorSnapshots);
            }
            return Fail(result,
                DFG::Status::DPFReleaseFailed,
                std::format("DPF could not release slot {}:{:06X}.", releasedForm.pluginNumber, releasedForm.localId));
        }

        if (!DeleteStoredForm(form)) {
            auto restored = form;
            restored.pluginNumber = 0;
            restored.localId = 0;
            if (auto* restoredRuntime = ResolveDPFFormObject(restored, true)) {
                forms[index] = restored;
                Manager::SaveForm(forms[index]);
                if (auto* restoredPerk = restoredRuntime->As<RE::BGSPerk>()) {
                    RestorePerkToLoadedActors(*restoredPerk, actorSnapshots);
                }
                FillFormResult(result, forms[index], restoredRuntime, false);
            }
            return Fail(result, DFG::Status::PersistenceFailed, "DPF released the form, but package.db deletion failed; DFG attempted to restore it.");
        }

        const auto signature = ToSignature(form.kind);
        forms.erase(forms.begin() + static_cast<std::ptrdiff_t>(index));
        if (notify) {
            ListManager::GetSingleton()->PopulateAllLists(true);
            DispatchEvent(UPDATED_EVENT, signature, static_cast<float>(releasedForm.localId));
        }

        result.status = DFG::Status::Success;
        result.form = nullptr;
        result.formID = deletedFormID;
        result.pluginNumber = releasedForm.pluginNumber;
        result.localId = releasedForm.localId;
        CopyResultText(result.pluginName, DPF::PluginNameForNumber(releasedForm.pluginNumber));
        logger::info("API requester '{}' deleted '{}'.", requester, editorId);
        return result;
    }

    struct OwnedOperation
    {
        DFG::Operation operation{ DFG::Operation::Create };
        std::string requester;
        std::string packageName;
        std::string editorId;
        std::string payload;
        bool valid{ true };
        std::string validationError;
        DFG::FormOperationCallback callback{ nullptr };
        void* userData{ nullptr };
    };

    DFG::FormOperationResult ExecuteOwnedOperation(const OwnedOperation& operation, const bool notify) noexcept
    {
        auto result = MakeResult(operation.operation);
        if (!operation.valid) {
            return Fail(result, DFG::Status::InvalidArgument, operation.validationError);
        }

        try {
            switch (operation.operation) {
            case DFG::Operation::Create:
                result = ExecuteCreate(operation.requester, operation.packageName, operation.payload, notify);
                break;
            case DFG::Operation::Update:
                result = ExecuteUpdate(operation.requester, operation.editorId, operation.payload, notify);
                break;
            case DFG::Operation::Delete:
                result = ExecuteDelete(operation.requester, operation.editorId, notify);
                break;
            }
        } catch (const std::exception& error) {
            Fail(result, DFG::Status::InternalError, std::format("Unhandled DFG operation error: {}", error.what()));
            logger::error("Unhandled external DFG operation error: {}", error.what());
        } catch (...) {
            Fail(result, DFG::Status::InternalError, "Unhandled unknown DFG operation error.");
            logger::error("Unhandled unknown external DFG operation error.");
        }
        return result;
    }

    void RunOperation(OwnedOperation operation) noexcept
    {
        auto result = ExecuteOwnedOperation(operation, true);
        try {
            operation.callback(&result, operation.userData);
        } catch (...) {
            logger::error("A Dynamic Forms Generator API callback threw an exception.");
        }
    }

    bool QueueOperation(OwnedOperation operation) noexcept
    {
        if (!operation.callback) {
            return false;
        }
        try {
            auto* taskInterface = SKSE::GetTaskInterface();
            if (!taskInterface) {
                return false;
            }
            taskInterface->AddTask([operation = std::move(operation)]() mutable {
                RunOperation(std::move(operation));
            });
            return true;
        } catch (...) {
            logger::error("Could not queue an external Dynamic Forms Generator API operation.");
            return false;
        }
    }

    struct OwnedBatch
    {
        DFG::Operation operation{ DFG::Operation::Create };
        std::vector<OwnedOperation> operations;
        DFG::BatchOperationCallback callback{ nullptr };
        void* userData{ nullptr };
    };

    void RunBatch(OwnedBatch batch) noexcept
    {
        std::vector<DFG::FormOperationResult> results;
        DFG::BatchOperationResult batchResult;
        batchResult.operation = batch.operation;

        try {
            results.reserve(batch.operations.size());
            std::set<std::string> updatedSignatures;
            std::uint32_t successCount = 0;

            for (const auto& operation : batch.operations) {
                std::string deleteSignature;
                if (operation.operation == DFG::Operation::Delete && operation.valid) {
                    const auto found = std::ranges::find_if(forms, [&operation](const DynamicForms::DynamicForm& form) {
                        return !form.externalPatch && form.editorId == operation.editorId;
                    });
                    if (found != forms.end()) {
                        deleteSignature = ToSignature(found->kind);
                    }
                }

                auto itemResult = ExecuteOwnedOperation(operation, false);
                if (itemResult.status == DFG::Status::Success) {
                    ++successCount;
                    if (!deleteSignature.empty()) {
                        updatedSignatures.insert(std::move(deleteSignature));
                    } else {
                        const auto found = std::ranges::find_if(forms, [&itemResult](const DynamicForms::DynamicForm& form) {
                            return !form.externalPatch && form.editorId == itemResult.editorId;
                        });
                        if (found != forms.end()) {
                            updatedSignatures.insert(ToSignature(found->kind));
                        }
                    }
                }
                results.push_back(std::move(itemResult));
            }

            if (successCount > 0) {
                ListManager::GetSingleton()->PopulateAllLists(true);
                DispatchEvent(
                    UPDATED_EVENT,
                    JoinSignatures(updatedSignatures),
                    static_cast<float>(successCount));
            }

            batchResult.resultCount = static_cast<std::uint32_t>(results.size());
            batchResult.successCount = successCount;
            batchResult.failureCount = batchResult.resultCount - successCount;
            CopyResultText(batchResult.updatedSignatures, JoinSignatures(updatedSignatures));
            if (batchResult.failureCount == 0) {
                batchResult.status = DFG::Status::Success;
            } else if (batchResult.successCount > 0) {
                batchResult.status = DFG::Status::BatchPartialSuccess;
                CopyResultText(batchResult.error, "Some batch operations failed; inspect the individual results.");
            } else {
                batchResult.status = DFG::Status::BatchFailed;
                CopyResultText(batchResult.error, "All batch operations failed; inspect the individual results.");
            }
        } catch (const std::exception& error) {
            batchResult.status = DFG::Status::InternalError;
            batchResult.resultCount = static_cast<std::uint32_t>(results.size());
            CopyResultText(batchResult.error, std::format("Unhandled DFG batch error: {}", error.what()));
            logger::error("Unhandled external DFG batch error: {}", error.what());
        } catch (...) {
            batchResult.status = DFG::Status::InternalError;
            batchResult.resultCount = static_cast<std::uint32_t>(results.size());
            CopyResultText(batchResult.error, "Unhandled unknown DFG batch error.");
            logger::error("Unhandled unknown external DFG batch error.");
        }
        if (batchResult.status == DFG::Status::InternalError) {
            batchResult.successCount = static_cast<std::uint32_t>(std::ranges::count_if(
                results,
                [](const DFG::FormOperationResult& result) {
                    return result.status == DFG::Status::Success;
                }));
            batchResult.failureCount = batchResult.resultCount - batchResult.successCount;
        }
        batchResult.results = results.data();

        try {
            batch.callback(&batchResult, batch.userData);
        } catch (...) {
            logger::error("A Dynamic Forms Generator batch API callback threw an exception.");
        }
    }

    bool QueueBatch(OwnedBatch batch) noexcept
    {
        if (!batch.callback || batch.operations.empty()) {
            return false;
        }
        try {
            auto* taskInterface = SKSE::GetTaskInterface();
            if (!taskInterface) {
                return false;
            }
            taskInterface->AddTask([batch = std::move(batch)]() mutable {
                RunBatch(std::move(batch));
            });
            return true;
        } catch (...) {
            logger::error("Could not queue an external Dynamic Forms Generator batch operation.");
            return false;
        }
    }

    OwnedOperation CopyCreateRequest(const DFG::CreateFormRequest& request)
    {
        OwnedOperation operation;
        operation.operation = DFG::Operation::Create;
        if (request.structSize < sizeof(DFG::CreateFormRequest)) {
            operation.valid = false;
            operation.validationError = "CreateFormRequest.structSize is smaller than the supported structure.";
            return operation;
        }
        operation.requester = request.requester ? request.requester : "UnknownPlugin";
        operation.packageName = request.packageName ? request.packageName : "";
        operation.payload = request.formJson ? request.formJson : "";
        return operation;
    }

    OwnedOperation CopyUpdateRequest(const DFG::UpdateFormRequest& request)
    {
        OwnedOperation operation;
        operation.operation = DFG::Operation::Update;
        if (request.structSize < sizeof(DFG::UpdateFormRequest)) {
            operation.valid = false;
            operation.validationError = "UpdateFormRequest.structSize is smaller than the supported structure.";
            return operation;
        }
        operation.requester = request.requester ? request.requester : "UnknownPlugin";
        operation.editorId = request.editorId ? request.editorId : "";
        operation.payload = request.patchJson ? request.patchJson : "";
        return operation;
    }

    OwnedOperation CopyDeleteRequest(const DFG::DeleteFormRequest& request)
    {
        OwnedOperation operation;
        operation.operation = DFG::Operation::Delete;
        if (request.structSize < sizeof(DFG::DeleteFormRequest)) {
            operation.valid = false;
            operation.validationError = "DeleteFormRequest.structSize is smaller than the supported structure.";
            return operation;
        }
        operation.requester = request.requester ? request.requester : "UnknownPlugin";
        operation.editorId = request.editorId ? request.editorId : "";
        return operation;
    }

    struct OwnedLookup
    {
        std::string requester;
        std::string editorId;
        bool valid{ true };
        std::string validationError;
        DFG::FormLookupCallback callback{ nullptr };
        void* userData{ nullptr };
    };

    OwnedLookup CopyLookupRequest(const DFG::LookupFormRequest& request)
    {
        OwnedLookup lookup;
        if (request.structSize < sizeof(DFG::LookupFormRequest)) {
            lookup.valid = false;
            lookup.validationError = "LookupFormRequest.structSize is smaller than the supported structure.";
            return lookup;
        }
        lookup.requester = request.requester ? request.requester : "UnknownPlugin";
        lookup.editorId = request.editorId ? request.editorId : "";
        return lookup;
    }

    struct LookupExecutionResult
    {
        DFG::FormLookupResult result;
        std::string json;
    };

    void BindLookupJson(DFG::FormLookupResult& result, const std::string& json) noexcept
    {
        if (json.empty()) {
            result.formJson = nullptr;
            result.formJsonLength = 0;
            return;
        }
        result.formJson = json.c_str();
        result.formJsonLength = static_cast<std::uint32_t>((std::min)(
            json.size(),
            static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())));
    }

    LookupExecutionResult ExecuteLookup(const OwnedLookup& lookup) noexcept
    {
        LookupExecutionResult execution;
        auto& result = execution.result;
        CopyResultText(result.editorId, lookup.editorId);
        if (!lookup.valid) {
            result.status = DFG::Status::InvalidArgument;
            CopyResultText(result.error, lookup.validationError);
            return execution;
        }
        if (!Manager::IsReady()) {
            result.status = DFG::Status::NotReady;
            CopyResultText(result.error, "Dynamic Forms Generator has not finished loading.");
            return execution;
        }
        if (lookup.editorId.empty()) {
            result.status = DFG::Status::MissingEditorId;
            CopyResultText(result.error, "editorId is required.");
            return execution;
        }
        if (!IsValidExternalEditorId(lookup.editorId)) {
            result.status = DFG::Status::InvalidEditorId;
            CopyResultText(result.error, "editorId must contain only letters, numbers, and underscores.");
            return execution;
        }

        try {
            const auto found = std::ranges::find_if(forms, [&lookup](const DynamicForms::DynamicForm& form) {
                return !form.externalPatch && form.editorId == lookup.editorId;
            });
            result.status = DFG::Status::Success;
            if (found == forms.end()) {
                return execution;
            }

            result.exists = 1;
            result.pluginNumber = found->pluginNumber;
            result.localId = found->localId;
            CopyResultText(result.packageName, EffectivePackageName(*found));
            CopyResultText(result.pluginName, DPF::PluginNameForNumber(found->pluginNumber));
            CopyResultText(result.formKind, ToString(found->kind));
            CopyResultText(result.sourceSignature, ToSignature(found->kind));
            execution.json = Manager::SerializeFormJson(*found);
            if (execution.json.empty()) {
                result.status = DFG::Status::InternalError;
                CopyResultText(result.error, "The form exists, but DFG could not serialize it.");
                return execution;
            }

            if (found->pluginNumber != 0 && found->localId != 0) {
                if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
                    result.form = dataHandler->LookupForm(
                        found->localId,
                        DPF::PluginNameForNumber(found->pluginNumber));
                    if (result.form) {
                        result.formID = result.form->GetFormID();
                    }
                }
            }
            logger::debug(
                "API requester '{}' looked up '{}': exists={}, slot={}:{:06X}.",
                lookup.requester,
                lookup.editorId,
                result.exists,
                result.pluginNumber,
                result.localId);
        } catch (const std::exception& error) {
            result.status = DFG::Status::InternalError;
            CopyResultText(result.error, std::format("Unhandled DFG lookup error: {}", error.what()));
            logger::error("Unhandled external DFG lookup error: {}", error.what());
        } catch (...) {
            result.status = DFG::Status::InternalError;
            CopyResultText(result.error, "Unhandled unknown DFG lookup error.");
            logger::error("Unhandled unknown external DFG lookup error.");
        }
        return execution;
    }

    void RunLookup(OwnedLookup lookup) noexcept
    {
        auto execution = ExecuteLookup(lookup);
        BindLookupJson(execution.result, execution.json);
        try {
            lookup.callback(&execution.result, lookup.userData);
        } catch (...) {
            logger::error("A Dynamic Forms Generator lookup callback threw an exception.");
        }
    }

    bool QueueLookup(OwnedLookup lookup) noexcept
    {
        if (!lookup.callback) {
            return false;
        }
        try {
            auto* taskInterface = SKSE::GetTaskInterface();
            if (!taskInterface) {
                return false;
            }
            taskInterface->AddTask([lookup = std::move(lookup)]() mutable {
                RunLookup(std::move(lookup));
            });
            return true;
        } catch (...) {
            logger::error("Could not queue an external Dynamic Forms Generator lookup.");
            return false;
        }
    }

    struct OwnedLookupBatch
    {
        std::vector<OwnedLookup> lookups;
        DFG::BatchLookupCallback callback{ nullptr };
        void* userData{ nullptr };
    };

    void RunLookupBatch(OwnedLookupBatch batch) noexcept
    {
        std::vector<DFG::FormLookupResult> results;
        std::vector<std::string> jsonPayloads;
        DFG::BatchLookupResult batchResult;
        try {
            results.reserve(batch.lookups.size());
            jsonPayloads.reserve(batch.lookups.size());
            for (const auto& lookup : batch.lookups) {
                auto execution = ExecuteLookup(lookup);
                results.push_back(std::move(execution.result));
                jsonPayloads.push_back(std::move(execution.json));
            }
            for (std::size_t i = 0; i < results.size(); ++i) {
                BindLookupJson(results[i], jsonPayloads[i]);
            }

            batchResult.resultCount = static_cast<std::uint32_t>(results.size());
            for (const auto& result : results) {
                if (result.status != DFG::Status::Success) {
                    ++batchResult.failureCount;
                } else if (result.exists != 0) {
                    ++batchResult.foundCount;
                } else {
                    ++batchResult.missingCount;
                }
            }

            if (batchResult.failureCount == 0) {
                batchResult.status = DFG::Status::Success;
            } else if (batchResult.failureCount < batchResult.resultCount) {
                batchResult.status = DFG::Status::BatchPartialSuccess;
                CopyResultText(batchResult.error, "Some lookups failed; inspect the individual results.");
            } else {
                batchResult.status = DFG::Status::BatchFailed;
                CopyResultText(batchResult.error, "All lookups failed; inspect the individual results.");
            }
        } catch (const std::exception& error) {
            batchResult.status = DFG::Status::InternalError;
            batchResult.resultCount = static_cast<std::uint32_t>(results.size());
            CopyResultText(batchResult.error, std::format("Unhandled DFG lookup batch error: {}", error.what()));
            logger::error("Unhandled external DFG lookup batch error: {}", error.what());
        } catch (...) {
            batchResult.status = DFG::Status::InternalError;
            batchResult.resultCount = static_cast<std::uint32_t>(results.size());
            CopyResultText(batchResult.error, "Unhandled unknown DFG lookup batch error.");
            logger::error("Unhandled unknown external DFG lookup batch error.");
        }
        batchResult.results = results.data();

        try {
            batch.callback(&batchResult, batch.userData);
        } catch (...) {
            logger::error("A Dynamic Forms Generator lookup batch callback threw an exception.");
        }
    }

    bool QueueLookupBatch(OwnedLookupBatch batch) noexcept
    {
        if (!batch.callback || batch.lookups.empty()) {
            return false;
        }
        try {
            auto* taskInterface = SKSE::GetTaskInterface();
            if (!taskInterface) {
                return false;
            }
            taskInterface->AddTask([batch = std::move(batch)]() mutable {
                RunLookupBatch(std::move(batch));
            });
            return true;
        } catch (...) {
            logger::error("Could not queue an external Dynamic Forms Generator lookup batch.");
            return false;
        }
    }

    class DynamicFormsGeneratorAPI final : public DFG::IDynamicFormsGenerator
    {
    public:
        [[nodiscard]] std::uint32_t GetVersion() const noexcept override
        {
            return DFG::InterfaceVersion;
        }

        [[nodiscard]] bool IsReady() const noexcept override
        {
            return Manager::IsReady();
        }

        bool QueueCreateForm(
            const DFG::CreateFormRequest* request,
            const DFG::FormOperationCallback callback,
            void* userData) noexcept override
        {
            if (!request || request->structSize < sizeof(DFG::CreateFormRequest) || !callback) {
                return false;
            }
            try {
                auto operation = CopyCreateRequest(*request);
                operation.callback = callback;
                operation.userData = userData;
                return QueueOperation(std::move(operation));
            } catch (...) {
                return false;
            }
        }

        bool QueueUpdateForm(
            const DFG::UpdateFormRequest* request,
            const DFG::FormOperationCallback callback,
            void* userData) noexcept override
        {
            if (!request || request->structSize < sizeof(DFG::UpdateFormRequest) || !callback) {
                return false;
            }
            try {
                auto operation = CopyUpdateRequest(*request);
                operation.callback = callback;
                operation.userData = userData;
                return QueueOperation(std::move(operation));
            } catch (...) {
                return false;
            }
        }

        bool QueueDeleteForm(
            const DFG::DeleteFormRequest* request,
            const DFG::FormOperationCallback callback,
            void* userData) noexcept override
        {
            if (!request || request->structSize < sizeof(DFG::DeleteFormRequest) || !callback) {
                return false;
            }
            try {
                auto operation = CopyDeleteRequest(*request);
                operation.callback = callback;
                operation.userData = userData;
                return QueueOperation(std::move(operation));
            } catch (...) {
                return false;
            }
        }

        bool QueueCreateForms(
            const DFG::CreateFormsRequest* request,
            const DFG::BatchOperationCallback callback,
            void* userData) noexcept override
        {
            if (!request || request->structSize < sizeof(DFG::CreateFormsRequest) ||
                !request->requests || request->requestCount == 0 || request->requestCount > 10000 || !callback)
            {
                return false;
            }
            try {
                OwnedBatch batch;
                batch.operation = DFG::Operation::Create;
                batch.callback = callback;
                batch.userData = userData;
                batch.operations.reserve(request->requestCount);
                for (std::uint32_t i = 0; i < request->requestCount; ++i) {
                    batch.operations.push_back(CopyCreateRequest(request->requests[i]));
                }
                return QueueBatch(std::move(batch));
            } catch (...) {
                return false;
            }
        }

        bool QueueUpdateForms(
            const DFG::UpdateFormsRequest* request,
            const DFG::BatchOperationCallback callback,
            void* userData) noexcept override
        {
            if (!request || request->structSize < sizeof(DFG::UpdateFormsRequest) ||
                !request->requests || request->requestCount == 0 || request->requestCount > 10000 || !callback)
            {
                return false;
            }
            try {
                OwnedBatch batch;
                batch.operation = DFG::Operation::Update;
                batch.callback = callback;
                batch.userData = userData;
                batch.operations.reserve(request->requestCount);
                for (std::uint32_t i = 0; i < request->requestCount; ++i) {
                    batch.operations.push_back(CopyUpdateRequest(request->requests[i]));
                }
                return QueueBatch(std::move(batch));
            } catch (...) {
                return false;
            }
        }

        bool QueueDeleteForms(
            const DFG::DeleteFormsRequest* request,
            const DFG::BatchOperationCallback callback,
            void* userData) noexcept override
        {
            if (!request || request->structSize < sizeof(DFG::DeleteFormsRequest) ||
                !request->requests || request->requestCount == 0 || request->requestCount > 10000 || !callback)
            {
                return false;
            }
            try {
                OwnedBatch batch;
                batch.operation = DFG::Operation::Delete;
                batch.callback = callback;
                batch.userData = userData;
                batch.operations.reserve(request->requestCount);
                for (std::uint32_t i = 0; i < request->requestCount; ++i) {
                    batch.operations.push_back(CopyDeleteRequest(request->requests[i]));
                }
                return QueueBatch(std::move(batch));
            } catch (...) {
                return false;
            }
        }

        bool QueueLookupForm(
            const DFG::LookupFormRequest* request,
            const DFG::FormLookupCallback callback,
            void* userData) noexcept override
        {
            if (!request || request->structSize < sizeof(DFG::LookupFormRequest) || !callback) {
                return false;
            }
            try {
                auto lookup = CopyLookupRequest(*request);
                lookup.callback = callback;
                lookup.userData = userData;
                return QueueLookup(std::move(lookup));
            } catch (...) {
                return false;
            }
        }

        bool QueueLookupForms(
            const DFG::LookupFormsRequest* request,
            const DFG::BatchLookupCallback callback,
            void* userData) noexcept override
        {
            if (!request || request->structSize < sizeof(DFG::LookupFormsRequest) ||
                !request->requests || request->requestCount == 0 || request->requestCount > 10000 || !callback)
            {
                return false;
            }
            try {
                OwnedLookupBatch batch;
                batch.callback = callback;
                batch.userData = userData;
                batch.lookups.reserve(request->requestCount);
                for (std::uint32_t i = 0; i < request->requestCount; ++i) {
                    batch.lookups.push_back(CopyLookupRequest(request->requests[i]));
                }
                return QueueLookupBatch(std::move(batch));
            } catch (...) {
                return false;
            }
        }
    };
}

extern "C" __declspec(dllexport) void* GetDynamicFormsGeneratorAPI()
{
    static DynamicFormsGeneratorAPI api;
    return std::addressof(api);
}
