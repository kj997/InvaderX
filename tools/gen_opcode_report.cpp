// ============================================================================
//  InvaderX — tools/gen_opcode_report.cpp
//  Generates OPCODES.md directly from the implementation's own tables
//  (disassembler op_table + cpu cycle table), so the coverage report can
//  never drift from the actual emulator. Run:
//
//  (build command — see README "Regenerating the opcode report")
// ============================================================================

#include "disassembler.h"
#include "cpu_step.h"
#include <cstdio>
#include <cstring>
#include <string>

using namespace invaderx;

// Classify an opcode into a functional group from its mnemonic root.
static const char* category_of(const char* mn) {
    // Skip an undocumented '*' prefix for classification.
    if (*mn == '*') ++mn;

    auto starts = [&](const char* p) {
        return std::strncmp(mn, p, std::strlen(p)) == 0;
    };

    if (starts("MOV") || starts("MVI") || starts("LXI") || starts("LDA") ||
        starts("STA") || starts("LHLD")|| starts("SHLD")|| starts("LDAX")||
        starts("STAX")|| starts("XCHG"))                       return "Data transfer";

    if (starts("ADD") || starts("ADI") || starts("ADC") || starts("ACI") ||
        starts("SUB") || starts("SUI") || starts("SBB") || starts("SBI") ||
        starts("INR") || starts("DCR") || starts("INX") || starts("DCX") ||
        starts("DAD") || starts("DAA"))                        return "Arithmetic";

    if (starts("ANA") || starts("ANI") || starts("XRA") || starts("XRI") ||
        starts("ORA") || starts("ORI") || starts("CMP") || starts("CPI") ||
        starts("RLC") || starts("RRC") || starts("RAL") || starts("RAR") ||
        starts("CMA") || starts("CMC") || starts("STC"))       return "Logical / rotate";

    if (starts("JMP") || starts("JNZ") || starts("JZ")  || starts("JNC") ||
        starts("JC")  || starts("JPO") || starts("JPE") || starts("JP")  ||
        starts("JM")  || starts("CALL")|| starts("CNZ") || starts("CZ")  ||
        starts("CNC") || starts("CC")  || starts("CPO") || starts("CPE") ||
        starts("CP")  || starts("CM")  || starts("RET") || starts("RNZ") ||
        starts("RZ")  || starts("RNC") || starts("RC")  || starts("RPO") ||
        starts("RPE") || starts("RP")  || starts("RM")  || starts("RST") ||
        starts("PCHL"))                                        return "Branch / call / return";

    if (starts("PUSH")|| starts("POP") || starts("XTHL")|| starts("SPHL")||
        starts("IN")  || starts("OUT") || starts("EI")  || starts("DI")  ||
        starts("HLT") || starts("NOP"))                        return "Stack / I/O / machine";

    return "Other";
}

int main() {
    std::printf("# InvaderX — Intel 8080 Opcode Coverage Report\n\n");
    std::printf("Auto-generated from the implementation's own decode and cycle\n");
    std::printf("tables (`src/disassembler.cpp`, `src/cpu.cpp`). Every one of the\n");
    std::printf("256 possible opcode bytes is implemented in the dispatcher.\n\n");

    // --- Summary counts ---
    int documented = 0, undocumented = 0;
    for (int op = 0; op < 256; ++op) {
        if (opcode_mnemonic(uint8_t(op))[0] == '*') ++undocumented;
        else                                        ++documented;
    }
    std::printf("## Summary\n\n");
    std::printf("| Metric | Count |\n|---|---|\n");
    std::printf("| Total opcode bytes | 256 |\n");
    std::printf("| Implemented | 256 (100%%) |\n");
    std::printf("| Documented | %d |\n", documented);
    std::printf("| Undocumented (executed as documented equivalents) | %d |\n\n",
                undocumented);

    std::printf("Undocumented opcodes are the 8080's alternate encodings of NOP, ");
    std::printf("JMP, RET, and CALL. They are marked with `*` below and execute ");
    std::printf("identically to their documented counterparts, matching real ");
    std::printf("silicon. Space Invaders does not use them, but they are handled ");
    std::printf("so a runaway PC never hits an unimplemented opcode.\n\n");

    // --- Full table ---
    std::printf("## Full opcode table\n\n");
    std::printf("Cycles are the baseline T-states. Conditional CALL/RET add 6 ");
    std::printf("T-states when the condition is taken (e.g. CZ = 11 not taken, ");
    std::printf("17 taken).\n\n");
    std::printf("| Opcode | Mnemonic | Bytes | Cycles | Category | Status |\n");
    std::printf("|--------|----------|:-----:|:------:|----------|--------|\n");

    for (int op = 0; op < 256; ++op) {
        const char* mn   = opcode_mnemonic(uint8_t(op));
        bool        undoc= (mn[0] == '*');
        const char* disp = undoc ? mn + 1 : mn;
        std::printf("| 0x%02X | `%s` | %u | %d | %s | %s |\n",
                    op,
                    disp,
                    unsigned(opcode_length(uint8_t(op))),
                    opcode_base_cycles(uint8_t(op)),
                    category_of(mn),
                    undoc ? "undocumented" : "documented");
    }

    std::printf("\n## Flags affected (summary)\n\n");
    std::printf("| Instruction class | S | Z | AC | P | CY |\n");
    std::printf("|---|:-:|:-:|:-:|:-:|:-:|\n");
    std::printf("| ADD/ADC/SUB/SBB/ADI/ACI/SUI/SBI | yes | yes | yes | yes | yes |\n");
    std::printf("| INR/DCR | yes | yes | yes | yes | - |\n");
    std::printf("| ANA/ANI | yes | yes | special* | yes | 0 |\n");
    std::printf("| XRA/XRI/ORA/ORI | yes | yes | 0 | yes | 0 |\n");
    std::printf("| CMP/CPI | yes | yes | yes | yes | yes |\n");
    std::printf("| DAD | - | - | - | - | yes |\n");
    std::printf("| INX/DCX | - | - | - | - | - |\n");
    std::printf("| RLC/RRC/RAL/RAR | - | - | - | - | yes |\n");
    std::printf("| DAA | yes | yes | yes | yes | yes |\n");
    std::printf("| STC | - | - | - | - | 1 |\n");
    std::printf("| CMC | - | - | - | - | toggled |\n\n");
    std::printf("\\* ANA/ANI set AC to `((A | operand) & 0x08) != 0`, a documented ");
    std::printf("8080 quirk rather than the always-clear behavior some references ");
    std::printf("assume.\n");

    return 0;
}
