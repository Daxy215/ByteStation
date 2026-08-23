#include "CPU.h"
//#include "Instruction.h"

#include <algorithm>
#include <bitset>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <optional>
#include <unordered_set>
#include <vector>

#include "imgui.h"
#include "../Core/config.h"
#include "../Utils/Bitwise.h"

/**
 * Byte - 8 bits or 1 byte
 * HalfWord - 16 bits or 2 bytes
 * Word - 32 bits or 4 bytes.
 */
int CPU::executeNextInstruction() {
    /**
     * First amazing error, here I got "0x1300083c" which means..
     * I was reading the data in big-edian format amazingly..
     * To fix it, I just reversed how I was reading the BIOS.bin file.
     */

    // Save the address of the current instruction to save in
    // 'EPC' in case of an exception.
    currentpc = pc;

    if(currentpc % 4 != 0) {
        _cop0.badVaddr = currentpc;
        exception(LoadAddressError);

        return 1;
    }

    const bool stepping = stepRequested || stepUntilBranchTakenRequested || stepUntilBranchNotTakenRequested;

    if (disasmState.suppress_breakpoint_once && disasmState.suppressed_breakpoint_addr == currentpc) {
        disasmState.suppress_breakpoint_once = false;
    } else if (disasmState.suppress_breakpoint_once) {
        disasmState.suppress_breakpoint_once = false;

        if (!stepping && disasmState.breakpoints.count(currentpc)) {
            paused = true;
            return 0;
        }
    } else if (!stepping && disasmState.breakpoints.count(currentpc)) {
        paused = true;
        return 0;
    }

    // If the last instruction was a branch then we're in the delay slot
    delaySlot = branchSlot;
    branchSlot = false;

    /**
     * I am currently cheating as I've been stuck for a very while,
     * and just comparing my cpu logs to avacods cpu's logs,
     * he updates bit 30 if an instruction actually jumped,
     * to a different pc address though doc says it's an unkown bit?
     * so, no idea what I'm really missing here?
     *
     * 30    -      Not used (zero)=
     */
    delayJumpSlot = jumpSlot;
    jumpSlot = false;

    extraCycles = 0;

    Instruction instruction{fetchInstruction(pc)};

    IRQ::step();

    const bool irqPending =
        ((_cop0.cause & _cop0.sr & 0xFF00) != 0) &&
        ((_cop0.sr & 0x1) != 0);

    const bool delayIrqUntilAfterThisInstruction = irqPending && isCop2Command(instruction.op);

    if (irqPending && !delayIrqUntilAfterThisInstruction) {
        exception(Interrupt);

        currentpc = pc;

        extraCycles = 0;

        instruction = Instruction(fetchInstruction(pc));
    }

    // increment the PC
    pc = nextpc;
    nextpc += 4;

    //if (pc == 0x800419fc || nextpc == 0x800419fc || currentpc == 0x800419fc || this->_cop0.epc == 0x800419fc) {
        //printf("f");
    //}

    uint32_t instrPc = currentpc;

    const int instructionCycles = decodeAndExecute(instruction);
    bool executedBranch = branchSlot;
    bool branchTaken = jumpSlot;
    uint32_t traceTarget = branchTaken ? nextpc : pc;
    const int cycles = instructionCycles + extraCycles;

    recordExecution(instrPc, instruction, executedBranch, branchTaken, traceTarget);

    if (disasmState.print_copied_lines_to_console && disasmState.show) {
        DisassembledInstruction di = disassemble(instruction, instrPc);

        std::stringstream copyLine;
        copyLine << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << instrPc << ": " << di.text;

        std::cout << copyLine.str() << std::endl;
    }

    // Shift load registers
    if(loads[0].index != 32)
        set_reg(loads[0].index, loads[0].value);

    loads[0] = loads[1];
    loads[1].index = 32;

    if (delayIrqUntilAfterThisInstruction) {
        exception(Interrupt);
    }

    checkForTTY();

    return cycles;
}

int CPU::decodeAndExecuteSubFunctions(Instruction& instruction) {
    switch (instruction.subfunc) {
        case 0b000000: return opsll(instruction);
        case 0b000010: return opsrl(instruction);
        case 0b000011: return opsra(instruction);
        case 0b000100: return opsllv(instruction);
        case 0b000110: return opsrlv(instruction);
        case 0b000111: return opsrav(instruction);
        case 0b001000: return opjr(instruction);
        case 0b001001: return opjalr(instruction);
        case 0b001100: return opSyscall(instruction);
        case 0b001101: return opbreak(instruction);
        case 0b010000: return opmfhi(instruction);
        case 0b010001: return opmthi(instruction);
        case 0b010010: return opmflo(instruction);
        case 0b010011: return opmtlo(instruction);
        case 0b011000: return opmult(instruction);
        case 0b011001: return opmultu(instruction);
        case 0b011010: return opdiv(instruction);
        case 0b011011: return opdivu(instruction);
        case 0b100000: return opadd(instruction);
        case 0b100001: return opaddu(instruction);
        case 0b100010: return opsub(instruction);
        case 0b100011: return opsubu(instruction);
        case 0b100100: return opand(instruction);
        case 0b100101: return opor(instruction);
        case 0b100110: return opxor(instruction);
        case 0b100111: return opnor(instruction);
        case 0b101010: return opslt(instruction);
        case 0b101011: return opsltu(instruction);
    }

    opillegal(instruction);
    printf("Unhandled sub instruction %0x8. Function call was: %x\n", instruction.op, instruction.subfunc);

    return 0;
}

/*int CPU::decodeAndExecuteSubFunctions(Instruction& instruction) {
    using OpHandler = int (CPU::*)(Instruction&);
    static const std::unordered_map<uint32_t, OpHandler> table = {
        {0b000000, &CPU::opsll},
        {0b100101, &CPU::opor},
        {0b000111, &CPU::opsrav},
        {0b100111, &CPU::opnor},
        {0b101011, &CPU::opsltu},
        {0b100001, &CPU::opaddu},
        {0b001000, &CPU::opjr},
        {0b100100, &CPU::opand},
        {0b100000, &CPU::opadd},
        {0b001001, &CPU::opjalr},
        {0b100011, &CPU::opsubu},
        {0b000011, &CPU::opsra},
        {0b011010, &CPU::opdiv},
        {0b010010, &CPU::opmflo},
        {0b010000, &CPU::opmfhi},
        {0b000010, &CPU::opsrl},
        {0b011011, &CPU::opdivu},
        {0b101010, &CPU::opslt},
        {0b001100, &CPU::opSyscall},
        {0b001101, &CPU::opbreak},
        {0b010011, &CPU::opmtlo},
        {0b010001, &CPU::opmthi},
        {0b000100, &CPU::opsllv},
        {0b100110, &CPU::opxor},
        {0b011001, &CPU::opmultu},
        {0b000110, &CPU::opsrlv},
        {0b011000, &CPU::opmult},
        {0b100010, &CPU::opsub}
    };

    auto it = table.find(instruction.subfunc);
    if (it != table.end())
        return (this->*it->second)(instruction);

    opillegal(instruction);
    printf("Unhandled sub instruction %0x8. Function call was: %x\n", instruction.op, instruction.subfunc);
    std::cerr << "";

    return 0;
}*/

uint32_t CPU::fetchInstruction(uint32_t addr) {
    uint32_t value = interconnect.loadInstruction(addr);

    if (interconnect.lastICacheMiss)
        extraCycles += memoryAccessCycles(addr, 4, false);

    return value;
}

int CPU::memoryAccessCycles(uint32_t addr, uint8_t size, bool write) const {
    (void)size;
    (void)write;

    uint32_t absAddr = map::maskRegion(addr);
    uint32_t offset = 0;

    if (map::SCRATCHPAD.contains(absAddr, offset))
        return 0;

    if (map::RAM.contains(absAddr, offset))
        return 3;

    if (map::BIOS.contains(absAddr, offset))
        return 5;

    if (map::IRQ_CONTROL.contains(absAddr, offset))
        return 2;

    if (map::DMA.contains(absAddr, offset))
        return 2;

    if (map::GPU.contains(absAddr, offset))
        return 2;

    // Making ps1-test/timer be 2 cycles too long,
    // making this return 0 seems to fix it
    // Ok apparently duckstation gets 1013 and not 1011
    if (map::TIMERS.contains(absAddr, offset))
        return 2;

    if (map::CDROM.contains(absAddr, offset))
        return 2;

    if (map::MDEC.contains(absAddr, offset))
        return 2;

    if (map::SPU.contains(absAddr, offset))
        return 2;

    if (map::PADMEMCARD.contains(absAddr, offset))
        return 2;

    if (map::MEMCONTROL.contains(absAddr, offset))
        return 2;

    if (map::CACHECONTROL.contains(absAddr, offset))
        return 2;

    if (map::EXPANSION1.contains(absAddr, offset))
        return 8;

    if (map::EXPANSION2.contains(absAddr, offset))
        return 2;

    return 1;
}

static const char* hangRegName(uint32_t reg) {
    static const char* names[32] = {
        "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
        "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"
    };

    return names[reg & 31];
}

static std::string hangHex(uint32_t value) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
    return ss.str();
}

void CPU::recordExecution(uint32_t instrPc, Instruction& instruction, bool branch, bool taken, uint32_t target) {
    if (!disasmState.show && !disasmState.print_copied_lines_to_console) {
        return;
    }

    disasmState.totalCycles++;
    disasmState.executionHits[instrPc]++;

    if (branch)
        disasmState.edgeHits[makeEdge(instrPc, target)]++;

    if (disasmState.totalCycles % DisassemblerState::TraceSampleInterval != 0 && !branch)
        return;

    ExecutionTraceEntry entry;
    entry.pc = instrPc;
    entry.opcode = instruction.op;
    entry.target = target;
    entry.cycle = disasmState.totalCycles;
    entry.branch = branch;
    entry.taken = taken;

    disasmState.executionTrace.push_back(entry);

    if (disasmState.executionTrace.size() > DisassemblerState::MaxTrace)
        disasmState.executionTrace.pop_front();
}

void CPU::recordMemoryAccess(uint32_t addr, uint32_t value, uint8_t size, bool write) {
    if (!disasmState.show && !disasmState.print_copied_lines_to_console) {
        return;
    }

    MemoryTraceEntry entry;
    entry.pc = currentpc;
    entry.addr = addr;
    entry.value = value;
    entry.size = size;
    entry.cycle = disasmState.totalCycles;
    entry.write = write;

    disasmState.memoryTrace.push_back(entry);

    if (disasmState.memoryTrace.size() > DisassemblerState::MaxMemoryTrace)
        disasmState.memoryTrace.pop_front();
}

static uint32_t disasmPeekInstruction(Interconnect& interconnect, uint32_t addr) {
    uint32_t absAddr = map::maskRegion(addr);
    uint32_t offset = 0;

    if (map::RAM.contains(absAddr, offset))
        return interconnect._ram.load<uint32_t>(offset);

    if (map::BIOS.contains(absAddr, offset))
        return interconnect._bios.load<uint32_t>(offset);

    if (map::SCRATCHPAD.contains(absAddr, offset))
        return interconnect._scratchPad.load<uint32_t>(offset);

    return 0;
}

static std::vector<uint32_t> sortedAddresses(const std::unordered_set<uint32_t>& addresses) {
    std::vector<uint32_t> result(addresses.begin(), addresses.end());
    std::sort(result.begin(), result.end());
    return result;
}

static void loadDisassemblerConfig(DisassemblerState& state) {
    auto& config = Config::Get();
    config.Load();

    state.breakpoints.insert(config.breakpoints.begin(), config.breakpoints.end());
    state.bookmarks.insert(config.bookmarks.begin(), config.bookmarks.end());
    //state.print_copied_lines_to_console = config.printDisassemblyCopiesToConsole;
    state.config_loaded = true;
}

static void saveDisassemblerConfig(const DisassemblerState& state) {
    auto& config = Config::Get();
    config.breakpoints = sortedAddresses(state.breakpoints);
    config.bookmarks = sortedAddresses(state.bookmarks);
    config.printDisassemblyCopiesToConsole = state.print_copied_lines_to_console;
    config.Save();
}

void CPU::showDisassembler() {
    if (!disasmState.config_loaded)
        loadDisassemblerConfig(disasmState);

    if (!disasmState.show)
        return;

    ImGui::Begin("Disassembler", &disasmState.show);
    auto executionHitsFor = [this](uint32_t addr) -> uint64_t {
        auto it = disasmState.executionHits.find(addr);
        return it != disasmState.executionHits.end() ? it->second : 0;
    };

    if (ImGui::Button(paused ? "Continue (F5)" : "Pause (F5)")) {
        if (paused) {
            disasmState.suppress_breakpoint_once = true;
            disasmState.suppressed_breakpoint_addr = pc;
            paused = false;
        } else {
            paused = true;
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Step (F10)")) {
        paused        = true;
        stepRequested = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("Step Until Branch Taken")) {
        paused                        = true;
        stepUntilBranchTakenRequested = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("Step Until Branch Not Taken")) {
        paused                           = true;
        stepUntilBranchNotTakenRequested = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("Skip Next Instruction"))
        nextpc += 4;

    ImGui::SameLine();
    ImGui::Checkbox("Follow PC", &disasmState.follow_pc);

    ImGui::SameLine();
    ImGui::Checkbox("Show Addresses", &disasmState.show_address);

    ImGui::SameLine();
    if (ImGui::Checkbox("Print Copies", &disasmState.print_copied_lines_to_console))
        saveDisassemblerConfig(disasmState);

    ImGui::Separator();

    auto &h = disasmState.hang;

    if (h.detected) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(40, 10, 10, 255));

        ImGui::BeginChild("##hang_analysis", ImVec2(0, 220), true);

        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "HANG DETECTED");

        ImGui::Separator();

        ImGui::Text("Reason:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 1, 0.4f, 1), "%s", h.reason.c_str());

            ImGui::Text("Details:");
            ImGui::TextWrapped("%s", h.details.c_str());

            if (!h.condition.empty()) {
                ImGui::Text("Condition:");
                ImGui::TextWrapped("%s", h.condition.c_str());
            }

            if (!h.waitingOn.empty()) {
                ImGui::Text("Waiting On:");
                ImGui::TextWrapped("%s", h.waitingOn.c_str());
            }

            if (!h.addressDetails.empty()) {
                ImGui::Text("Address Details:");
                ImGui::TextWrapped("%s", h.addressDetails.c_str());
            }

            ImGui::Text("Hotspot:");
            ImGui::SameLine();
            ImGui::Text("0x%08X", h.hotspot);

            if (h.hasEffectiveAddress) {
                ImGui::Text("Address:");
                ImGui::SameLine();
                ImGui::Text("0x%08X (%s)", h.effectiveAddress/*, describeAddress(h.effectiveAddress).c_str()*/);
            }

        if (h.repeatedTarget) {
            ImGui::Text("Loop Target:");
            ImGui::SameLine();
            ImGui::Text("0x%08X", h.repeatedTarget);
        }

        ImGui::Text("Repeat Count:");
        ImGui::SameLine();
        ImGui::Text("%llu", h.repeatCount);

        if (ImGui::Button("Jump To Hang")) {
            disasmState.view_center = h.hotspot;
            disasmState.follow_pc   = false;
            disasmState.extraLinesAbove = 0;
            disasmState.extraLinesBelow = 0;
        }

        ImGui::SameLine();

        if (ImGui::Button("Break Here")) {
            paused = true;
            pc     = h.hotspot;
        }

        ImGui::EndChild();

        ImGui::PopStyleColor();
    }

    ImGui::InputText("Go to (hex)", disasmState.addrBuf, sizeof(disasmState.addrBuf),
                     ImGuiInputTextFlags_CharsHexadecimal);

    ImGui::SameLine();

    if (ImGui::Button("Go")) {
        uint32_t addr           = strtoul(disasmState.addrBuf, nullptr, 16);
        disasmState.view_center = addr;
        disasmState.follow_pc   = false;
        disasmState.extraLinesAbove = 0;
        disasmState.extraLinesBelow = 0;
    }

    ImGui::InputText("Filter (mnemonic/op)", disasmState.filterBuf, sizeof(disasmState.filterBuf));

    ImGui::SameLine();

    if (ImGui::Button("Clear"))
        disasmState.filterBuf[0] = '\0';

    ImGui::Separator();

    if (disasmState.follow_pc || pc != disasmState.prev_pc) {
        disasmState.view_center = pc;
        disasmState.prev_pc     = pc;
        disasmState.extraLinesAbove = 0;
        disasmState.extraLinesBelow = 0;
    }

    ImGui::BeginChild("##disasm_scroll", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

    if (paused) {
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        const float scrollY    = ImGui::GetScrollY();
        const float scrollMaxY = ImGui::GetScrollMaxY();
        const float edge       = lineHeight * 2.0f;

        if (scrollMaxY > 0.0f && scrollY <= edge) {
            disasmState.extraLinesAbove += disasmState.contextLines;
            disasmState.follow_pc        = false;
            ImGui::SetScrollY(scrollY + disasmState.contextLines * lineHeight);
        } else if (scrollMaxY > 0.0f && scrollY >= scrollMaxY - edge) {
            disasmState.extraLinesBelow += disasmState.contextLines;
            disasmState.follow_pc        = false;
        }
    }

    const int linesAbove = disasmState.contextLines + disasmState.extraLinesAbove;
    const int linesBelow = disasmState.contextLines * 2 + disasmState.extraLinesBelow;

    const uint32_t startAddr = disasmState.view_center - linesAbove * 4;

    const uint32_t totalLines = linesAbove + linesBelow + 1;
    std::string filter;

    if (disasmState.filterBuf[0]) {
        filter = disasmState.filterBuf;
        std::transform(filter.begin(), filter.end(), filter.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    ImGuiListClipper clipper;

    clipper.Begin(totalLines, ImGui::GetTextLineHeightWithSpacing());

    while (clipper.Step()) {
        for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; line++) {

            uint32_t addr = startAddr + line * 4;

            uint32_t op = disasmPeekInstruction(interconnect, addr);
            Instruction inst(op);
            DisassembledInstruction di = disassemble(inst, addr);

            if (!filter.empty()) {
                std::string txt = di.text;

                std::transform(txt.begin(), txt.end(), txt.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                if (txt.find(filter) == std::string::npos)
                    continue;
            }

            bool isPC      = addr == pc;
            bool isHotspot = h.detected && addr == h.hotspot;

            ImGui::PushID(static_cast<int>(addr));

            if (isHotspot)
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 80, 80, 255));
            else if (isPC)
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 200, 80, 255));

            uint64_t hits = executionHitsFor(addr);

            if (hits > 5000)
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "██");
            else if (hits > 1000)
                ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "▓▓");
            else if (hits > 100)
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "▒▒");
            else
                ImGui::Text("  ");

            ImGui::SameLine();

            if (disasmState.breakpoints.count(addr)) {
                ImGui::Text("●");
                ImGui::SameLine();
            } else {
                ImGui::Text(" ");
                ImGui::SameLine();
            }

            if (disasmState.bookmarks.count(addr)) {
                ImGui::Text("*");
                ImGui::SameLine();
            } else {
                ImGui::Text(" ");
                ImGui::SameLine();
            }

            if (disasmState.show_address) {
                ImGui::Text("%08X:", addr);
                ImGui::SameLine();
            }

            if (di.is_branch)
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 220, 120, 255));

            ImGui::Text("%s", di.text.c_str());
            const bool lineClicked = ImGui::IsItemClicked();

            if (di.is_branch)
                ImGui::PopStyleColor();

            std::stringstream copyLine;
            copyLine << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << addr << ": " << di.text;

            if (lineClicked) {
                ImGui::SetClipboardText(copyLine.str().c_str());
            }

            if (ImGui::BeginPopupContextItem("##disasm_line_context")) {
                const bool hasBreakpoint = disasmState.breakpoints.count(addr) != 0;
                const bool hasBookmark = disasmState.bookmarks.count(addr) != 0;

                if (ImGui::MenuItem(hasBreakpoint ? "Remove Breakpoint" : "Add Breakpoint")) {
                    if (hasBreakpoint)
                        disasmState.breakpoints.erase(addr);
                    else
                        disasmState.breakpoints.insert(addr);

                    saveDisassemblerConfig(disasmState);
                }

                if (ImGui::MenuItem(hasBookmark ? "Remove Bookmark" : "Add Bookmark")) {
                    if (hasBookmark)
                        disasmState.bookmarks.erase(addr);
                    else
                        disasmState.bookmarks.insert(addr);

                    saveDisassemblerConfig(disasmState);
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Copy Line")) {
                    ImGui::SetClipboardText(copyLine.str().c_str());

                    if (disasmState.print_copied_lines_to_console)
                        std::cout << copyLine.str() << std::endl;
                }

                ImGui::EndPopup();
            }

            if (hits > 100) {
                ImGui::SameLine();

                ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "[%llu]", hits);
            }

            if (isHotspot) {
                ImGui::SameLine();

                ImGui::TextColored(ImVec4(1, 0.2f, 0.2f, 1), "<HANG>");
            }

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();

                ImGui::Text("Opcode: 0x%08X", di.opcode);

                ImGui::Text("Execution Hits: %llu", hits);

                auto edge = makeEdge(addr, di.branch_taken ? di.target : addr + 4);

                auto eit = disasmState.edgeHits.find(edge);

                if (eit != disasmState.edgeHits.end()) {
                    ImGui::Text("Edge Hits: %llu", eit->second);
                }

                if (isHotspot) {
                    ImGui::Separator();

                    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Likely Hang Location");

                    ImGui::TextWrapped("%s", h.details.c_str());
                }

                ImGui::EndTooltip();
            }

            if (isHotspot || isPC)
                ImGui::PopStyleColor();

            ImGui::PopID();
        }
    }

    clipper.End();

    ImGui::EndChild();

    if (!disasmState.executionTrace.empty()) {
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Recent Execution Trace", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("##trace_table", 5,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
                                  ImVec2(0, 240))) {
                ImGui::TableSetupColumn("Cycle");
                ImGui::TableSetupColumn("PC");
                ImGui::TableSetupColumn("Next");
                ImGui::TableSetupColumn("Hits");
                ImGui::TableSetupColumn("Type");

                ImGui::TableHeadersRow();

                int shown = 0;

                for (auto it = disasmState.executionTrace.rbegin(); it != disasmState.executionTrace.rend(); ++it) {

                    if (shown++ > 128)
                        break;

                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%llu", it->cycle);

                    ImGui::TableSetColumnIndex(1);

                    if (h.detected && it->pc == h.hotspot)
                        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%08X", it->pc);
                    else
                        ImGui::Text("%08X", it->pc);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%08X", it->target);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%llu", executionHitsFor(it->pc));

                    ImGui::TableSetColumnIndex(4);

                    if (it->branch)
                        ImGui::TextColored(ImVec4(1, 1, 0.3f, 1), "BRANCH");
                    else
                        ImGui::Text("NORMAL");
                }

                ImGui::EndTable();
            }
        }
    }

    ImGui::End();
}

static std::string disasmHex(uint32_t value, int width = 8) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(width) << std::setfill('0') << value;
    return ss.str();
}

static std::string disasmImm(int32_t value) {
    std::stringstream ss;

    if (value < 0) {
        int64_t wide = value;
        ss << "-0x" << std::hex << std::uppercase << -wide;
    } else {
        ss << "0x" << std::hex << std::uppercase << value;
    }

    return ss.str();
}

static const char* disasmRegName(uint32_t reg) {
    static const char* names[32] = {
        "zero(0)", "at(1)", "v0(2)", "v1(3)", "a0(4)", "a1(5)", "a2(6)", "a3(7)",
        "t0(8)", "t1(9)", "t2(10)", "t3(11)", "t4(12)", "t5(13)", "t6(14)", "t7(15)",
        "s0(16)", "s1(17)", "s2(18)", "s3(19)", "s4(20)", "s5(21)", "s6(22)", "s7(23)",
        "t8(24)", "t9(25)", "k0(26)", "k1(27)", "gp(28)", "sp(29)", "fp(30)", "ra(31)"
    };

    return names[reg & 31];
}

static const char* disasmCop0RegName(uint32_t reg) {
    static const char* names[32] = {
        "Index", "Random", "EntryLo0", "BPC", "Context", "BDA", "JUMPDEST", "DCIC",
        "BadVaddr", "BDAM", "EntryHi", "BPCM", "SR", "CAUSE", "EPC", "PRID",
        "COP0_16", "COP0_17", "COP0_18", "COP0_19", "COP0_20", "COP0_21", "COP0_22", "COP0_23",
        "COP0_24", "COP0_25", "COP0_26", "COP0_27", "COP0_28", "COP0_29", "COP0_30", "COP0_31"
    };

    return names[reg & 31];
}

static const char* disasmGteCommandName(uint32_t cmd) {
    switch (cmd) {
        case 0x01: return "RTPS";
        case 0x06: return "NCLIP";
        case 0x0c: return "OP";
        case 0x10: return "DPCS";
        case 0x11: return "INTPL";
        case 0x12: return "MVMVA";
        case 0x13: return "NCDS";
        case 0x14: return "CDP";
        case 0x16: return "NCDT";
        case 0x1b: return "NCCS";
        case 0x1c: return "CC";
        case 0x1e: return "NCS";
        case 0x20: return "NCT";
        case 0x28: return "SQR";
        case 0x29: return "DCPL";
        case 0x2a: return "DPCT";
        case 0x2d: return "AVSZ3";
        case 0x2e: return "AVSZ4";
        case 0x30: return "RTPT";
        case 0x3d: return "GPF";
        case 0x3e: return "GPL";
        case 0x3f: return "NCCT";
        default: return "GTE";
    }
}

static std::string disasmAddressRegion(uint32_t addr) {
    uint32_t absAddr = map::maskRegion(addr);
    uint32_t offset = 0;

    if (map::RAM.contains(absAddr, offset))
        return "ram+" + disasmHex(offset, 6);
    if (map::SCRATCHPAD.contains(absAddr, offset))
        return "scratchpad+" + disasmHex(offset, 3);
    if (map::BIOS.contains(absAddr, offset))
        return "bios+" + disasmHex(offset, 5);
    if (map::GPU.contains(absAddr, offset))
        return "gpu+" + disasmHex(offset, 1);
    if (map::DMA.contains(absAddr, offset))
        return "dma+" + disasmHex(offset, 2);
    if (map::SPU.contains(absAddr, offset))
        return "spu+" + disasmHex(offset, 3);
    if (map::CDROM.contains(absAddr, offset))
        return "cdrom+" + disasmHex(offset, 1);
    if (map::MDEC.contains(absAddr, offset))
        return "mdec+" + disasmHex(offset, 1);
    if (map::TIMERS.contains(absAddr, offset))
        return "timers+" + disasmHex(offset, 2);
    if (map::IRQ_CONTROL.contains(absAddr, offset))
        return "irq+" + disasmHex(offset, 1);
    if (map::PADMEMCARD.contains(absAddr, offset))
        return "sio+" + disasmHex(offset, 2);
    if (map::MEMCONTROL.contains(absAddr, offset))
        return "memctrl+" + disasmHex(offset, 2);
    if (map::CACHECONTROL.contains(absAddr, offset))
        return "cachectrl+" + disasmHex(offset, 1);
    if (map::EXPANSION1.contains(absAddr, offset))
        return "exp1+" + disasmHex(offset, 5);
    if (map::EXPANSION2.contains(absAddr, offset))
        return "exp2+" + disasmHex(offset, 3);
    if (map::PCSX_REDUX_EXPANSION.contains(absAddr, offset))
        return "pcsx+" + disasmHex(offset, 1);
    if (map::RAM_SIZE.contains(absAddr, offset))
        return "ramsize+" + disasmHex(offset, 1);

    return "unknown";
}

DisassembledInstruction CPU::disassemble(Instruction& inst, uint32_t address) {
    DisassembledInstruction result;

    uint32_t rs = inst.rs;
    uint32_t rt = inst.rt;
    uint32_t rd = inst.rd;
    int32_t simm = inst.imm_se;
    uint32_t uimm = inst.imm;
    uint32_t jump_target = inst.jump << 2;
    uint32_t branch_target = address + 4 + static_cast<uint32_t>(simm * 4);

    result.opcode = inst.op;

    std::stringstream ss;

    bool hasDetail = false;

    auto appendDetail = [&](const std::string& detail) {
        if (detail.empty())
            return;

        if (!hasDetail) {
            ss << " ; ";
            hasDetail = true;
        } else {
            ss << " ";
        }

        ss << detail;
    };

    auto regDetail = [&](uint32_t reg) {
        return std::string(disasmRegName(reg)) + "=" + disasmHex(regs[reg & 31]);
    };

    auto cop0Value = [&](uint32_t reg) -> uint32_t {
        switch (reg & 31) {
            case 3: return _cop0.bpc;
            case 5: return _cop0.bda;
            case 7: return _cop0.dcic;
            case 8: return _cop0.badVaddr;
            case 9: return _cop0.bdam;
            case 11: return _cop0.bpcm;
            case 12: return _cop0.sr;
            case 13: return _cop0.cause;
            case 14: return _cop0.epc;
            case 15: return 0x00000001;
            default: return 0;
        }
    };

    auto latestMemory = [&](bool write, uint8_t size) -> std::optional<MemoryTraceEntry> {
        for (auto it = disasmState.memoryTrace.rbegin(); it != disasmState.memoryTrace.rend(); ++it) {
            if (it->pc == address && it->write == write && it->size == size)
                return *it;
        }

        return std::nullopt;
    };

    auto appendBranchDetails = [&](bool taken) {
        appendDetail(regDetail(rs));
        appendDetail(regDetail(rt));
        appendDetail(std::string("isTaken=") + (taken ? "true" : "false"));
    };

    auto appendSingleBranchDetails = [&](bool taken) {
        appendDetail(regDetail(rs));
        appendDetail(std::string("isTaken=") + (taken ? "true" : "false"));
    };

    auto memoryOperand = [&](uint32_t base, int32_t imm) {
        std::stringstream out;
        out << disasmImm(imm) << "(" << disasmRegName(base) << ")";
        return out.str();
    };

    auto appendLoadDetails = [&](uint8_t size, uint32_t effectiveAddr, uint32_t targetReg) {
        result.memory_access = true;
        result.reads_memory = true;

        appendDetail(regDetail(rs));

        std::optional<MemoryTraceEntry> mem = latestMemory(false, size);
        uint32_t shownAddr = mem ? mem->addr : effectiveAddr;
        std::string addrText = "addr=" + disasmAddressRegion(shownAddr) + "[" + disasmHex(shownAddr) + "]";

        if (mem)
            addrText += "(" + disasmHex(mem->value) + ")";

        appendDetail(addrText);

        if (mem)
            appendDetail(std::string(disasmRegName(targetReg)) + "=" + disasmHex(mem->value));
    };

    auto appendStoreDetails = [&](uint8_t size, uint32_t effectiveAddr, uint32_t value) {
        result.memory_access = true;
        result.writes_memory = true;

        appendDetail(regDetail(rs));
        appendDetail(regDetail(rt));

        std::optional<MemoryTraceEntry> mem = latestMemory(true, size);
        uint32_t shownAddr = mem ? mem->addr : effectiveAddr;
        uint32_t shownValue = mem ? mem->value : value;
        appendDetail("addr=" + disasmAddressRegion(shownAddr) + "[" + disasmHex(shownAddr) + "]");
        appendDetail("stored=" + disasmHex(shownValue));
    };

    auto appendCopStoreDetails = [&](uint8_t size, uint32_t effectiveAddr, uint32_t value) {
        result.memory_access = true;
        result.writes_memory = true;

        appendDetail(regDetail(rs));

        std::optional<MemoryTraceEntry> mem = latestMemory(true, size);
        uint32_t shownAddr = mem ? mem->addr : effectiveAddr;
        uint32_t shownValue = mem ? mem->value : value;
        appendDetail("addr=" + disasmAddressRegion(shownAddr) + "[" + disasmHex(shownAddr) + "]");
        appendDetail("stored=" + disasmHex(shownValue));
    };

    auto unsupported = [&](const char* group, uint32_t value) {
        ss << group << "_" << std::hex << std::uppercase << value;
    };

    switch (inst.func) {
        case 0x00: {
            if (inst.op == 0) {
                ss << "NOP";
                break;
            }

            switch (inst.subfunc) {
                case 0x00:
                    ss << "SLL    " << disasmRegName(rd) << ", " << disasmRegName(rt) << ", " << inst.shamt;
                    appendDetail(regDetail(rt));
                    appendDetail(regDetail(rd));
                    break;
                case 0x02:
                    ss << "SRL    " << disasmRegName(rd) << ", " << disasmRegName(rt) << ", " << inst.shamt;
                    appendDetail(regDetail(rt));
                    appendDetail(regDetail(rd));
                    break;
                case 0x03:
                    ss << "SRA    " << disasmRegName(rd) << ", " << disasmRegName(rt) << ", " << inst.shamt;
                    appendDetail(regDetail(rt));
                    appendDetail(regDetail(rd));
                    break;
                case 0x04:
                    ss << "SLLV   " << disasmRegName(rd) << ", " << disasmRegName(rt) << ", " << disasmRegName(rs);
                    appendDetail(regDetail(rt));
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rd));
                    break;
                case 0x06:
                    ss << "SRLV   " << disasmRegName(rd) << ", " << disasmRegName(rt) << ", " << disasmRegName(rs);
                    appendDetail(regDetail(rt));
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rd));
                    break;
                case 0x07:
                    ss << "SRAV   " << disasmRegName(rd) << ", " << disasmRegName(rt) << ", " << disasmRegName(rs);
                    appendDetail(regDetail(rt));
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rd));
                    break;
                case 0x08:
                    result.target = regs[rs];
                    result.is_branch = true;
                    result.branch_taken = true;
                    ss << "JR     " << disasmRegName(rs);
                    appendDetail(regDetail(rs));
                    appendDetail("target=" + disasmHex(result.target));
                    break;
                case 0x09:
                    result.target = regs[rs];
                    result.is_branch = true;
                    result.branch_taken = true;
                    ss << "JALR   " << disasmRegName(rd) << ", " << disasmRegName(rs);
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rd));
                    appendDetail("target=" + disasmHex(result.target));
                    break;
                case 0x0c:
                    ss << "SYSCALL";
                    appendDetail("code=" + disasmHex((inst.op >> 6) & 0xfffff, 5));
                    break;
                case 0x0d:
                    ss << "BREAK";
                    appendDetail("code=" + disasmHex((inst.op >> 6) & 0xfffff, 5));
                    break;
                case 0x10:
                    ss << "MFHI   " << disasmRegName(rd);
                    appendDetail("hi=" + disasmHex(hi));
                    appendDetail(regDetail(rd));
                    break;
                case 0x11:
                    ss << "MTHI   " << disasmRegName(rs);
                    appendDetail(regDetail(rs));
                    appendDetail("hi=" + disasmHex(hi));
                    break;
                case 0x12:
                    ss << "MFLO   " << disasmRegName(rd);
                    appendDetail("lo=" + disasmHex(lo));
                    appendDetail(regDetail(rd));
                    break;
                case 0x13:
                    ss << "MTLO   " << disasmRegName(rs);
                    appendDetail(regDetail(rs));
                    appendDetail("lo=" + disasmHex(lo));
                    break;
                case 0x18:
                    ss << "MULT   " << disasmRegName(rs) << ", " << disasmRegName(rt);
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rt));
                    appendDetail("hi=" + disasmHex(hi));
                    appendDetail("lo=" + disasmHex(lo));
                    break;
                case 0x19:
                    ss << "MULTU  " << disasmRegName(rs) << ", " << disasmRegName(rt);
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rt));
                    appendDetail("hi=" + disasmHex(hi));
                    appendDetail("lo=" + disasmHex(lo));
                    break;
                case 0x1a:
                    ss << "DIV    " << disasmRegName(rs) << ", " << disasmRegName(rt);
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rt));
                    appendDetail("hi=" + disasmHex(hi));
                    appendDetail("lo=" + disasmHex(lo));
                    break;
                case 0x1b:
                    ss << "DIVU   " << disasmRegName(rs) << ", " << disasmRegName(rt);
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rt));
                    appendDetail("hi=" + disasmHex(hi));
                    appendDetail("lo=" + disasmHex(lo));
                    break;
                case 0x20:
                    ss << "ADD    " << disasmRegName(rd) << ", " << disasmRegName(rs) << ", " << disasmRegName(rt);
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rt));
                    appendDetail(regDetail(rd));
                    break;
                case 0x21:
                    ss << "ADDU   " << disasmRegName(rd) << ", " << disasmRegName(rs) << ", " << disasmRegName(rt);
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rt));
                    appendDetail(regDetail(rd));
                    break;
                case 0x22:
                    ss << "SUB    " << disasmRegName(rd) << ", " << disasmRegName(rs) << ", " << disasmRegName(rt);
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rt));
                    appendDetail(regDetail(rd));
                    break;
                case 0x23:
                    ss << "SUBU   " << disasmRegName(rd) << ", " << disasmRegName(rs) << ", " << disasmRegName(rt);
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rt));
                    appendDetail(regDetail(rd));
                    break;
                case 0x24:
                    ss << "AND    " << disasmRegName(rd) << ", " << disasmRegName(rs) << ", " << disasmRegName(rt);
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rt));
                    appendDetail(regDetail(rd));
                    break;
                case 0x25:
                    ss << "OR     " << disasmRegName(rd) << ", " << disasmRegName(rs) << ", " << disasmRegName(rt);
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rt));
                    appendDetail(regDetail(rd));
                    break;
                case 0x26:
                    ss << "XOR    " << disasmRegName(rd) << ", " << disasmRegName(rs) << ", " << disasmRegName(rt);
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rt));
                    appendDetail(regDetail(rd));
                    break;
                case 0x27:
                    ss << "NOR    " << disasmRegName(rd) << ", " << disasmRegName(rs) << ", " << disasmRegName(rt);
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rt));
                    appendDetail(regDetail(rd));
                    break;
                case 0x2a:
                    ss << "SLT    " << disasmRegName(rd) << ", " << disasmRegName(rs) << ", " << disasmRegName(rt);
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rt));
                    appendDetail(regDetail(rd));
                    break;
                case 0x2b:
                    ss << "SLTU   " << disasmRegName(rd) << ", " << disasmRegName(rs) << ", " << disasmRegName(rt);
                    appendDetail(regDetail(rs));
                    appendDetail(regDetail(rt));
                    appendDetail(regDetail(rd));
                    break;
                default:
                    unsupported("SPECIAL", inst.subfunc);
                    break;
            }

            break;
        }

        case 0x01: {
            bool validRegimm = rt == 0x00 || rt == 0x01 || rt == 0x10 || rt == 0x11;
            bool isBgez = rt == 0x01 || rt == 0x11;
            bool isLink = rt == 0x10 || rt == 0x11;
            bool taken = isBgez ? static_cast<int32_t>(regs[rs]) >= 0 : static_cast<int32_t>(regs[rs]) < 0;

            if (!validRegimm) {
                unsupported("REGIMM", rt);
                break;
            }

            result.target = branch_target;
            result.is_branch = true;
            result.branch_taken = taken;

            switch (rt) {
                case 0x00: ss << "BLTZ   "; break;
                case 0x01: ss << "BGEZ   "; break;
                case 0x10: ss << "BLTZAL "; break;
                case 0x11: ss << "BGEZAL "; break;
            }

            ss << disasmRegName(rs) << ", " << getFunctionLabel(result.target);
            appendSingleBranchDetails(taken);

            if (isLink)
                appendDetail(regDetail(31));

            break;
        }

        case 0x02:
            result.target = ((address + 4) & 0xf0000000) | jump_target;
            result.is_branch = true;
            result.branch_taken = true;
            ss << "J      " << getFunctionLabel(result.target);
            appendDetail("target=" + disasmHex(result.target));
            break;

        case 0x03:
            result.target = ((address + 4) & 0xf0000000) | jump_target;
            result.is_branch = true;
            result.branch_taken = true;
            ss << "JAL    " << getFunctionLabel(result.target);
            appendDetail(regDetail(31));
            appendDetail("target=" + disasmHex(result.target));
            break;

        case 0x04:
            result.target = branch_target;
            result.is_branch = true;
            result.branch_taken = regs[rs] == regs[rt];
            ss << "BEQ    " << disasmRegName(rs) << ", " << disasmRegName(rt) << ", " << getFunctionLabel(result.target);
            appendBranchDetails(result.branch_taken);
            break;

        case 0x05:
            result.target = branch_target;
            result.is_branch = true;
            result.branch_taken = regs[rs] != regs[rt];
            ss << "BNE    " << disasmRegName(rs) << ", " << disasmRegName(rt) << ", " << getFunctionLabel(result.target);
            appendBranchDetails(result.branch_taken);
            break;

        case 0x06:
            result.target = branch_target;
            result.is_branch = true;
            result.branch_taken = static_cast<int32_t>(regs[rs]) <= 0;
            ss << "BLEZ   " << disasmRegName(rs) << ", " << getFunctionLabel(result.target);
            appendSingleBranchDetails(result.branch_taken);
            break;

        case 0x07:
            result.target = branch_target;
            result.is_branch = true;
            result.branch_taken = static_cast<int32_t>(regs[rs]) > 0;
            ss << "BGTZ   " << disasmRegName(rs) << ", " << getFunctionLabel(result.target);
            appendSingleBranchDetails(result.branch_taken);
            break;

        case 0x08:
            ss << "ADDI   " << disasmRegName(rt) << ", " << disasmRegName(rs) << ", " << disasmImm(simm);
            appendDetail(regDetail(rs));
            appendDetail(regDetail(rt));
            break;

        case 0x09:
            ss << "ADDIU  " << disasmRegName(rt) << ", " << disasmRegName(rs) << ", " << disasmImm(simm);
            appendDetail(regDetail(rs));
            appendDetail(regDetail(rt));
            break;

        case 0x0a:
            ss << "SLTI   " << disasmRegName(rt) << ", " << disasmRegName(rs) << ", " << disasmImm(simm);
            appendDetail(regDetail(rs));
            appendDetail(regDetail(rt));
            break;

        case 0x0b:
            ss << "SLTIU  " << disasmRegName(rt) << ", " << disasmRegName(rs) << ", " << disasmImm(simm);
            appendDetail(regDetail(rs));
            appendDetail(regDetail(rt));
            break;

        case 0x0c:
            ss << "ANDI   " << disasmRegName(rt) << ", " << disasmRegName(rs) << ", " << disasmHex(uimm, 4);
            appendDetail(regDetail(rs));
            appendDetail(regDetail(rt));
            break;

        case 0x0d:
            ss << "ORI    " << disasmRegName(rt) << ", " << disasmRegName(rs) << ", " << disasmHex(uimm, 4);
            appendDetail(regDetail(rs));
            appendDetail(regDetail(rt));
            break;

        case 0x0e:
            ss << "XORI   " << disasmRegName(rt) << ", " << disasmRegName(rs) << ", " << disasmHex(uimm, 4);
            appendDetail(regDetail(rs));
            appendDetail(regDetail(rt));
            break;

        case 0x0f:
            ss << "LUI    " << disasmRegName(rt) << ", " << disasmHex(uimm, 4);
            appendDetail(regDetail(rt));
            break;

        case 0x10:
            switch (rs) {
                case 0x00:
                    ss << "MFC0   " << disasmRegName(rt) << ", " << disasmCop0RegName(rd);
                    appendDetail(std::string(disasmCop0RegName(rd)) + "=" + disasmHex(cop0Value(rd)));
                    appendDetail(regDetail(rt));
                    break;
                case 0x04:
                    ss << "MTC0   " << disasmRegName(rt) << ", " << disasmCop0RegName(rd);
                    appendDetail(regDetail(rt));
                    appendDetail(std::string(disasmCop0RegName(rd)) + "=" + disasmHex(cop0Value(rd)));
                    break;
                case 0x10:
                    if ((inst.op & 0x3f) == 0x10)
                        ss << "RFE";
                    else
                        unsupported("COP0", inst.op & 0x01ffffff);
                    break;
                default:
                    unsupported("COP0", rs);
                    break;
            }

            break;

        case 0x11:
            unsupported("COP1", rs);
            break;

        case 0x12:
            if (inst.op & (1 << 25)) {
                uint32_t cmd = inst.op & 0x3f;
                ss << disasmGteCommandName(cmd) << "  " << disasmHex(inst.op & 0x01ffffff, 7);
            } else {
                switch (rs) {
                    case 0x00:
                        ss << "MFC2   " << disasmRegName(rt) << ", data" << rd;
                        appendDetail(regDetail(rt));
                        break;
                    case 0x02:
                        ss << "CFC2   " << disasmRegName(rt) << ", ctrl" << rd;
                        appendDetail(regDetail(rt));
                        break;
                    case 0x04:
                        ss << "MTC2   " << disasmRegName(rt) << ", data" << rd;
                        appendDetail(regDetail(rt));
                        break;
                    case 0x06:
                        ss << "CTC2   " << disasmRegName(rt) << ", ctrl" << rd;
                        appendDetail(regDetail(rt));
                        break;
                    default:
                        unsupported("COP2", rs);
                        break;
                }
            }

            break;

        case 0x13:
            unsupported("COP3", rs);
            break;

        case 0x20:
            ss << "LB     " << disasmRegName(rt) << ", " << memoryOperand(rs, simm);
            appendLoadDetails(1, regs[rs] + simm, rt);
            break;

        case 0x21:
            ss << "LH     " << disasmRegName(rt) << ", " << memoryOperand(rs, simm);
            appendLoadDetails(2, regs[rs] + simm, rt);
            break;

        case 0x22:
            ss << "LWL    " << disasmRegName(rt) << ", " << memoryOperand(rs, simm);
            appendLoadDetails(4, (regs[rs] + simm) & ~3u, rt);
            break;

        case 0x23:
            ss << "LW     " << disasmRegName(rt) << ", " << memoryOperand(rs, simm);
            appendLoadDetails(4, regs[rs] + simm, rt);
            break;

        case 0x24:
            ss << "LBU    " << disasmRegName(rt) << ", " << memoryOperand(rs, simm);
            appendLoadDetails(1, regs[rs] + simm, rt);
            break;

        case 0x25:
            ss << "LHU    " << disasmRegName(rt) << ", " << memoryOperand(rs, simm);
            appendLoadDetails(2, regs[rs] + simm, rt);
            break;

        case 0x26:
            ss << "LWR    " << disasmRegName(rt) << ", " << memoryOperand(rs, simm);
            appendLoadDetails(4, (regs[rs] + simm) & ~3u, rt);
            break;

        case 0x28:
            ss << "SB     " << disasmRegName(rt) << ", " << memoryOperand(rs, simm);
            appendStoreDetails(1, regs[rs] + simm, regs[rt] & 0xff);
            break;

        case 0x29:
            ss << "SH     " << disasmRegName(rt) << ", " << memoryOperand(rs, simm);
            appendStoreDetails(2, regs[rs] + simm, regs[rt] & 0xffff);
            break;

        case 0x2a:
            ss << "SWL    " << disasmRegName(rt) << ", " << memoryOperand(rs, simm);
            appendStoreDetails(4, (regs[rs] + simm) & ~3u, regs[rt]);
            break;

        case 0x2B:
            ss << "SW     " << disasmRegName(rt) << ", " << memoryOperand(rs, simm);
            appendStoreDetails(4, regs[rs] + simm, regs[rt]);
            break;

        case 0x2e:
            ss << "SWR    " << disasmRegName(rt) << ", " << memoryOperand(rs, simm);
            appendStoreDetails(4, (regs[rs] + simm) & ~3u, regs[rt]);
            break;

        case 0x30:
            ss << "LWC0   cop0data" << rt << ", " << memoryOperand(rs, simm);
            appendLoadDetails(4, regs[rs] + simm, rt);
            break;

        case 0x31:
            ss << "LWC1   cop1data" << rt << ", " << memoryOperand(rs, simm);
            appendLoadDetails(4, regs[rs] + simm, rt);
            break;

        case 0x32:
            ss << "LWC2   data" << rt << ", " << memoryOperand(rs, simm);
            appendLoadDetails(4, regs[rs] + simm, rt);
            break;

        case 0x33:
            ss << "LWC3   cop3data" << rt << ", " << memoryOperand(rs, simm);
            appendLoadDetails(4, regs[rs] + simm, rt);
            break;

        case 0x38:
            ss << "SWC0   cop0data" << rt << ", " << memoryOperand(rs, simm);
            appendCopStoreDetails(4, regs[rs] + simm, 0);
            break;

        case 0x39:
            ss << "SWC1   cop1data" << rt << ", " << memoryOperand(rs, simm);
            appendCopStoreDetails(4, regs[rs] + simm, 0);
            break;

        case 0x3a:
            ss << "SWC2   data" << rt << ", " << memoryOperand(rs, simm);
            appendCopStoreDetails(4, regs[rs] + simm, latestMemory(true, 4).value_or(MemoryTraceEntry{}).value);
            break;

        case 0x3b:
            ss << "SWC3   cop3data" << rt << ", " << memoryOperand(rs, simm);
            appendCopStoreDetails(4, regs[rs] + simm, 0);
            break;

        default:
            unsupported("ILLEGAL", inst.func);
            break;
    }

    result.text = ss.str();

    return result;
}

std::string CPU::getFunctionLabel(uint32_t addr) {
    auto& labels = disasmState.functionLabels;

    if (labels.count(addr))
        return labels[addr];

    std::stringstream ss;
    ss << "func_" << std::hex << addr;
    std::string label = ss.str();

    labels[addr] = label;
    disasmState.knownFunctions.insert(addr);

    return label;
}

// This is also stolen straight up from
// https://github.com/BluestormDNA/ProjectPSX/blob/master/ProjectPSX/Core/CPU.cs#L128
bool CPU::handleInterrupts(Instruction& instruction) {
    //uint32_t load = interconnect.loadInstruction(pc);
    //uint32_t instr = instruction.op >> 26;

    // Delay instruction
    //if(instr == 0x12) {
    /*if ((instruction.op & 0xFE000000u) == 0x4A000000u)
        // COP2 MTC2
        return false;
    }*/

    IRQ::step();

    bool IEC = (_cop0.sr & 0x1) != 0;
    uint32_t IM = (_cop0.sr >> 8) & 0xff;
    uint32_t IP = (_cop0.cause >> 8) & 0xff;

    /*bool IEC = (_cop0.sr & 0x1) == 1;
    uint8_t IM = static_cast<uint8_t>(_cop0.sr >> 8) & 0xFF;
    uint8_t IP = static_cast<uint8_t>(_cop0.cause >> 8) & 0xFF;*/

    if(IEC && (IM & IP) > 0) {
        exception(Interrupt);

        return true;
    }

    return false;
}

bool CPU::checkDataWriteBreakpoint(uint32_t addr) {
    const uint32_t dcic = _cop0.dcic;

    const bool masterEnabled = (dcic & (1u << 31)) && (dcic & (1u << 30));
    const bool dataEnabled = dcic & (1u << 25);
    const bool writeEnabled = dcic & (1u << 27);

    if (!masterEnabled || !dataEnabled || !writeEnabled) {
        return false;
    }

    if (((addr ^ _cop0.bda) & _cop0.bdam) != 0) {
        return false;
    }

    _cop0.dcic |= (1u << 0); // any break
    _cop0.dcic |= (1u << 2); // BDA data break
    _cop0.dcic |= (1u << 4); // BDA data-write break

    exception(Exception::Break, 0x80000040);

    return true;
}

int CPU::opsll(Instruction& instruction) {
    uint32_t i = instruction.shamt;
    uint32_t t = instruction.rt;
    uint32_t d = instruction.rd;

    uint32_t v = reg(t) << i;

    set_reg(d, v);

    return 1;
}

int CPU::opsllv(Instruction& instruction) {
    uint32_t d = instruction.rd;
    uint32_t s = instruction.rs;
    uint32_t t = instruction.rt;

    // Shift amount is truncated to 5 bits
    uint32_t v = reg(t) << (reg(s) & 0x1F);

    set_reg(d, v);

    return 1;
}

int CPU::opsra(Instruction& instruction) {
    uint32_t i = instruction.shamt;
    uint32_t t = instruction.rt;
    uint32_t d = instruction.rd;

    int32_t v = static_cast<int32_t>(reg(t)) >> i;

    set_reg(d, static_cast<uint32_t>(v));

    return 1;
}

int CPU::opsrav(Instruction& instruction) {
    auto d = instruction.rd;
    auto s = instruction.rs;
    auto t = instruction.rt;

    // Shift amount is truncated to 5 bits
    // I just... Another issue here was that I was converting the entire result into an int32_t
    auto v = static_cast<int32_t>(reg(t)) >> (reg(s) & 0x1F);

    set_reg(d, static_cast<uint32_t>(v));

    return 1;
}

int CPU::opsrl(Instruction& instruction) {
    auto i = instruction.shamt;
    auto t = instruction.rt;
    auto d = instruction.rd;

    uint32_t v = reg(t) >> i;

    set_reg(d, v);

    return 1;
}

int CPU::opsrlv(Instruction& instruction) {
    auto d = instruction.rd;
    auto s = instruction.rs;
    auto t = instruction.rt;

    // Shift amount is truncated to 5 bits
    uint32_t v = reg(t) >> (reg(s) & 0x1F);

    set_reg(d, v);

    return 1;
}

int CPU::opsltiu(Instruction& instruction) {
    auto i = instruction.imm_se;
    auto s = instruction.rs;
    auto t = instruction.rt;

    auto v = reg(s) < i;

    set_reg(t, static_cast<uint32_t>(v));

    return 1;
}

// Load Upper Immediate(LUI)
int CPU::oplui(Instruction& instruction) {
    auto i = instruction.imm;
    auto t = instruction.rt;

    auto v = i << 16;

    set_reg(t, v);

    return 1;
}

int CPU::opori(Instruction& instruction) {
    uint32_t i = instruction.imm;
    uint32_t t = instruction.rt;
    uint32_t s = instruction.rs;

    uint32_t v = reg(s) | i;

    set_reg(t, v);

    return 1;
}

int CPU::opor(Instruction& instruction) {
    auto d = instruction.rd;
    auto s = instruction.rs;
    auto t = instruction.rt;

    auto v = reg(s) | reg(t);

    set_reg(d, v);

    return 1;
}

int CPU::opnor(Instruction& instruction) {
    uint32_t d = instruction.rd;
    uint32_t s = instruction.rs;
    uint32_t t = instruction.rt;

    // Was doing '!' instead of '~' :)
    uint32_t v = ~(reg(s) | reg(t));

    set_reg(d, v);

    return 1;
}

int CPU::opxor(Instruction& instruction) {
    auto d = instruction.rd;
    auto s = instruction.rs;
    auto t = instruction.rt;

    auto v = reg(s) ^ reg(t);

    set_reg(d, v);

    return 1;
}

int CPU::opxori(Instruction& instruction) {
    auto i = instruction.imm;
    auto t = instruction.rt;
    auto s = instruction.rs;

    uint32_t v = reg(s) ^ i;

    set_reg(t, v);

    return 1;
}

int CPU::opsltu(Instruction& instruction) {
    uint32_t d = instruction.rd;
    uint32_t s = instruction.rs;
    uint32_t t = instruction.rt;

    bool v = reg(s) < reg(t);

    set_reg(d, static_cast<uint32_t>(v)); // V gets converted into a uint32

    return 1;
}

int CPU::opslti(Instruction& instruction) {
    auto i = static_cast<int32_t>(instruction.imm_se);
    uint32_t s = instruction.rs;
    uint32_t t = instruction.rt;

    bool v = (static_cast<int32_t>(reg(s))) < i;

    set_reg(t, v);

    return 1;
}

int CPU::opslt(Instruction& instruction) {
    uint32_t d = instruction.rd;
    uint32_t sReg = instruction.rs;
    uint32_t tReg = instruction.rt;

    int32_t s = static_cast<int32_t>(reg(sReg));
    int32_t t = static_cast<int32_t>(reg(tReg));

    bool v = s < t;

    set_reg(d, static_cast<uint32_t>(v));

    return 1;
}

// Store word
int CPU::opsw(Instruction& instruction) {
    // Can't write if we are in cache isolation mode!
    /*if((sr & 0x10000) != 0) {
        //std::cerr << "Ignoring store-word while cache is isolated!\n";

        return;
    }*/

    auto i = instruction.imm_se;
    auto t = instruction.rt;
    auto s = instruction.rs;

    uint32_t addr = (reg(s) + i) & 0xFFFFFFFF;

    if(addr % 4 == 0) {
        uint32_t v    = reg(t);

        store32(addr, v);
        checkDataWriteBreakpoint(addr);
    } else {
        _cop0.badVaddr = addr;

        exception(StoreAddressError);
    }

    return 1;
}

int CPU::opswl(Instruction& instruction) {
    auto i = instruction.imm_se;
    auto t = instruction.rt;
    auto s = instruction.rs;

    uint32_t addr = reg(s) + i;
    uint32_t v    = reg(t);

    uint32_t alignedAddr = addr & ~3;

    // Load the current value for the aligned word,
    // at the target address
    uint32_t curMem = load32(alignedAddr);
    uint32_t mem;

    switch (addr & 3) {
        case 0:
            mem = (curMem & 0xFFFFFF00) | (v >> 24);
            break;
        case 1:
            mem = (curMem & 0xFFFF0000) | (v >> 16);
            break;
        case 2:
            mem = (curMem & 0xFF000000) | (v >> 8);
            break;
        case 3:
            mem = (curMem & 0x00000000) | (v >> 0);
            break;
        default:
            throw std::runtime_error("Unreachable code!");
    }

    store32(alignedAddr, mem);

    return 1;
}

int CPU::opswr(Instruction& instruction) {
    auto i = instruction.imm_se;
    auto t = instruction.rt;
    auto s = instruction.rs;

    uint32_t addr = (reg(s) + i) & 0xFFFFFFFF;
    uint32_t v    = reg(t);

    uint32_t alignedAddr = addr & ~3;

    // Load the current value for the aligned word,
    // at the target address
    uint32_t curMem = load32(alignedAddr);
    uint32_t mem;

    switch (addr & 3) {
        case 0:
            mem = (curMem & 0x0000000) | (v << 0);
            break;
        case 1:
            mem = (curMem & 0x000000FF) | (v << 8);
            break;
        case 2:
            mem = (curMem & 0x0000FFFF) | (v << 16);
            break;
        case 3:
            mem = (curMem & 0x00FFFFFF) | (v << 24);
            break;
        default:
            throw std::runtime_error("Unreachable code!");
    }

    store32(alignedAddr, mem);

    return 1;
}

// Store halfword
int CPU::opsh(Instruction& instruction) {
    /*if((sr & 0x10000) != 0) {
        std::cout << "Ignoring store-halfword while cache is isolated!\n";

        return;
    }*/

    auto i = instruction.imm_se;
    auto t = instruction.rt;
    auto s = instruction.rs;

    uint32_t addr = reg(s) + i;

    if(addr % 2 == 0) {
        uint32_t v = reg(t);

        store16(addr, v);
        checkDataWriteBreakpoint(addr);
    } else {
        _cop0.badVaddr = addr;
       exception(StoreAddressError);
    }

    return 1;
}

int CPU::opsb(Instruction& instruction) {
    /*if((sr & 0x10000) != 0) {
        std::cout << "Ignoring store-byte while cache is isolated!\n";

        return;
    }*/

    auto i = instruction.imm_se;
    auto t = instruction.rt;
    auto s = instruction.rs;

    uint32_t addr = reg(s) + i;
    uint32_t v    = reg(t);

    store8(addr, static_cast<uint8_t>(v));

    checkDataWriteBreakpoint(addr);

    return 1;
}

// Load word
int CPU::oplw(Instruction& instruction) {
    auto i = instruction.imm_se;
    auto t = instruction.rt;
    auto s = instruction.rs;
    
    auto addr = reg(s) + i;
    
    if(addr % 4 == 0) {
        uint32_t v = load32(addr);

        // Put the load in the delay slot
        setLoad(t, v);
    } else {
        _cop0.badVaddr = addr;

        exception(LoadAddressError);
    }
    
    return 1;
}

int CPU::oplwl(Instruction& instruction) {
    auto i = instruction.imm_se;
    uint32_t t = instruction.rt;
    uint32_t s = instruction.rs;

    uint32_t addr = reg(s) + i;

    // This instruction bypasses the load delay restriction:
    // this instruction will merge the new contents,
    // with the value that is currently being loaded if needed.
    //uint32_t curv = outRegs[static_cast<size_t>(t)];
    //uint32_t curv = rt;

    uint32_t rt = t;
    if(loads[0].index == t) {
        rt = loads[0].value;
    } else {
        rt = reg(t);
    }

    // Next we load the *aligned* word containing the first address byte
    uint32_t alignedAddr = addr & ~3;
    uint32_t alignedWord = load32(alignedAddr);

    // Depending on the address alignment we fetch, 1, 2, 3 or 4,
    // *most* significant bytes and put them in the target register.
    uint32_t v = addr & 3;

    switch (v) {
    case 0:
        v = (rt & 0x00FFFFFF) | (alignedWord << 24);
        break;
    case 1:
        v = (rt & 0x0000FFFF) | (alignedWord << 16);
        break;
    case 2:
        v = (rt & 0x000000FF) | (alignedWord << 8);
        break;
    case 3:
        v = (rt & 0x00000000) | (alignedWord << 0);
        break;
    default:
        throw std::runtime_error("Unreachable code!");
    }

    // Put the load in the delay slot
    //load = {t, v};
    setLoad(t, v);

    return 1;
}

int CPU::oplwr(Instruction& instruction) {
    auto i = instruction.imm_se;
    uint32_t t = instruction.rt;
    uint32_t s = instruction.rs;

    uint32_t addr = (reg(s) + i) & 0xFFFFFFFF;

    // This instruction bypasses the load delay restriction:
    // this instruction will merge the new contents,
    // with the value that is currently being loaded if needed.
    //uint32_t curv = outRegs[static_cast<size_t>(t)];
    //uint32_t curv = reg(t);

    uint32_t rt = t;
    if(loads[0].index == t) {
        rt = loads[0].value;
    } else {
        rt = reg(t);
    }

    // Next we load the *aligned* word containing the first address byte
    uint32_t alignedAddr = addr & ~3;
    uint32_t alignedWord = load32(alignedAddr);

    // Depending on the address alignment we fetch, 1, 2, 3 or 4,
    // *most* significant bytes and put them in the target register.
    uint32_t v = addr & 3;

    switch (v) {
        case 0:
            v = (rt & 0x00000000) | (alignedWord >> 0);
            break;
        case 1:
            v = (rt & 0xFF000000) | (alignedWord >> 8);
            break;
        case 2:
            v = (rt & 0xFFFF0000) | (alignedWord >> 16);
            break;
        case 3:
            v = (rt & 0xFFFFFF00) | (alignedWord >> 24);
            break;
        default:
            throw std::runtime_error("Unreachable code!");
    }

    // Put the load in the delay slot
    //load = {t, v};
    setLoad(t, v);

    return 1;
}

int CPU::oplh(Instruction& instruction) {
    // Can't write if we are in cache isolation mode!
    /*if((sr & 0x10000) != 0) {
        std::cout << "Ignoring store-word while cache is isolated!\n";

        return;
    }*/

    auto i = instruction.imm_se;
    auto t = instruction.rt;
    auto s = instruction.rs;

    uint32_t addr = (reg(s) + i) & 0xFFFFFFFF;

    if(addr % 2 == 0) {
        // Cast as i16 to force sign extension
        // Had an issue here; I was making this as a uint32_t rather than int16_t
        int16_t v = static_cast<int16_t>(load16(addr));

        setLoad(t, static_cast<uint32_t>(v));
    } else {
        _cop0.badVaddr = addr;
        exception(LoadAddressError);
    }

    return 1;
}

int CPU::oplhu(Instruction& instruction) {
    auto i = instruction.imm_se;
    uint32_t t = instruction.rt;
    uint32_t s = instruction.rs;

    uint32_t addr = (reg(s) + i) & 0xFFFFFFFF;

    // Address must be 16bit aligned
    if(addr % 2 == 0) {
        uint16_t v = load16(addr);

        setLoad(t, static_cast<uint32_t>(v));
    } else {
        _cop0.badVaddr = addr;
        exception(LoadAddressError);
    }

    return 1;
}

// Load byte
int CPU::oplb(Instruction& instruction) {
    auto i = instruction.imm_se;
    auto t = instruction.rt;
    auto s = instruction.rs;

    auto addr = (reg(s) + i) & 0xFFFFFFFF;
    int8_t v = static_cast<int8_t>(load8(addr));

    // Put the load in the delay slot
    setLoad(t, static_cast<uint32_t>(v));

    return 1;
}

int CPU::oplbu(Instruction& instruction) {
    auto i = instruction.imm_se;
    auto t = instruction.rt;
    auto s = instruction.rs;

    uint32_t addr = reg(s) + i;

    uint8_t v = load8(addr);

    // Put the load in the delay slot
    setLoad(t, static_cast<uint32_t>(v));

    return 1;
}

// addiu $8, $zero, 0xb88
int CPU::addiu(Instruction& instruction) {
    auto i = instruction.imm_se;
    auto t = instruction.rt;
    auto s = instruction.rs;

    uint32_t v = (reg(s) + i) & 0xFFFFFFFF;

    set_reg(t, v);

    return 1;
}

int CPU::addi(Instruction& instruction) {
    auto i = static_cast<int32_t>(instruction.imm_se);
    uint32_t t = instruction.rt;
    uint32_t sReg = instruction.rs;

    int32_t s = static_cast<int32_t>(reg(sReg));

    if(std::optional<int32_t> v = check_add<int32_t>(s, i)) {
        set_reg(t, static_cast<uint32_t>(v.value()));
    } else {
        exception(Overflow);
    }

    return 1;
}

int CPU::opmultu(Instruction& instruction) {
    auto s = instruction.rs;
    auto t = instruction.rt;

    uint64_t a = static_cast<uint64_t>(reg(s));
    uint64_t b = static_cast<uint64_t>(reg(t));

    uint64_t v = a * b;

    hi = static_cast<uint32_t>(v >> 32);
    lo = static_cast<uint32_t>(v);

    uint32_t rsVal = reg(s);

    return (rsVal < 0x800) ? 6 : ((rsVal < 0x100000) ? 9 : 13);
}

int CPU::opmult(Instruction& instruction) {
    auto s = instruction.rs;
    auto t = instruction.rt;

    int64_t a = static_cast<int64_t>(static_cast<int32_t>(reg(s)));
    int64_t b = static_cast<int64_t>(static_cast<int32_t>(reg(t)));

    uint64_t v = static_cast<uint64_t>((a * b));

    hi = static_cast<uint32_t>(v >> 32);
    lo = static_cast<uint32_t>(v);

    int32_t rsVal = static_cast<int32_t>(reg(s));

    if (rsVal < 0)
        return (rsVal >= -2048) ? 6 : ((rsVal >= -1048576) ? 9 : 13);

    return (rsVal < 0x800) ? 6 : ((rsVal < 0x100000) ? 9 : 13);
}

int CPU::opdiv(Instruction& instruction) {
    uint32_t s = instruction.rs;
    uint32_t t = instruction.rt;

    int32_t n = static_cast<int32_t>(reg(s));
    int32_t d = static_cast<int32_t>(reg(t));

    if(d == 0) {
        // Division by zero.. Please don't
        hi = static_cast<uint32_t>(n);

        if(n >= 0)
            lo = 0xffffffff;
        else
            lo = 1;
    } else if(static_cast<uint32_t>(n) == 0x80000000 && d == -1) {
        // Result is not representable in a 32bit signed integer
        hi = 0;
        lo = 0x80000000;
    } else {
        hi = static_cast<uint32_t>(n % d);
        lo = static_cast<uint32_t>(n / d);
    }

    return 36; // TODO; Confirm?
}

int CPU::opdivu(Instruction& instruction) {
    auto s = instruction.rs;
    auto t = instruction.rt;

    auto n = reg(s);
    auto d = reg(t);

    if(d == 0) {
        // Division by zero..
        hi = n;
        lo = 0xffffffff;
    } else {
        hi = n % d;
        lo = n / d;
    }

    return 36;
}

int CPU::opmfhi(Instruction& instruction) {
    auto d = instruction.rd;

    set_reg(d, hi);

    return 1;
}

int CPU::opmthi(Instruction& instruction) {
    auto s = instruction.rs;

    hi = reg(s);

    return 1; // TODO; CONFIRM??
}

int CPU::opmflo(Instruction& instruction) {
    uint32_t d = instruction.rd;

    set_reg(d, lo);

    return 1;
}

int CPU::opmtlo(Instruction& instruction) {
    auto s = instruction.rs;

    lo = reg(s);

    return 1;
}

int CPU::oplwc0(Instruction& instruction) {
    // Not supported by this COP
    exception(CoprocessorError);

    return 1;
}

int CPU::oplwc1(Instruction& instruction) {
    // Not supported by this COP
    exception(CoprocessorError);

    return 1;
}

int CPU::oplwc2(Instruction& instruction) {
    auto i = instruction.imm_se;
    auto t = instruction.rt;
    auto s = instruction.rs;

    auto addr = reg(s) + i;

    if((addr % 4) == 0) {
        uint32_t v = load32(addr);

        gte.write(t, v);
        //_cop2.setData(t, v);
    } else {
        _cop0.badVaddr = addr;

        exception(LoadAddressError);
    }

    return 1;
}

int CPU::oplwc3(Instruction& instruction) {
    // Not supported by this COP
    exception(CoprocessorError);

    return 1;
}

int CPU::opswc0(Instruction& instruction) {
    // Not supported by this COP
    exception(CoprocessorError);

    return 1;
}

int CPU::opswc1(Instruction& instruction) {
    // Not supported by this COP
    exception(CoprocessorError);

    return 1;
}

int CPU::opswc2(Instruction& instruction) {
    auto s = instruction.rs;
    auto t = instruction.rt;
    auto i = instruction.imm_se;

    auto addr = (reg(s) + i) & 0xFFFFFFFF;

    if(addr % 4 == 0) {
        auto v = gte.read(t);//_cop2.getData(t);

        store32(addr, v);
    } else {
        _cop0.badVaddr = addr;

        exception(LoadAddressError);
    }

    return 1;
}

int CPU::opswc3(Instruction& instruction) {
    // Not supported by this COP
    exception(CoprocessorError);

    return 1;
}

int CPU::opsubu(Instruction& instruction) {
    uint32_t s = instruction.rs;
    uint32_t t = instruction.rt;
    uint32_t d = instruction.rd;

    // I CANT BELIEVE I HAD THIS AS WRAPPINGADD AND NOT WRAPPINGSUB!!!!!
    uint32_t v = (reg(s) - reg(t)) & 0xFFFFFFFF;

    set_reg(d, v);

    return 1;
}

int CPU::opsub(Instruction& instruction) {
    auto sReg = instruction.rs;
    auto tReg = instruction.rt;
    auto d = instruction.rd;

    int32_t s = static_cast<int32_t>(reg(sReg));
    int32_t t = static_cast<int32_t>(reg(tReg));

    // Ain't no way I'm doing check_add instead of check_sub
    if(std::optional<int32_t> v = check_sub<int32_t>(s, t)) {
        set_reg(d, static_cast<uint32_t>(v.value()));
    } else {
        exception(Overflow);
    }

    return 1;
}

int CPU::opand(Instruction& instruction) {
    uint32_t d = instruction.rd;
    uint32_t s = instruction.rs;
    uint32_t t = instruction.rt;

    uint32_t v = reg(s) & reg(t);

    set_reg(d, v);

    return 1;
}

int CPU::opandi(Instruction& instruction){
    uint32_t i = instruction.imm;
    uint32_t s = instruction.rs;
    uint32_t t = instruction.rt;

    uint32_t v = reg(s) & i;

    set_reg(t, v);

    return 1;
}

int CPU::opcop0(Instruction& instruction) {
    // Formation goes as:
    // 0b0100nn where nn is the coprocessor number.

    switch (instruction.rs) {
    case 0b000000:
        opmfc0(instruction);
        return 1;
    case 0b00100:
        opmtc0(instruction);
        return 1;
    case 0b10000:
        oprfe(instruction);
        return 1;
    default:
        opillegal(instruction);
        std::cerr << "Unhandled COP0 instruction\n";
        //throw std::runtime_error("Unhandled COP instruction: " + std::to_string((instruction.copOpcode())));
        return 1;
    }
}

// Doesn't exists
int CPU::opcop1(Instruction& instruction) {
    exception(CoprocessorError);
    //throw std::runtime_error("Unhandled coprocessor 1 instructions\n");
    return 1;
}

int CPU::opcop2(Instruction& instruction) {
    auto opcode = instruction.rs;

    if (instruction.op & (1 << 25)) {
        // GTE command
        gte::Command command(instruction.op);
        return gte.command(command);
    }

    switch (opcode) {
        case 0b000000:
            // Move from GTE to Data register
            opmfc2(instruction);
            return 1;
        case 0b000010:
            // From GTE to Control register
            opcfc2(instruction);
            return 1;
        case 0b00100:
            // Data Register to GTE
            opmtc2(instruction);
            return 1;
        case 0b00110:
            // Data Register to GTE Control register
            opctc2(instruction);
            return 1;
        default:
            std::cerr << "Unhandled COP2 instruction: " + std::to_string((instruction.rs)) << "\n";
            throw std::runtime_error("Unhandled COP instruction: " + std::to_string((instruction.rs)));
    }
}

// Doesn't exists
int CPU::opcop3(Instruction& instruction) {
    exception(CoprocessorError);
    return 1;
}

void CPU::opmtc0(Instruction& instruction) {
    // This basically makes it so that all the data,
    // goes to the cache instead of the main memory.
    uint32_t cpur = instruction.rt;
    uint32_t copr = instruction.rd;

    uint32_t v = reg(cpur);

    /**
     * cop0r0-r2   - N/A
     * cop0r3      - BPC - Breakpoint on execute (R/W)
     * cop0r4      - N/A
     * cop0r5      - BDA - Breakpoint on data access (R/W)
     * cop0r6      - JUMPDEST - Randomly memorized jump address (R)
     * cop0r7      - DCIC - Breakpoint control (R/W)
     * cop0r8      - BadVaddr - Bad Virtual Address (R)
     * cop0r9      - BDAM - Data Access breakpoint mask (R/W)
     * cop0r10     - N/A
     * cop0r11     - BPCM - Execute breakpoint mask (R/W)
     * cop0r12     - SR - System status register (R/W)
     * cop0r13     - CAUSE - Describes the most recently recognised exception (R)
     * cop0r14     - EPC - Return Address from Trap (R)
     * cop0r15     - PRID - Processor ID (R)
     * cop0r16-r31 - Garbage
     * cop0r32-r63 - N/A - None such (Control regs)
     */

    switch (copr) {
        //Breakpoints registers for the future
        case 3:
            _cop0.bpc = v;
            break;
        case 5:
            _cop0.bda = v;
            break;
        case 6:
            break;
        case 7:
            _cop0.dcic = v;
            break;
        case 9:
            _cop0.bdam = v;
            break;
        case 11:
            _cop0.bpcm = v;
            break;
        case 12: {
            _cop0.sr = v;
            IRQ::step();

            /*bool shouldInterrupt = (_cop0.sr & 0x1) == 1;
            bool cur = (v & 0x1) == 1;

            _cop0.sr = v;

            uint32_t IM = (v >> 8) & 0x3;
            uint32_t IP = (_cop0.cause >> 8) & 0x3;

            if(!shouldInterrupt && cur && (IM & IP) > 0) {
                pc = nextpc;
                currentpc = pc;
                //nextpc += 4;

                exception(Exception::Interrupt);
            }*/

            break;
        }
        case 13: {
            _cop0.cause &= ~0x300;
            _cop0.cause |= v & 0x300;

            break;
        }
        default:
            std::cout << "Unhandled cop0 register " << copr << "\n";
            throw std::runtime_error("Unhandled cop0 register " + std::to_string(copr));
    }
}

void CPU::opmfc0(Instruction& instruction) {
    auto cpur = instruction.rt;
    uint32_t copr = instruction.rd;

    uint32_t v = 0;

    /**
     * cop0r0-r2   - N/A
     * cop0r3      - BPC - Breakpoint on execute (R/W)
     * cop0r4      - N/A
     * cop0r5      - BDA - Breakpoint on data access (R/W)
     * cop0r6      - JUMPDEST - Randomly memorized jump address (R)
     * cop0r7      - DCIC - Breakpoint control (R/W)
     * cop0r8      - BadVaddr - Bad Virtual Address (R)
     * cop0r9      - BDAM - Data Access breakpoint mask (R/W)
     * cop0r10     - N/A
     * cop0r11     - BPCM - Execute breakpoint mask (R/W)
     * cop0r12     - SR - System status register (R/W)
     * cop0r13     - CAUSE - Describes the most recently recognised exception (R)
     * cop0r14     - EPC - Return Address from Trap (R)
     * cop0r15     - PRID - Processor ID (R)
     * cop0r16-r31 - Garbage
     * cop0r32-r63 - N/A - None such (Control regs)
     */

    switch (copr) {
    // Unknown registers
    case 0:
    case 1:
    case 2:
        break;
    case 3:
        v = _cop0.bpc;
        break;
    case 5:
        v = _cop0.bda;
        break;
    case 6:
        break;
    case 7:
        v = _cop0.dcic;
        break;
    case 8:
        v = _cop0.badVaddr;
        break;
    case 9:
        v = _cop0.bdam;
        break;
    case 11:
        v = _cop0.bpcm;
        break;
    case 12:
        v = _cop0.sr;
        break;
    case 13:
        v = _cop0.cause;
        break;
    case 14:
        v = _cop0.epc;
        break;
    case 15:
        /*
         * cop0r15     - PRID - Processor ID (R)
         *
         * SCPH-1001 (North America): 0x00000001
         * SCPH-7502 (Europe): 0x00000002
         */

        v = 0x00000001;
        break;
    default:
        throw std::runtime_error("Unhandled read from cop0r " + std::to_string(copr));
    }

    setLoad(cpur, v);
}

void CPU::oprfe(Instruction& instruction) {
    // There are other instructions with the same encoding but all
    // are virtual memory related And the Playstation doesnt't
    // implement them. Still, let's make sure we're not running
    // buggy code.
    if((instruction.op & 0x3f) != 0b010000) {
        throw std::runtime_error("Invalid cop0 instruction; )" + instruction.op);
    }

    // Uhh was clearing bit 4, apparently it's bit 6
    /*uint32_t mode = _cop0.sr & 0x3F;
    _cop0.sr &= ~0xF;
    _cop0.sr |= mode >> 2;*/
    /*uint32_t mode = _cop0.sr & 0x3F;
    _cop0.sr &= ~0x3f;
    _cop0.sr |= mode >> 2;*/

    /**
     * Ok nvm then.. apparently its (i cant read):
     * SR[1:0] = old SR[3:2]
     * SR[3:2] = old SR[5:4]
     * SR[5:4] unchanged
     */
    _cop0.sr = (_cop0.sr & ~0xFu) | ((_cop0.sr >> 2) & 0xFu);
    IRQ::step();
}

void CPU::opmfc2(Instruction& instruction) {
    auto t = instruction.rt;
    auto d = instruction.rd;

    auto v = gte.read(d);//_cop2.getData(d);

    setLoad(t, v);
}

void CPU::opcfc2(Instruction& instruction) {
    auto t = instruction.rt;
    auto d = instruction.rd;

    auto v = gte.read(d + 32);//_cop2.getControl(d);

    setLoad(t, v);
}

void CPU::opmtc2(Instruction& instruction) {
    auto t = instruction.rt;
    auto d = instruction.rd;

    auto v = reg(t);

    gte.write(d, v);
    //_cop2.setData(d, v);
}

void CPU::opctc2(Instruction& instruction) {
    auto t = instruction.rt;
    auto d = instruction.rd;

    auto v = reg(t);

    gte.write(d + 32, v);
    //_cop2.setControl(d, v);
}

void CPU::opgte(Instruction& instruction) {
    throw std::runtime_error("Unhandled GTE operator\n");
}

void CPU::exception(const Exception exception, uint32_t handlerOverride) {
    // Only 0x4h and 0x5h updates BadVaddr
    // Bruh..Shouldn't point to 'currentpc'
    // but the addr it'll jump to?
    /*if(exception == LoadAddressError || exception == StoreAddressError) {
        _cop0.badVaddr = currentpc;
    }*/

    // Shift bits [5:0] of 'SR' two places to the left
    uint32_t mode = _cop0.sr & 0x3f;

    _cop0.sr &= ~0x3f;
    _cop0.sr |= (mode << 2) & 0x3f;

    uint8_t IP = static_cast<uint8_t>(_cop0.cause >> 8) & 0xFF;
    _cop0.cause = 0;
    _cop0.cause |= (IP) << 8;

    // Update 'CAUSE' register with the exception code (bits [6:2])
    _cop0.cause &= ~0x7C;
    _cop0.cause |= static_cast<uint32_t>(exception) << 2;

    /**
     * Been stuck at this issue for a while so I'm cheating:
     * Avocado seems to set this bit this way, docs say:
     *
     * 28-29 CE Contains the coprocessor number if the exception
     *          occurred because of a coprocessor instuction for
     *          a coprocessor which wasn't enabled in SR.
     */
    _cop0.cause &= ~(0x3u << 28);

    if (exception == CoprocessorError) {
        Instruction load{interconnect.loadInstruction(currentpc)};
        uint8_t coprocessorNumber = load.op & 0x3; // was .func nice
        _cop0.cause |= static_cast<uint32_t>(coprocessorNumber) << 28;
    }

    if(delayJumpSlot) {
        _cop0.cause |= (static_cast<uint32_t>(1) << (30));
    }

    if(delaySlot) {
        // When an exception occurs in a delay slot 'EPC' points
        // to the branch instruction and bit 31 of 'CAUSE' is set.
        _cop0.epc = currentpc - 4;
        _cop0.cause |= (static_cast<uint32_t>(1) << (31));
    } else {
        // Save current instruction address in 'EPC'
        _cop0.epc = currentpc;
        _cop0.cause &= ~(static_cast<uint32_t>(1) << 31);
    }

    // Determine the exception handler address based on the 'BEV' bit
    uint32_t handler = handlerOverride
        ? handlerOverride
        : ((_cop0.sr & (1 << 22)) ? 0xbfc00180 : 0x80000080);

    // Exceptions don’t have a branch delay, we jump directly into the handler
    pc = handler;
    nextpc = handler + 4;

    if(exception != 8 && exception != 0)
        std::cerr << "EXCEPTION OCCURRED!!" << exception << "\n";
}

/*void CPU::exception(const Exception exception) {
    // Only 0x4h and 0x5h updates BadVaddr
    // Bruh..Shouldn't point to 'currentpc'
    // but the addr it'll jump to?
    /*if(exception == LoadAddressError || exception == StoreAddressError) {
        _cop0.badVaddr = currentpc;
    }#1#

    // Shift bits [5:0] of 'SR' two places to the left
    uint32_t mode = _cop0.sr & 0x3f;

    _cop0.sr &= ~0x3f;
    _cop0.sr |= (mode << 2) & 0x3f;

    uint8_t IP = static_cast<uint8_t>(_cop0.cause >> 8) & 0xFF;
    _cop0.cause = 0;
    _cop0.cause |= (IP) << 8;

    // Update 'CAUSE' register with the exception code (bits [6:2])
    _cop0.cause &= ~0x7C;
    _cop0.cause |= static_cast<uint32_t>(exception) << 2;

    /**
     * Been stuck at this issue for a while so I'm cheating:
     * Avocado seems to set this bit this way, docs say:
     *
     * 28-29 CE Contains the coprocessor number if the exception
     *          occurred because of a coprocessor instuction for
     *          a coprocessor which wasn't enabled in SR.
     #1#
    //Instruction load = load32(currentpc);
    /*Instruction load{interconnect.loadInstruction(currentpc)};
    uint8_t coprocessorNumber = load.func & 0x3;#1#

    if (exception == CoprocessorError) {
        Instruction load{interconnect.loadInstruction(currentpc)};
        uint8_t coprocessorNumber = load.func & 0x3;
        _cop0.cause |= coprocessorNumber << 28;
    }

    _cop0.cause &= ~(0x3 << 28);
    //_cop0.cause |= (coprocessorNumber << 28);

    /*if(delayJumpSlot) {
        _cop0.cause |= (static_cast<uint32_t>(1) << (30));
    }#1#

    if(delaySlot) {
        // When an exception occurs in a delay slot 'EPC' points
        // to the branch instruction and bit 31 of 'CAUSE' is set.
        _cop0.epc = currentpc - 4;
        _cop0.cause |= (static_cast<uint32_t>(1) << (31));
    } else {
        // Save current instruction address in 'EPC'
        _cop0.epc = currentpc;
        _cop0.cause &= ~(static_cast<uint32_t>(1) << 31);
    }

    // Determine the exception handler address based on the 'BEV' bit
    uint32_t handler = (_cop0.sr & (1 << 22)) != 0 ? 0xbfc00180 : 0x80000080;

    // Exceptions don’t have a branch delay, we jump directly into the handler
    pc = handler;
    nextpc = handler + 4;

    if(exception != 8 && exception != 0)
        std::cerr << "EXCEPTION OCCURRED!!" << exception << "\n";
}*/

int CPU::opSyscall(Instruction& instruction) {
    exception(SysCall);

    return 1;
}

int CPU::opbreak(Instruction& instruction) {
    exception(Break);

    return 1;
}

int CPU::opillegal(Instruction& instruction) {
    paused = true;
    printf("Illegal instruction %d %d PC; %d\n", instruction.func, instruction.subfunc, currentpc);
    std::cerr << "Illegal instruction " << instruction.op << " PC; " << currentpc << "\n";
    exception(IllegalInstruction);

    return 1;
}

// Take from; https://github.com/allkern/psxe/blob/master/psx/cpu_debug.h#L3
static const char* g_psx_cpu_a_kcall_symtable[] = {
    "open(filename=%08x,accessmode=%08x)",
    "lseek(fd=%08x,offset=%08x,seektype=%08x)",
    "read(fd=%08x,dst=%08x,length=%08x)",
    "write(fd=%08x,src=%08x,length=%08x)",
    "close(fd=%08x)",
    "ioctl(fd=%08x,cmd=%08x,arg=%08x)",
    "exit(exitcode=%08x)",
    "isatty(fd=%08x)",
    "getc(fd=%08x)",
    "putc(char=%08x,fd=%08x)",
    "todigit(char=%08x)",
    "atof(src=%08x)",
    "strtoul(src=%08x,src_end=%08x,base=%08x)",
    "strtol(src=%08x,src_end=%08x,base=%08x)",
    "abs(val=%08x)",
    "labs(val=%08x)",
    "atoi(src=%08x)",
    "atol(src=%08x)",
    "atob(src=%08x,num_dst=%08x)",
    "setjmp(buf=%08x)",
    "longjmp(buf=%08x,param=%08x)",
    "strcat(dst=%08x,src=%08x)",
    "strncat(dst=%08x,src=%08x,maxlen=%08x)",
    "strcmp(str1=%08x,str2=%08x)",
    "strncmp(str1=%08x,str2=%08x,maxlen=%08x)",
    "strcpy(dst=%08x,src=%08x)",
    "strncpy(dst=%08x,src=%08x,maxlen=%08x)",
    "strlen(src=%08x)",
    "index(src=%08x,char=%08x)",
    "rindex(src=%08x,char=%08x)",
    "strchr(src=%08x,char=%08x)",
    "strrchr(src=%08x,char=%08x)",
    "strpbrk(src=%08x,list=%08x)",
    "strspn(src=%08x,list=%08x)",
    "strcspn(src=%08x,list=%08x)",
    "strtok(src=%08x,list=%08x)",
    "strstr(str=%08x,substr=%08x)",
    "toupper(char=%08x)",
    "tolower(char=%08x)",
    "bcopy(src=%08x,dst=%08x,len=%08x)",
    "bzero(dst=%08x,len=%08x)",
    "bcmp(ptr1=%08x,ptr2=%08x,len=%08x)",
    "memcpy(dst=%08x,src=%08x,len=%08x)",
    "memset(dst=%08x,fillbyte=%08x,len=%08x)",
    "memmove(dst=%08x,src=%08x,len=%08x)",
    "memcmp(src1=%08x,src2=%08x,len=%08x)",
    "memchr(src=%08x,scanbyte=%08x,len=%08x)",
    "rand()",
    "srand(seed=%08x)",
    "qsort(base=%08x,nel=%08x,width=%08x,callback=%08x)",
    "strtod(src=%08x,src_end=%08x)",
    "malloc(size=%08x)",
    "free(buf=%08x)",
    "lsearch(key=%08x,base=%08x,nel=%08x,width=%08x,callback=%08x)",
    "bsearch(key=%08x,base=%08x,nel=%08x,width=%08x,callback=%08x)",
    "calloc(sizx=%08x,sizy=%08x)",
    "realloc(old_buf=%08x,new_siz=%08x)",
    "InitHeap(addr=%08x,size=%08x)",
    "_exit(exitcode=%08x)",
    "getchar()",
    "putchar(char=%08x)",
    "gets(dst=%08x)",
    "puts(src=%08x)",
    "printf(txt=%08x,param1=%08x,param2=%08x,etc.=%08x)",
    "SystemErrorUnresolvedException()",
    "LoadTest(filename=%08x,headerbuf=%08x)",
    "Load(filename=%08x,headerbuf=%08x)",
    "Exec(headerbuf=%08x,param1=%08x,param2=%08x)",
    "FlushCache()",
    "init_a0_b0_c0_vectors()",
    "GPU_dw(Xdst=%08x,Ydst=%08x,Xsiz=%08x,Ysiz=%08x,src=%08x)",
    "gpu_send_dma(Xdst=%08x,Ydst=%08x,Xsiz=%08x,Ysiz=%08x,src=%08x)",
    "SendGP1Command(gp1cmd=%08x)",
    "GPU_cw(gp0cmd=%08x)",
    "GPU_cwp(src=%08x,num=%08x)",
    "send_gpu_linked_list(src=%08x)",
    "gpu_abort_dma()",
    "GetGPUStatus()",
    "gpu_sync()",
    "SystemError()",
    "SystemError()",
    "LoadExec(filename=%08x,stackbase=%08x,stackoffset=%08x)",
    "GetSysSp()",
    "SystemError()",
    "_96_init()",
    "_bu_init()",
    "_96_remove()",
    "return 0",
    "return 0",
    "return 0",
    "return 0",
    "dev_tty_init()",
    "dev_tty_open(fcb=%08x, (unused)path=%08x,accessmode=%08x)",
    "dev_tty_in_out(fcb=%08x,cmd=%08x)",
    "dev_tty_ioctl(fcb=%08x,cmd=%08x,arg=%08x)",
    "dev_cd_open(fcb=%08x,path=%08x,accessmode=%08x)",
    "dev_cd_read(fcb=%08x,dst=%08x,len=%08x)",
    "dev_cd_close(fcb=%08x)",
    "dev_cd_firstfile(fcb=%08x,path=%08x,direntry=%08x)",
    "dev_cd_nextfile(fcb=%08x,direntry=%08x)",
    "dev_cd_chdir(fcb=%08x,path=%08x)",
    "dev_card_open(fcb=%08x,path=%08x,accessmode=%08x)",
    "dev_card_read(fcb=%08x,dst=%08x,len=%08x)",
    "dev_card_write(fcb=%08x,src=%08x,len=%08x)",
    "dev_card_close(fcb=%08x)",
    "dev_card_firstfile(fcb=%08x,path=%08x,direntry=%08x)",
    "dev_card_nextfile(fcb=%08x,direntry=%08x)",
    "dev_card_erase(fcb=%08x,path=%08x)",
    "dev_card_undelete(fcb=%08x,path=%08x)",
    "dev_card_format(fcb=%08x)",
    "dev_card_rename(fcb1=%08x,path=%08x)",
    "card_clear_error(fcb=%08x) (?)",
    "_bu_init()",
    "_96_init()",
    "_96_remove()",
    "return 0",
    "return 0",
    "return 0",
    "return 0",
    "return 0",
    "CdAsyncSeekL(src=%08x)",
    "return 0",
    "return 0",
    "return 0",
    "CdAsyncGetStatus(dst=%08x)",
    "return 0",
    "CdAsyncReadSector(count=%08x,dst=%08x,mode=%08x)",
    "return 0",
    "return 0",
    "CdAsyncSetMode(mode=%08x)",
    "return 0",
    "return 0",
    "return 0",
    "return 0",
    "return 0",
    "return 0",
    "return 0",
    "return 0",
    "return 0",
    "return 0",
    "return 0",
    "return 0",
    "return 0",
    "return 0",
    "CdromIoIrqFunc1()",
    "CdromDmaIrqFunc1()",
    "CdromIoIrqFunc2()",
    "CdromDmaIrqFunc2()",
    "CdromGetInt5errCode(dst1=%08x,dst2=%08x)",
    "CdInitSubFunc()",
    "AddCDROMDevice()",
    "AddMemCardDevice()",
    "AddDuartTtyDevice()",
    "add_nullcon_driver()",
    "SystemError()",
    "SystemError()",
    "SetConf(num_EvCB=%08x,num_TCB=%08x,stacktop=%08x)",
    "GetConf(num_EvCB_dst=%08x,num_TCB_dst=%08x,stacktop_dst=%08x)",
    "SetCdromIrqAutoAbort(type=%08x,flag=%08x)",
    "SetMem(megabytes=%08x)",
    "_boot()",
    "SystemError(type=%08x,errorcode=%08x)",
    "EnqueueCdIntr()",
    "DequeueCdIntr()",
    "CdGetLbn(filename=%08x)",
    "CdReadSector(count=%08x,sector=%08x,buffer=%08x)",
    "CdGetStatus()",
    "bufs_cb_0()",
    "bufs_cb_1()",
    "bufs_cb_2()",
    "bufs_cb_3()",
    "_card_info(port=%08x)",
    "_card_load(port=%08x)",
    "_card_auto(flag=%08x)",
    "bufs_cb_4()",
    "card_write_test(port=%08x)",
    "return 0",
    "return 0",
    "ioabort_raw(param=%08x)",
    "return 0",
    "GetSystemInfo(index=%08x)"
};

static const char* g_psx_cpu_b_kcall_symtable[] = {
    "alloc_kernel_memory(size=%08x)",
    "free_kernel_memory(buf=%08x)",
    "init_timer(t=%08x,reload=%08x,flags=%08x)",
    "get_timer(t=%08x)",
    "enable_timer_irq(t=%08x)",
    "disable_timer_irq(t=%08x)",
    "restart_timer(t=%08x)",
    "DeliverEvent(class=%08x, spec=%08x)",
    "OpenEvent(class=%08x,spec=%08x,mode=%08x,func=%08x)",
    "CloseEvent(event=%08x)",
    "WaitEvent(event=%08x)",
    "TestEvent(event=%08x)",
    "EnableEvent(event=%08x)",
    "DisableEvent(event=%08x)",
    "OpenTh(reg_PC=%08x,reg_SP_FP=%08x,reg_GP=%08x)",
    "CloseTh(handle=%08x)",
    "ChangeTh(handle=%08x)",
    "jump_to_00000000h()",
    "InitPAD2(buf1=%08x,siz1=%08x,buf2=%08x,siz2=%08x)",
    "StartPAD2()",
    "StopPAD2()",
    "PAD_init2(type=%08x,button_dest=%08x,unused=%08x,unused=%08x)",
    "PAD_dr()",
    "ReturnFromException()",
    "ResetEntryInt()",
    "HookEntryInt(addr=%08x)",
    "SystemError()",
    "SystemError()",
    "SystemError()",
    "SystemError()",
    "SystemError()",
    "SystemError()",
    "UnDeliverEvent(class=%08x,spec=%08x)",
    "SystemError()",
    "SystemError()",
    "SystemError()",
    "jump_to_00000000h()",
    "jump_to_00000000h()",
    "jump_to_00000000h()",
    "jump_to_00000000h()",
    "jump_to_00000000h()",
    "jump_to_00000000h()",
    "SystemError()",
    "SystemError()",
    "jump_to_00000000h()",
    "jump_to_00000000h()",
    "jump_to_00000000h()",
    "jump_to_00000000h()",
    "jump_to_00000000h()",
    "jump_to_00000000h()",
    "open(filename=%08x,accessmode=%08x)",
    "lseek(fd=%08x,offset=%08x,seektype=%08x)",
    "read(fd=%08x,dst=%08x,length=%08x)",
    "write(fd=%08x,src=%08x,length=%08x)",
    "close(fd=%08x)",
    "ioctl(fd=%08x,cmd=%08x,arg=%08x)",
    "exit(exitcode=%08x)",
    "isatty(fd=%08x)",
    "getc(fd=%08x)",
    "putc(char=%08x,fd=%08x)",
    "getchar()",
    "putchar(char=%08x)",
    "gets(dst=%08x)",
    "puts(src=%08x)",
    "cd(name=%08x)",
    "format(devicename=%08x)",
    "firstfile2(filename=%08x,direntry=%08x)",
    "nextfile(direntry=%08x)",
    "rename(old_filename=%08x,new_filename=%08x)",
    "erase(filename=%08x)",
    "undelete(filename=%08x)",
    "AddDrv(device_info=%08x)",
    "DelDrv(device_name_lowercase=%08x)",
    "PrintInstalledDevices()",
    "InitCARD2(pad_enable=%08x)",
    "StartCARD2()",
    "StopCARD2()",
    "_card_info_subfunc(port=%08x)",
    "_card_write(port=%08x,sector=%08x,src=%08x)",
    "_card_read(port=%08x,sector=%08x,dst=%08x)",
    "_new_card()",
    "Krom2RawAdd(shiftjis_code=%08x)",
    "SystemError()",
    "Krom2Offset(shiftjis_code=%08x)",
    "_get_errno()",
    "_get_error(fd=%08x)",
    "GetC0Table()",
    "GetB0Table()",
    "_card_chan()",
    "testdevice(devicename=%08x)",
    "SystemError()",
    "ChangeClearPAD(int=%08x)",
    "_card_status(slot=%08x)",
    "_card_wait(slot=%08x)"
};

static const char* g_psx_cpu_c_kcall_symtable[] = {
    "EnqueueTimerAndVblankIrqs(priority=%08x)",
    "EnqueueSyscallHandler(priority=%08x)",
    "SysEnqIntRP(priority=%08x,struc=%08x)",
    "SysDeqIntRP(priority=%08x,struc=%08x)",
    "get_free_EvCB_slot()",
    "get_free_TCB_slot()",
    "ExceptionHandler()",
    "InstallExceptionHandlers()",
    "SysInitMemory(addr=%08x,size=%08x)",
    "SysInitKernelVariables()",
    "ChangeClearRCnt(t=%08x,flag=%08x)",
    "SystemError()",
    "InitDefInt(priority=%08x)",
    "SetIrqAutoAck(irq=%08x,flag=%08x)",
    "return 0",
    "return 0",
    "return 0",
    "return 0",
    "InstallDevices(ttyflag=%08x)",
    "FlushStdInOutPut()",
    "return 0",
    "_cdevinput(circ=%08x,char=%08x)",
    "_cdevscan()",
    "_circgetc(circ=%08x)",
    "_circputc(char=%08x,circ=%08x)",
    "_ioabort(txt1=%08x,txt2=%08x)",
    "set_card_find_mode(mode=%08x)",
    "KernelRedirect(ttyflag=%08x)",
    "AdjustA0Table()",
    "get_card_find_mode()"
};

static std::string readCString(Interconnect& interconnect, uint32_t addr) {
    std::string result;

    while (true) {
        uint8_t ch = interconnect.load<uint8_t>(addr++);

        if (ch == 0)
            break;

        result += static_cast<char>(ch);
    }

    return result;
}

void CPU::checkForTTY() {
    if (pc == 0x000000A0 || pc == 0x000000B0 || pc == 0x000000C0) {
        if (reg(9) == 0x3c) {
            char ch = static_cast<char>(reg(4) & 0xFF);

            if ((ch >= 32 && ch <= 126) || ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ') {
                std::cerr << ch;
            }
        }
    }

    if (pc != 0x000000A0)
        return;

    uint32_t func = reg(9);

    if (func == 0x3e) {
        std::cerr << readCString(interconnect, reg(4)) << "\n";

        pc     = reg(31);
        nextpc = pc + 4;
    } else if (func == 0x3f) {
        uint32_t fmtAddr  = reg(4);
        uint32_t argRegs[3] = { reg(5), reg(6), reg(7) };
        uint32_t sp       = reg(29);
        int      argIndex = 0;

        auto nextArg = [&]() -> uint32_t {
            uint32_t value = argIndex < 3 ? argRegs[argIndex] : interconnect.load<uint32_t>(sp + 0x10 + (argIndex - 3) * 4);
            argIndex++;
            return value;
        };

        uint8_t ch;
        while ((ch = interconnect.load<uint8_t>(fmtAddr++)) != 0) {
            if (ch != '%') {
                std::cerr << static_cast<char>(ch);
                continue;
            }

            std::string spec = "%";
            char        conv = 0;

            for (int i = 0; i < 8; i++) {
                conv = static_cast<char>(interconnect.load<uint8_t>(fmtAddr++));
                spec += conv;

                if (std::string("diouxXcs%").find(conv) != std::string::npos)
                    break;
            }

            char buf[64];

            switch (conv) {
                case '%':
                    std::cerr << '%';
                    break;
                case 'c':
                    std::cerr << static_cast<char>(nextArg());
                    break;
                case 's':
                    std::cerr << readCString(interconnect, nextArg());
                    break;
                case 'd':
                case 'i':
                    std::snprintf(buf, sizeof(buf), spec.c_str(), static_cast<int32_t>(nextArg()));
                    std::cerr << buf;
                    break;
                default:
                    std::snprintf(buf, sizeof(buf), spec.c_str(), nextArg());
                    std::cerr << buf;
                    break;
            }
        }

        pc     = reg(31);
        nextpc = pc + 4;
    } else if (func == 0x3a) {
        std::cerr << "\n=== guest exit(" << static_cast<int32_t>(reg(4)) << ") ===\n";

        paused = true;
        pc     = reg(31);
        nextpc = pc + 4;
    }
}

void CPU::reset() {
    branchSlot = false;
    jumpSlot = false;

    delaySlot = false;
    delayJumpSlot = false;

    currentpc = 0;
    pc = 0xBFC00000;
    nextpc = pc + 4;

    hi = lo = 0;

    loads[0] = {};
    loads[1] = {};

    for(int i = 0; i < 32; i++)
        regs[i] = 0;

    interconnect.reset();
    _cop0.reset();
    gte.reset();
}

int CPU::opj(Instruction& instruction) {
    //if(pc == 0x80031dbc)
    //    return;

    uint32_t i = instruction.jump;

    nextpc = (pc & 0xf0000000) | (i << 2);

    branchSlot = true;
    jumpSlot = true;

    return 1;
}

int CPU::opjr(Instruction& instruction) {
    uint32_t s = instruction.rs;
    uint32_t addr = reg(s);

    branchSlot = true;

    nextpc = addr;
    jumpSlot = true;

    return 1;
}

int CPU::opjal(Instruction& instruction) {
    // Store return address in £31($ra)
    set_reg(31, nextpc);

    return opj(instruction);
}

int CPU::opjalr(Instruction& instruction) {
    uint32_t d = instruction.rd;
    uint32_t s = instruction.rs;

    uint32_t addr = reg(s);

    /**
     * jr/jalr can be used to jump to an unaligned address,
     * in which case an address error (AdEL) exception will be raised on the next instruction fetch.
     */

    branchSlot = true;

    set_reg(d, nextpc);

    nextpc = addr;
    jumpSlot = true;

    return 1;
}

int CPU::opbxx(Instruction& instruction) {
    auto i = instruction.imm_se;
    auto s = instruction.rs;

    uint32_t op = instruction.op;

    bool isbgez = (op >> 16) & 1;

    // It's not enough to test for bit 20 to see if we're supposed
    // to link, if any bit in the range [19:17] is set the link
    // doesn't take place And RA is left untouched.
    bool islink = ((op >> 17) & 0xF) == 0x8;

    int32_t v = static_cast<int32_t>(reg(s));

    // Test "less than zero"
    auto test = (static_cast<uint32_t>(v < 0));

    // If the test is "greater than or equal to zero" we need
    // to negate the comparison above since
    // ("a >= 0" <=> "!(a < 0)"). The xor takes care of that.
    test = test ^ isbgez;

    if(islink) {
        // Store return address in R31 of RA
        set_reg(31, nextpc);
    }

    branchSlot = true;

    if(test != 0) {
        int32_t offset = (int32_t((pc) + (i * 4)));

        branch(offset);

        return 1;
    }

    return 1;
}

int CPU::opbne(Instruction& instruction) {
    auto i = instruction.imm_se;
    uint32_t s = instruction.rs;
    uint32_t t = instruction.rt;

    branchSlot = true;

    // Check if not equal
    if(reg(s) != reg(t)) {
        // Jump to the offset of i
        int32_t offset = (int32_t((pc) + (i * 4)));

        branch(offset);

        return 1;
    }

    return 1;
}

int CPU::opbeq(Instruction& instruction) {
    auto i = instruction.imm_se;
    uint32_t s = instruction.rs;
    uint32_t t = instruction.rt;

    // 0x80031614 -> Checks for GPU bit 14, just always never take it I suppose
    // 0x80031624 -> Checks for another bit from the GPU

    /**
     * Some function:
     * 80031604: LW     v1(3), [0x1f801814]    ; s2(18)=0x1f800000, v1(3)=0x1c4e0000    ; accessing GPU + 0x4
     * 80031608: LW     v0(2), [0x80044e98]    ; s1(17)=0x80040000, v0(2)=0x00000000    ; accessing RAM + 0x44e98
     * 8003160C: SLL    zero(0), zero(0), 0
     * 80031610: BEQ    v0(2), zero(0), func_80031694    ; v0(2)=0x00000000, zero(0)=0x00000000 ? TAKEN
     * 80031614: ANDI   a0(4), v1(3), 0x2000
     * 80031618: BLTZ   v1(3), func_800316a4    ; v1(3)=0x1c4e0000, zero(0)=0x00000000 ? NOT TAKEN
     * 8003161C: SLL    zero(0), zero(0), 0
     * 80031620: BEQ    a0(4), zero(0), func_800316a4    ; a0(4)=0x00000000, zero(0)=0x00000000 ? TAKEN
     * 80031624: SLTIU  v0(2), v0(2), 0x1
     */

    // CPU stuck here?
    //if(pc == 0x80031614) {
    //    printf("");
    //}

    branchSlot = true;

    // 0x801db49c - Checks something from the GPU's bit 24?

    //800316AC: BEQ    zero(0), zero(0), func_80031604    ; zero(0)=0x00000000, zero(0)=0x00000000 ? TAKEN
    if(reg(s) == reg(t) /*&& pc != 0x800316b0*/ /*|| pc == 0x801db49c*/) {
        int32_t offset = (int32_t((pc) + (i * 4)));

        branch(offset);

        return 1;
    }

    return 1;
}

int CPU::opbqtz(Instruction& instruction) {
    auto i = instruction.imm_se;
    uint32_t s = instruction.rs;

    int32_t v = static_cast<int32_t>(reg(s));

    branchSlot = true;

    if(v > 0) {
        int32_t offset = (int32_t((pc) + (i * 4)));

        branch(offset);

        return 1;
    }

    return 1;
}

// TODO; Rename to blez(branch on less than or equal to zero)
int CPU::opblez(Instruction& instruction) {
    auto i = instruction.imm_se;
    auto s = instruction.rs;

    int32_t v = static_cast<int32_t>(reg(s));

    branchSlot = true;

    if(v <= 0) {
        int32_t offset = (int32_t((pc) + (i * 4)));

        branch(offset);

        return 1;
    }

    return 1;
}

void CPU::branch(uint32_t offset) {
    // Offset immediate are always shifted to two places,
    // to the right as 'PC' addresses have to be aligned with 32 bits.
    //offset = offset << 2;

    nextpc = offset;//pc + (offset * 4);

    branchSlot = true;
    jumpSlot = true;
}

int CPU::opadd(Instruction& instruction) {
    auto sReg = instruction.rs;
    auto tReg = instruction.rt;
    auto d = instruction.rd;

    int32_t s = static_cast<int32_t>(reg(sReg));
    int32_t t = static_cast<int32_t>(reg(tReg));

    if (std::optional<int32_t> v = check_add<int32_t>(s, t)) {
        set_reg(d, static_cast<uint32_t>(v.value()));
    } else {
        exception(Overflow);
    }

    return 1;
}

int CPU::opaddu(Instruction& instruction) {
    auto s = instruction.rs;
    auto t = instruction.rt;
    auto d = instruction.rd;

    auto v = (reg(s) + reg(t)) & 0xFFFFFFFF;

    set_reg(d, v);

    return 1;
}

uint32_t CPU::load32(uint32_t addr) {
    // Check if cache is isolated
    //assert((sr & 0x10000) == 0x10000);
    extraCycles += memoryAccessCycles(addr, 4, false);

    uint32_t value = interconnect.load<uint32_t>(addr);
    recordMemoryAccess(addr, value, 4, false);

    return value;
}

uint16_t CPU::load16(uint32_t addr) {
    // Check if cache is isolated
    //assert((sr & 0x10000) == 0x10000);
    extraCycles += memoryAccessCycles(addr, 2, false);

    uint16_t value = interconnect.load<uint16_t>(addr);
    recordMemoryAccess(addr, value, 2, false);

    return value;
}

uint8_t CPU::load8(uint32_t addr) {
    // Check if cache is isolated
    //assert((sr & 0x10000) == 0x10000);
    extraCycles += memoryAccessCycles(addr, 1, false);

    uint8_t value = interconnect.load<uint8_t>(addr);
    recordMemoryAccess(addr, value, 1, false);

    return value;
}

void CPU::store32(uint32_t addr, uint32_t val) {
    // Check if cache is isolated
    if((_cop0.sr & 0x10000) != 0) {
        handleCache(addr, val);
    } else {
        extraCycles += memoryAccessCycles(addr, 4, true);
        interconnect.store<uint32_t>(addr, val);
    }

    recordMemoryAccess(addr, val, 4, true);
}

void CPU::store16(uint32_t addr, uint16_t val) {
    // Check if cache is isolated
    if((_cop0.sr & 0x10000) != 0) {
        handleCache(addr, val);
    } else {
        extraCycles += memoryAccessCycles(addr, 2, true);
        interconnect.store<uint16_t>(addr, val);
    }

    recordMemoryAccess(addr, val, 2, true);
}

void CPU::store8(uint32_t addr, uint8_t val) {
    // Check if cache is isolated
    if((_cop0.sr & 0x10000) != 0) {
        handleCache(addr, val);
    } else {
        extraCycles += memoryAccessCycles(addr, 1, true);
        interconnect.store<uint8_t>(addr, val);
    }

    recordMemoryAccess(addr, val, 1, true);
}

void CPU::handleCache(uint32_t addr, uint32_t val) {
    auto cc = interconnect._cacheControl;

    if(!cc.isCacheEnabled()) {
        printf("Bruh??? This shouldn't happen :(");
        std::cerr << "";
        return;
    }

    if (addr >= 0xA0000000) {
        printf("uhh???\n");
        return;
    }

    // Tag = physical_address[31:12] | valid[3:0]
    uint32_t physical = addr & 0x1FFFFFFF;
    uint32_t tag = physical >> 12;           // addr[31:12]
    uint16_t index = (physical >> 4) & 0xFF; // addr[11:4 ]
    uint8_t word = (physical >> 2) & 3;

    interconnect.icache[index].valid &= ~(1 << word);
}

//https://stackoverflow.com/questions/3944505/detecting-signed-overflow-in-c-c
template <typename T>
std::optional<T> CPU::check_add(T lhs, T rhs) {
    if(lhs >= 0) {
        if(std::numeric_limits<T>::max() - lhs < rhs) {
            return std::nullopt;
        }
    } else {
        // Had std::numeric_limits<T>::max() instead of std::numeric_limits<T>::min()
        if(rhs < std::numeric_limits<T>::min() - lhs) {
            return std::nullopt;
        }
    }
    
    return lhs + rhs;
}

template <typename T>
std::optional<T> CPU::check_sub(T a, T b) {
    if (b >= 0) {
        if (a < std::numeric_limits<T>::min() + b) {
            return std::nullopt;
        }
    } else {
        if (a > std::numeric_limits<T>::max() + b) {
            return std::nullopt;
        }
    }
    
    return a - b;
}
