#pragma once
// ============================================================================
//  InvaderX — disassembler.h
//  Side-effect-free 8080 disassembler. Used for ROM dumps, debug tracing,
//  and the opcode-coverage report (Step 8 deliverable).
// ============================================================================

#include <cstdint>
#include <string>
#include "memory.h"

namespace invaderx {

struct DisasmEntry {
    std::string text;      // human-readable mnemonic + operands
    uint8_t     length;    // 1, 2 or 3 bytes consumed
};

// Disassemble the instruction starting at `addr` in `mem`. The address is
// not modified; the caller advances by `length` bytes to step.
DisasmEntry disassemble(const Memory& mem, uint16_t addr);

// Returns the canonical mnemonic for an opcode WITHOUT formatting operands.
// Used by the opcode-coverage report so the table can be dumped at compile
// time. Always returns a non-empty string for all 256 opcodes (undocumented
// opcodes are listed with a "*" prefix).
const char* opcode_mnemonic(uint8_t opcode);

// Returns the length in bytes for an opcode (1, 2 or 3).
uint8_t opcode_length(uint8_t opcode);

} // namespace invaderx
