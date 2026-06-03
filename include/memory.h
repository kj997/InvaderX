#pragma once
// ============================================================================
//  InvaderX — memory.h
//  Flat 64 KB address space with ROM/RAM region constants and a
//  write-protected bus interface.
// ============================================================================

#include <array>
#include <cstdint>

namespace invaderx {

// ----------------------------------------------------------------------------
//  Memory map (Space Invaders, 1978 Taito arcade board)
// ----------------------------------------------------------------------------
//
//      0x0000 ─┬─ ROM ──── invaders.e   (2 KB)   ┐
//      0x0800 ─┼─ ROM ──── invaders.f   (2 KB)   │ 8 KB total ROM —
//      0x1000 ─┼─ ROM ──── invaders.g   (2 KB)   │ read-only after load.
//      0x1800 ─┼─ ROM ──── invaders.h   (2 KB)   ┘
//      0x2000 ─┼─ RAM ──── work RAM     (1 KB)     stack, game variables
//      0x2400 ─┼─ RAM ──── VIDEO RAM    (7 KB)     1 bpp framebuffer
//      0x4000 ─┴─ unused / mirror of 0x0000-0x3FFF on real PCB
//
//  Spec note: this file uses the ROM ordering specified in the project
//  brief (e -> 0x0000, f -> 0x0800, g -> 0x1000, h -> 0x1800). Some ROM
//  dumps in the wild use the REVERSE ordering (h at 0x0000). The README
//  in Step 8 will document this prominently so users with either dump
//  set know what to do.
//
//  The real PCB only decodes 14 address lines for RAM, so 0x4000-0xFFFF
//  is undefined territory. We still allocate the full 64 KB because
//  (a) range checks become trivial — every uint16_t is a valid index,
//  (b) the cost is 56 KB of zeroed BSS — negligible,
//  (c) it makes the disassembler robust against runaway PCs.
// ----------------------------------------------------------------------------
namespace mem {
    // --- ROM region (all four chips together) ---
    constexpr uint16_t ROM_BASE      = 0x0000;
    constexpr uint16_t ROM_END       = 0x1FFF;   // inclusive
    constexpr uint16_t ROM_SIZE      = 0x2000;   // 8 KB

    // --- Per-chip load addresses (per project specification) ---
    constexpr uint16_t ROM_E_ADDR    = 0x0000;
    constexpr uint16_t ROM_F_ADDR    = 0x0800;
    constexpr uint16_t ROM_G_ADDR    = 0x1000;
    constexpr uint16_t ROM_H_ADDR    = 0x1800;
    constexpr uint16_t ROM_BANK_SIZE = 0x0800;   // 2 KB per chip

    // --- Work RAM (stack + game state) ---
    constexpr uint16_t WRAM_BASE     = 0x2000;
    constexpr uint16_t WRAM_END      = 0x23FF;

    // --- Video RAM ---
    //  256 columns × 224 rows ÷ 8 bits-per-byte = 7168 bytes = 0x1C00.
    //  See display.h for the rotation/scan layout.
    constexpr uint16_t VRAM_BASE     = 0x2400;
    constexpr uint16_t VRAM_END      = 0x3FFF;
    constexpr uint16_t VRAM_SIZE     = 0x1C00;   // 7168 bytes

    constexpr uint32_t ADDR_SPACE    = 0x10000;  // 64 KB
} // namespace mem

// ----------------------------------------------------------------------------
//  Memory  — the bus.
//  read()  : unrestricted; the CPU and renderer both go through here.
//  write() : enforces ROM read-only-ness; returns false on stray writes
//            so the debugger can flag them. Real silicon silently drops
//            them, which we replicate (no exception, no abort).
//  load_byte() : back-door used by the ROM loader at boot. Bypasses the
//                ROM guard so the .e/.f/.g/.h files actually land in ROM.
// ----------------------------------------------------------------------------
struct Memory {
    // Compile-time-sized, no heap allocation, zero-initialized.
    std::array<uint8_t, mem::ADDR_SPACE> bytes{};

    uint8_t read(uint16_t addr) const {
        return bytes[addr];
    }

    bool write(uint16_t addr, uint8_t value) {
        if (addr <= mem::ROM_END) {
            return false;       // Drop stray writes to ROM, like real HW.
        }
        bytes[addr] = value;
        return true;
    }

    // 16-bit little-endian read (8080 stores LSB first).
    // Used by LXI, JMP, CALL, etc. — defined here so the dispatcher
    // doesn't have to manually combine bytes in 20 different opcodes.
    uint16_t read16(uint16_t addr) const {
        return uint16_t(bytes[addr]) | (uint16_t(bytes[addr + 1]) << 8);
    }

    // Unguarded write — ONLY for ROM loader use at boot.
    void load_byte(uint16_t addr, uint8_t value) {
        bytes[addr] = value;
    }
};

} // namespace invaderx
