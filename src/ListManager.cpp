#include "ListManager.h"

#include "DPFAPI.h"
#include "logger.h"

namespace FormUtil {
    std::string GetEditorIDSafe(const RE::TESForm* form) {
        if (!form) {
            return {};
        }

        const char* editorID = nullptr;
        switch (form->GetFormType()) {
        case RE::FormType::Keyword:
        case RE::FormType::LocationRefType:
        case RE::FormType::Action:
        case RE::FormType::MenuIcon:
        case RE::FormType::Global:
        case RE::FormType::HeadPart:
        case RE::FormType::Race:
        case RE::FormType::Sound:
        case RE::FormType::Script:
        case RE::FormType::Navigation:
        case RE::FormType::Cell:
        case RE::FormType::WorldSpace:
        case RE::FormType::Land:
        case RE::FormType::NavMesh:
        case RE::FormType::Dialogue:
        case RE::FormType::Quest:
        case RE::FormType::Idle:
        case RE::FormType::AnimatedObject:
        case RE::FormType::ImageAdapter:
        case RE::FormType::VoiceType:
        case RE::FormType::Ragdoll:
        case RE::FormType::DefaultObject:
        case RE::FormType::MusicType:
        case RE::FormType::StoryManagerBranchNode:
        case RE::FormType::StoryManagerQuestNode:
        case RE::FormType::StoryManagerEventNode:
            editorID = form->GetFormEditorID();
            break;
        default: {
            using GetFormEditorID = const char* (*)(std::uint32_t);
            static const auto tweaks = GetModuleHandleW(L"po3_Tweaks");
            static const auto getFormEditorID = tweaks ?
                reinterpret_cast<GetFormEditorID>(GetProcAddress(tweaks, "GetFormEditorID")) : nullptr;
            if (getFormEditorID) {
                editorID = getFormEditorID(form->GetFormID());
            }
            break;
        }
        }

        return editorID && *editorID != '\0' ? std::string(editorID) : std::string{};
    }

    const RE::TESFile* GetMasterFile(RE::TESForm* ref) {
        if (!ref) return nullptr;

        uint32_t formID = ref->GetFormID();
        uint8_t modIndex = static_cast<uint8_t>(formID >> 24);

        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return nullptr;

        if (modIndex == 0xFE) {
            uint16_t eslIndex = (formID >> 12) & 0xFFF;
            return dataHandler->LookupLoadedLightModByIndex(eslIndex);
        }

        return dataHandler->LookupLoadedModByIndex(modIndex);
    }

    std::string NormalizeFormID(RE::TESForm* form) {
        if (!form) return {};

        RE::FormID formID = form->GetFormID();
        uint8_t modIndex = (formID >> 24) & 0xFF;

        if (modIndex == 0xFF) {
            return std::format("{:X}", formID);
        }

        auto file = GetMasterFile(form);
        if (!file) return std::format("{:X}", formID);

        uint32_t localID = formID & 0x00FFFFFF;

        if (modIndex == 0xFE) {
            uint32_t eslID = localID & 0xFFF;
            return std::format("{}|{:X}", file->GetFilename(), eslID);
        }

        return std::format("{}|{:X}", file->GetFilename(), localID);
    }

    RE::FormID FormIDFromString(const std::string& str) {
        auto pos = str.find('|');
        if (pos != std::string::npos) {
            std::string plugin = str.substr(0, pos);
            std::string idStr = str.substr(pos + 1);
            RE::FormID localId = std::stoul(idStr, nullptr, 16);
            auto dataHandler = RE::TESDataHandler::GetSingleton();
            return dataHandler ? dataHandler->LookupFormID(localId, plugin) : 0;
        }
        return str.empty() ? 0 : std::stoul(str, nullptr, 16);
    }
}

namespace {
    bool IsDPFPluginName(const std::string_view name) {
        if (name == "DPF.esp") return true;
        if (!name.starts_with("DPF ") || !name.ends_with(".esp")) return false;
        const auto number = name.substr(4, name.size() - 8);
        return !number.empty() && std::ranges::all_of(number, [](const char value) {
            return value >= '0' && value <= '9';
        });
    }

    bool IsValidUTF8(const std::string& string) {
        int c, i, ix, n, j;
        for (i = 0, ix = static_cast<int>(string.length()); i < ix; i++) {
            c = static_cast<unsigned char>(string[i]);
            if (c <= 0x7f) n = 0;
            else if ((c & 0xE0) == 0xC0) n = 1;
            else if (c == 0xED && i < (ix - 1) && (static_cast<unsigned char>(string[i + 1]) & 0xA0) == 0xA0) return false;
            else if ((c & 0xF0) == 0xE0) n = 2;
            else if ((c & 0xF8) == 0xF0) n = 3;
            else return false;

            for (j = 0; j < n && i < ix; j++) {
                if ((++i == ix) || ((static_cast<unsigned char>(string[i]) & 0xC0) != 0x80)) {
                    return false;
                }
            }
        }
        return true;
    }

    std::uint32_t GetLocalFormID(const RE::FormID formID) {
        if ((formID & 0xFF000000) == 0xFE000000) {
            return formID & 0xFFF;
        }
        return formID & 0x00FFFFFF;
    }

    void LogConditionList(const char* label, RE::TESCondition& conditions) {
        std::uint32_t index = 0;
        for (auto* item = conditions.head; item; item = item->next) {
            const auto functionID = static_cast<std::uint32_t>(item->data.functionData.function.get());
            const auto opCode = static_cast<std::uint32_t>(item->data.flags.opCode);
            const bool isOr = item->data.flags.isOR;
            const auto param1 = reinterpret_cast<std::uintptr_t>(item->data.functionData.params[0]);
            const auto param2 = reinterpret_cast<std::uintptr_t>(item->data.functionData.params[1]);

            if (item->data.flags.global && item->data.comparisonValue.g) {
                logger::info("[NPC Senses perk FE000809] {} condition[{}]: functionID={} opCode={} OR={} compareGlobal={:08X} param1={:X} param2={:X}",
                    label,
                    index,
                    functionID,
                    opCode,
                    isOr,
                    item->data.comparisonValue.g->GetFormID(),
                    param1,
                    param2);
            } else {
                logger::info("[NPC Senses perk FE000809] {} condition[{}]: functionID={} opCode={} OR={} compareValue={} param1={:X} param2={:X}",
                    label,
                    index,
                    functionID,
                    opCode,
                    isOr,
                    item->data.comparisonValue.f,
                    param1,
                    param2);
            }
            ++index;
        }

        if (index == 0) {
            logger::info("[NPC Senses perk FE000809] {} has no conditions.", label);
        }
    }

    void LogNpcSensesPerkConditions(RE::BGSPerk* perk, const std::string& pluginName) {
        if (!perk || pluginName != "NPC Senses.esp" || GetLocalFormID(perk->GetFormID()) != 0x809) {
            return;
        }

        const auto editorID = FormUtil::GetEditorIDSafe(perk);
        logger::info("[NPC Senses perk FE000809] Found perk. EditorID='{}' FormID={:08X} entries={}",
            editorID,
            perk->GetFormID(),
            perk->perkEntries.size());

        LogConditionList("perk", perk->perkConditions);

        for (std::uint32_t entryIndex = 0; entryIndex < perk->perkEntries.size(); ++entryIndex) {
            auto* entry = perk->perkEntries[entryIndex];
            if (!entry) {
                logger::info("[NPC Senses perk FE000809] entry[{}] is null.", entryIndex);
                continue;
            }

            logger::info("[NPC Senses perk FE000809] entry[{}]: type={} rank={} priority={}",
                entryIndex,
                static_cast<std::uint32_t>(entry->GetType()),
                entry->header.rank,
                entry->header.priority);

            if (entry->GetType() != RE::PERK_ENTRY_TYPE::kEntryPoint) {
                continue;
            }

            auto* entryPoint = static_cast<RE::BGSEntryPointPerkEntry*>(entry);
            logger::info("[NPC Senses perk FE000809] entry[{}] entryPoint={} function={} numArgs={} conditionLists={}",
                entryIndex,
                static_cast<std::uint32_t>(entryPoint->entryData.entryPoint.get()),
                static_cast<std::uint32_t>(entryPoint->entryData.function.get()),
                entryPoint->entryData.numArgs,
                entryPoint->conditions.size());

            for (std::size_t conditionIndex = 0; conditionIndex < entryPoint->conditions.size(); ++conditionIndex) {
                const auto label = std::format("entry[{}].conditions[{}]", entryIndex, conditionIndex);
                LogConditionList(label.c_str(), entryPoint->conditions[conditionIndex]);
            }
        }
    }
}

bool ListManager::PopulateAllLists(const bool forceRefresh) {
    if (!DPF::GetAPI()) {
        logger::warn("[ListManager] DPF API indisponivel. Listas nao serao atualizadas.");
        return false;
    }

    if (_isPopulated && !forceRefresh) {
        return true;
    }

    if (!RE::TESDataHandler::GetSingleton()) {
        logger::warn("[ListManager] TESDataHandler indisponivel. Listas nao serao atualizadas.");
        return false;
    }

    logger::info("Iniciando escaneamento de FormTypes...");

    _dataStore.clear();
    ++_generation;
    PopulateList<RE::BGSPerk>("Perk", [](RE::BGSPerk* perk) -> bool {
        return perk != nullptr;
    });

    PopulateList<RE::TESObjectBOOK>("Book", [](RE::TESObjectBOOK* book) -> bool {
        return book != nullptr;
    });
    PopulateList<RE::TESObjectMISC>("MiscItem", [](RE::TESObjectMISC* misc) -> bool {
        return misc != nullptr;
    });
    PopulateList<RE::TESKey>("Key", [](RE::TESKey* key) -> bool {
        return key != nullptr;
    });
    PopulateList<RE::TESSoulGem>("SoulGem", [](RE::TESSoulGem* soulGem) -> bool {
        return soulGem != nullptr;
    });
    PopulateList<RE::TESAmmo>("Ammo", [](RE::TESAmmo* ammo) -> bool {
        return ammo != nullptr;
    });
    PopulateList<RE::TESObjectWEAP>("Weapon", [](RE::TESObjectWEAP* weapon) -> bool {
        return weapon != nullptr;
    });
    PopulateList<RE::AlchemyItem>("AlchemyItem", [](RE::AlchemyItem* item) -> bool {
        return item != nullptr;
    });
    PopulateList<RE::IngredientItem>("Ingredient", [](RE::IngredientItem* item) -> bool {
        return item != nullptr;
    });

    PopulateList<RE::TESGlobal>("Global", [](RE::TESGlobal* global) -> bool {
        return global != nullptr;
    });

    PopulateList<RE::BGSKeyword>("Keyword", [](RE::BGSKeyword* keyword) -> bool {
        return keyword != nullptr;
    });

    PopulateList<RE::BGSOutfit>("Outfit", [](RE::BGSOutfit* outfit) -> bool {
        return outfit != nullptr;
    });

    PopulateList<RE::TESObjectARMO>("Armor", [](RE::TESObjectARMO* armor) -> bool {
        return armor != nullptr;
    });

    PopulateList<RE::TESObjectARMA>("ArmorType", [](RE::TESObjectARMA* armorType) -> bool {
        return armorType != nullptr;
    });

    PopulateList<RE::TESObjectSTAT>("Static", [](RE::TESObjectSTAT* stat) -> bool {
        return stat != nullptr;
    });
    PopulateList<RE::BGSMovableStatic>("MovableStatic", [](RE::BGSMovableStatic* stat) -> bool { return stat != nullptr; });
    PopulateList<RE::TESObjectDOOR>("Door", [](RE::TESObjectDOOR* door) -> bool { return door != nullptr; });
    PopulateList<RE::TESFlora>("Flora", [](RE::TESFlora* flora) -> bool { return flora != nullptr; });
    PopulateList<RE::TESObjectTREE>("Tree", [](RE::TESObjectTREE* tree) -> bool { return tree != nullptr; });

    PopulateList<RE::TESLevItem>("LeveledItem", [](RE::TESLevItem* leveledItem) -> bool {
        return leveledItem != nullptr;
    });

    PopulateList<RE::BGSColorForm>("Color", [](RE::BGSColorForm* color) -> bool {
        return color != nullptr;
    });

    PopulateList<RE::BGSArtObject>("ArtObject", [](RE::BGSArtObject* artObject) -> bool {
        return artObject != nullptr;
    });

    PopulateList<RE::TESQuest>("Quest", [](RE::TESQuest* quest) -> bool {
        return quest != nullptr;
    });

    PopulateList<RE::SpellItem>("Spell", [](RE::SpellItem* spell) -> bool {
        return spell != nullptr;
    });
    PopulateList<RE::SpellItem>("Ability", [](RE::SpellItem* spell) -> bool {
        return spell &&
               spell->GetSpellType() == RE::MagicSystem::SpellType::kAbility;
    });

    PopulateList<RE::ScrollItem>("Scroll", [](RE::ScrollItem* scroll) -> bool {
        return scroll != nullptr;
    });

    PopulateList<RE::BGSHeadPart>("HeadPart", [](RE::BGSHeadPart* headPart) -> bool {
        return headPart != nullptr;
    });

    PopulateList<RE::BGSHeadPart>("Hair", [](RE::BGSHeadPart* headPart) -> bool {
        return headPart && headPart->type == RE::BGSHeadPart::HeadPartType::kHair;
    });

    PopulateList<RE::BGSHeadPart>("Facial Hair", [](RE::BGSHeadPart* headPart) -> bool {
        return headPart && headPart->type == RE::BGSHeadPart::HeadPartType::kFacialHair;
    });

    PopulateList<RE::BGSHeadPart>("Eye Brows", [](RE::BGSHeadPart* headPart) -> bool {
        return headPart && headPart->type == RE::BGSHeadPart::HeadPartType::kEyebrows;
    });

    PopulateList<RE::BGSHeadPart>("Eye", [](RE::BGSHeadPart* headPart) -> bool {
        return headPart && headPart->type == RE::BGSHeadPart::HeadPartType::kEyes;
    });

    PopulateList<RE::BGSHeadPart>("Face", [](RE::BGSHeadPart* headPart) -> bool {
        return headPart && headPart->type == RE::BGSHeadPart::HeadPartType::kFace;
    });

    PopulateList<RE::BGSHeadPart>("Misc", [](RE::BGSHeadPart* headPart) -> bool {
        return headPart && headPart->type == RE::BGSHeadPart::HeadPartType::kMisc;
    });

    PopulateList<RE::BGSHeadPart>("Scar", [](RE::BGSHeadPart* headPart) -> bool {
        return headPart && headPart->type == RE::BGSHeadPart::HeadPartType::kScar;
    });

    PopulateList<RE::BGSTextureSet>("TextureSet", [](RE::BGSTextureSet* textureSet) -> bool {
        return textureSet != nullptr;
    });

    PopulateList<RE::BGSListForm>("FormList", [](RE::BGSListForm* formList) -> bool {
        return formList != nullptr;
    });

    PopulateList<RE::TESPackage>("Package", [](RE::TESPackage* package) -> bool {
        return package != nullptr;
    });

    PopulateList<RE::BGSSoundDescriptorForm>("SoundDescriptor", [](RE::BGSSoundDescriptorForm* sound) -> bool {
        return sound != nullptr;
    });

    PopulateList<RE::BGSSoundCategory>("SoundCategory", [](RE::BGSSoundCategory* category) -> bool {
        return category != nullptr;
    });

    PopulateList<RE::BGSSoundOutput>("SoundOutput", [](RE::BGSSoundOutput* output) -> bool {
        return output != nullptr;
    });

    PopulateList<RE::TESObjectLIGH>("Light", [](RE::TESObjectLIGH* light) -> bool {
        return light != nullptr;
    });

    PopulateList<RE::BGSExplosion>("Explosion", [](RE::BGSExplosion* explosion) -> bool {
        return explosion != nullptr;
    });

    PopulateList<RE::TESObjectACTI>("Activator", [](RE::TESObjectACTI* activator) -> bool {
        return activator != nullptr;
    });

    PopulateList<RE::TESEffectShader>("EffectShader", [](RE::TESEffectShader* shader) -> bool {
        return shader != nullptr;
    });

    PopulateList<RE::BGSImpactDataSet>("ImpactDataSet", [](RE::BGSImpactDataSet* impactDataSet) -> bool {
        return impactDataSet != nullptr;
    });
    PopulateList<RE::BGSImpactData>("ImpactData", [](RE::BGSImpactData* impactData) -> bool { return impactData != nullptr; });
    PopulateList<RE::BGSHazard>("Hazard", [](RE::BGSHazard* hazard) -> bool { return hazard != nullptr; });

    PopulateList<RE::BGSProjectile>("Projectile", [](RE::BGSProjectile* projectile) -> bool {
        return projectile != nullptr;
    });

    PopulateList<RE::BGSCollisionLayer>("CollisionLayer", [](RE::BGSCollisionLayer* layer) -> bool {
        return layer != nullptr;
    });

    PopulateList<RE::TESImageSpaceModifier>("ImageSpaceModifier", [](RE::TESImageSpaceModifier* imageSpaceModifier) -> bool {
        return imageSpaceModifier != nullptr;
    });

    PopulateList<RE::TESWaterForm>("Water", [](RE::TESWaterForm* water) -> bool {
        return water != nullptr;
    });

    PopulateList<RE::BGSLensFlare>("LensFlare", [](RE::BGSLensFlare* lensFlare) -> bool {
        return lensFlare != nullptr;
    });

    PopulateList<RE::EnchantmentItem>("Enchantment", [](RE::EnchantmentItem* enchantment) -> bool {
        return enchantment != nullptr;
    });
    PopulateList<RE::EffectSetting>("MagicEffect", [](RE::EffectSetting* effect) -> bool {
        return effect != nullptr;
    });
    PopulateList<RE::EffectSetting>("EffectSetting", [](RE::EffectSetting* effect) -> bool {
        return effect != nullptr;
    });

    PopulateList<RE::BGSDualCastData>("DualCastData", [](RE::BGSDualCastData* data) -> bool {
        return data != nullptr;
    });

    PopulateList<RE::BGSReferenceEffect>("ReferenceEffect", [](RE::BGSReferenceEffect* effect) -> bool {
        return effect != nullptr;
    });

    PopulateList<RE::BGSEquipSlot>("EquipSlot", [](RE::BGSEquipSlot* equipSlot) -> bool {
        return equipSlot != nullptr;
    });

    PopulateList<RE::BGSFootstepSet>("FootstepSet", [](RE::BGSFootstepSet* footstepSet) -> bool {
        return footstepSet != nullptr;
    });
    PopulateList<RE::BGSFootstep>("Footstep", [](RE::BGSFootstep* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSReverbParameters>("ReverbParameters", [](RE::BGSReverbParameters* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSAcousticSpace>("AcousticSpace", [](RE::BGSAcousticSpace* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSAssociationType>("AssociationType", [](RE::BGSAssociationType* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESImageSpace>("ImageSpace", [](RE::TESImageSpace* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESRegion>("Region", [](RE::TESRegion* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSLocation>("Location", [](RE::BGSLocation* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESIdleForm>("Idle", [](RE::TESIdleForm* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESShout>("Shout", [](RE::TESShout* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESWordOfPower>("WordOfPower", [](RE::TESWordOfPower* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESLevItem>("LeveledItem", [](RE::TESLevItem* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESLevCharacter>("LeveledNPC", [](RE::TESLevCharacter* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESLevSpell>("LeveledSpell", [](RE::TESLevSpell* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSLocationRefType>("LocationRefType", [](RE::BGSLocationRefType* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSAction>("Action", [](RE::BGSAction* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSMenuIcon>("MenuIcon", [](RE::BGSMenuIcon* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESEyes>("Eyes", [](RE::TESEyes* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSNote>("Note", [](RE::BGSNote* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESObjectANIO>("AnimatedObject", [](RE::TESObjectANIO* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESLoadScreen>("LoadScreen", [](RE::TESLoadScreen* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSShaderParticleGeometryData>("ShaderParticleGeometry", [](RE::BGSShaderParticleGeometryData* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSAddonNode>("AddonNode", [](RE::BGSAddonNode* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSApparatus>("Apparatus", [](RE::BGSApparatus* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSStaticCollection>("StaticCollection", [](RE::BGSStaticCollection* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESGrass>("Grass", [](RE::TESGrass* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSIdleMarker>("IdleMarker", [](RE::BGSIdleMarker* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSEncounterZone>("EncounterZone", [](RE::BGSEncounterZone* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSRelationship>("Relationship", [](RE::BGSRelationship* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSMovementType>("MovementType", [](RE::BGSMovementType* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSLightingTemplate>("LightingTemplate", [](RE::BGSLightingTemplate* value) -> bool { return value != nullptr; });

    PopulateList<RE::BGSMaterialType>("MaterialType", [](RE::BGSMaterialType* materialType) -> bool {
        return materialType != nullptr;
    });
    PopulateList<RE::BGSMaterialObject>("MaterialObject", [](RE::BGSMaterialObject* materialObject) -> bool { return materialObject != nullptr; });

    PopulateList<RE::TESNPC>("NPC", [](RE::TESNPC* npc) -> bool {
        return npc != nullptr;
    });

    PopulateList<RE::TESRace>("Race", [](RE::TESRace* race) -> bool {
        return race != nullptr;
    });

    PopulateList<RE::BGSVoiceType>("Voice", [](RE::BGSVoiceType* voice) -> bool {
        return voice != nullptr;
    });

    PopulateList<RE::TESClass>("Class", [](RE::TESClass* npcClass) -> bool {
        return npcClass != nullptr;
    });

    PopulateList<RE::TESCombatStyle>("CombatStyle", [](RE::TESCombatStyle* combatStyle) -> bool {
        return combatStyle != nullptr;
    });
    PopulateList<RE::BGSConstructibleObject>("ConstructibleObject", [](RE::BGSConstructibleObject* recipe) -> bool {
        return recipe != nullptr;
    });
    PopulateList<RE::TESObjectCONT>("Container", [](RE::TESObjectCONT* container) -> bool {
        return container != nullptr;
    });

    PopulateList<RE::TESFaction>("Faction", [](RE::TESFaction* faction) -> bool {
        return faction != nullptr;
    });
    PopulateList<RE::TESQuest>("Quest", [](RE::TESQuest* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSMaterialObject>("MaterialObject", [](RE::BGSMaterialObject* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSMessage>("Message", [](RE::BGSMessage* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESLandTexture>("LandTexture", [](RE::TESLandTexture* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSSoundOutput>("SoundOutputModel", [](RE::BGSSoundOutput* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSLensFlare>("LensFlare", [](RE::BGSLensFlare* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSDebris>("Debris", [](RE::BGSDebris* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESImageSpaceModifier>("ImageSpaceModifier", [](RE::TESImageSpaceModifier* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSCameraShot>("CameraShot", [](RE::BGSCameraShot* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSCameraPath>("CameraPath", [](RE::BGSCameraPath* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSTalkingActivator>("TalkingActivator", [](RE::BGSTalkingActivator* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESFurniture>("Furniture", [](RE::TESFurniture* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESWeather>("Weather", [](RE::TESWeather* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESClimate>("Climate", [](RE::TESClimate* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSMusicType>("MusicType", [](RE::BGSMusicType* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSMusicTrackFormWrapper>("MusicTrack", [](RE::BGSMusicTrackFormWrapper* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSBodyPartData>("BodyPartData", [](RE::BGSBodyPartData* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSVolumetricLighting>("VolumetricLighting", [](RE::BGSVolumetricLighting* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESSound>("Sound", [](RE::TESSound* value) -> bool { return value != nullptr; });
    PopulateList<RE::ActorValueInfo>("ActorValueInfo", [](RE::ActorValueInfo* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSVoiceType>("VoiceType", [](RE::BGSVoiceType* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSDialogueBranch>("DialogueBranch", [](RE::BGSDialogueBranch* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESTopic>("DialogueTopic", [](RE::TESTopic* value) -> bool { return value != nullptr; });
    PopulateList<RE::TESTopicInfo>("DialogueInfo", [](RE::TESTopicInfo* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSScene>("Scene", [](RE::BGSScene* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSStoryManagerBranchNode>("StoryManagerBranchNode", [](RE::BGSStoryManagerBranchNode* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSStoryManagerQuestNode>("StoryManagerQuestNode", [](RE::BGSStoryManagerQuestNode* value) -> bool { return value != nullptr; });
    PopulateList<RE::BGSStoryManagerEventNode>("StoryManagerEventNode", [](RE::BGSStoryManagerEventNode* value) -> bool { return value != nullptr; });

    _isPopulated = true;
    for (auto cb : _readyCallbacks) {
        if (cb) cb();
    }
    _readyCallbacks.clear();
    return true;
}

const std::vector<InternalFormInfo>& ListManager::GetList(const std::string& typeName) {
    static std::vector<InternalFormInfo> empty;
    auto it = _dataStore.find(typeName);
    if (it != _dataStore.end()) {
        return it->second;
    }
    return empty;
}

void ListManager::RegisterReadyCallback(std::function<void()> callback) {
    if (_isPopulated) {
        callback();
    }
    else {
        _readyCallbacks.push_back(callback);
    }
}

const InternalFormInfo* ListManager::GetInfoByID(const std::string& type, RE::FormID id) {
    const auto& list = GetList(type);
    for (const auto& info : list) {
        if (info.formID == id) {
            return &info;
        }
    }
    return nullptr;
}

void ListManager::Save(SKSE::SerializationInterface*) {}

void ListManager::Load(SKSE::SerializationInterface*) {}

void ListManager::Revert(SKSE::SerializationInterface*) {
    _isPopulated = false;
    _dataStore.clear();
    _readyCallbacks.clear();
}

std::string ListManager::ToUTF8(std::string_view a_str) {
    if (a_str.empty()) return "";

    std::string srcString(a_str);

    if (IsValidUTF8(srcString)) {
        return srcString;
    }

    int wlen = MultiByteToWideChar(CP_ACP, 0, srcString.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return srcString;

    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_ACP, 0, srcString.c_str(), -1, &wstr[0], wlen);

    int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return srcString;

    std::string u8str(u8len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &u8str[0], u8len, nullptr, nullptr);

    if (!u8str.empty() && u8str.back() == '\0') u8str.pop_back();

    return u8str;
}

template <typename T>
void ListManager::PopulateList(const std::string& a_typeName, std::function<bool(T*)> a_filter) {
    auto dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) return;

    auto& list = _dataStore[a_typeName];
    list.clear();

    // TESDataHandler stores TESForm pointers. Reinterpreting that array as T* is
    // unsafe for forms whose TESForm base is not at offset zero (for example,
    // BGSMovableStatic). Keep the original TESForm pointer and let As<T>() apply
    // the required multiple-inheritance adjustment.
    const auto& forms = dataHandler->GetFormArray(T::FORMTYPE);
    list.reserve(forms.size());

    for (auto* rawForm : forms) {
        if (!rawForm) continue;

        if (rawForm->IsDeleted() || rawForm->IsIgnored()) {
            continue;
        }

        auto* form = rawForm->As<T>();
        if (!form) {
            logger::warn(
                "[PopulateList] Skipping form {:08X}: expected type '{}' but runtime type is {}.",
                rawForm->GetFormID(),
                a_typeName,
                static_cast<std::uint32_t>(rawForm->GetFormType()));
            continue;
        }

        if (a_filter && !a_filter(form)) {
            continue;
        }

        RE::FormID currentID = rawForm->GetFormID();
        std::string currentPlugin = "Unknown";

        try {
            if (auto primaryFile = rawForm->GetFile(0)) {
                currentPlugin = std::string(primaryFile->GetFilename());
            }
            else if (auto masterFile = FormUtil::GetMasterFile(rawForm)) {
                currentPlugin = std::string(masterFile->GetFilename());
            }
            else {
                currentPlugin = "Dynamic";
            }

            InternalFormInfo info;
            info.formID = currentID;
            info.formType = a_typeName;
            info.pluginName = ToUTF8(currentPlugin);
            info.normalizedFormID = FormUtil::NormalizeFormID(rawForm);

            std::string rawEditorID = FormUtil::GetEditorIDSafe(rawForm);
            info.editorID = ToUTF8(rawEditorID);

            std::string rawName = "";
            if (rawForm->Is(RE::FormType::NPC)) {
                if (auto npc = rawForm->As<RE::TESNPC>()) {
                    rawName = npc->fullName.c_str();
                }
            }
            else if (auto fullName = rawForm->As<RE::TESFullName>()) {
                rawName = fullName->fullName.c_str();
            }

            info.name = ToUTF8(rawName);
            info.description = "";
            info.nextPerkId = "";

            if (auto perk = rawForm->As<RE::BGSPerk>()) {
                RE::BSString descStr;
                perk->TESDescription::GetDescription(descStr, perk);
                info.description = ToUTF8(descStr.c_str());

                if (perk->nextPerk) {
                    auto npFile = perk->nextPerk->GetFile(0);
                    std::string npPlugin = npFile ? std::string(npFile->GetFilename()) : "Dynamic";
                    uint32_t npLocalID = (perk->nextPerk->GetFormID() & 0xFF000000) == 0xFE000000 ?
                        (perk->nextPerk->GetFormID() & 0xFFF) :
                        (perk->nextPerk->GetFormID() & 0xFFFFFF);
                    info.nextPerkId = fmt::format("{}|{:X}", npPlugin, npLocalID);
                }

                LogNpcSensesPerkConditions(perk, info.pluginName);
            }

            list.push_back(info);

            if (IsDPFPluginName(info.pluginName)) {
                logger::info("[ListManager] DPF form found. Type: {} EditorID: '{}' FormID: {:08X}",
                    a_typeName,
                    info.editorID,
                    info.formID);
            }
        }
        catch (const std::exception& e) {
            logger::error("[PopulateList] Critical error on item {:08X} of plugin '{}' (Type: {}). Error: {}",
                currentID, currentPlugin, a_typeName, e.what());
        }
        catch (...) {
            logger::error("[PopulateList] Uknown error on item {:08X} of plugin '{}' (Type: {})",
                currentID, currentPlugin, a_typeName);
        }
    }
    logger::info("Carregados {} itens do tipo {}", list.size(), a_typeName);
}
