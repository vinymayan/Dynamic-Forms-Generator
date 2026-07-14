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
        FormList,
        EquipSlot,
        VoiceType,
        Outfit,
        ArmorType,
        Armor,
        Book,
        Misc,
        Key,
        SoulGem,
        MaterialType,
        Ammo,
        Weapon,
        AlchemyItem,
        Ingredient,
        Spell,
        Color,
        ArtObject,
        Perk,
        HeadPart,
        SoundDescriptor,
        Light,
        Explosion,
        Activator,
        EffectShader,
        NPC,
        MagicEffect,
        Enchantment,
        Scroll,
        Projectile,
        TextureSet,
        Hazard,
        ImpactData,
        ReferenceEffect,
        DualCastData,
        Static,
        MovableStatic,
        Door,
        CombatStyle,
        SoundCategory,
        Class,
        Flora,
        Tree,
        ConstructibleObject,
        Container,
        ImpactDataSet,
        CollisionLayer,
        Footstep,
        FootstepSet,
        ReverbParameters,
        AcousticSpace,
        Apparatus,
        StaticCollection,
        Grass,
        IdleMarker,
        EncounterZone,
        Relationship,
        AssociationType,
        MovementType,
        WordOfPower,
        Water,
        ImageSpace,
        LightingTemplate,
        Shout,
        LeveledItem,
        LeveledNPC,
        LeveledSpell,
        LocationRefType,
        Action,
        MenuIcon,
        Eyes,
        Note,
        AnimatedObject,
        LoadScreen,
        ShaderParticleGeometry,
        AddonNode
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

    struct MagicEffectEntry
    {
        FormRef effectSetting;
        float magnitude{ 0.0F };
        std::uint32_t area{ 0 };
        std::uint32_t duration{ 0 };
        float cost{ 0.0F };
        std::vector<PerkCondition> conditions;
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

    struct ContainerEntry
    {
        FormRef item;
        std::int32_t count{ 1 };
        FormRef owner;
        FormRef conditionGlobal;
        std::int32_t requiredRank{ 0 };
        float healthMult{ 100.0F };
    };

    struct FormRefPair
    {
        FormRef key;
        FormRef value;
    };

    struct LeveledEntry
    {
        FormRef form;
        std::uint16_t level{ 1 };
        std::uint16_t count{ 1 };
        FormRef owner;
        FormRef conditionGlobal;
        std::int32_t requiredRank{ 0 };
        float healthMult{ 100.0F };
    };

    struct DynamicForm
    {
        FormKind kind{ FormKind::Global };
        std::string editorId;
        std::string packageName{ "Local Forms" };
        std::string basePackageName;
        std::vector<std::string> patchPackageNames;
        GlobalType globalType{ GlobalType::Float };
        float defaultValue{ 0.0F };
        std::vector<FormRef> formListItems;
        std::vector<FormRef> equipSlotParents;
        std::uint32_t equipSlotFlags{ 0 };
        bool voiceTypeAllowDefaultDialogue{ true };
        bool voiceTypeFemale{ false };
        std::vector<FormRef> outfitPieces;
        std::uint32_t bipedSlots{ 0 };
        std::uint32_t armorType{ 2 };
        std::int32_t itemValue{ 0 };
        float itemWeight{ 0.0F };
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
        std::string inventoryIcon;
        std::string messageIcon;
        FormRef materialParent;
        FormRef havokImpactDataSet;
        std::string materialName;
        std::uint32_t materialId{ 0 };
        float buoyancy{ 0.0F };
        FormRef projectile;
        std::uint32_t ammoFlags{ 0 };
        std::uint32_t weaponType{ 1 };
        std::uint32_t weaponFlags{ 0 };
        std::uint32_t weaponFlags2{ 0 };
        std::uint32_t weaponSkill{ 6 };
        std::uint32_t weaponResist{ 24 };
        std::uint32_t weaponCritFlags{ 0 };
        std::uint32_t weaponCritDamage{ 0 };
        float weaponSpeed{ 1.0F };
        float weaponReach{ 1.0F };
        float weaponMinRange{ 0.0F };
        float weaponMaxRange{ 0.0F };
        float weaponStagger{ 0.0F };
        float weaponCritMult{ 1.0F };
        FormRef templateWeapon;
        FormRef critEffect;
        FormRef attackSound;
        FormRef attackSound2D;
        FormRef attackLoopSound;
        FormRef attackFailSound;
        FormRef idleSound;
        FormRef equipSound;
        FormRef unequipSound;
        FormRef firstPersonModelObject;
        std::uint32_t alchemyFlags{ 0 };
        std::int32_t alchemyCostOverride{ 0 };
        FormRef addictionItem;
        float addictionChance{ 0.0F };
        FormRef consumptionSound;
        std::uint32_t ingredientFlags{ 0 };
        std::int32_t ingredientCostOverride{ 0 };
        std::uint16_t knownEffectFlags{ 0 };
        std::uint16_t playerUses{ 0 };
        bool magicEffectsOverride{ false };
        std::vector<MagicEffectEntry> magicEffects;
        std::uint32_t spellFlags{ 0 };
        std::uint32_t spellType{ 0 };
        std::int32_t spellCostOverride{ 0 };
        float spellChargeTime{ 0.0F };
        std::uint32_t spellCastingType{ 1 };
        std::uint32_t spellDelivery{ 0 };
        float spellCastDuration{ 0.0F };
        float spellRange{ 0.0F };
        FormRef castingPerk;
        FormRef menuDisplayObject;
        std::uint32_t enchantmentFlags{ 0 };
        std::int32_t enchantmentCostOverride{ 0 };
        std::uint32_t enchantmentCastingType{ 1 };
        std::int32_t enchantmentChargeOverride{ 0 };
        std::uint32_t enchantmentDelivery{ 0 };
        std::uint32_t enchantmentSpellType{ 6 };
        float enchantmentChargeTime{ 0.0F };
        FormRef baseEnchantment;
        FormRef wornRestrictions;
        std::uint32_t scrollFlags{ 0 };
        std::int32_t scrollCostOverride{ 0 };
        float scrollChargeTime{ 0.0F };
        std::uint32_t scrollDelivery{ 0 };
        float scrollCastDuration{ 0.0F };
        float scrollRange{ 0.0F };
        FormRef scrollCastingPerk;
        std::uint32_t projectileFlags{ 0 };
        std::uint32_t projectileTypes{ 1 };
        float projectileGravity{ 0.0F };
        float projectileSpeed{ 1000.0F };
        float projectileRange{ 10000.0F };
        FormRef projectileLight;
        FormRef projectileMuzzleFlashLight;
        float projectileTracerChance{ 0.0F };
        float projectileExplosionProximity{ 0.0F };
        float projectileExplosionTimer{ 0.0F };
        FormRef projectileExplosionType;
        FormRef projectileActiveSoundLoop;
        float projectileMuzzleFlashDuration{ 0.0F };
        float projectileFadeOutTime{ 0.0F };
        float projectileForce{ 0.0F };
        FormRef projectileCountdownSound;
        FormRef projectileDeactivateSound;
        FormRef projectileDefaultWeaponSource;
        float projectileConeSpread{ 0.0F };
        float projectileCollisionRadius{ 0.0F };
        float projectileLifetime{ 0.0F };
        float projectileRelaunchInterval{ 0.0F };
        FormRef projectileDecalData;
        FormRef projectileCollisionLayer;
        std::string projectileMuzzleFlashModel;
        std::uint32_t projectileSoundLevel{ 1 };
        std::array<std::string, 8> textureSetPaths;
        std::uint32_t textureSetFlags{ 0 };
        bool textureSetHasDecal{ false };
        float decalMinWidth{ 0.0F };
        float decalMaxWidth{ 0.0F };
        float decalMinHeight{ 0.0F };
        float decalMaxHeight{ 0.0F };
        float decalDepth{ 0.0F };
        float decalShininess{ 0.0F };
        float decalParallaxScale{ 0.0F };
        std::int32_t decalParallaxPasses{ 0 };
        std::uint32_t decalFlags{ 0 };
        std::uint8_t decalRed{ 255 };
        std::uint8_t decalGreen{ 255 };
        std::uint8_t decalBlue{ 255 };
        std::uint8_t decalAlpha{ 255 };
        std::uint32_t hazardLimit{ 0 };
        float hazardRadius{ 0.0F };
        float hazardLifetime{ 0.0F };
        float hazardImageSpaceRadius{ 0.0F };
        float hazardTargetInterval{ 0.0F };
        std::uint32_t hazardFlags{ 0 };
        FormRef hazardSpell;
        FormRef hazardLight;
        FormRef hazardImpactDataSet;
        FormRef hazardSound;
        FormRef hazardImageSpaceModifier;
        float impactEffectDuration{ 0.0F };
        std::uint32_t impactOrientation{ 0 };
        float impactAngleThreshold{ 0.0F };
        float impactPlacementRadius{ 0.0F };
        std::uint32_t impactSoundLevel{ 1 };
        std::uint32_t impactFlags{ 0 };
        std::uint32_t impactResultOverride{ 0 };
        FormRef impactDecalTextureSet;
        FormRef impactDecalTextureSet2;
        FormRef impactSound1;
        FormRef impactSound2;
        FormRef impactHazard;
        FormRef referenceEffectArtObject;
        FormRef referenceEffectShader;
        std::uint32_t referenceEffectFlags{ 0 };
        FormRef dualCastProjectile;
        FormRef dualCastExplosion;
        FormRef dualCastEffectShader;
        FormRef dualCastHitEffectArt;
        FormRef dualCastImpactDataSet;
        std::uint32_t dualCastFlags{ 0 };
        float staticMaterialThresholdAngle{ 0.0F };
        FormRef staticMaterialObject;
        std::uint32_t staticFlags{ 0 };
        std::uint32_t recordFlags{ 0 };
        FormRef movableStaticSoundLoop;
        std::uint32_t movableStaticFlags{ 0 };
        FormRef doorOpenSound;
        FormRef doorCloseSound;
        FormRef doorLoopSound;
        std::uint32_t doorFlags{ 0 };
        std::array<float, 10> combatGeneral{};
        std::array<float, 8> combatMelee{};
        std::array<float, 4> combatCloseRange{};
        float combatLongRangeStrafe{ 0.0F };
        std::array<float, 8> combatFlight{};
        std::uint32_t combatStyleFlags{ 0 };
        std::uint32_t soundCategoryFlags{ 0 };
        FormRef soundCategoryParent;
        std::uint16_t soundCategoryAttenuation{ 0 };
        float soundCategoryStaticMult{ 1.0F };
        float soundCategoryDefaultMenuValue{ 1.0F };
        float soundCategoryVolumeMult{ 1.0F };
        float soundCategoryFrequencyMult{ 1.0F };
        std::uint32_t classTeachesSkill{ 0 };
        std::uint8_t classMaximumTrainingLevel{ 0 };
        std::array<std::uint8_t, 18> classSkillWeights{};
        float classBleedoutDefault{ 0.0F };
        std::uint32_t classVoicePoints{ 0 };
        std::array<std::uint8_t, 3> classAttributeWeights{};
        std::string classIconPath;
        FormRef produceItem;
        FormRef harvestSound;
        std::array<std::int8_t, 4> produceChance{};
        std::uint32_t floraFlags{ 0 };
        FormRef floraSoundLoop;
        FormRef floraSoundActivate;
        FormRef floraWaterType;
        std::array<float, 12> treeAnimation{};
        std::uint32_t treeType{ 0 };
        FormRef createdItem;
        FormRef benchKeyword;
        std::uint16_t numConstructed{ 1 };
        std::vector<ContainerEntry> requiredItems;
        std::vector<ContainerEntry> containerItems;
        std::uint32_t containerFlags{ 0 };
        bool containerAllowStolenItems{ false };
        FormRef containerOpenSound;
        FormRef containerCloseSound;
        std::vector<FormRefPair> impactDataSetEntries;
        std::uint32_t collisionLayerIndex{ 0 };
        std::uint32_t collisionLayerColor{ 0xFFFFFFFFu };
        std::uint32_t collisionLayerFlags{ 0 };
        std::string collisionLayerName;
        std::vector<FormRef> collisionLayers;
        std::string footstepTag;
        FormRef footstepImpactDataSet;
        std::array<std::vector<FormRef>, 5> footstepSets;
        std::uint16_t reverbDecayTime{ 1000 };
        std::uint16_t reverbHFReference{ 5000 };
        std::array<std::int8_t, 9> reverbValues{};
        FormRef acousticLoopingSound;
        FormRef acousticSoundRegion;
        FormRef acousticReverb;
        std::uint32_t apparatusQuality{ 0 };
        std::uint8_t grassDensity{ 50 };
        std::uint8_t grassMinSlope{ 0 };
        std::uint8_t grassMaxSlope{ 90 };
        std::uint16_t grassDistanceFromWater{ 0 };
        std::uint32_t grassWaterState{ 0 };
        float grassPositionRange{ 0.0F };
        float grassHeightRange{ 0.0F };
        float grassColorRange{ 0.0F };
        float grassWavePeriod{ 1.0F };
        std::uint32_t grassFlags{ 0 };
        std::uint32_t idleFlags{ 0 };
        float idleTimer{ 0.0F };
        std::vector<FormRef> idleAnimations;
        FormRef encounterOwner;
        FormRef encounterLocation;
        std::int8_t encounterOwnerRank{ 0 };
        std::int8_t encounterMinLevel{ 0 };
        std::int8_t encounterMaxLevel{ 0 };
        std::uint32_t encounterFlags{ 0 };
        FormRef relationshipNpc1;
        FormRef relationshipNpc2;
        FormRef relationshipAssociation;
        std::uint32_t relationshipLevel{ 4 };
        std::uint32_t relationshipFlags{ 0 };
        std::array<std::string, 4> associationLabels;
        std::uint32_t associationFlags{ 0 };
        std::string movementName;
        std::array<float, 10> movementSpeeds{};
        float movementRotateWhileMoving{ 0.0F };
        float movementDirectional{ 0.0F };
        float movementSpeed{ 0.0F };
        float movementRotationSpeed{ 0.0F };
        std::string wordTranslation;
        std::array<std::string, 4> waterNoiseTextures;
        std::uint8_t waterAlpha{ 255 };
        std::uint32_t waterFlags{ 0 };
        FormRef waterMaterial;
        FormRef waterSound;
        FormRef waterContactSpell;
        FormRef waterImageSpace;
        std::array<float, 3> waterLinearVelocity{};
        std::array<float, 3> waterAngularVelocity{};
        std::array<float, 9> imageSpaceHDR{};
        std::array<float, 3> imageSpaceCinematic{};
        float imageSpaceTintAmount{ 0.0F };
        std::array<float, 3> imageSpaceTintColor{};
        std::array<float, 3> imageSpaceDOF{};
        std::uint16_t imageSpaceDOFFlags{ 0 };
        std::uint16_t imageSpaceSkyBlur{ 16384 };
        std::array<std::uint32_t, 7> lightingColors{};
        std::array<float, 8> lightingValues{};
        std::uint32_t lightingDirectionalXY{ 0 };
        std::uint32_t lightingDirectionalZ{ 0 };
        std::uint32_t lightingInheritanceFlags{ 0 };
        std::array<FormRef, 3> shoutWords;
        std::array<FormRef, 3> shoutSpells;
        std::array<float, 3> shoutRecoveryTimes{};
        std::vector<LeveledEntry> leveledEntries;
        std::uint8_t leveledChanceNone{ 0 };
        std::uint32_t leveledFlags{ 0 };
        FormRef leveledChanceGlobal;
        std::uint32_t actionIndex{ 0 };
        std::string eyesTexture;
        std::uint32_t eyesFlags{ 1 };
        std::string animatedUnloadEvent;
        std::string loadScreenText;
        FormRef loadScreenObject;
        float loadScreenInitialScale{ 1.0F };
        std::array<std::int16_t, 3> loadScreenRotationConstraints{};
        std::array<std::int16_t, 2> loadScreenRotationOffsetConstraints{};
        std::array<float, 3> loadScreenTranslationOffset{};
        std::string loadScreenCameraPath;
        std::array<float, 12> shaderParticleSettings{};
        std::string shaderParticleTexture;
        std::uint32_t addonIndex{ 0 };
        FormRef addonSound;
        std::uint16_t addonMasterParticleCap{ 0 };
        std::uint32_t addonFlags{ 0 };
        std::uint32_t magicEffectFlags{ 0 };
        float magicEffectBaseCost{ 0.0F };
        FormRef magicEffectAssociatedForm;
        std::int32_t magicEffectAssociatedSkill{ -1 };
        std::int32_t magicEffectResistVariable{ -1 };
        std::vector<FormRef> magicEffectCounterEffects;
        FormRef magicEffectLight;
        float magicEffectTaperWeight{ 0.0F };
        FormRef magicEffectShader;
        FormRef magicEffectEnchantShader;
        std::int32_t magicEffectMinimumSkill{ 0 };
        std::int32_t magicEffectSpellmakingArea{ 0 };
        float magicEffectSpellmakingChargeTime{ 0.0F };
        float magicEffectTaperCurve{ 0.0F };
        float magicEffectTaperDuration{ 0.0F };
        float magicEffectSecondAVWeight{ 0.0F };
        std::int32_t magicEffectArchetype{ 0 };
        std::int32_t magicEffectPrimaryAV{ -1 };
        FormRef magicEffectProjectile;
        FormRef magicEffectExplosion;
        std::uint32_t magicEffectCastingType{ 1 };
        std::uint32_t magicEffectDelivery{ 0 };
        std::int32_t magicEffectSecondaryAV{ -1 };
        FormRef magicEffectCastingArt;
        FormRef magicEffectHitEffectArt;
        FormRef magicEffectImpactDataSet;
        float magicEffectSkillUsageMult{ 0.0F };
        FormRef magicEffectDualCastData;
        float magicEffectDualCastScale{ 1.0F };
        FormRef magicEffectEnchantEffectArt;
        FormRef magicEffectHitVisuals;
        FormRef magicEffectEnchantVisuals;
        FormRef magicEffectEquipAbility;
        FormRef magicEffectImageSpaceMod;
        FormRef magicEffectPerk;
        std::uint32_t magicEffectCastingSoundLevel{ 1 };
        float magicEffectAIScore{ 0.0F };
        float magicEffectAIDelayTime{ 0.0F };
        std::array<FormRef, 6> magicEffectSounds;
        std::string magicItemDescription;
        std::uint32_t bookFlags{ 0 };
        std::uint32_t bookType{ 0 };
        FormRef teachesSpell;
        std::int32_t teachesActorValue{ -1 };
        FormRef linkedSoulGem;
        std::uint32_t currentSoul{ 0 };
        std::uint32_t soulCapacity{ 0 };
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
