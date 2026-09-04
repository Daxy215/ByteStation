#pragma once
#include <cstdint>
#include <string>
#include <vector>

class Config {
    public:
        static Config& Get();
        
        void Load();
        void Save();
        
    private:
        Config() = default;
        [[nodiscard]] static std::string GetConfigPath();
        
    public:
        std::vector<uint32_t> breakpoints;
        std::vector<uint32_t> bookmarks;
        bool printDisassemblyCopiesToConsole = false;
        std::string biosPath;

        bool hasPostProcessing = false;

        bool  ppUseShaders = false;
        bool  ppEnableBloom = false;
        float ppThreshold = 0.0f;
        float ppBlurRadius = 0.0f;
        int   ppBloomPasses = 0;
        float ppBloomIntensity = 0.0f;
        bool  ppEnableUpscaling = false;
        float ppLodBias = 0.0f;
        float ppKernelB = 0.0f;
        float ppKernelC = 0.0f;
        float ppSharpness = 0.0f;
        float ppEdgeThreshold = 0.0f;
        bool  ppEnableAdaptiveSharpening = false;
        bool  ppEnableColorAdjustments = false;
        float ppContrast = 0.0f;
        float ppSaturation = 0.0f;
        float ppGamma = 0.0f;
        bool  ppEnableCrtEffects = false;
        float ppScanline = 0.0f;
        float ppHalation = 0.0f;
        float ppDitherStrength = 0.0f;
        float ppNoiseStrength = 0.0f;

        bool  hasAudio = false;
        bool  audioEnabled = true;
        float audioMasterVolume = 1.0f;
};
