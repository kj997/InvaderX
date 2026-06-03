// ============================================================================
//  InvaderX — src/hardware.cpp
//  Strong definitions of cpu_in_port / cpu_out_port (overriding the weak
//  stubs in cpu.cpp), plus InterruptScheduler::tick and input helpers.
//
//  The whole file is data manipulation on the Hardware struct from Step 1.
//  Nothing here allocates, nothing throws.
// ============================================================================

#include "hardware_ops.h"

namespace invaderx {

// ============================================================================
//  §1  I/O port semantics  (Computer Archaeology — Space Invaders hardware)
// ============================================================================
//
//  READS (IN port):
//    0   credit / unused          — game never reads it; we return 0
//    1   Player 1 controls + coin — see Hardware::P1_* masks
//    2   Player 2 controls + DIPs — default 0 = 3 lives, bonus at 1500
//    3   Shift register result   = (shift_reg >> (8 - shift_offset)) & 0xFF
//
//  WRITES (OUT port):
//    2   shift_offset latch       = value & 0x07
//    3   sound bank A (UFO, shot, player die, invader die, extended play)
//    4   shift register data feed:
//        shift_reg = (value << 8) | (shift_reg >> 8)
//        — new value enters the HIGH byte; old HIGH slides into LOW;
//          old LOW is discarded.
//    5   sound bank B (fleet movement 1-4, UFO hit)
//    6   watchdog timer reset    — ignored; the real PCB resets on no-write
// ============================================================================

uint8_t cpu_in_port(Hardware& hw, uint8_t port) {
    switch (port) {
        case 0:
            // Unused on the production cabinet. Some test ROMs probe it;
            // returning the "no-buttons-pressed, bit-3-tied-high" pattern
            // is the safest default.
            return 0x0E;

        case 1:
            return hw.port1;

        case 2:
            return hw.port2;

        case 3: {
            // Barrel-shift: shift_reg right by (8 - offset) and take the
            // low byte. offset 0 returns the HIGH byte of shift_reg;
            // offset 7 returns bit-1-through-bit-8 view.
            unsigned shift = unsigned(8 - hw.shift_offset);
            return uint8_t((hw.shift_reg >> shift) & 0xFFu);
        }

        default:
            // Unmapped ports read as 0xFF on TTL buses (floating inputs
            // pull high). Doesn't matter here — Invaders never reads them.
            return 0xFF;
    }
}

void cpu_out_port(Hardware& hw, uint8_t port, uint8_t value) {
    switch (port) {
        case 2:
            // Only the bottom 3 bits of the offset latch are wired.
            hw.shift_offset = uint8_t(value & 0x07);
            break;

        case 3:
            // Sound bank A. We latch the byte for an audio backend to pick
            // up edge transitions; no playback in this build.
            hw.port3_sound = value;
            break;

        case 4:
            // Feed new byte into the shift register.
            hw.shift_reg = uint16_t((uint16_t(value) << 8) | (hw.shift_reg >> 8));
            break;

        case 5:
            // Sound bank B.
            hw.port5_sound = value;
            break;

        case 6:
            // Watchdog reset. The real PCB resets the system if no write
            // happens within ~250 ms — we don't enforce that.
            break;

        default:
            // Unmapped ports — drop the write silently.
            break;
    }
}

// ============================================================================
//  §2  Interrupt scheduler
// ============================================================================

void InterruptScheduler::reset(uint64_t now_cycle) {
    frame_start_cycle = now_cycle;
    phase             = 0;
}

bool InterruptScheduler::tick(Hardware& hw, uint64_t now_cycle) {
    // Use a signed-safe delta. now_cycle is monotonic so this never wraps,
    // but the comparison is uint64_t-clean.
    uint64_t into_frame = now_cycle - frame_start_cycle;

    // Phase 0 → 1 : fire RST 1 at scanline 96.
    if (phase == 0 && into_frame >= uint64_t(timing::CYCLES_TO_RST1)) {
        cpu_request_interrupt(hw, Hardware::INT_RST_1);
        phase = 1;
    }

    // Phase 1 → 2 : fire RST 2 at scanline 224 (VBLANK start).
    if (phase == 1 && into_frame >= uint64_t(timing::CYCLES_TO_RST2)) {
        cpu_request_interrupt(hw, Hardware::INT_RST_2);
        phase = 2;
    }

    // Phase 2 → 0 : end of frame; rebase. Adding a fixed delta (instead of
    // snapping to now_cycle) prevents accumulating drift across thousands
    // of frames.
    if (phase == 2 && into_frame >= uint64_t(display::CYCLES_PER_FRAME)) {
        frame_start_cycle += uint64_t(display::CYCLES_PER_FRAME);
        phase             = 0;
        return true;                 // signal "frame complete" to the caller
    }

    return false;
}

// ============================================================================
//  §3  Input helpers — trivial mask ops, but the names document intent
// ============================================================================

void hw_input_press(Hardware& hw, uint8_t mask) {
    hw.port1 = uint8_t(hw.port1 | mask);
}

void hw_input_release(Hardware& hw, uint8_t mask) {
    hw.port1 = uint8_t(hw.port1 & ~mask);
}

void hw_input_set(Hardware& hw, uint8_t mask, bool pressed) {
    if (pressed) hw_input_press(hw, mask);
    else         hw_input_release(hw, mask);
}

} // namespace invaderx
