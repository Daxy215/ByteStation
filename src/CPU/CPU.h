#pragma once

#include <optional>
#include <deque>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "Instruction.h"
#include "../Memory/interconnect.h"
#include "COP/Stolen/gte/gte.h"

class Instruction;

// Used for the load register
struct Load {
    uint32_t index;
    uint32_t value;
    
    Load(uint32_t reg, uint32_t val) : index(reg), value(val) {}
    Load() : index(32), value(0) {}
};

struct ExecutionTraceEntry {
    uint32_t pc = 0;
    uint32_t opcode = 0;
    uint32_t target = 0;
    uint64_t cycle = 0;
    bool branch = false;
    bool taken = false;
};

struct MemoryTraceEntry {
    uint32_t pc = 0;
    uint32_t addr = 0;
    uint32_t value = 0;
    uint8_t size = 0;
    uint64_t cycle = 0;
    bool write = false;
};

struct HangAnalysis {
    bool detected = false;
    bool hardLoop = false;
    bool branchLoop = false;
    bool memoryStall = false;
    bool selfJump = false;
    bool hasEffectiveAddress = false;
    uint32_t hotspot = 0;
    uint32_t repeatedTarget = 0;
    uint32_t effectiveAddress = 0;
    uint64_t repeatCount = 0;
    uint64_t stallCycles = 0;
    std::string reason;
    std::string details;
    std::string waitingOn;
    std::string addressDetails;
    std::string condition;
    std::string loopBody;
};

struct DisassembledInstruction {
    std::string text;
    uint32_t opcode = 0;
    uint32_t target = 0;
    bool is_branch = false;
    bool branch_taken = false;
    bool memory_access = false;
    bool writes_memory = false;
    bool reads_memory = false;
};

struct DisassemblerState {
    bool show = false;
    bool follow_pc = true;
    uint32_t view_center = 0;
    uint32_t prev_pc = 0;
    int extraLinesAbove = 0;
    int extraLinesBelow = 0;
    std::unordered_map<uint32_t, DisassembledInstruction> cache;
    char addrBuf[13] = {0};
    char filterBuf[64] = {0};
    int contextLines = 20;
    std::unordered_set<uint32_t> breakpoints;
    std::unordered_set<uint32_t> bookmarks;
    std::unordered_map<uint32_t, std::string> functionLabels;
    std::unordered_set<uint32_t> knownFunctions;
    bool show_address = true;
    bool print_copied_lines_to_console = false;
    bool config_loaded = false;
    bool suppress_breakpoint_once = false;
    uint32_t suppressed_breakpoint_addr = 0;

    std::deque<ExecutionTraceEntry> executionTrace;
    std::unordered_map<uint32_t, uint64_t> executionHits;
    std::unordered_map<uint64_t, uint64_t> edgeHits;
    std::deque<MemoryTraceEntry> memoryTrace;
    uint64_t stableEdge = 0;
    uint64_t stableEdgeCycles = 0;

    HangAnalysis hang;

    uint64_t totalCycles = 0;
    uint32_t lastPC = 0;
    uint32_t stablePC = 0;
    uint64_t stableCycles = 0;

    static constexpr size_t MaxTrace = 8192;
    static constexpr size_t MaxMemoryTrace = 8192;
    static constexpr size_t MaxDisassemblyCache = 4096;
    static constexpr uint64_t TraceSampleInterval = 16;
    static constexpr uint64_t MemoryTraceSampleInterval = 4;
};

inline uint64_t makeEdge(uint32_t a, uint32_t b) {
    return (uint64_t(a) << 32ull) | uint64_t(b);
}

enum Exception {
    /// Interrupt Request
    Interrupt = 0x0,
    /// Address error on load
    LoadAddressError = 0x4,
    /// Address error on store
    StoreAddressError = 0x5,
    /// System call (caused by the SYSCALL opcode)
    SysCall = 0x8,
    /// Breakpoint (caused by the BREAK opcode)
    Break = 0x9,
    /// CPU encountered an unknown instruction
    IllegalInstruction = 0xa,
    /// Unsupported coprocessor operation
    CoprocessorError = 0xb,
    /// Arithmetic overflow
    Overflow = 0xc,
};

class CPU {
    public:
        CPU() : currentpc(0), nextpc(pc + 4), regs() {}
        CPU(Interconnect interconnect) : currentpc(0), nextpc(pc + 4), regs{}, interconnect(std::move(interconnect)) {}
        
        int executeNextInstruction();
        inline int decodeAndExecute(Instruction& instruction) {
            // Gotta decode the instructions using the;
            // Playstation R3000 processor
            // https://en.wikipedia.org/wiki/R3000
            
            // TODO; Handle cycles correctly - Currently all instructions are 2 cycles:
            // https://gist.github.com/allkern/b6ab6db6ac32f1489ad571af6b48ae8b
            
            static constexpr size_t TABLE_SIZE = 64;
            
            using OpHandler = int (CPU::*)(Instruction&);
            
            static OpHandler lookupTable[TABLE_SIZE] = {
                /* 00 */ &CPU::decodeAndExecuteSubFunctions, // SPECIAL
                /* 01 */ &CPU::opbxx,                        // BCOND / REGIMM
                /* 02 */ &CPU::opj,
                /* 03 */ &CPU::opjal,
                /* 04 */ &CPU::opbeq,
                /* 05 */ &CPU::opbne,
                /* 06 */ &CPU::opblez,
                /* 07 */ &CPU::opbqtz,                       // BGTZ
                
                /* 08 */ &CPU::addi,
                /* 09 */ &CPU::addiu,
                /* 0A */ &CPU::opslti,
                /* 0B */ &CPU::opsltiu,
                /* 0C */ &CPU::opandi,
                /* 0D */ &CPU::opori,
                /* 0E */ &CPU::opxori,
                /* 0F */ &CPU::oplui,
                
                /* 10 */ &CPU::opcop0,
                /* 11 */ &CPU::opcop1,
                /* 12 */ &CPU::opcop2,
                /* 13 */ &CPU::opcop3,
                
                /* 14 */ &CPU::opillegal,
                /* 15 */ &CPU::opillegal,
                /* 16 */ &CPU::opillegal,
                /* 17 */ &CPU::opillegal,
                /* 18 */ &CPU::opillegal,
                /* 19 */ &CPU::opillegal,
                /* 1A */ &CPU::opillegal,
                /* 1B */ &CPU::opillegal,
                
                /* 1C */ &CPU::opillegal,
                /* 1D */ &CPU::opillegal,
                /* 1E */ &CPU::opillegal,
                /* 1F */ &CPU::opillegal,
                
                /* 20 */ &CPU::oplb,
                /* 21 */ &CPU::oplh,
                /* 22 */ &CPU::oplwl,
                /* 23 */ &CPU::oplw,
                /* 24 */ &CPU::oplbu,
                /* 25 */ &CPU::oplhu,
                /* 26 */ &CPU::oplwr,
                /* 27 */ &CPU::opillegal,
                
                /* 28 */ &CPU::opsb,
                /* 29 */ &CPU::opsh,
                /* 2A */ &CPU::opswl,
                /* 2B */ &CPU::opsw,
                /* 2C */ &CPU::opillegal,
                /* 2D */ &CPU::opillegal,
                /* 2E */ &CPU::opswr,
                /* 2F */ &CPU::opillegal,
                
                /* 30 */ &CPU::oplwc0,
                /* 31 */ &CPU::oplwc1,
                /* 32 */ &CPU::oplwc2,
                /* 33 */ &CPU::oplwc3,
                /* 34 */ &CPU::opillegal,
                /* 35 */ &CPU::opillegal,
                /* 36 */ &CPU::opillegal,
                /* 37 */ &CPU::opillegal,
                
                /* 38 */ &CPU::opswc0,
                /* 39 */ &CPU::opswc1,
                /* 3A */ &CPU::opswc2,
                /* 3B */ &CPU::opswc3,
                /* 3C */ &CPU::opillegal,
                /* 3D */ &CPU::opillegal,
                /* 3E */ &CPU::opillegal,
                /* 3F */ &CPU::opillegal
            };
            
            int cycles = 0;
            uint32_t func = instruction.func;
            
            OpHandler handler = lookupTable[func];
            if (handler)
                cycles = (this->*handler)(instruction);
            
            return cycles;
        }
        
        int decodeAndExecuteSubFunctions(Instruction& instruction);
        uint32_t fetchInstruction(uint32_t addr);

        int memoryAccessCycles(uint32_t addr, uint8_t size, bool write) const;

        void recordExecution(uint32_t instrPc, Instruction& instruction, bool branch, bool taken, uint32_t target);
        void recordMemoryAccess(uint32_t addr, uint32_t value, uint8_t size, bool write);

        void showDisassembler();
        DisassembledInstruction disassemble(Instruction& inst, uint32_t address);
        std::string getFunctionLabel(uint32_t addr);
        
        bool handleInterrupts(Instruction& instruction);
        bool checkDataWriteBreakpoint(uint32_t addr);
        
        // Instructions
        // TODO; Please move those in a different class future me!
        // Hell no
        
        // Shift Left Logical
        int opsll(Instruction& instruction);
        
        // Shift Left Logical Variable
        int opsllv(Instruction& instruction);
        
        // Shift Right Arithmetic
        int opsra(Instruction& instruction);
        
        // Shift Right Arithmetic Variable
        int opsrav(Instruction& instruction);
        
        // Shift Right Logical
        int opsrl(Instruction& instruction);
        
        // Shift Right Logical Variable
        int opsrlv(Instruction& instruction);
        
        // Set if Less Than Immediate Unsigned
        int opsltiu(Instruction& instruction);
        
        // Load Upper Immediate(LUI)
        int oplui(Instruction& instruction);
        
        // ori $8, $8, 0x243f
        // Puts the result of the "bitwise" or of $8 and 0x243f back into $8
        int opori(Instruction& instruction);
        
        // Bitwise or
        int opor(Instruction& instruction);
        
        // Bitwise not or
        int opnor(Instruction& instruction);
        
        // Bitwise Exclusive Or
        int opxor(Instruction& instruction);
        
        // Bitwise Exclusive or immediate
        int opxori(Instruction& instruction);
        
        // Set on less than unsigned
        int opsltu(Instruction& instruction);
        
        // Set on less than immediate
        int opslti(Instruction& instruction);
        
        // Set on Less Than signed
        int opslt(Instruction& instruction);
        
        // Store Word
        int opsw(Instruction& instruction);
        
        // Store Word Left (Little Endian)
        int opswl(Instruction& instruction);
        
        // Store Word Right (Little Endian)
        int opswr(Instruction& instruction);
        
        // Store Halfword
        int opsh(Instruction& instruction);
        
        // Store byte
        int opsb(Instruction& instruction);
        
        // Load word
        int oplw(Instruction& instruction);
        
        // Load Word Left (Could be unaligned - Little endian)
        int oplwl(Instruction& instruction);
        
        // Load Word Right little endian
        int oplwr(Instruction& instruction);
        
        // Store Word Left little endian
        //void opslw(Instruction& instruction);
        
        // Load Halfword signed
        int oplh(Instruction& instruction);
        
        // Load halfword unsigned
        int oplhu(Instruction& instruction);
        
        // Load byte
        int oplb(Instruction& instruction);
        
        // Load Byte Unsigned
        int oplbu(Instruction& instruction);
        
        // Jump
        int opj(Instruction& instruction);
        
        // Jump register
        int opjr(Instruction& instruction);
        
        // Jump and link
        int opjal(Instruction& instruction);
        
        // Jump and link register
        int opjalr(Instruction& instruction);
        
        // Various branch instructions; BEGZ, BLTZ, BGEZAL, BLTZAL
        // Bits 16 and 20 are used to figure out which one to use.
        int opbxx(Instruction& instruction);
        
        // Branch if not equal
        int opbne(Instruction& instruction);
        
        // Branch if Equal
        int opbeq(Instruction& instruction);
        
        // Branch if greater than zero
        int opbqtz(Instruction& instruction);
        
        // Branch if less than or equal to zero
        int opblez(Instruction& instruction);
        
        // Branch to immediate value 'offset'
        void branch(uint32_t offset);
        
        // Add with expection on overflow
        int opadd(Instruction& instruction);
        
        // Add unsigned
        int opaddu(Instruction& instruction);
        
        // Add Immediate Unsigned
        int addiu(Instruction& instruction);
        
        // Same as addiu but generates an exception if it overflows
        int addi(Instruction& instruction);
        
        // Subtract Unsigned
        int opsubu(Instruction& instruction);
        
        // Subtract and check for signed overflow
        int opsub(Instruction& instruction);
        
        // Multiply Unsigned
        int opmultu(Instruction& instruction);
        
        // Multiply signed
        int opmult(Instruction& instruction);
        
        // Divide signed
        int opdiv(Instruction& instruction);
        
        // Divide Unsigned
        int opdivu(Instruction& instruction);
        
        // Move From HI
        int opmfhi(Instruction& instruction);
        
        // Move to HI
        int opmthi(Instruction& instruction);
        
        // Move from LO
        int opmflo(Instruction& instruction);
        
        // Move to LO
        int opmtlo(Instruction& instruction);
        
        // Load word in COP 0
        int oplwc0(Instruction& instruction);
        
        // Load word in COP 1
        int oplwc1(Instruction& instruction);
        
        // Load word in COP 2
        int oplwc2(Instruction& instruction);
        
        // Load word in COP 3
        int oplwc3(Instruction& instruction);
        
        // Store word in COP 0
        int opswc0(Instruction& instruction);
        
        // Store word in COP 1
        int opswc1(Instruction& instruction);
        
        // Store word in COP 2
        int opswc2(Instruction& instruction);
        
        // Store word in COP 3
        int opswc3(Instruction& instruction);
        
        // Bitwise And
        int opand(Instruction& instruction);
        
        // Bitwise And Immediate
        int opandi(Instruction& instruction);
        
        // Coprocessors handling
        int opcop0(Instruction& instruction);
        
        // Coprocessor 1 opcode (Doesn't exist on the PS1)
        int opcop1(Instruction& instruction);
        
        // Coprocessor 2w opcode (GTE)
        int opcop2(Instruction& instruction);
        
        // Coprocessor 3 opcode (Also doesn't exist on the PS1)
        int opcop3(Instruction& instruction);
        
        // Opcodes for coprocessor 0 - System Control
        // Also Coprocessor handling but only for COP0
        void opmtc0(Instruction& instruction);
        
        // Move from coprocessor 0
        void opmfc0(Instruction& instruction);
        
        // Return from exception
        void oprfe(Instruction& instruction);
        
        // Opcodes for coprocessor 2 - GTE
        void opmfc2(Instruction& instruction);
        void opcfc2(Instruction& instruction);
        void opmtc2(Instruction& instruction);
        void opctc2(Instruction& instruction);
        
        void opgte(Instruction& instruction);
        
        void exception(Exception cause, uint32_t handlerOverride = 0);

        int opSyscall(Instruction& instruction);
        
        int opbreak(Instruction& instruction);
        
        int opillegal(Instruction& instruction);
        
        void checkForTTY();

        void reset();
        
        // Register related functions
        uint32_t reg(uint32_t index) {
            return regs[static_cast<size_t>(index)];
        }
        
        void set_reg(const uint32_t index, const uint32_t val) {
            // We need to always rest R0 to 0
            regs[0] = 0;
            
            if(index == 0)
                return;
            
            regs[index] = val;
            
            //regs[0] = {0};
            
            if(loads[0].index == index) {
                loads[0].index = 32;
            }
        }
        
        void setLoad(uint32_t index, uint32_t val) {
            if(index == 0)
                return;
            
            //load = {index, val};
            if(loads[0].index == index) {
                // Override previous write to the same register
                loads[0].index = 32;
            }
            
            loads[1] = {index, val};
        }

        static bool isCop2Command(uint32_t op) {
            return (op & 0xFE000000u) == 0x4A000000u;
        }
        
        // Memory related functions
        uint32_t load32(uint32_t addr);
        uint16_t load16(uint32_t addr);
        uint8_t  load8(uint32_t addr);
        
        void store32(uint32_t addr, uint32_t val);
        void store16(uint32_t addr, uint16_t val);
        void store8(uint32_t addr, uint8_t val);
        
        void handleCache(uint32_t addr, uint32_t val);
        
        // Helper functions
        template<typename T>
        static std::optional<T> check_add(T a, T b);
        
        template<typename T>
        static std::optional<T> check_sub(T a, T b);
        
    private:
        template<int (CPU::*Fn)(Instruction&)>
        int wrap(CPU& cpu, Instruction& inst) {
            return (cpu.*Fn)(inst);
        }
        
    public:
        // Sets by the current instruction; if a branch occured,
        // and the next instruction will be in the delay slot.
        bool branchSlot = false;
        bool jumpSlot = false;
        
        // If the current instruction executes in the delay slot
        bool delaySlot = false;
        bool delayJumpSlot = false;
        
        // Used for setting the EPC in expections
        uint32_t currentpc;
        
        // PC initial value should be at the beginning of the BIOS
        uint32_t pc = 0xBFC00000;
        
        // Next value of the PC - Used to simulate the branch delay slot
        uint32_t nextpc;
        
        // High register for division remainder and multiplication high
        uint32_t hi = 0;
        
        // Low register for division quotient and multiplication low
        uint32_t lo = 0;

        // For caching missing.. Apparently that's a thing??
        int extraCycles = 0;
        
        // Load initiated by the current instruction
        //Load load = {{0}, 0};
        Load loads[2];
        
        // General purpose registers
        // First entry must always contain a 0.
        uint32_t regs[32];
        
        bool paused = false;
        bool stepRequested = false;
        bool stepUntilBranchTakenRequested = false;
        bool stepUntilBranchNotTakenRequested = false;
        
        DisassemblerState disasmState;
        
    public:
        Interconnect interconnect;
        
        COP0 _cop0;
        
    private:
        //COP2 _cop2;
        GTE gte;
};
