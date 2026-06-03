#pragma once
// ============================================================================
//  InvaderX — hardware.h
//  Space Invaders custom I/O hardware state.
//  The 8080 talks to this struct via IN/OUT opcodes (Step 2's dispatcher
//  routes those to in_port() / out_port() in Step 4).
// ============================================================================

#include <cstdint>

namespace invaderx {

// ----------------------------------------------------------------------------
//  I/O port map (writes and reads are on SEPARATE ports — IN and OUT are
//  distinct address spaces on the 8080, not memory-mapped).
// ----------------------------------------------------------------------------
//
//   READS (IN):
//      port 0  — credit / tilt status; the game never actually reads it
//      port 1  — Player 1 controls + coin                    (see masks below)
//      port 2  — Player 2 controls + DIP switches
//      port 3  — SHIFT REGISTER result (8 bits, offset by `shift_offset`)
//
//   WRITES (OUT):
//      port 2  — shift offset latch (low 3 bits used)
//      port 3  — sound bank A (UFO sound, shot, player die, invader die)
//      port 4  — shift register data input (see semantics below)
//      port 5  — sound bank B (fleet movement 1-4, UFO hit)
//      port 6  — watchdog timer reset (ignored by us)
//
// ─── The hardware shift register (the heart of the trick) ────────────────────
//  The 8080 can only rotate one bit per instruction. Space Invaders has to
//  blit sprites against video memory that is ROTATED 90° on the CRT, so a
//  single sprite scanline can land at any 0–7 bit offset. Doing that with
//  RAR/RAL would torch the per-frame cycle budget.
//
//  Taito's fix was an external 74-series 16-bit shift register with a
//  programmable barrel-shift tap:
//
//      OUT 4, v   shift_reg     := (uint16_t(v) << 8) | (shift_reg >> 8)
//                                  ^^^^^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^
//                                  new value goes to    old high half
//                                  the HIGH byte        slides down
//                                                       (old low is lost)
//
//      OUT 2, n   shift_offset  := n & 0x07
//
//      IN  3   →  result        := (shift_reg >> (8 - shift_offset)) & 0xFF
//
//  Two OUTs + one IN replaces ~16 RAR instructions. The game uses this
//  hundreds of times per frame to render aliens, the player, and shots.
//  Getting the offset math wrong is the #1 reason sprite rendering fails.
// ----------------------------------------------------------------------------
struct Hardware {
    // ----- Shift register (the trick described above) -----
    uint16_t shift_reg    = 0;
    uint8_t  shift_offset = 0;          // 0..7, written via OUT 2

    // ----- Input port mirrors -----
    //  Step 5's SDL event handler will set/clear bits on these in response
    //  to keyboard input. Defaults match a quiescent cabinet: nothing
    //  pressed, no coin, DIP-switches at factory settings.
    //
    //  Port 1: bit 3 is wired to +5V on the real PCB, so reads always
    //  return 1 there. Several games rely on this; we preload it.
    uint8_t  port1 = 0x08;
    uint8_t  port2 = 0x00;              // P2 controls + 3 lives, no extra ship

    // ----- Sound port latches -----
    //  Saved on every OUT 3 / OUT 5 so an audio backend (out of scope for
    //  now) can pick up edge transitions and trigger samples.
    uint8_t  port3_sound = 0;
    uint8_t  port5_sound = 0;

    // ----- Interrupt request state -----
    //  Space Invaders fires two interrupts per 60 Hz frame:
    //      RST 1 (vector 0x08)  at scanline 96   (mid-screen)
    //      RST 2 (vector 0x10)  at scanline 224  (start of VBLANK)
    //  The screen scan splits VRAM in half; the game does the top half
    //  of its draw pass on RST 1 and the bottom half on RST 2 to avoid
    //  tearing. If the CPU's INTE is false, the request is dropped.
    //
    //  Step 4 owns the timing; this struct just carries the pending flag
    //  and the opcode the CPU will execute when it services the interrupt
    //  (0xCF = RST 1, 0xD7 = RST 2).
    bool     interrupt_pending = false;
    uint8_t  interrupt_opcode  = 0;

    // ----- Bit masks for PORT 1 (P1 inputs) -----
    //  Mapped to the keyboard in Step 5:
    //      Enter        -> P1_COIN
    //      '1'          -> P1_START
    //      Space        -> P1_FIRE
    //      Left arrow   -> P1_LEFT
    //      Right arrow  -> P1_RIGHT
    static constexpr uint8_t P1_COIN  = 1 << 0;   // bit 0
    static constexpr uint8_t P1_START = 1 << 2;   // bit 2  (P1 start)
    static constexpr uint8_t P1_FIRE  = 1 << 4;   // bit 4
    static constexpr uint8_t P1_LEFT  = 1 << 5;   // bit 5
    static constexpr uint8_t P1_RIGHT = 1 << 6;   // bit 6

    // ----- Interrupt opcode constants -----
    //  These are the actual RST instruction bytes the 8080 will execute
    //  when the interrupt is acknowledged. They push PC and jump to the
    //  vector address (0x08 for RST 1, 0x10 for RST 2).
    static constexpr uint8_t INT_RST_1 = 0xCF;    // RST 1 -> 0x08
    static constexpr uint8_t INT_RST_2 = 0xD7;    // RST 2 -> 0x10
};

} // namespace invaderx
