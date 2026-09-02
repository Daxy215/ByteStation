#include "config.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

Config& Config::Get() {
    static Config instance;
    return instance;
}

std::string Config::GetConfigPath() {
    const char* home = std::getenv("HOME");
    auto base = std::filesystem::path(home ? home : ".");
    return (base / ".ide_config.json").string();
}

void Config::Load() {
    breakpoints.clear();
    bookmarks.clear();
    printDisassemblyCopiesToConsole = false;
    biosPath.clear();
    hasPostProcessing = false;
    hasAudio = false;

    std::ifstream file(GetConfigPath());
    if (!file.is_open()) {
        return;
    }

    json j;
    try {
        file >> j;

        if (j.contains("breakpoints") && j["breakpoints"].is_array())
            breakpoints = j["breakpoints"].get<std::vector<uint32_t>>();

        if (j.contains("bookmarks") && j["bookmarks"].is_array())
            bookmarks = j["bookmarks"].get<std::vector<uint32_t>>();

        printDisassemblyCopiesToConsole = j.value("printDisassemblyCopiesToConsole", false);
        biosPath = j.value("biosPath", std::string());

        if (j.contains("postProcessing") && j["postProcessing"].is_object()) {
            const auto& pp = j["postProcessing"];

            ppUseShaders               = pp.value("useShaders", false);
            ppEnableBloom              = pp.value("enableBloom", false);
            ppThreshold                = pp.value("threshold", 0.0f);
            ppBlurRadius               = pp.value("blurRadius", 0.0f);
            ppBloomPasses              = pp.value("bloomPasses", 0);
            ppBloomIntensity           = pp.value("bloomIntensity", 0.0f);
            ppEnableUpscaling          = pp.value("enableUpscaling", false);
            ppSampleRadius             = pp.value("sampleRadius", 0);
            ppLodBias                  = pp.value("lodBias", 0.0f);
            ppKernelB                  = pp.value("kernelB", 0.0f);
            ppKernelC                  = pp.value("kernelC", 0.0f);
            ppSharpness                = pp.value("sharpness", 0.0f);
            ppEdgeThreshold            = pp.value("edgeThreshold", 0.0f);
            ppEnableAdaptiveSharpening = pp.value("enableAdaptiveSharpening", false);
            ppContrast                 = pp.value("contrast", 0.0f);
            ppSaturation               = pp.value("saturation", 0.0f);
            ppGamma                    = pp.value("gamma", 0.0f);
            ppScanline                 = pp.value("scanline", 0.0f);
            ppHalation                 = pp.value("halation", 0.0f);
            ppDitherStrength           = pp.value("ditherStrength", 0.0f);
            ppNoiseStrength            = pp.value("noiseStrength", 0.0f);

            hasPostProcessing = true;
        }

        if (j.contains("audio") && j["audio"].is_object()) {
            const auto& audio = j["audio"];

            audioEnabled      = audio.value("enabled", true);
            audioMasterVolume = audio.value("masterVolume", 1.0f);

            hasAudio = true;
        }
    } catch (const json::exception&) {
        breakpoints.clear();
        bookmarks.clear();
        printDisassemblyCopiesToConsole = false;
        biosPath.clear();
        hasPostProcessing = false;
        hasAudio = false;
    }
}

void Config::Save() {
    json j;

    j["breakpoints"] = breakpoints;
    j["bookmarks"] = bookmarks;
    j["printDisassemblyCopiesToConsole"] = printDisassemblyCopiesToConsole;
    j["biosPath"] = biosPath;

    if (hasPostProcessing) {
        json pp;

        pp["useShaders"]               = ppUseShaders;
        pp["enableBloom"]              = ppEnableBloom;
        pp["threshold"]                = ppThreshold;
        pp["blurRadius"]               = ppBlurRadius;
        pp["bloomPasses"]              = ppBloomPasses;
        pp["bloomIntensity"]           = ppBloomIntensity;
        pp["enableUpscaling"]          = ppEnableUpscaling;
        pp["sampleRadius"]             = ppSampleRadius;
        pp["lodBias"]                  = ppLodBias;
        pp["kernelB"]                  = ppKernelB;
        pp["kernelC"]                  = ppKernelC;
        pp["sharpness"]                = ppSharpness;
        pp["edgeThreshold"]            = ppEdgeThreshold;
        pp["enableAdaptiveSharpening"] = ppEnableAdaptiveSharpening;
        pp["contrast"]                 = ppContrast;
        pp["saturation"]               = ppSaturation;
        pp["gamma"]                    = ppGamma;
        pp["scanline"]                 = ppScanline;
        pp["halation"]                 = ppHalation;
        pp["ditherStrength"]           = ppDitherStrength;
        pp["noiseStrength"]            = ppNoiseStrength;

        j["postProcessing"] = pp;
    }

    if (hasAudio) {
        json audio;

        audio["enabled"]      = audioEnabled;
        audio["masterVolume"] = audioMasterVolume;

        j["audio"] = audio;
    }

    std::ofstream file(GetConfigPath());
    file << j.dump(4);
}
