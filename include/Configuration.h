#pragma once

namespace Configuration {
    void Register();
    void LoadLanguage();
    const char* GetLoc(const char* key, const char* fallback);
    void LoadForms();
    void SaveForms();
    void RenderFormsMenu();
}
