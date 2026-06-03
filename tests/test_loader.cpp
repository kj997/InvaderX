// ============================================================================
//  InvaderX — src/test_loader.cpp
//  Validation harness for rom_loader.cpp + memory.h.
//
//  We don't have the real Space Invaders ROMs (they're still copyrighted
//  by Taito), so we generate SYNTHETIC 2 KB files with chip-distinct
//  byte patterns. That lets us prove:
//
//    L1  Happy-path load in Spec order: each chip lands at the right addr.
//    L2  Happy-path load in MAME order: same files, swapped addresses.
//    L3  Missing file -> structured error.
//    L4  Wrong size  -> structured error.
//    L5  ROM write protection: writes to 0x0000-0x1FFF are dropped.
//    L6  RAM accepts writes everywhere in 0x2000-0x3FFF.
//    L7  read16 is little-endian.
//    L8  Boot-disassembly dump produces sensible output on chip E.
// ============================================================================

#include "cpu.h"
#include "memory.h"
#include "rom_loader.h"
#include "disassembler.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace invaderx;
namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do {                                                 \
    if (cond) { ++g_pass; }                                              \
    else      { ++g_fail; std::printf("  FAIL %s:%d  %s\n",              \
                                      __FILE__, __LINE__, #cond); }      \
} while (0)

// ----------------------------------------------------------------------------
//  Synthetic ROM generation
//  Chip E gets a tiny "real-looking" boot sequence at offset 0; the rest of
//  E and the other chips are filled with chip-distinct byte patterns so we
//  can prove the address mapping is correct.
// ----------------------------------------------------------------------------
static const uint8_t E_BOOT[] = {
    0x00, 0x00, 0x00,              // NOP NOP NOP — classic boot pad
    0xC3, 0x00, 0x18,              // JMP 0x1800   — jump into chip H
    0x31, 0x00, 0x24,              // LXI SP, 0x2400 (top of WRAM)
    0x3E, 0x00,                    // MVI A, 0x00
    0xCD, 0x10, 0x18,              // CALL 0x1810
    0xFB,                          // EI
    0x76,                          // HLT (waits for interrupt)
};

static void make_rom(const fs::path& path, uint8_t fill, const uint8_t* prefix, std::size_t prefix_len) {
    std::vector<uint8_t> buf(mem::ROM_BANK_SIZE, fill);
    if (prefix && prefix_len <= buf.size()) {
        std::copy(prefix, prefix + prefix_len, buf.begin());
    }
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
}

static void make_synthetic_set(const fs::path& dir) {
    fs::create_directories(dir);
    make_rom(dir / "invaders.e", 0xE5, E_BOOT, sizeof(E_BOOT));  // chip E
    make_rom(dir / "invaders.f", 0xF5, nullptr, 0);              // chip F
    make_rom(dir / "invaders.g", 0x65, nullptr, 0);              // chip G ('g')
    make_rom(dir / "invaders.h", 0x85, nullptr, 0);              // chip H
}

static void make_truncated_set(const fs::path& dir) {
    fs::create_directories(dir);
    make_rom(dir / "invaders.e", 0xE5, E_BOOT, sizeof(E_BOOT));
    make_rom(dir / "invaders.f", 0xF5, nullptr, 0);
    make_rom(dir / "invaders.g", 0x65, nullptr, 0);
    // invaders.h is intentionally truncated to 1 KB.
    std::vector<uint8_t> buf(mem::ROM_BANK_SIZE / 2, 0x85);
    std::ofstream f(dir / "invaders.h", std::ios::binary);
    f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
}

// ----------------------------------------------------------------------------
//  L1 — Happy path, Spec order.
// ----------------------------------------------------------------------------
static void L1_spec_order(const fs::path& dir) {
    std::printf("L1  load spec order\n");
    Memory m;
    auto res = load_invaders_roms(m, dir.string(), RomOrder::Spec);
    CHECK(res.ok);
    CHECK(res.error.empty());

    // Chip E filler is 0xE5 -> first byte AFTER the boot prefix at 0x0000
    // is at offset sizeof(E_BOOT). Boot pattern verified separately.
    CHECK(m.read(uint16_t(sizeof(E_BOOT))) == 0xE5);   // tail of chip E
    CHECK(m.read(0x07FF) == 0xE5);                     // last byte of chip E
    CHECK(m.read(0x0800) == 0xF5);                     // start of chip F
    CHECK(m.read(0x0FFF) == 0xF5);                     // end   of chip F
    CHECK(m.read(0x1000) == 0x65);                     // start of chip G
    CHECK(m.read(0x17FF) == 0x65);                     // end   of chip G
    CHECK(m.read(0x1800) == 0x85);                     // start of chip H
    CHECK(m.read(0x1FFF) == 0x85);                     // end   of chip H

    // Boot prefix is intact at 0x0000.
    CHECK(m.read(0x0000) == 0x00);
    CHECK(m.read(0x0003) == 0xC3);
    CHECK(m.read(0x0004) == 0x00);
    CHECK(m.read(0x0005) == 0x18);

    // Per-chip metadata is correct.
    CHECK(std::string(res.chips[0].filename) == "invaders.e");
    CHECK(res.chips[0].addr == 0x0000);
    CHECK(res.chips[0].loaded);
    CHECK(res.chips[3].filename == std::string("invaders.h"));
    CHECK(res.chips[3].addr == 0x1800);
}

// ----------------------------------------------------------------------------
//  L2 — Happy path, MAME order. Same files, swapped addresses.
// ----------------------------------------------------------------------------
static void L2_mame_order(const fs::path& dir) {
    std::printf("L2  load MAME order\n");
    Memory m;
    auto res = load_invaders_roms(m, dir.string(), RomOrder::Mame);
    CHECK(res.ok);

    // chip H -> 0x0000, chip G -> 0x0800, chip F -> 0x1000, chip E -> 0x1800
    CHECK(m.read(0x0000) == 0x85);   // chip H
    CHECK(m.read(0x0800) == 0x65);   // chip G
    CHECK(m.read(0x1000) == 0xF5);   // chip F
    // For chip E: first bytes are the boot prefix, then 0xE5 filler.
    CHECK(m.read(0x1800) == 0x00);                                // chip E start (NOP)
    CHECK(m.read(0x1803) == 0xC3);                                // JMP opcode
    CHECK(m.read(uint16_t(0x1800 + sizeof(E_BOOT))) == 0xE5);     // chip E filler
    CHECK(m.read(0x1FFF) == 0xE5);                                // chip E end
}

// ----------------------------------------------------------------------------
//  L3 — Missing file. Loader should fail cleanly, not crash or partial-load.
// ----------------------------------------------------------------------------
static void L3_missing(const fs::path& empty_dir) {
    std::printf("L3  missing file -> structured error\n");
    fs::create_directories(empty_dir);
    Memory m;
    auto res = load_invaders_roms(m, empty_dir.string());
    CHECK(!res.ok);
    CHECK(!res.error.empty());
    CHECK(res.error.find("not found") != std::string::npos
       || res.error.find("Cannot")    != std::string::npos);
    // No chip should be marked loaded.
    for (const auto& c : res.chips) CHECK(!c.loaded);
}

// ----------------------------------------------------------------------------
//  L4 — Wrong size. invaders.h is 1 KB; loader must reject.
// ----------------------------------------------------------------------------
static void L4_wrong_size(const fs::path& dir) {
    std::printf("L4  wrong-size file -> structured error\n");
    Memory m;
    auto res = load_invaders_roms(m, dir.string());
    CHECK(!res.ok);
    CHECK(res.error.find("wrong size") != std::string::npos);
    // First three chips should be marked loaded (they were fine).
    CHECK(res.chips[0].loaded);
    CHECK(res.chips[1].loaded);
    CHECK(res.chips[2].loaded);
    CHECK(!res.chips[3].loaded);
}

// ----------------------------------------------------------------------------
//  L5 — ROM write protection covers the entire 0x0000-0x1FFF range.
//        Spot-check every 256 bytes plus the boundary at 0x1FFF / 0x2000.
// ----------------------------------------------------------------------------
static void L5_rom_write_protect(const fs::path& dir) {
    std::printf("L5  ROM write protection (full 0x0000-0x1FFF sweep)\n");
    Memory m;
    auto res = load_invaders_roms(m, dir.string());
    CHECK(res.ok);

    // Sample every 256 bytes across the ROM region.
    for (uint32_t a = 0x0000; a <= 0x1FFF; a += 0x100) {
        uint8_t before = m.read(uint16_t(a));
        bool wrote = m.write(uint16_t(a), uint8_t(before ^ 0xFF));
        CHECK(!wrote);                            // write must be REJECTED
        CHECK(m.read(uint16_t(a)) == before);     // byte must be UNCHANGED
    }
    // Boundary: 0x1FFF is still ROM, 0x2000 is RAM.
    CHECK(!m.write(0x1FFF, 0xAA));
    CHECK( m.write(0x2000, 0xAA));
    CHECK( m.read(0x2000) == 0xAA);
}

// ----------------------------------------------------------------------------
//  L6 — RAM is writable across its full range (0x2000-0x3FFF).
// ----------------------------------------------------------------------------
static void L6_ram_writable() {
    std::printf("L6  RAM writable across 0x2000-0x3FFF\n");
    Memory m;
    for (uint32_t a = 0x2000; a <= 0x3FFF; a += 0x100) {
        bool wrote = m.write(uint16_t(a), uint8_t(a & 0xFF));
        CHECK(wrote);
        CHECK(m.read(uint16_t(a)) == uint8_t(a & 0xFF));
    }
    // VRAM range specifically — Step 5's renderer will read these.
    CHECK(m.write(mem::VRAM_BASE,           0xCC));
    CHECK(m.write(uint16_t(mem::VRAM_END), 0xDD));
}

// ----------------------------------------------------------------------------
//  L7 — read16 is little-endian (LSB at lower address). The CPU dispatcher
//        relies on this for LXI / JMP / CALL operand decode.
// ----------------------------------------------------------------------------
static void L7_endianness() {
    std::printf("L7  read16 little-endian\n");
    Memory m;
    m.write(0x2000, 0x34);   // low byte
    m.write(0x2001, 0x12);   // high byte
    CHECK(m.read16(0x2000) == 0x1234);
}

// ----------------------------------------------------------------------------
//  L8 — Disassemble the boot vector. Must produce real-looking 8080 code
//        that matches what we encoded in E_BOOT.
// ----------------------------------------------------------------------------
static void L8_boot_disasm(const fs::path& dir) {
    std::printf("L8  boot disassembly dump\n");
    Memory m;
    auto res = load_invaders_roms(m, dir.string());
    CHECK(res.ok);

    // Verify the first six disassembled instructions match E_BOOT.
    struct { uint16_t addr; const char* expected; } expected[] = {
        { 0x0000, "NOP" },
        { 0x0001, "NOP" },
        { 0x0002, "NOP" },
        { 0x0003, "JMP  0x1800" },
        { 0x0006, "LXI  SP, 0x2400" },
        { 0x0009, "MVI  A, 0x00" },
        { 0x000B, "CALL 0x1810" },
        { 0x000E, "EI" },
        { 0x000F, "HLT" },
    };
    for (const auto& e : expected) {
        auto d = disassemble(m, e.addr);
        bool ok = (d.text == e.expected);
        if (!ok) std::printf("  got '%s' at 0x%04X, expected '%s'\n",
                             d.text.c_str(), unsigned(e.addr), e.expected);
        CHECK(ok);
    }

    // Also print it so a human can eyeball — same routine the main exe will use.
    std::printf("  (showing first 9 instructions:)\n");
    dump_disassembly(m, 0x0000, 9);
}

int main() {
    // Use a unique temp directory under /tmp so reruns are clean.
    fs::path tmp_root  = fs::temp_directory_path() / "invaderx_test";
    fs::remove_all(tmp_root);
    fs::path good_dir  = tmp_root / "good";
    fs::path empty_dir = tmp_root / "empty";
    fs::path short_dir = tmp_root / "short_h";

    make_synthetic_set(good_dir);
    make_truncated_set(short_dir);

    L1_spec_order(good_dir);
    L2_mame_order(good_dir);
    L3_missing(empty_dir);
    L4_wrong_size(short_dir);
    L5_rom_write_protect(good_dir);
    L6_ram_writable();
    L7_endianness();
    L8_boot_disasm(good_dir);

    std::printf("\nPASS %d   FAIL %d\n", g_pass, g_fail);

    // Also exercise print_load_report so we see the format.
    std::printf("\n--- sample print_load_report output ---\n");
    {
        Memory m;
        auto res = load_invaders_roms(m, good_dir.string(), RomOrder::Spec);
        print_load_report(res, RomOrder::Spec);
    }

    fs::remove_all(tmp_root);
    return g_fail == 0 ? 0 : 1;
}
