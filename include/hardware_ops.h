#pragma once
// ============================================================================
//  InvaderX — hardware_ops.h
//  All Step-4 declarations: scanline timing, interrupt scheduler, input
//  helpers. Strong implementations of cpu_in_port / cpu_out_port (declared
//  in cpu_step.h) also live in hardware.cpp; the linker prefers them over
//  the weak stubs in cpu.cpp.
// ============================================================================

#include <cstdint>
#include "hardware.h"
#include "cpu_step.h"
#include "display.h"

namespace invaderx {

// ----------------------------------------------------------------------------
//  timing — scanline-accurate interrupt schedule
// ----------------------------------------------------------------------------
//  Space Invaders displays at ~60 Hz with a vertical-scan period of 262
//  scanlines (224 visible + 38 VBLANK). The CPU runs at 2 MHz, giving
//  33,333 T-states per frame and ~127 T-states per scanline.
//
//  Two interrupts fire per frame:
//    • RST 1 (opcode 0xCF, vector 0x08)  at scanline  96 — mid-screen.
//      The game updates the BOTTOM half of VRAM here (which is the upper
//      half of the player's view, since the cabinet rotates the CRT 90°).
//    • RST 2 (opcode 0xD7, vector 0x10)  at scanline 224 — start of VBLANK.
//      The game updates the OTHER half of VRAM during the ~38 scanlines
//      of VBLANK, finishing before scanline 0 of the next frame.
//
//  Doing it this way avoids tearing: the beam never overtakes the half
//  the CPU is currently writing.
// ----------------------------------------------------------------------------
namespace timing {
    constexpr int SCANLINES_PER_FRAME = 262;
    constexpr int CYCLES_PER_SCANLINE = display::CYCLES_PER_FRAME / SCANLINES_PER_FRAME;
                                        // 33333 / 262 = 127 (integer truncation)

    constexpr int RST1_SCANLINE       = 96;     // mid-screen
    constexpr int RST2_SCANLINE       = 224;    // start of VBLANK

    constexpr int CYCLES_TO_RST1      = RST1_SCANLINE * CYCLES_PER_SCANLINE;  // 12,192
    constexpr int CYCLES_TO_RST2      = RST2_SCANLINE * CYCLES_PER_SCANLINE;  // 28,448
}

// ----------------------------------------------------------------------------
//  InterruptScheduler
//      Tiny state machine. Caller (the Step 5 frame pump) calls tick()
//      after every cpu_step() with the running cycle total. We fire the
//      two interrupts at their scheduled cycle offsets within the frame,
//      and return true on the cycle that closes the frame so the caller
//      can render and wait for vsync.
//
//      Phase progression within a frame:
//          0  — counting up to CYCLES_TO_RST1
//          1  — RST 1 fired; counting up to CYCLES_TO_RST2
//          2  — RST 2 fired; counting up to CYCLES_PER_FRAME
//          → wraps to 0 with frame_start_cycle += CYCLES_PER_FRAME
//
//      We advance frame_start_cycle by a fixed delta rather than snapping
//      to now_cycle so timing drift doesn't accumulate across frames.
// ----------------------------------------------------------------------------
struct InterruptScheduler {
    uint64_t frame_start_cycle = 0;     // cycle at which the current frame began
    int      phase             = 0;     // 0 / 1 / 2 as above

    // Reset to "start of frame at now_cycle". Used after init and any time
    // the CPU's cycle counter is rebased.
    void reset(uint64_t now_cycle = 0);

    // Advance the scheduler. Returns true exactly once per frame, on the
    // tick that completes the frame; that's the cue to render.
    bool tick(Hardware& hw, uint64_t now_cycle);
};

// ----------------------------------------------------------------------------
//  Input helpers — the SDL event handler (Step 5) calls these. They keep
//  the masking discipline out of the rendering code.
//
//  Active-high convention: the real PCB wires the buttons to pull the
//  port HIGH when pressed, which is what these helpers do.
// ----------------------------------------------------------------------------
void hw_input_press  (Hardware& hw, uint8_t mask);  // sets   the bit(s)
void hw_input_release(Hardware& hw, uint8_t mask);  // clears the bit(s)
void hw_input_set    (Hardware& hw, uint8_t mask, bool pressed);

} // namespace invaderx
