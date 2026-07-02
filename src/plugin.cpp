#include "Configuration.h"
#include "Manager.h"
#include "logger.h"

namespace {
    void InitializeDynamicForms() {
        Manager::LoadForms();
        Manager::ApplyAllForms();
    }

    void QueueInitializeDynamicForms() {
        if (const auto task = SKSE::GetTaskInterface()) {
            task->AddTask(InitializeDynamicForms);
        } else {
            InitializeDynamicForms();
        }
    }
}

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kPostLoad) {
        Configuration::Register();
    }
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        Manager::LoadForms();
        QueueInitializeDynamicForms();
    }
    if (message->type == SKSE::MessagingInterface::kNewGame) {
        InitializeDynamicForms();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {

    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
