#pragma once

#include <array>
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
        Activator,
        NPC
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
        std::string functionName;
        std::uint32_t opCode{ 0 };
        float comparisonValue{ 1.0F };
        bool isOr{ false };
        bool useAliases{ false };
        bool useGlobalComparison{ false };
        bool usePackData{ false };
        bool swapTarget{ false };
        std::uint32_t runOn{ 0 };
        std::uint32_t dataId{ 0 };
        std::string runOnRef;
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

    struct RankedFormRef
    {
        FormRef form;
        std::int32_t rank{ 0 };
    };

    struct TintLayer
    {
        std::uint16_t index{ 0 };
        std::uint16_t preset{ 0 };
        float interpolation{ 0.0F };
        std::uint8_t red{ 0 };
        std::uint8_t green{ 0 };
        std::uint8_t blue{ 0 };
        std::uint8_t alpha{ 255 };
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
        FormRef race;
        FormRef skin;
        FormRef defaultOutfit;
        FormRef sleepOutfit;
        FormRef voice;
        FormRef hairColor;
        FormRef faceTexture;
        FormRef npcClass;
        FormRef combatStyle;
        FormRef giftFilter;
        FormRef deathItem;
        FormRef defaultPackageList;
        FormRef crimeFaction;
        bool femaleNpc{ false };
        bool oppositeGenderAnim{ false };
        bool essential{ false };
        bool protectedNpc{ false };
        bool unique{ false };
        bool calcStats{ false };
        bool respawn{ false };
        bool doesntAffectStealthMeter{ false };
        bool doesntBleed{ false };
        bool bleedoutOverrideFlag{ false };
        bool simpleActor{ false };
        bool noActivation{ false };
        bool ghost{ false };
        bool invulnerable{ false };
        float height{ 1.0F };
        float weight{ 50.0F };
        std::uint16_t health{ 100 };
        std::uint16_t magicka{ 50 };
        std::uint16_t stamina{ 50 };
        std::int16_t healthOffset{ 0 };
        std::int16_t magickaOffset{ 0 };
        std::int16_t staminaOffset{ 0 };
        std::uint16_t calcMinLevel{ 1 };
        std::uint16_t calcMaxLevel{ 1 };
        std::uint16_t npcLevel{ 1 };
        std::uint16_t speedMult{ 100 };
        std::uint16_t dispositionBase{ 35 };
        std::int16_t bleedoutOverride{ 0 };
        std::array<std::uint8_t, 18> skills{};
        std::array<std::uint8_t, 18> skillOffsets{};
        std::array<float, 19> faceMorphs{};
        std::array<std::int32_t, 4> faceParts{};
        std::vector<FormRef> headParts;
        std::vector<TintLayer> tintLayers;
        std::vector<RankedFormRef> npcFactions;
        std::vector<RankedFormRef> npcPerks;
        std::vector<FormRef> spells;
        std::uint32_t localId{ 0 };
        bool dirty{ false };
    };
}
