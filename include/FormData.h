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
        ArmorType,
        Armor,
        Color,
        ArtObject,
        Perk,
        HeadPart,
        SoundDescriptor,
        Light,
        Explosion,
        Activator,
        EffectShader,
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
        std::uint32_t bipedSlots{ 0 };
        std::uint32_t armorType{ 2 };
        std::int32_t armorValue{ 0 };
        float armorWeight{ 0.0F };
        float armorRating{ 0.0F };
        std::uint16_t enchantmentAmount{ 0 };
        std::string maleWorldModel;
        std::string femaleWorldModel;
        std::string maleFirstPersonModel;
        std::string femaleFirstPersonModel;
        std::string maleInventoryIcon;
        std::string femaleInventoryIcon;
        std::string maleMessageIcon;
        std::string femaleMessageIcon;
        FormRef enchantment;
        FormRef equipSlot;
        FormRef templateArmor;
        FormRef pickupSound;
        FormRef putdownSound;
        FormRef blockBashImpactDataSet;
        FormRef altBlockMaterialType;
        std::vector<FormRef> armorAddons;
        std::vector<FormRef> keywords;
        FormRef maleSkinTexture;
        FormRef femaleSkinTexture;
        FormRef maleSkinTextureSwapList;
        FormRef femaleSkinTextureSwapList;
        FormRef footstepSet;
        FormRef armorArtObject;
        std::vector<FormRef> additionalRaces;
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
        std::string fillTexturePath;
        std::string particleShaderTexturePath;
        std::string holesTexturePath;
        std::string membranePaletteTexturePath;
        std::string particlePaletteTexturePath;
        FormRef ambientSound;
        std::uint8_t fillColor1Red{ 255 };
        std::uint8_t fillColor1Green{ 255 };
        std::uint8_t fillColor1Blue{ 255 };
        std::uint8_t fillColor1Alpha{ 0 };
        std::uint8_t fillColor2Red{ 255 };
        std::uint8_t fillColor2Green{ 255 };
        std::uint8_t fillColor2Blue{ 255 };
        std::uint8_t fillColor2Alpha{ 0 };
        std::uint8_t fillColor3Red{ 255 };
        std::uint8_t fillColor3Green{ 255 };
        std::uint8_t fillColor3Blue{ 255 };
        std::uint8_t fillColor3Alpha{ 0 };
        std::uint8_t edgeEffectRed{ 255 };
        std::uint8_t edgeEffectGreen{ 255 };
        std::uint8_t edgeEffectBlue{ 255 };
        std::uint8_t edgeEffectAlpha{ 0 };
        std::uint8_t edgeColorRed{ 255 };
        std::uint8_t edgeColorGreen{ 255 };
        std::uint8_t edgeColorBlue{ 255 };
        std::uint8_t edgeColorAlpha{ 0 };
        std::uint8_t particleColor1Red{ 255 };
        std::uint8_t particleColor1Green{ 255 };
        std::uint8_t particleColor1Blue{ 255 };
        std::uint8_t particleColor1Alpha{ 0 };
        std::uint8_t particleColor2Red{ 255 };
        std::uint8_t particleColor2Green{ 255 };
        std::uint8_t particleColor2Blue{ 255 };
        std::uint8_t particleColor2Alpha{ 0 };
        std::uint8_t particleColor3Red{ 255 };
        std::uint8_t particleColor3Green{ 255 };
        std::uint8_t particleColor3Blue{ 255 };
        std::uint8_t particleColor3Alpha{ 0 };
        float fillAlphaFadeIn{ 0.0F };
        float fillFullAlphaTime{ 0.0F };
        float fillAlphaFadeOut{ 0.0F };
        float fillPersistentAlphaRatio{ 0.0F };
        float fillAlphaPulseAmplitude{ 0.0F };
        float fillAlphaPulseFrequency{ 0.0F };
        float fillTextureAnimationSpeedU{ 0.0F };
        float fillTextureAnimationSpeedV{ 0.0F };
        float fillTextureScaleU{ 1.0F };
        float fillTextureScaleV{ 1.0F };
        float fillFullAlphaRatio{ 1.0F };
        float edgeFalloff{ 0.0F };
        float edgeAlphaFadeIn{ 0.0F };
        float edgeFullAlphaTime{ 0.0F };
        float edgeAlphaFadeOut{ 0.0F };
        float edgePersistentAlphaRatio{ 0.0F };
        float edgeAlphaPulseAmplitude{ 0.0F };
        float edgeAlphaPulseFrequency{ 0.0F };
        float edgeFullAlphaRatio{ 1.0F };
        float edgeWidthAlphaUnits{ 0.0F };
        float particleBirthRampUpTime{ 0.0F };
        float particleFullBirthTime{ 0.0F };
        float particleBirthRampDownTime{ 0.0F };
        float particleFullBirthRatio{ 1.0F };
        float particleCount{ 0.0F };
        float particleLifetime{ 0.0F };
        float particleLifetimeVariance{ 0.0F };
        float particleInitialSpeedAlongNormal{ 0.0F };
        float particleAccelerationAlongNormal{ 0.0F };
        float particleScaleKey1{ 1.0F };
        float particleScaleKey2{ 1.0F };
        float particleScaleKey1Time{ 0.0F };
        float particleScaleKey2Time{ 1.0F };
        float particleColor1AlphaValue{ 1.0F };
        float particleColor2AlphaValue{ 1.0F };
        float particleColor3AlphaValue{ 1.0F };
        float particleColor1Time{ 0.0F };
        float particleColor2Time{ 0.5F };
        float particleColor3Time{ 1.0F };
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
        std::int32_t aiAggression{ 0 };
        std::int32_t aiConfidence{ 2 };
        std::uint8_t aiEnergyLevel{ 50 };
        std::int32_t aiMorality{ 0 };
        std::int32_t aiMood{ 0 };
        std::int32_t aiAssistance{ 0 };
        bool aiAggroRadiusBehavior{ false };
        std::uint16_t aiAggroRadiusWarn{ 0 };
        std::uint16_t aiAggroRadiusWarnAndAttack{ 0 };
        std::uint16_t aiAggroRadiusAttack{ 0 };
        bool aiNoSlowApproach{ false };
        std::array<std::uint8_t, 18> skills{};
        std::array<std::uint8_t, 18> skillOffsets{};
        std::array<float, 19> faceMorphs{};
        std::array<std::int32_t, 4> faceParts{};
        std::vector<FormRef> headParts;
        std::vector<TintLayer> tintLayers;
        std::vector<RankedFormRef> npcFactions;
        std::vector<RankedFormRef> npcPerks;
        std::vector<FormRef> spells;
        std::vector<FormRef> packages;
        std::uint32_t localId{ 0 };
        bool dirty{ false };
    };
}
