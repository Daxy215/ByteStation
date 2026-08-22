#include "R3000Tests.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "CPU.h"

namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr const char* TestsPath = "../../ROMS/r3000/v1";

void resetCpu(CPU& cpu) {
    cpu.branchSlot = false;
    cpu.jumpSlot = false;
    cpu.delaySlot = false;
    cpu.delayJumpSlot = false;
    cpu.currentpc = 0;
    cpu.hi = 0;
    cpu.lo = 0;
    cpu.extraCycles = 0;
    cpu.loads[0] = {};
    cpu.loads[1] = {};
    cpu.paused = false;
    cpu._cop0.reset();

    for (uint32_t& reg : cpu.regs)
        reg = 0;

    cpu.interconnect.flatMemory.clear();
}

bool runTest(CPU& cpu, const json& test, std::string& failure) {
    resetCpu(cpu);

    const json& initial = test["initial"];
    const json& final = test["final"];

    for (uint32_t i = 1; i < 32; i++)
        cpu.regs[i] = initial["R"][i].get<uint32_t>();

    cpu.hi = initial["hi"].get<uint32_t>();
    cpu.lo = initial["lo"].get<uint32_t>();
    cpu.pc = initial["PC"].get<uint32_t>();
    cpu.nextpc = cpu.pc + 4;

    int32_t pendingLoadTarget = initial["delay"]["branch"]["target"].get<int32_t>();
    if (pendingLoadTarget >= 0) {
        cpu.loads[0].index = static_cast<uint32_t>(pendingLoadTarget);
        cpu.loads[0].value = initial["delay"]["branch"]["val"].get<uint32_t>();
    }

    bool inBranchDelaySlot = initial["delay"]["load"]["slot"].get<bool>();
    bool branchDelayTaken = initial["delay"]["load"]["take"].get<bool>();
    if (inBranchDelaySlot && branchDelayTaken)
        cpu.nextpc = initial["delay"]["load"]["target"].get<uint32_t>();

    cpu.branchSlot = inBranchDelaySlot;

    for (const json& cycle : test["cycles"]) {
        uint32_t addr = cycle["addr"].get<uint32_t>();
        uint32_t val = cycle["val"].get<uint32_t>();
        uint32_t sz = cycle["sz"].get<uint32_t>();

        for (uint32_t i = 0; i < sz; i++)
            cpu.interconnect.flatMemory[addr + i] = static_cast<uint8_t>(val >> (8 * i));
    }

    uint32_t opcode = test["opcode"].get<uint32_t>();

    try {
        cpu.currentpc = cpu.pc;
        cpu.delaySlot = cpu.branchSlot;
        cpu.branchSlot = false;
        cpu.delayJumpSlot = cpu.jumpSlot;
        cpu.jumpSlot = false;
        cpu.extraCycles = 0;

        Instruction instruction{opcode};

        cpu.pc = cpu.nextpc;
        cpu.nextpc += 4;

        cpu.decodeAndExecute(instruction);

        if (cpu.loads[0].index != 32)
            cpu.set_reg(cpu.loads[0].index, cpu.loads[0].value);

        cpu.loads[0] = cpu.loads[1];
        cpu.loads[1].index = 32;
    } catch (const std::exception& e) {
        failure = std::string("threw ") + e.what();
        return false;
    }

    for (uint32_t i = 1; i < 32; i++) {
        uint32_t wanted = final["R"][i].get<uint32_t>();
        if (cpu.regs[i] != wanted) {
            failure = "R" + std::to_string(i) + " got " + std::to_string(cpu.regs[i]) + ", wanted " + std::to_string(wanted);
            return false;
        }
    }

    uint32_t wantedHi = final["hi"].get<uint32_t>();
    uint32_t wantedLo = final["lo"].get<uint32_t>();
    uint32_t wantedPc = final["PC"].get<uint32_t>();

    if (cpu.hi != wantedHi) {
        failure = "hi got " + std::to_string(cpu.hi) + ", wanted " + std::to_string(wantedHi);
        return false;
    }

    if (cpu.lo != wantedLo) {
        failure = "lo got " + std::to_string(cpu.lo) + ", wanted " + std::to_string(wantedLo);
        return false;
    }

    if (cpu.pc != wantedPc) {
        failure = "pc got " + std::to_string(cpu.pc) + ", wanted " + std::to_string(wantedPc);
        return false;
    }

    return true;
}

bool runFile(const fs::path& path, int& passed, int& failed, std::vector<std::string>& failures) {
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    json tests;
    try {
        file >> tests;
    } catch (const json::exception& e) {
        failures.push_back(path.filename().string() + ": failed to parse (" + e.what() + ")");
        return false;
    }

    CPU cpu;
    cpu.interconnect.flatMemoryMode = true;

    for (const json& test : tests) {
        std::string failure;
        std::string name = test.value("name", path.filename().string());

        if (runTest(cpu, test, failure)) {
            passed++;
        } else {
            failed++;
            failures.push_back(name + ": " + failure);
        }
    }

    return true;
}
}

bool R3000SingleStepTests::runAll() {
    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    if (!fs::exists(TestsPath)) {
        std::cerr << "R3000 single-step tests: " << TestsPath << " not found, skipping\n";
        return true;
    }

    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(TestsPath))
        if (entry.path().extension() == ".json")
            paths.push_back(entry.path());

    std::sort(paths.begin(), paths.end());

    for (const fs::path& path : paths)
        runFile(path, passed, failed, failures);

    std::cerr << "R3000 single-step tests: " << passed << " passed, " << failed << " failed\n";

    for (size_t i = 0; i < failures.size() && i < 50; i++)
        std::cerr << "  " << failures[i] << '\n';

    return failed == 0;
}
