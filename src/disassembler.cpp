// ============================================================================
//  InvaderX — src/disassembler.cpp
//  A static 256-entry opcode table drives both opcode_mnemonic() (which
//  returns the bare mnemonic with %B/%W placeholders for operands) and
//  disassemble() (which formats them with actual immediate bytes).
//
//  Placeholders:
//      %B   — 1-byte immediate operand (printed as "0xHH")
//      %W   — 2-byte little-endian immediate operand (printed as "0xHHHH")
//
//  Format of each table entry: { mnemonic-with-placeholders, length-in-bytes }
//
//  Undocumented opcodes are prefixed with "*" so the coverage report makes
//  the distinction obvious. Real silicon executes them as duplicates of
//  documented opcodes (see cpu.cpp).
// ============================================================================

#include "disassembler.h"
#include <cstdio>

namespace invaderx {

struct OpEntry {
    const char* fmt;
    uint8_t     len;
};

static const OpEntry op_table[256] = {
    // 0x00 - 0x0F
    {"NOP",            1}, {"LXI  B, %W",    3}, {"STAX B",        1}, {"INX  B",        1},
    {"INR  B",         1}, {"DCR  B",        1}, {"MVI  B, %B",    2}, {"RLC",           1},
    {"*NOP",           1}, {"DAD  B",        1}, {"LDAX B",        1}, {"DCX  B",        1},
    {"INR  C",         1}, {"DCR  C",        1}, {"MVI  C, %B",    2}, {"RRC",           1},
    // 0x10 - 0x1F
    {"*NOP",           1}, {"LXI  D, %W",    3}, {"STAX D",        1}, {"INX  D",        1},
    {"INR  D",         1}, {"DCR  D",        1}, {"MVI  D, %B",    2}, {"RAL",           1},
    {"*NOP",           1}, {"DAD  D",        1}, {"LDAX D",        1}, {"DCX  D",        1},
    {"INR  E",         1}, {"DCR  E",        1}, {"MVI  E, %B",    2}, {"RAR",           1},
    // 0x20 - 0x2F
    {"*NOP",           1}, {"LXI  H, %W",    3}, {"SHLD %W",       3}, {"INX  H",        1},
    {"INR  H",         1}, {"DCR  H",        1}, {"MVI  H, %B",    2}, {"DAA",           1},
    {"*NOP",           1}, {"DAD  H",        1}, {"LHLD %W",       3}, {"DCX  H",        1},
    {"INR  L",         1}, {"DCR  L",        1}, {"MVI  L, %B",    2}, {"CMA",           1},
    // 0x30 - 0x3F
    {"*NOP",           1}, {"LXI  SP, %W",   3}, {"STA  %W",       3}, {"INX  SP",       1},
    {"INR  M",         1}, {"DCR  M",        1}, {"MVI  M, %B",    2}, {"STC",           1},
    {"*NOP",           1}, {"DAD  SP",       1}, {"LDA  %W",       3}, {"DCX  SP",       1},
    {"INR  A",         1}, {"DCR  A",        1}, {"MVI  A, %B",    2}, {"CMC",           1},
    // 0x40 - 0x4F : MOV B,r and MOV C,r
    {"MOV  B, B",      1}, {"MOV  B, C",     1}, {"MOV  B, D",     1}, {"MOV  B, E",     1},
    {"MOV  B, H",      1}, {"MOV  B, L",     1}, {"MOV  B, M",     1}, {"MOV  B, A",     1},
    {"MOV  C, B",      1}, {"MOV  C, C",     1}, {"MOV  C, D",     1}, {"MOV  C, E",     1},
    {"MOV  C, H",      1}, {"MOV  C, L",     1}, {"MOV  C, M",     1}, {"MOV  C, A",     1},
    // 0x50 - 0x5F : MOV D,r and MOV E,r
    {"MOV  D, B",      1}, {"MOV  D, C",     1}, {"MOV  D, D",     1}, {"MOV  D, E",     1},
    {"MOV  D, H",      1}, {"MOV  D, L",     1}, {"MOV  D, M",     1}, {"MOV  D, A",     1},
    {"MOV  E, B",      1}, {"MOV  E, C",     1}, {"MOV  E, D",     1}, {"MOV  E, E",     1},
    {"MOV  E, H",      1}, {"MOV  E, L",     1}, {"MOV  E, M",     1}, {"MOV  E, A",     1},
    // 0x60 - 0x6F : MOV H,r and MOV L,r
    {"MOV  H, B",      1}, {"MOV  H, C",     1}, {"MOV  H, D",     1}, {"MOV  H, E",     1},
    {"MOV  H, H",      1}, {"MOV  H, L",     1}, {"MOV  H, M",     1}, {"MOV  H, A",     1},
    {"MOV  L, B",      1}, {"MOV  L, C",     1}, {"MOV  L, D",     1}, {"MOV  L, E",     1},
    {"MOV  L, H",      1}, {"MOV  L, L",     1}, {"MOV  L, M",     1}, {"MOV  L, A",     1},
    // 0x70 - 0x7F : MOV M,r , HLT , MOV A,r
    {"MOV  M, B",      1}, {"MOV  M, C",     1}, {"MOV  M, D",     1}, {"MOV  M, E",     1},
    {"MOV  M, H",      1}, {"MOV  M, L",     1}, {"HLT",           1}, {"MOV  M, A",     1},
    {"MOV  A, B",      1}, {"MOV  A, C",     1}, {"MOV  A, D",     1}, {"MOV  A, E",     1},
    {"MOV  A, H",      1}, {"MOV  A, L",     1}, {"MOV  A, M",     1}, {"MOV  A, A",     1},
    // 0x80 - 0x8F : ADD r, ADC r
    {"ADD  B",         1}, {"ADD  C",        1}, {"ADD  D",        1}, {"ADD  E",        1},
    {"ADD  H",         1}, {"ADD  L",        1}, {"ADD  M",        1}, {"ADD  A",        1},
    {"ADC  B",         1}, {"ADC  C",        1}, {"ADC  D",        1}, {"ADC  E",        1},
    {"ADC  H",         1}, {"ADC  L",        1}, {"ADC  M",        1}, {"ADC  A",        1},
    // 0x90 - 0x9F : SUB r, SBB r
    {"SUB  B",         1}, {"SUB  C",        1}, {"SUB  D",        1}, {"SUB  E",        1},
    {"SUB  H",         1}, {"SUB  L",        1}, {"SUB  M",        1}, {"SUB  A",        1},
    {"SBB  B",         1}, {"SBB  C",        1}, {"SBB  D",        1}, {"SBB  E",        1},
    {"SBB  H",         1}, {"SBB  L",        1}, {"SBB  M",        1}, {"SBB  A",        1},
    // 0xA0 - 0xAF : ANA r, XRA r
    {"ANA  B",         1}, {"ANA  C",        1}, {"ANA  D",        1}, {"ANA  E",        1},
    {"ANA  H",         1}, {"ANA  L",        1}, {"ANA  M",        1}, {"ANA  A",        1},
    {"XRA  B",         1}, {"XRA  C",        1}, {"XRA  D",        1}, {"XRA  E",        1},
    {"XRA  H",         1}, {"XRA  L",        1}, {"XRA  M",        1}, {"XRA  A",        1},
    // 0xB0 - 0xBF : ORA r, CMP r
    {"ORA  B",         1}, {"ORA  C",        1}, {"ORA  D",        1}, {"ORA  E",        1},
    {"ORA  H",         1}, {"ORA  L",        1}, {"ORA  M",        1}, {"ORA  A",        1},
    {"CMP  B",         1}, {"CMP  C",        1}, {"CMP  D",        1}, {"CMP  E",        1},
    {"CMP  H",         1}, {"CMP  L",        1}, {"CMP  M",        1}, {"CMP  A",        1},
    // 0xC0 - 0xCF
    {"RNZ",            1}, {"POP  B",        1}, {"JNZ  %W",       3}, {"JMP  %W",       3},
    {"CNZ  %W",        3}, {"PUSH B",        1}, {"ADI  %B",       2}, {"RST  0",        1},
    {"RZ",             1}, {"RET",           1}, {"JZ   %W",       3}, {"*JMP %W",       3},
    {"CZ   %W",        3}, {"CALL %W",       3}, {"ACI  %B",       2}, {"RST  1",        1},
    // 0xD0 - 0xDF
    {"RNC",            1}, {"POP  D",        1}, {"JNC  %W",       3}, {"OUT  %B",       2},
    {"CNC  %W",        3}, {"PUSH D",        1}, {"SUI  %B",       2}, {"RST  2",        1},
    {"RC",             1}, {"*RET",          1}, {"JC   %W",       3}, {"IN   %B",       2},
    {"CC   %W",        3}, {"*CALL %W",      3}, {"SBI  %B",       2}, {"RST  3",        1},
    // 0xE0 - 0xEF
    {"RPO",            1}, {"POP  H",        1}, {"JPO  %W",       3}, {"XTHL",          1},
    {"CPO  %W",        3}, {"PUSH H",        1}, {"ANI  %B",       2}, {"RST  4",        1},
    {"RPE",            1}, {"PCHL",          1}, {"JPE  %W",       3}, {"XCHG",          1},
    {"CPE  %W",        3}, {"*CALL %W",      3}, {"XRI  %B",       2}, {"RST  5",        1},
    // 0xF0 - 0xFF
    {"RP",             1}, {"POP  PSW",      1}, {"JP   %W",       3}, {"DI",            1},
    {"CP   %W",        3}, {"PUSH PSW",      1}, {"ORI  %B",       2}, {"RST  6",        1},
    {"RM",             1}, {"SPHL",          1}, {"JM   %W",       3}, {"EI",            1},
    {"CM   %W",        3}, {"*CALL %W",      3}, {"CPI  %B",       2}, {"RST  7",        1},
};

const char* opcode_mnemonic(uint8_t opcode) {
    return op_table[opcode].fmt;
}

uint8_t opcode_length(uint8_t opcode) {
    return op_table[opcode].len;
}

DisasmEntry disassemble(const Memory& mem, uint16_t addr) {
    uint8_t      op    = mem.read(addr);
    const auto&  entry = op_table[op];
    DisasmEntry  out;
    out.length = entry.len;

    // Format the mnemonic, replacing %B / %W with operand bytes.
    char buf[64];
    const char* fmt = entry.fmt;
    char*       dst = buf;
    char* const end = buf + sizeof(buf);

    while (*fmt && dst < end - 8) {
        if (fmt[0] == '%' && fmt[1] == 'B') {
            uint8_t imm = mem.read(uint16_t(addr + 1));
            int n = std::snprintf(dst, size_t(end - dst), "0x%02X", imm);
            if (n > 0) dst += n;
            fmt += 2;
        } else if (fmt[0] == '%' && fmt[1] == 'W') {
            uint16_t imm = mem.read16(uint16_t(addr + 1));
            int n = std::snprintf(dst, size_t(end - dst), "0x%04X", imm);
            if (n > 0) dst += n;
            fmt += 2;
        } else {
            *dst++ = *fmt++;
        }
    }
    *dst = '\0';
    out.text = buf;
    return out;
}

} // namespace invaderx
