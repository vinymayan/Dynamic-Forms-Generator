#include "Configuration.h"
#include "Manager.h"
#include "logger.h"

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kPostLoad) {
        Configuration::Register();
    }
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        // Install after kPostPostLoad so prologue hooks from plugins such as
        // Dynamic String Distributor are already complete and can be chained.
        Manager::InstallHooks();
        Manager::LoadForms();
        Manager::ApplyAllForms();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {

    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
