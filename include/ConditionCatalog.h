#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace ConditionCatalog {
    struct FunctionInfo
    {
        std::uint32_t id;
        const char* name;
        const char* rawParam1;
        const char* rawParam2;
        const char* rawParam3;
    };

    std::span<const FunctionInfo> GetFunctions();
    const FunctionInfo* FindFunction(std::uint32_t id);
    const char* GetFunctionName(std::uint32_t id);
    bool IsIntegerParam(std::string_view rawType);
    bool IsFloatParam(std::string_view rawType);
    bool IsStringParam(std::string_view rawType);
    bool IsFormParam(std::string_view rawType);
}
