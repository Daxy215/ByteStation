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
    } catch (const json::exception&) {
        breakpoints.clear();
        bookmarks.clear();
        printDisassemblyCopiesToConsole = false;
    }
}

void Config::Save() {
    json j;

    j["breakpoints"] = breakpoints;
    j["bookmarks"] = bookmarks;
    j["printDisassemblyCopiesToConsole"] = printDisassemblyCopiesToConsole;

    std::ofstream file(GetConfigPath());
    file << j.dump(4);
}
