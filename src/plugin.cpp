#include "Configuration.h"
#include "Manager.h"
#include "logger.h"

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kPostLoad) {
        Configuration::Register();
    }
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        Manager::LoadForms();
        Manager::ApplyAllForms();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {

    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    Manager::InstallHooks();
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
