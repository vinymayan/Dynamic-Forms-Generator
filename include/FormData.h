#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace DynamicForms {
    enum class FormKind
    {
        Global,
        Keyword,
        Outfit,
        Color,
        ArtObject,
        Perk,
        HeadPart,
        SoundDescriptor,
        Light,
        Explosion,
        Activator
    };

    enum class GlobalType
    {
        Short,
        Long,
        Float
    };

    enum class ArtObjectType
    {
        MagicCasting,
        MagicHitEffect,
        MagicEnchantEffect
    };

    enum class PerkConditionKind
    {
        Raw,
        GetGlobalValue,
        GetActorValue,
        GetBaseActorValue,
        HasPerk,
        GetQuestCompleted,
        HasSpell
    };

    enum class HeadPartType
    {
        Misc,
        Face,
        Eyes,
        Hair,
        FacialHair,
        Scar,
        Eyebrows
    };

    struct PerkCondition
    {
        PerkConditionKind kind{ PerkConditionKind::Raw };
        std::uint32_t functionId{ 0 };
        std::uint32_t opCode{ 0 };
        float comparisonValue{ 1.0F };
        bool isOr{ false };
        bool useGlobalComparison{ false };
        std::string comparisonGlobal;
        std::string param1;
        std::string param2;
    };

    struct PerkEntry
    {
        std::uint32_t rank{ 0 };
        std::uint32_t priority{ 0 };
        std::uint32_t entryPoint{ 75 };
        std::uint32_t function{ 1 };
        std::uint32_t numArgs{ 0 };
        float value{ 1.0F };
        std::vector<PerkCondition> conditions;
    };

    struct FormRef
    {
        std::string editorID;
        std::string formID;

        [[nodiscard]] bool empty() const
        {
            return editorID.empty() && formID.empty();
        }

        [[nodiscard]] std::string Display() const
        {
            if (!editorID.empty() && !formID.empty()) {
                return editorID + " (" + formID + ")";
            }
            if (!editorID.empty()) {
                return editorID;
            }
            return formID;
        }

        friend bool operator==(const FormRef& lhs, const FormRef& rhs)
        {
            return lhs.editorID == rhs.editorID && lhs.formID == rhs.formID;
        }
    };

    struct DynamicForm
    {
        FormKind kind{ FormKind::Global };
        std::string editorId;
        GlobalType globalType{ GlobalType::Float };
        float defaultValue{ 0.0F };
        std::vector<FormRef> outfitPieces;
        std::string fullName;
        std::string description;
        std::uint8_t red{ 255 };
        std::uint8_t green{ 255 };
        std::uint8_t blue{ 255 };
        std::uint8_t alpha{ 0 };
        bool playable{ true };
        std::string modelPath;
        ArtObjectType artType{ ArtObjectType::MagicCasting };
        std::int16_t boundX1{ 0 };
        std::int16_t boundY1{ 0 };
        std::int16_t boundZ1{ 0 };
        std::int16_t boundX2{ 0 };
        std::int16_t boundY2{ 0 };
        std::int16_t boundZ2{ 0 };
        bool trait{ false };
        std::int8_t level{ 0 };
        std::int8_t numRanks{ 1 };
        bool hidden{ false };
        FormRef nextPerk;
        std::vector<PerkCondition> conditions;
        std::vector<PerkEntry> entries;
        HeadPartType headPartType{ HeadPartType::Misc };
        bool male{ false };
        bool female{ false };
        bool isExtraPart{ false };
        bool useSolidTint{ false };
        std::string raceMorphPath;
        std::string defaultMorphPath;
        std::string chargenMorphPath;
        FormRef textureSet;
        FormRef colorForm;
        FormRef validRaces;
        std::vector<FormRef> extraParts;
        std::vector<std::string> soundFiles;
        FormRef category;
        FormRef alternateSound;
        FormRef outputModel;
        std::uint8_t frequencyShift{ 0 };
        std::uint8_t frequencyVariance{ 0 };
        std::uint8_t priority{ 128 };
        std::uint8_t dbVariance{ 0 };
        float staticAttenuation{ 0.0F };
        std::uint8_t looping{ 0 };
        std::uint8_t rumbleSendValue{ 0 };
        std::int32_t lightTime{ -1 };
        std::uint32_t lightRadius{ 0 };
        std::uint32_t flags{ 0 };
        float falloffExponent{ 1.0F };
        float fov{ 90.0F };
        float nearClip{ 0.0F };
        float flickerPeriod{ 0.0F };
        float flickerIntensityAmplitude{ 0.0F };
        float flickerMovementAmplitude{ 0.0F };
        float fade{ 1.0F };
        FormRef sound;
        FormRef lensFlare;
        FormRef light;
        FormRef sound1;
        FormRef sound2;
        FormRef impactDataSet;
        FormRef placedObject;
        FormRef spawnProjectile;
        FormRef objectEffect;
        FormRef imageSpaceModifier;
        float force{ 0.0F };
        float damage{ 0.0F };
        float radius{ 0.0F };
        float imageSpaceRadius{ 0.0F };
        float verticalOffsetMult{ 0.0F };
        std::uint32_t soundLevel{ 0 };
        FormRef soundLoop;
        FormRef soundActivate;
        FormRef waterType;
        std::uint32_t localId{ 0 };
    };
}
