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
};
