#pragma once
// ============================================================================
//  InvaderX — rom_loader.h
//  Loads the four 2 KB Space Invaders ROM chips from disk into the bus.
//
//  This is a startup-time operation. After it succeeds, the memory bus
//  is in a known-good state and the CPU can begin executing from 0x0000.
// ============================================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include "memory.h"

namespace invaderx {

// ----------------------------------------------------------------------------
//  RomOrder
//      The brief specifies:    e -> 0x0000, f -> 0x0800, g -> 0x1000, h -> 0x1800
//      MAME-standard dumps:    h -> 0x0000, g -> 0x0800, f -> 0x1000, e -> 0x1800
//      Hardware reality:       the four chips sat on the PCB labeled E/F/G/H
//                              but the physical decoder mapped them in MAME
//                              order. Most dumps in the wild follow that.
//
//      Supporting both lets users with either dump set run InvaderX without
//      renaming files. Default is `Spec` (matches project brief).
// ----------------------------------------------------------------------------
enum class RomOrder {
    Spec,   // e at 0x0000 (project specification)
    Mame,   // h at 0x0000 (MAME-standard dumps)
};

// Per-chip load record — populated even on partial failure so the caller
// can show "loaded 2 of 4 chips before failing on invaders.g".
struct ChipLoadInfo {
    const char* filename = nullptr;     // "invaders.e" etc.
    uint16_t    addr     = 0;           // load address
    std::size_t bytes    = 0;           // 0 means "not attempted"
    bool        loaded   = false;       // true iff this chip is in memory
};

struct RomLoadResult {
    bool        ok = false;             // all four chips loaded successfully
    std::string error;                  // empty iff ok
    std::array<ChipLoadInfo, 4> chips{};
};

// ----------------------------------------------------------------------------
//  load_invaders_roms
//      Reads {rom_dir}/invaders.{e,f,g,h} into the memory bus using the
//      ordering selected by `order`. Each chip must be exactly 2048 bytes.
//
//      On success:  result.ok == true, all four chips populated.
//      On failure:  result.ok == false, result.error describes the problem,
//                   and result.chips reflects whichever chips loaded before
//                   the error. Memory contents of failed-or-unattempted
//                   chips are undefined (do NOT execute).
// ----------------------------------------------------------------------------
RomLoadResult load_invaders_roms(
    Memory&            mem,
    const std::string& rom_dir,
    RomOrder           order = RomOrder::Spec);

// ----------------------------------------------------------------------------
//  print_load_report
//      Writes a human-readable summary of the load to stdout. Used by both
//      the test harness and (in Step 6) the main emulator startup.
// ----------------------------------------------------------------------------
void print_load_report(const RomLoadResult& result, RomOrder order);

// ----------------------------------------------------------------------------
//  dump_disassembly
//      Disassembles `count` instructions starting at `start`, writing each
//      to stdout with address + raw bytes + mnemonic. Used to sanity-check
//      the ROM load by eyeballing the boot vector.
// ----------------------------------------------------------------------------
void dump_disassembly(const Memory& mem, uint16_t start, int count);

} // namespace invaderx
