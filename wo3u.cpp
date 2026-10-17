#include "stdafx.h"
#include "version.h"
#include "helper.hpp"

#ifdef _DEBUG
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#else
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <cmath>
#include <cstring>
#include <windows.h>

HMODULE exeModule = GetModuleHandle(NULL);
HMODULE thisModule;

inipp::Ini<char> ini;
std::string sConfigFile = "WO3U_ARFix.ini"; 

std::shared_ptr<spdlog::logger> logger;
std::string sLogFile = "WO3U_ARFix.log";
std::filesystem::path ExePath;

bool bEnableARFix = true;
int iAspectWidth = 4;
int iAspectHeight = 3;
float fTargetARValue = 1.3333333f;

bool bEnableFOVFix = true;
float fFOVPercent = 120.0f; 
float fTargetFOVValue = 0.01745329238f * 1.20f; 

template<typename T>
T GetConfigAndLog(inipp::Ini<char>& ini, const std::string& section, const std::string& key, T defaultValue) {
    T value = defaultValue;
    inipp::get_value(ini.sections[section], key, value);
    SPDLOG_INFO("Config Parse: [{}]: {} = {}", section, key, value);
    return value;
}

void Logging() {
    WCHAR gameExePath[_MAX_PATH] = {0};
    GetModuleFileNameW(exeModule, gameExePath, MAX_PATH);
    ExePath = gameExePath;
    ExePath = ExePath.remove_filename();

    try {
        std::ofstream file(ExePath / sLogFile, std::ios::trunc);
        if (file.is_open()) file.close();

        logger = std::make_shared<spdlog::logger>("WO3U_Fix", std::make_shared<spdlog::sinks::rotating_file_sink_st>((ExePath / sLogFile).string(), 10 * 1024 * 1024, 1));
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
        spdlog::flush_on(spdlog::level::debug);
        spdlog::set_level(spdlog::level::debug); 
        
        SPDLOG_INFO("Warriors Orochi 3 Ultimate Final AR & FOV Fix Initialized");
    }
    catch (...) {}
}

void Configuration() {
    std::ifstream iniFile(ExePath / sConfigFile);
    if (!iniFile) {
        SPDLOG_WARN("Config file {} not found! Using hardcoded defaults.", (ExePath / sConfigFile).string());
        return;
    }
    
    ini.parse(iniFile);
    ini.strip_trailing_comments();
    
    bEnableARFix = GetConfigAndLog(ini, "AspectRatio", "EnableARFix", true);
    iAspectWidth = GetConfigAndLog(ini, "AspectRatio", "AspectWidth", 4);
    iAspectHeight = GetConfigAndLog(ini, "AspectRatio", "AspectHeight", 3);
    
    if (iAspectHeight > 0) {
        fTargetARValue = static_cast<float>(iAspectWidth) / static_cast<float>(iAspectHeight);
    }
    SPDLOG_INFO("Target Aspect Ratio: {} / {} = {:.6f}", iAspectWidth, iAspectHeight, fTargetARValue);

    bEnableFOVFix = GetConfigAndLog(ini, "FOV", "EnableFOVFix", true);
    fFOVPercent = GetConfigAndLog(ini, "FOV", "FOVPercent", 120.0f);
    
    float fBaseFOV = 0.01745329238f;
    fTargetFOVValue = fBaseFOV * (fFOVPercent / 100.0f);
    SPDLOG_INFO("Target FOV Calculated: {:.6f} ({}%)", fTargetFOVValue, fFOVPercent);
}

void AspectRatio() {
    if (!bEnableARFix) return;

    std::uint8_t* arWriteScan = Memory::PatternScan(exeModule, 
        "F3 0F 10 4C 24 70 F3 0F 11 81 5C 01 00 00 F3 0F 11 89 4C 01 00 00");

    if (arWriteScan) {
        static SafetyHookMid ARWriteHook{};
        std::uint8_t* hookTarget = arWriteScan + 14;

        ARWriteHook = safetyhook::create_mid(hookTarget, [](SafetyHookContext& ctx) {
            if (std::abs(ctx.xmm1.f32[0] - 1.7777778f) < 0.01f) {
                ctx.xmm1.f32[0] = fTargetARValue;
            }
        });
        SPDLOG_INFO("AR Writer Hook injected successfully at 0x{:X}", reinterpret_cast<uintptr_t>(hookTarget));
    } else {
        SPDLOG_ERROR("AR Writer pattern scan failed!");
    }
}

void PatchStaticFOV() {
    if (!bEnableFOVFix) return;

    uintptr_t fovPointer = reinterpret_cast<uintptr_t>(exeModule) + 0xADF63C;
    DWORD oldProtect;

    if (VirtualProtect(reinterpret_cast<LPVOID>(fovPointer), sizeof(float), PAGE_READWRITE, &oldProtect)) {
        std::memcpy(reinterpret_cast<void*>(fovPointer), &fTargetFOVValue, sizeof(float));
        VirtualProtect(reinterpret_cast<LPVOID>(fovPointer), sizeof(float), oldProtect, &oldProtect);
        
        SPDLOG_INFO("Static FOV memory OVERWRITTEN successfully at 0x{:X} to {:.6f}", fovPointer, fTargetFOVValue);
    } else {
        SPDLOG_ERROR("Failed to bypass memory protection for FOV! Error Code: {}", GetLastError());
    }
}

DWORD __stdcall Main(void*) {
    Sleep(500); 
    
    Logging();
    Configuration();
    
    AspectRatio();
    PatchStaticFOV(); 

    return 1;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        thisModule = hModule;
        if (HANDLE mainHandle = CreateThread(NULL, 0, Main, 0, NULL, 0)) {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_HIGHEST);
            CloseHandle(mainHandle);
        }
    }
    return TRUE;
}