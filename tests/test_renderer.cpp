// ============================================================================
//  InvaderX — src/test_renderer.cpp
//
//  R1  Empty VRAM -> all-black framebuffer
//  R2  All-on VRAM -> all-white framebuffer
//  R3  Corner mapping: 4 single-bit writes -> 4 expected framebuffer pixels
//  R4  Full row in memory layout -> full COLUMN in screen layout
//  R5  Full column in memory layout (32 bytes of 0xFF) -> full ROW in screen
//  R6  Specific pixel pattern -> recognizable ASCII art dump
// ============================================================================

#include "cpu.h"
#include "memory.h"
#include "display.h"
#include "renderer.h"

#include <cstdio>
#include <cstring>

using namespace invaderx;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do {                                                 \
    if (cond) { ++g_pass; }                                              \
    else      { ++g_fail; std::printf("  FAIL %s:%d  %s\n",              \
                                      __FILE__, __LINE__, #cond); }      \
} while (0)

static inline uint32_t fb_pixel(const Display& d, int sx, int sy) {
    return d.framebuffer[size_t(sy * display::NATIVE_W + sx)];
}

// ----------------------------------------------------------------------------
//  R1
// ----------------------------------------------------------------------------
static void R1_empty() {
    std::printf("R1  empty VRAM -> all black\n");
    Memory m; Display d;
    // Pre-fill framebuffer with a sentinel so we can confirm we OVERWROTE it.
    d.framebuffer.fill(0xDEADBEEF);
    render_vram_to_framebuffer(m, d);
    bool all_black = true;
    for (auto px : d.framebuffer) {
        if (px != Display::COLOR_OFF) { all_black = false; break; }
    }
    CHECK(all_black);
}

// ----------------------------------------------------------------------------
//  R2
// ----------------------------------------------------------------------------
static void R2_full() {
    std::printf("R2  all-on VRAM -> all white\n");
    Memory m; Display d;
    for (uint32_t a = mem::VRAM_BASE; a <= mem::VRAM_END; ++a) {
        m.write(uint16_t(a), 0xFF);
    }
    render_vram_to_framebuffer(m, d);
    bool all_white = true;
    for (auto px : d.framebuffer) {
        if (px != Display::COLOR_ON) { all_white = false; break; }
    }
    CHECK(all_white);
}

// ----------------------------------------------------------------------------
//  R3 — Corner mapping. Four single-bit writes; verify all four corners.
//
//  The rotation maps (mem_x, mem_y) -> (mem_y, 255 - mem_x):
//
//    VRAM byte 0,  bit 0  : memory (0, 0)     -> screen (0, 255)   BL
//    VRAM byte 31, bit 7  : memory (255, 0)   -> screen (0, 0)     TL
//    VRAM byte 7136, bit 0: memory (0, 223)   -> screen (223, 255) BR
//    VRAM byte 7167, bit 7: memory (255, 223) -> screen (223, 0)   TR
// ----------------------------------------------------------------------------
static void R3_corners() {
    std::printf("R3  corner mapping (4 corners, exact pixel match)\n");
    struct Corner {
        uint16_t vram_offset;
        uint8_t  bit_mask;
        int      screen_x;
        int      screen_y;
        const char* name;
    };
    Corner corners[4] = {
        { 0,    0x01, 0,   255, "bottom-left"  },
        { 31,   0x80, 0,   0,   "top-left"     },
        { 7136, 0x01, 223, 255, "bottom-right" },
        { 7167, 0x80, 223, 0,   "top-right"    },
    };

    for (auto& c : corners) {
        Memory m; Display d;
        // Wipe VRAM is implicit (zero-init Memory), set just the one bit.
        m.write(uint16_t(mem::VRAM_BASE + c.vram_offset), c.bit_mask);
        render_vram_to_framebuffer(m, d);

        // Exactly one pixel should be lit: the targeted corner.
        int lit_count = 0;
        int lit_x = -1, lit_y = -1;
        for (int y = 0; y < display::NATIVE_H; ++y) {
            for (int x = 0; x < display::NATIVE_W; ++x) {
                if (fb_pixel(d, x, y) == Display::COLOR_ON) {
                    ++lit_count;
                    lit_x = x; lit_y = y;
                }
            }
        }
        bool ok = (lit_count == 1 && lit_x == c.screen_x && lit_y == c.screen_y);
        if (!ok) std::printf("  %s: lit_count=%d at (%d,%d), wanted (%d,%d)\n",
                             c.name, lit_count, lit_x, lit_y, c.screen_x, c.screen_y);
        CHECK(ok);
    }
}

// ----------------------------------------------------------------------------
//  R4 — A full ROW of memory layout (one byte across 32 bytes of one memory
//       row) maps to a full COLUMN of the screen. Sanity-check on rotation.
//
//       Memory row 0 = bytes 0..31, all bits set -> 256 memory pixels in
//       row mem_y=0. After rotation -> screen column screen_x=0, all 256
//       pixels lit.
// ----------------------------------------------------------------------------
static void R4_row_becomes_column() {
    std::printf("R4  full memory row 0 -> full screen column 0\n");
    Memory m; Display d;
    for (int b = 0; b < 32; ++b) {
        m.write(uint16_t(mem::VRAM_BASE + b), 0xFF);
    }
    render_vram_to_framebuffer(m, d);

    // Screen column 0: all 256 pixels lit, all other columns dark.
    for (int y = 0; y < display::NATIVE_H; ++y) {
        CHECK(fb_pixel(d, 0, y) == Display::COLOR_ON);
        // Spot-check the next column is dark.
        CHECK(fb_pixel(d, 1, y) == Display::COLOR_OFF);
        CHECK(fb_pixel(d, 223, y) == Display::COLOR_OFF);
    }
}

// ----------------------------------------------------------------------------
//  R5 — A full COLUMN of memory layout (32 bytes at offsets N, N+32, N+64,
//       ..., one per memory row) maps to a full ROW of the screen.
//
//       Let N = 0 (memory columns 0..7). Setting bit 0 of every 32-byte
//       stride lights memory pixel (0, mem_y) for all mem_y in [0, 224).
//       After rotation -> screen pixels (0..223, 255).
// ----------------------------------------------------------------------------
static void R5_column_becomes_row() {
    std::printf("R5  full memory column 0 -> full screen row 255\n");
    Memory m; Display d;
    for (int r = 0; r < 224; ++r) {
        m.write(uint16_t(mem::VRAM_BASE + r * 32), 0x01);
    }
    render_vram_to_framebuffer(m, d);

    // Screen row 255: all 224 pixels lit.
    for (int x = 0; x < display::NATIVE_W; ++x) {
        CHECK(fb_pixel(d, x, 255) == Display::COLOR_ON);
    }
    // Adjacent row should be dark.
    for (int x = 0; x < display::NATIVE_W; ++x) {
        CHECK(fb_pixel(d, x, 254) == Display::COLOR_OFF);
    }
}

// ----------------------------------------------------------------------------
//  R6 — Recognizable pattern + ASCII dump so a human can eyeball it.
//        Draw an "L" shape on the screen:
//          • a vertical bar down the left edge (screen column 0, rows 0..255)
//          • a horizontal bar across the bottom (screen row 255, cols 0..223)
//        We have to write the VRAM bytes that, after rotation, produce that.
// ----------------------------------------------------------------------------
static void R6_pattern_and_dump() {
    std::printf("R6  draw 'L' shape and dump ASCII art\n");
    Memory m; Display d;

    // Left vertical bar  = screen column 0  = memory ROW 0 lit  = bytes 0..31, all 0xFF.
    for (int b = 0; b < 32; ++b) m.write(uint16_t(mem::VRAM_BASE + b), 0xFF);

    // Bottom horizontal bar = screen row 255 = memory COLUMN 0 lit
    //   = bit 0 of bytes at offsets 0, 32, 64, ..., 32*223.
    // The vertical bar already set bit 0 of byte 0; the rest still need bit 0.
    for (int r = 1; r < 224; ++r) m.write(uint16_t(mem::VRAM_BASE + r * 32), 0x01);

    render_vram_to_framebuffer(m, d);

    // Verify: column 0 fully lit; row 255 fully lit; the rest dark.
    bool ok = true;
    for (int y = 0; y < display::NATIVE_H; ++y) {
        if (fb_pixel(d, 0, y) != Display::COLOR_ON) ok = false;
    }
    for (int x = 0; x < display::NATIVE_W; ++x) {
        if (fb_pixel(d, x, 255) != Display::COLOR_ON) ok = false;
    }
    // A pixel in the interior must be off.
    if (fb_pixel(d, 100, 100) != Display::COLOR_OFF) ok = false;
    CHECK(ok);

    // ASCII dump, downsampled 8x for terminal readability.
    std::printf("  (downsampled 8x; '#' = lit, '.' = dark)\n");
    for (int y = 0; y < display::NATIVE_H; y += 8) {
        std::printf("  ");
        for (int x = 0; x < display::NATIVE_W; x += 4) {
            std::printf("%c", (fb_pixel(d, x, y) == Display::COLOR_ON) ? '#' : '.');
        }
        std::printf("\n");
    }
}

int main() {
    R1_empty();
    R2_full();
    R3_corners();
    R4_row_becomes_column();
    R5_column_becomes_row();
    R6_pattern_and_dump();

    std::printf("\nPASS %d   FAIL %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
