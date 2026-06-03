#pragma once
// ============================================================================
//  InvaderX — display.h
//  Framebuffer, SDL2 window/renderer/texture handles, and the timing
//  constants that pace the CPU against the 60 Hz video frame.
//
//  Step 5 populates the SDL_* pointers and implements the renderer.
//  This header only DECLARES state — no SDL_* function calls here.
// ============================================================================

#include <array>
#include <cstdint>

// Forward declarations — we don't pull in <SDL.h> at this layer so that
// non-display translation units (cpu.cpp, memory.cpp, etc.) compile fast
// and stay decoupled from SDL.
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace invaderx {

// ----------------------------------------------------------------------------
//  Video geometry & timing constants
// ----------------------------------------------------------------------------
//
//  Native CRT (rotated inside the cabinet):
//      • Logical screen on the rotated CRT is 224 wide × 256 tall.
//      • VRAM at 0x2400 stores 256 ROWS × 224 COLUMNS, 1 bit per pixel,
//        but each byte holds 8 vertically-stacked pixels of ONE column.
//      • Byte layout: bit 0 = topmost pixel of that column (after rotation),
//        bit 7 = bottommost. Addresses advance DOWN a column first, then
//        across to the next column.
//      • Step 5's renderer converts VRAM → framebuffer with the rotation
//        baked in, so the framebuffer is already in display orientation
//        (224 wide × 256 tall, row-major).
//
//  Color overlay (original cabinet, optional for Step 5):
//      The real cabinet had a colored gel pasted on the CRT:
//        - red horizontal strip near the top    (UFO band)
//        - white middle band
//        - green strip near the bottom          (player + shields)
//      We start with pure white-on-black; the overlay is a Step 8 stretch.
//
//  Timing:
//      The 8080 runs at 2 MHz. At 60 Hz that's 33,333 T-states per frame.
//      The frame is split in half by the mid-screen interrupt (RST 1 at
//      scanline 96, RST 2 at scanline 224), so each half-frame is ~16,667
//      cycles. Step 4 uses CYCLES_PER_HALF_FRAME to schedule the two
//      interrupts; Step 5 uses CYCLES_PER_FRAME for the SDL frame pump.
// ----------------------------------------------------------------------------
namespace display {
    // --- Native resolution after CRT rotation ---
    constexpr int NATIVE_W           = 224;
    constexpr int NATIVE_H           = 256;
    constexpr int FRAMEBUFFER_PIXELS = NATIVE_W * NATIVE_H;   // 57,344 px

    // --- Window scale factor (cabinet pixels are tiny; 3x = 672x768) ---
    constexpr int SCALE              = 3;
    constexpr int WINDOW_W           = NATIVE_W * SCALE;
    constexpr int WINDOW_H           = NATIVE_H * SCALE;

    // --- Frame & CPU timing ---
    //  Note: 2,000,000 / 60 = 33,333.33...  We truncate to 33,333. The
    //  ~10 cycles per frame of drift is below the precision of any 8080
    //  cycle counter and well within the budget of Space Invaders' game
    //  loop, which uses interrupt-driven timing anyway.
    constexpr double FRAME_HZ              = 60.0;
    constexpr int    CPU_HZ                = 2'000'000;
    constexpr int    CYCLES_PER_FRAME      = CPU_HZ / int(FRAME_HZ);    // 33,333
    constexpr int    CYCLES_PER_HALF_FRAME = CYCLES_PER_FRAME / 2;      // 16,666
} // namespace display

// ----------------------------------------------------------------------------
//  Display  — owns the framebuffer and the SDL2 resource handles.
//
//  Why store the SDL pointers in a struct rather than as globals?
//   • Lifetime is explicit — Step 5's init/shutdown functions take a
//     Display& and can be unit-tested with a stub.
//   • No singleton, no static-init-order problems.
//   • Keeps all per-emulator state in one place if we ever want to run
//     two instances side-by-side (e.g. for diff-debugging).
// ----------------------------------------------------------------------------
struct Display {
    // Compile-time-sized framebuffer in ARGB8888 format.
    // Step 5 writes pixels here; SDL_UpdateTexture streams it to the GPU.
    std::array<uint32_t, display::FRAMEBUFFER_PIXELS> framebuffer{};

    // SDL2 resource handles. Initialized in Step 5; null until then.
    SDL_Window*   window   = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture*  texture  = nullptr;

    // Two-color palette. Documented as constants so the optional color
    // overlay in Step 8 can extend this to a small lookup table without
    // anyone wondering where the magic numbers came from.
    //   0xAARRGGBB layout, opaque alpha.
    static constexpr uint32_t COLOR_ON  = 0xFFFFFFFFu;  // pixel set
    static constexpr uint32_t COLOR_OFF = 0xFF000000u;  // pixel clear
};

} // namespace invaderx
