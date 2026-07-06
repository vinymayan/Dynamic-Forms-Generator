#include "ListManager.h"

#include "DPFAPI.h"
#include "logger.h"

namespace FormUtil {
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

        const auto editorID = clib_util::editorID::get_editorID(perk);
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

    PopulateList<RE::BGSProjectile>("Projectile", [](RE::BGSProjectile* projectile) -> bool {
        return projectile != nullptr;
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

    PopulateList<RE::BGSEquipSlot>("EquipSlot", [](RE::BGSEquipSlot* equipSlot) -> bool {
        return equipSlot != nullptr;
    });

    PopulateList<RE::BGSFootstepSet>("FootstepSet", [](RE::BGSFootstepSet* footstepSet) -> bool {
        return footstepSet != nullptr;
    });

    PopulateList<RE::BGSMaterialType>("MaterialType", [](RE::BGSMaterialType* materialType) -> bool {
        return materialType != nullptr;
    });

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

    PopulateList<RE::TESFaction>("Faction", [](RE::TESFaction* faction) -> bool {
        return faction != nullptr;
    });

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

    const auto& forms = dataHandler->GetFormArray<T>();
    list.reserve(forms.size());

    for (const auto& form : forms) {
        if (!form) continue;

        if (form->IsDeleted() || form->IsIgnored()) {
            continue;
        }

        if (a_filter && !a_filter(form)) {
            continue;
        }

        RE::FormID currentID = 0;
        std::string currentPlugin = "Unknown";

        try {
            currentID = form->GetFormID();

            if (auto primaryFile = form->GetFile(0)) {
                currentPlugin = std::string(primaryFile->GetFilename());
            }
            else if (auto masterFile = FormUtil::GetMasterFile(form)) {
                currentPlugin = std::string(masterFile->GetFilename());
            }
            else {
                currentPlugin = "Dynamic";
            }

            InternalFormInfo info;
            info.formID = currentID;
            info.formType = a_typeName;
            info.pluginName = ToUTF8(currentPlugin);

            std::string rawEditorID = clib_util::editorID::get_editorID(form);
            info.editorID = ToUTF8(rawEditorID);

            std::string rawName = "";
            if (form->Is(RE::FormType::NPC)) {
                if (auto npc = form->As<RE::TESNPC>()) {
                    rawName = npc->fullName.c_str();
                }
            }
            else if (auto fullName = form->As<RE::TESFullName>()) {
                rawName = fullName->fullName.c_str();
            }

            info.name = ToUTF8(rawName);
            info.description = "";
            info.nextPerkId = "";

            if (auto perk = form->As<RE::BGSPerk>()) {
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

            if (info.pluginName == "Dynamic Persistent Forms.esp") {
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
