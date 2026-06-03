// ============================================================================
//  InvaderX — src/rom_loader.cpp
//
//  ROM-loading strategy:
//    1. Resolve each chip filename to a path under `rom_dir`.
//    2. Open binary; bail with a clear error if the file is missing.
//    3. Verify the file is EXACTLY 2048 bytes; bail if not.
//       (Most ROM-loading bugs in the wild come from accepting truncated
//       or padded dumps and then chasing ghost JMP targets at runtime.)
//    4. Read straight into mem.bytes[addr..addr+2047] via load_byte().
//    5. Stop at first error; do not partially clobber further chips.
//
//  Why not just <cstdio> fread? std::filesystem gives us portable path
//  joining (works for "./roms" and "C:\\Users\\me\\roms" without manual
//  separator handling) and std::ifstream's binary mode is well-defined.
// ============================================================================

#include "rom_loader.h"
#include "disassembler.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace invaderx {

namespace fs = std::filesystem;

// ----------------------------------------------------------------------------
//  Fixed filenames (per project specification).
//  Listed in chip-letter order; the address each one maps to depends on
//  the RomOrder argument, not the index here.
// ----------------------------------------------------------------------------
static const char* const ROM_FILENAMES[4] = {
    "invaders.e",   // index 0
    "invaders.f",   // index 1
    "invaders.g",   // index 2
    "invaders.h",   // index 3
};

// Resolve chip index -> load address based on the requested ordering.
// Spec order:  index 0 (e) -> 0x0000, 1 -> 0x0800, 2 -> 0x1000, 3 -> 0x1800
// MAME order:  index 0 (e) -> 0x1800, 1 -> 0x1000, 2 -> 0x0800, 3 -> 0x0000
static uint16_t chip_addr(int chip_index, RomOrder order) {
    const uint16_t spec_addrs[4] = { 0x0000, 0x0800, 0x1000, 0x1800 };
    const uint16_t mame_addrs[4] = { 0x1800, 0x1000, 0x0800, 0x0000 };
    return (order == RomOrder::Spec) ? spec_addrs[chip_index]
                                     : mame_addrs[chip_index];
}

// ----------------------------------------------------------------------------
//  Load a single chip file into memory at `addr`.
//  Returns true on success; populates `info` either way.
// ----------------------------------------------------------------------------
static bool load_one_chip(Memory&       mem,
                          const fs::path& path,
                          uint16_t        addr,
                          ChipLoadInfo&   info,
                          std::string&    err) {
    info.addr     = addr;
    info.filename = ROM_FILENAMES[0]; // placeholder; caller overwrites
    info.bytes    = 0;
    info.loaded   = false;

    // --- 1. existence ---
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        err = "ROM file not found: " + path.string();
        return false;
    }

    // --- 2. size check (must be exactly 2 KB) ---
    auto sz = fs::file_size(path, ec);
    if (ec) {
        err = "Cannot stat ROM: " + path.string() + " (" + ec.message() + ")";
        return false;
    }
    if (sz != mem::ROM_BANK_SIZE) {
        err = "ROM has wrong size: " + path.string()
            + " (expected " + std::to_string(mem::ROM_BANK_SIZE)
            + " bytes, got "  + std::to_string(sz) + ")";
        return false;
    }

    // --- 3. open and read ---
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "Cannot open ROM for reading: " + path.string();
        return false;
    }

    // 2 KB stack buffer — no heap allocation for the load.
    char buf[mem::ROM_BANK_SIZE];
    if (!f.read(buf, mem::ROM_BANK_SIZE)) {
        err = "Short read on ROM: " + path.string();
        return false;
    }

    // --- 4. stream into memory through the back-door API ---
    //  load_byte() bypasses the ROM write guard intentionally — that's
    //  the whole reason it exists. Regular write() would silently drop
    //  every single byte we just read.
    for (std::size_t i = 0; i < mem::ROM_BANK_SIZE; ++i) {
        mem.load_byte(uint16_t(addr + i), uint8_t(buf[i]));
    }

    info.bytes  = mem::ROM_BANK_SIZE;
    info.loaded = true;
    return true;
}

// ----------------------------------------------------------------------------
//  Public entry — loads all four chips, stops on first error.
// ----------------------------------------------------------------------------
RomLoadResult load_invaders_roms(Memory&            mem,
                                 const std::string& rom_dir,
                                 RomOrder           order) {
    RomLoadResult res{};
    res.ok = false;

    for (int i = 0; i < 4; ++i) {
        res.chips[i].filename = ROM_FILENAMES[i];
        res.chips[i].addr     = chip_addr(i, order);

        fs::path     p   = fs::path(rom_dir) / ROM_FILENAMES[i];
        std::string  err;
        if (!load_one_chip(mem, p, chip_addr(i, order), res.chips[i], err)) {
            res.error = err;
            res.chips[i].filename = ROM_FILENAMES[i];  // restore after load_one_chip wrote a placeholder
            return res;
        }
        res.chips[i].filename = ROM_FILENAMES[i];      // re-stamp for safety
    }

    res.ok = true;
    return res;
}

// ----------------------------------------------------------------------------
//  Pretty-print the result of a load attempt.
// ----------------------------------------------------------------------------
void print_load_report(const RomLoadResult& result, RomOrder order) {
    const char* order_name = (order == RomOrder::Spec) ? "Spec (brief)" : "MAME";
    std::printf("ROM load report  [order = %s]\n", order_name);
    std::printf("  chip       address   bytes   status\n");
    std::printf("  ---------  --------  ------  --------\n");
    for (const auto& c : result.chips) {
        std::printf("  %-9s  0x%04X    %6zu  %s\n",
                    c.filename ? c.filename : "?",
                    unsigned(c.addr),
                    c.bytes,
                    c.loaded ? "OK" : "MISS");
    }
    if (result.ok) {
        std::printf("  -> all four chips loaded (8192 bytes total)\n");
    } else {
        std::printf("  -> FAILED: %s\n", result.error.c_str());
    }
}

// ----------------------------------------------------------------------------
//  Dump a few disassembled instructions starting at `start`.
//  Used as a smoke test: if the boot vector looks like real 8080 code
//  (NOPs, JMPs, LXI SP, MVI loads), the load almost certainly worked.
// ----------------------------------------------------------------------------
void dump_disassembly(const Memory& mem, uint16_t start, int count) {
    uint16_t pc = start;
    for (int n = 0; n < count; ++n) {
        DisasmEntry d = disassemble(mem, pc);

        std::printf("  0x%04X  ", unsigned(pc));
        // Raw bytes, right-padded so the mnemonic column lines up.
        for (int i = 0; i < 3; ++i) {
            if (i < d.length) std::printf("%02X ", unsigned(mem.read(uint16_t(pc + i))));
            else              std::printf("   ");
        }
        std::printf(" %s\n", d.text.c_str());
        pc = uint16_t(pc + d.length);
    }
}

} // namespace invaderx
