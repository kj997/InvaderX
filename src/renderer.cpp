// ============================================================================
//  InvaderX — src/renderer.cpp
//
//  VRAM layout (as the 8080 sees it, pre-rotation):
//      • 256 columns x 224 rows, 1 bit per pixel
//      • Stored column-major: byte at offset N occupies a horizontal strip
//        of 8 pixels at (8 * (N % 32), N / 32) ... (8 * (N % 32) + 7, N / 32)
//      • Bit 0 = leftmost pixel of the 8-pixel strip in memory orientation
//      • 224 rows * 32 bytes/row = 7168 bytes total
//
//  90 degrees counter-clockwise rotation, derived from the standard 2D
//  rotation about the origin combined with a translation to keep coords
//  non-negative:
//      (mem_x, mem_y)  ->  (mem_y, (NATIVE_H - 1) - mem_x)
//                       =  (screen_x, screen_y)
//      where screen_x in [0, NATIVE_W=224) and screen_y in [0, NATIVE_H=256).
//
//  Important consequence for performance: all 8 bits of one VRAM byte share
//  the SAME screen_x (= mem_y) and have screen_y values 255-mem_x_base
//  down to 248-mem_x_base. In the row-major framebuffer this means 8
//  writes at the same x column with y decreasing by 1, i.e. with a
//  framebuffer-stride of -NATIVE_W between consecutive bits. We exploit
//  that to compute one base index per byte instead of one per bit.
// ============================================================================

#include "renderer.h"

namespace invaderx {

void render_vram_to_framebuffer(const Memory& mem, Display& disp) {
    constexpr int W = display::NATIVE_W;        // 224
    constexpr int H = display::NATIVE_H;        // 256

    for (int i = 0; i < int(mem::VRAM_SIZE); ++i) {
        // Memory-space coordinates of the byte's pixel-0:
        int mem_y      = i / 32;                // 0..223  (becomes screen_x)
        int mem_x_base = (i % 32) * 8;          // 0..248  (multiples of 8)

        uint8_t byte = mem.read(uint16_t(mem::VRAM_BASE + i));

        // Screen-space index of bit-0 of this byte.
        //   screen_x = mem_y
        //   screen_y = (H - 1) - mem_x_base
        //   fb_base  = screen_y * W + screen_x
        int screen_x = mem_y;
        int screen_y = (H - 1) - mem_x_base;
        int fb_base  = screen_y * W + screen_x;

        // Each subsequent bit is one row HIGHER on the screen, i.e. has
        // a framebuffer index W lower than the previous.
        for (int b = 0; b < 8; ++b) {
            bool lit = ((byte >> b) & 1) != 0;
            disp.framebuffer[fb_base - b * W] = lit ? Display::COLOR_ON
                                                    : Display::COLOR_OFF;
        }
    }
}

} // namespace invaderx
