#pragma once
// ============================================================================
//  InvaderX — cpu.h
//  Intel 8080 register file, flag state, and execution-control flags.
//  Pure data; no behavior. The instruction dispatcher (Step 2) operates on it.
// ============================================================================

#include <cstdint>

namespace invaderx {

// ----------------------------------------------------------------------------
//  Flags  — the five 8080 condition flags, plus PSW packing.
// ----------------------------------------------------------------------------
//  The 8080's "PSW" (Program Status Word) is A in the high byte and the flag
//  byte in the low byte. PUSH PSW / POP PSW need the exact bit layout below
//  (Intel 8080 Programmer's Manual, page 22):
//
//     bit 7 6 5 4 3 2 1 0
//         S Z 0 A 0 P 1 C
//                A         (auxiliary carry)
//
//  Bits 5 and 3 are wired to 0; bit 1 is wired to 1. Real silicon honored
//  these even after a POP PSW would have tried to set them otherwise — we
//  replicate that by forcing those bits in pack().
// ----------------------------------------------------------------------------
struct Flags {
    bool s  = false;  // Sign      — copy of bit 7 of the result
    bool z  = false;  // Zero      — set when result == 0
    bool ac = false;  // AuxCarry  — carry out of bit 3 (used by DAA / BCD)
    bool p  = false;  // Parity    — set when result has an EVEN number of 1s
    bool cy = false;  // Carry     — carry out of bit 7 (or bit 15 for DAD)

    // Encode flags into the 8080's on-the-wire byte representation.
    // Used by PUSH PSW (and by the disassembler when dumping state).
    uint8_t pack() const {
        return uint8_t(
              (s  ? 0x80 : 0)  // bit 7 : Sign
            | (z  ? 0x40 : 0)  // bit 6 : Zero
                               // bit 5 : always 0
            | (ac ? 0x10 : 0)  // bit 4 : Auxiliary Carry
                               // bit 3 : always 0
            | (p  ? 0x04 : 0)  // bit 2 : Parity
            | 0x02             // bit 1 : always 1 (hardwired)
            | (cy ? 0x01 : 0)  // bit 0 : Carry
        );
    }

    // Decode the flag byte from a POP PSW. Bits 5/3/1 are ignored on read,
    // matching the silicon behavior.
    void unpack(uint8_t v) {
        s  = (v & 0x80) != 0;
        z  = (v & 0x40) != 0;
        ac = (v & 0x10) != 0;
        p  = (v & 0x04) != 0;
        cy = (v & 0x01) != 0;
    }
};

// ----------------------------------------------------------------------------
//  CPU  — register file + execution state.
// ----------------------------------------------------------------------------
//  Register pair convention on the 8080:
//      BC : B is the HIGH byte, C is the LOW byte
//      DE : D high, E low
//      HL : H high, L low
//  Many opcodes (LXI, DAD, INX, LDAX, STAX, etc.) operate on the pair as a
//  single 16-bit value. We expose pair access via inline helpers rather than
//  type-punned unions to stay strict-aliasing safe and host-endian agnostic.
// ----------------------------------------------------------------------------
struct CPU {
    // --- General-purpose 8-bit registers ---
    uint8_t a = 0;                  // Accumulator — target of ALU ops
    uint8_t b = 0, c = 0;           // BC pair
    uint8_t d = 0, e = 0;           // DE pair
    uint8_t h = 0, l = 0;           // HL pair — also memory pointer (M)

    // --- 16-bit pointers ---
    uint16_t sp = 0;                // Stack Pointer — grows DOWNWARD in 8080
    uint16_t pc = 0;                // Program Counter — boots at 0x0000

    // --- Condition flags ---
    Flags f;

    // --- Execution-control state ---
    bool     int_enable = false;    // INTE latch: EI sets, DI clears.
                                    // When false, the hardware INT line
                                    // is ignored — interrupts queue but
                                    // do not fire.
    bool     halted     = false;    // HLT pauses fetch until an interrupt
                                    // (or RESET) wakes the CPU.
    uint64_t cycles     = 0;        // Running count of T-states executed.
                                    // Step 5's frame pump uses this to
                                    // pace the CPU at ~2 MHz.

    // ------------------------------------------------------------------------
    //  Register-pair accessors.
    //  Forced-inline-friendly; modern compilers fold these into a single
    //  16-bit move on x86. Kept as member functions (not free helpers) so
    //  the call site reads naturally:  cpu.set_hl(cpu.hl() + 1);
    // ------------------------------------------------------------------------
    uint16_t bc() const { return (uint16_t(b) << 8) | c; }
    uint16_t de() const { return (uint16_t(d) << 8) | e; }
    uint16_t hl() const { return (uint16_t(h) << 8) | l; }

    void set_bc(uint16_t v) { b = uint8_t(v >> 8); c = uint8_t(v & 0xFF); }
    void set_de(uint16_t v) { d = uint8_t(v >> 8); e = uint8_t(v & 0xFF); }
    void set_hl(uint16_t v) { h = uint8_t(v >> 8); l = uint8_t(v & 0xFF); }
};

} // namespace invaderx
