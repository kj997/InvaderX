// ============================================================================
//  InvaderX — src/test_integration.cpp
//
//  End-to-end smoke test of the full stack:
//      rom_loader + cpu + hardware + scheduler + renderer + sdl_frontend
//
//  We can't run real Space Invaders ROMs here (copyrighted), so we build a
//  synthetic chip-E ROM that mirrors the structural shape of the real boot:
//
//      Reset @ 0x0000:
//          LXI  SP, 0x2400        ; top of WRAM
//          XRA  A                  ; A = 0 (so VRAM stays clean if we never paint)
//          EI                      ; arm interrupts
//          @loop: HLT              ; wait for the next interrupt
//          JMP @loop
//
//      RST 1 handler @ 0x0008:     ; mid-frame
//          MVI  A, 0xAA
//          STA  0x2400             ; paints byte 0 of VRAM
//          EI
//          RET
//
//      RST 2 handler @ 0x0010:     ; VBLANK
//          MVI  A, 0x55
//          STA  0x2401             ; paints byte 1 of VRAM
//          EI
//          RET
//
//  After N frames we expect both VRAM bytes painted (proving both interrupts
//  fired and the handlers wrote memory) AND the framebuffer to contain the
//  correctly-rotated pixels (proving the renderer ran on real CPU output).
//
//  Tests:
//      I1  Synthetic ROM loads cleanly
//      I2  Disassembler dumps it correctly (the same view a user gets at boot)
//      I3  60 frames run without crash or hang
//      I4  Both RST handlers wrote their bytes
//      I5  Framebuffer contains exactly the expected rotated pixels
//      I6  CPU cycle counter is within tolerance of CPU_HZ * 1 second
// ============================================================================

#include "cpu.h"
#include "memory.h"
#include "hardware.h"
#include "display.h"
#include "cpu_step.h"
#include "hardware_ops.h"
#include "rom_loader.h"
#include "renderer.h"
#include "disassembler.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
//  Build chip E as a 2 KB ROM with the boot/handler code described above.
//  All other chips are filler.
// ----------------------------------------------------------------------------
static std::vector<uint8_t> build_synthetic_chip_e() {
    std::vector<uint8_t> rom(mem::ROM_BANK_SIZE, 0x00);   // NOP fill

    // Reset vector @ 0x0000:
    //   31 00 24   LXI SP, 0x2400
    //   AF         XRA A
    //   FB         EI
    //   76         HLT      ; @ 0x0005
    //   C3 05 00   JMP 0x0005
    uint8_t reset[] = { 0x31, 0x00, 0x24, 0xAF, 0xFB, 0x76, 0xC3, 0x05, 0x00 };
    for (size_t i = 0; i < sizeof(reset); ++i) rom[i] = reset[i];

    // RST 1 handler @ 0x0008:
    // Memory occupied so far: 0..8 (9 bytes), one byte free at offset 0x08
    // before RST handler must begin AT 0x08. The reset stub above ends at
    // offset 8 (last byte is 0x00 of JMP imm), so we need to move JMP target.
    // Rewriting cleanly — pad to 8 first:
    rom.assign(mem::ROM_BANK_SIZE, 0x00);
    // Reset @ 0x0000:
    rom[0x00] = 0x31; rom[0x01] = 0x00; rom[0x02] = 0x24;        // LXI SP, 0x2400
    rom[0x03] = 0xAF;                                            // XRA A
    rom[0x04] = 0xFB;                                            // EI
    rom[0x05] = 0x76;                                            // HLT
    rom[0x06] = 0xC3; rom[0x07] = 0x05;                          // JMP 0x...
    // High byte of JMP target lives at 0x08 — but 0x08 is the RST 1 vector!
    // Pick a different layout: RET-to-main via JMP 0x0005 placed below the
    // vector area. We instead use a "JMP to free space" trick.
    //
    // Restart from scratch with a layout that respects the RST vectors:
    rom.assign(mem::ROM_BANK_SIZE, 0x00);
    // 0x0000: JMP 0x0040  (jump past the RST vector area)
    rom[0x00] = 0xC3; rom[0x01] = 0x40; rom[0x02] = 0x00;

    // 0x0008: RST 1 handler
    //   3E AA       MVI A, 0xAA
    //   32 00 24    STA 0x2400
    //   FB          EI
    //   C9          RET
    rom[0x08] = 0x3E; rom[0x09] = 0xAA;
    rom[0x0A] = 0x32; rom[0x0B] = 0x00; rom[0x0C] = 0x24;
    rom[0x0D] = 0xFB;
    rom[0x0E] = 0xC9;

    // 0x0010: RST 2 handler
    //   3E 55       MVI A, 0x55
    //   32 01 24    STA 0x2401
    //   FB          EI
    //   C9          RET
    rom[0x10] = 0x3E; rom[0x11] = 0x55;
    rom[0x12] = 0x32; rom[0x13] = 0x01; rom[0x14] = 0x24;
    rom[0x15] = 0xFB;
    rom[0x16] = 0xC9;

    // 0x0040: main code
    //   31 00 24    LXI SP, 0x2400
    //   AF          XRA A
    //   FB          EI
    //   76          HLT          ; @ 0x0045
    //   C3 45 00    JMP 0x0045
    rom[0x40] = 0x31; rom[0x41] = 0x00; rom[0x42] = 0x24;
    rom[0x43] = 0xAF;
    rom[0x44] = 0xFB;
    rom[0x45] = 0x76;
    rom[0x46] = 0xC3; rom[0x47] = 0x45; rom[0x48] = 0x00;

    return rom;
}

static void write_rom(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

static void write_filler_rom(const fs::path& path, uint8_t fill) {
    std::vector<uint8_t> v(mem::ROM_BANK_SIZE, fill);
    write_rom(path, v);
}

// ----------------------------------------------------------------------------
//  Mini frame pump: copies the production run_game_loop shape but without
//  any SDL dependency so the integration test runs headless. The renderer
//  call IS real — we want to verify the framebuffer for I5.
// ----------------------------------------------------------------------------
static void pump_frames(CPU& cpu, Memory& mem, Hardware& hw, Display& disp,
                        int n_frames) {
    InterruptScheduler sched;
    sched.reset(cpu.cycles);
    for (int frame = 0; frame < n_frames; ++frame) {
        // Run CPU until the scheduler reports frame end.
        int safety = display::CYCLES_PER_FRAME * 4;   // way more than enough
        while (safety-- > 0 && !sched.tick(hw, cpu.cycles)) {
            cpu_step(cpu, mem, hw);
        }
        render_vram_to_framebuffer(mem, disp);
    }
}

int main() {
    fs::path tmp = fs::temp_directory_path() / "invaderx_integration";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    write_rom        (tmp / "invaders.e", build_synthetic_chip_e());
    write_filler_rom (tmp / "invaders.f", 0x00);
    write_filler_rom (tmp / "invaders.g", 0x00);
    write_filler_rom (tmp / "invaders.h", 0x00);

    // ------------------- I1 -------------------
    std::printf("I1  synthetic ROM loads\n");
    CPU cpu; Memory mem; Hardware hw; Display disp;
    auto lr = load_invaders_roms(mem, tmp.string(), RomOrder::Spec);
    CHECK(lr.ok);
    CHECK(lr.error.empty());

    // ------------------- I2 -------------------
    std::printf("I2  disassembler reflects the synthetic boot vector\n");
    auto d0 = disassemble(mem, 0x0000);
    CHECK(d0.text == "JMP  0x0040");
    auto d8 = disassemble(mem, 0x0008);          // RST 1 handler entry
    CHECK(d8.text == "MVI  A, 0xAA");
    auto d10 = disassemble(mem, 0x0010);         // RST 2 handler entry
    CHECK(d10.text == "MVI  A, 0x55");
    auto d40 = disassemble(mem, 0x0040);
    CHECK(d40.text == "LXI  SP, 0x2400");

    // Print it (same view a user gets at boot) — handy for visual confirmation.
    std::printf("  (boot dump:)\n");
    dump_disassembly(mem, 0x0000, 4);
    std::printf("  ...\n");
    dump_disassembly(mem, 0x0040, 4);

    // ------------------- I3 -------------------
    std::printf("I3  pump 60 frames\n");
    pump_frames(cpu, mem, hw, disp, 60);
    CHECK(true);   // didn't crash or hang

    // ------------------- I4 -------------------
    std::printf("I4  both RST handlers wrote VRAM\n");
    CHECK(mem.read(0x2400) == 0xAA);
    CHECK(mem.read(0x2401) == 0x55);

    // ------------------- I5 -------------------
    //  Byte at 0x2400 has value 0xAA. It's VRAM offset 0, which corresponds
    //  to memory pixels (0..7, 0) -> after rotation, screen column 0,
    //  screen rows 255 (bit 0) down to 248 (bit 7).
    //  0xAA = 1010 1010 -> bits 1, 3, 5, 7 are set.
    //  Expected lit screen pixels at column 0:
    //      bit 1 (screen_y=254), bit 3 (252), bit 5 (250), bit 7 (248).
    //  Bits 0, 2, 4, 6 -> screen_y = 255, 253, 251, 249  -> dark.
    std::printf("I5  framebuffer reflects painted VRAM (0xAA at 0x2400)\n");
    auto fb = [&](int x, int y) {
        return disp.framebuffer[size_t(y * display::NATIVE_W + x)];
    };
    CHECK(fb(0, 255) == Display::COLOR_OFF);   // bit 0 of 0xAA = 0
    CHECK(fb(0, 254) == Display::COLOR_ON);    // bit 1 = 1
    CHECK(fb(0, 253) == Display::COLOR_OFF);   // bit 2 = 0
    CHECK(fb(0, 252) == Display::COLOR_ON);    // bit 3 = 1
    CHECK(fb(0, 251) == Display::COLOR_OFF);
    CHECK(fb(0, 250) == Display::COLOR_ON);
    CHECK(fb(0, 249) == Display::COLOR_OFF);
    CHECK(fb(0, 248) == Display::COLOR_ON);

    //  Byte at 0x2401 has value 0x55 = 0101 0101 -> bits 0, 2, 4, 6 set.
    //  This is VRAM offset 1, memory pixels (8..15, 0).
    //  After rotation -> screen column 0, screen rows 247 (bit 0) down to 240 (bit 7).
    CHECK(fb(0, 247) == Display::COLOR_ON);    // bit 0 of 0x55 = 1
    CHECK(fb(0, 246) == Display::COLOR_OFF);   // bit 1 = 0
    CHECK(fb(0, 245) == Display::COLOR_ON);
    CHECK(fb(0, 244) == Display::COLOR_OFF);
    CHECK(fb(0, 243) == Display::COLOR_ON);
    CHECK(fb(0, 242) == Display::COLOR_OFF);
    CHECK(fb(0, 241) == Display::COLOR_ON);
    CHECK(fb(0, 240) == Display::COLOR_OFF);

    // ------------------- I6 -------------------
    //  60 emulated frames = 60 * 33,333 = 1,999,980 cycles. CPU runs at
    //  CPU_HZ = 2,000,000, so we expect ~2 MHz worth of cycles. Allow
    //  +/- 5% for HLT cycle slop and interrupt overhead.
    std::printf("I6  CPU cycle counter within tolerance after 60 frames\n");
    uint64_t expected = uint64_t(display::CPU_HZ);             // 2,000,000
    uint64_t lo = expected * 95 / 100;
    uint64_t hi = expected * 105 / 100;
    bool ok = cpu.cycles >= lo && cpu.cycles <= hi;
    if (!ok) std::printf("  cycles=%llu, expected ~%llu (range %llu..%llu)\n",
                         (unsigned long long)cpu.cycles,
                         (unsigned long long)expected,
                         (unsigned long long)lo,
                         (unsigned long long)hi);
    CHECK(ok);
    std::printf("  (actual: %llu cycles after 60 frames; nominal 2,000,000)\n",
                (unsigned long long)cpu.cycles);

    fs::remove_all(tmp);
    std::printf("\nPASS %d   FAIL %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
