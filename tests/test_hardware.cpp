// ============================================================================
//  InvaderX — src/test_hardware.cpp
//  Validation suite for hardware.cpp.
//
//   H1  Shift register: empty state
//   H2  Shift register: byte slides through both halves
//   H3  Shift register: barrel-shift at every offset 0..7  (bit-exact)
//   H4  Shift register: OUT 2 only honors the low 3 bits
//   H5  Input port 1: bit-3 hardwired, defaults to 0x08
//   H6  Input helpers: press / release / set
//   H7  Watchdog port 6 and unmapped ports are no-ops
//   H8  Scheduler: RST 1 fires at CYCLES_TO_RST1
//   H9  Scheduler: RST 2 fires at CYCLES_TO_RST2
//   H10 Scheduler: frame boundary triggers exactly once
//   H11 Scheduler: rolls over correctly across many frames
//   H12 End-to-end: CPU OUT 4 / OUT 2 / IN 3 round-trips through hardware
//   H13 End-to-end: full frame with EI/HLT and interrupt handlers running
//   H14 Strong overrides: weak stubs in cpu.cpp are NOT used at link time
// ============================================================================

#include "cpu.h"
#include "memory.h"
#include "hardware.h"
#include "cpu_step.h"
#include "hardware_ops.h"

#include <cstdio>

using namespace invaderx;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do {                                                 \
    if (cond) { ++g_pass; }                                              \
    else      { ++g_fail; std::printf("  FAIL %s:%d  %s\n",              \
                                      __FILE__, __LINE__, #cond); }      \
} while (0)

static void load_at(Memory& mem, uint16_t addr, std::initializer_list<uint8_t> bytes) {
    uint16_t a = addr;
    for (uint8_t b : bytes) mem.load_byte(a++, b);
}

// ----------------------------------------------------------------------------
//  H1
// ----------------------------------------------------------------------------
static void H1_empty_shift() {
    std::printf("H1  shift register empty state\n");
    Hardware h;
    // Empty shift_reg, offset 0 — should read 0.
    CHECK(cpu_in_port(h, 3) == 0);
}

// ----------------------------------------------------------------------------
//  H2 — byte slides through both halves
// ----------------------------------------------------------------------------
static void H2_shift_slide() {
    std::printf("H2  shift register slide\n");
    Hardware h;
    // OUT 4, 0xCD: shift_reg = 0xCD00
    cpu_out_port(h, 4, 0xCD);
    CHECK(h.shift_reg == 0xCD00);
    // OUT 4, 0xAB: shift_reg = (0xAB << 8) | (0xCD00 >> 8) = 0xABCD
    cpu_out_port(h, 4, 0xAB);
    CHECK(h.shift_reg == 0xABCD);
    // offset 0 -> high byte = 0xAB
    cpu_out_port(h, 2, 0);
    CHECK(cpu_in_port(h, 3) == 0xAB);
    // offset 8 not legal — only 3 bits honored. But offset 4 -> 0xBC
    cpu_out_port(h, 2, 4);
    CHECK(cpu_in_port(h, 3) == 0xBC);
}

// ----------------------------------------------------------------------------
//  H3 — barrel shift at EVERY offset, bit-exact
//       For shift_reg = 0xABCD:
//         offset 0: 0xAB    offset 4: 0xBC
//         offset 1: 0x57    offset 5: 0x79
//         offset 2: 0xAF    offset 6: 0xF3
//         offset 3: 0x5E    offset 7: 0xE6
// ----------------------------------------------------------------------------
static void H3_barrel_shift() {
    std::printf("H3  shift register barrel at every offset\n");
    Hardware h;
    cpu_out_port(h, 4, 0xCD);
    cpu_out_port(h, 4, 0xAB);
    CHECK(h.shift_reg == 0xABCD);

    const uint8_t expected[8] = { 0xAB, 0x57, 0xAF, 0x5E, 0xBC, 0x79, 0xF3, 0xE6 };
    for (int off = 0; off < 8; ++off) {
        cpu_out_port(h, 2, uint8_t(off));
        uint8_t got = cpu_in_port(h, 3);
        bool ok = got == expected[off];
        if (!ok) std::printf("  offset %d: got 0x%02X expected 0x%02X\n",
                             off, got, expected[off]);
        CHECK(ok);
    }
}

// ----------------------------------------------------------------------------
//  H4 — OUT 2 only honors low 3 bits
// ----------------------------------------------------------------------------
static void H4_offset_mask() {
    std::printf("H4  shift offset only honors low 3 bits\n");
    Hardware h;
    cpu_out_port(h, 2, 0xFF);                  // all bits set
    CHECK(h.shift_offset == 0x07);             // only low 3 land
    cpu_out_port(h, 2, 0xF0);                  // no low bits
    CHECK(h.shift_offset == 0x00);
}

// ----------------------------------------------------------------------------
//  H5 — Port 1: bit 3 hardwired
// ----------------------------------------------------------------------------
static void H5_port1_hardwired() {
    std::printf("H5  port 1 default = 0x08 (bit-3 hardwired)\n");
    Hardware h;
    CHECK(h.port1 == 0x08);
    CHECK(cpu_in_port(h, 1) == 0x08);
}

// ----------------------------------------------------------------------------
//  H6 — Input helpers
// ----------------------------------------------------------------------------
static void H6_input_helpers() {
    std::printf("H6  input press/release/set\n");
    Hardware h;
    hw_input_press(h, Hardware::P1_FIRE);
    CHECK((cpu_in_port(h, 1) & Hardware::P1_FIRE) != 0);
    CHECK((cpu_in_port(h, 1) & 0x08) != 0);   // bit-3 still hardwired

    hw_input_press(h, Hardware::P1_LEFT | Hardware::P1_COIN);
    CHECK((cpu_in_port(h, 1) & Hardware::P1_LEFT) != 0);
    CHECK((cpu_in_port(h, 1) & Hardware::P1_COIN) != 0);

    hw_input_release(h, Hardware::P1_FIRE);
    CHECK((cpu_in_port(h, 1) & Hardware::P1_FIRE) == 0);
    CHECK((cpu_in_port(h, 1) & Hardware::P1_LEFT) != 0);   // others untouched

    hw_input_set(h, Hardware::P1_RIGHT, true);
    CHECK((cpu_in_port(h, 1) & Hardware::P1_RIGHT) != 0);
    hw_input_set(h, Hardware::P1_RIGHT, false);
    CHECK((cpu_in_port(h, 1) & Hardware::P1_RIGHT) == 0);
}

// ----------------------------------------------------------------------------
//  H7 — Watchdog and unmapped ports
// ----------------------------------------------------------------------------
static void H7_watchdog_unmapped() {
    std::printf("H7  watchdog + unmapped ports = no-op\n");
    Hardware h;
    uint16_t shift_before = h.shift_reg;
    uint8_t  port1_before = h.port1;
    cpu_out_port(h, 6, 0x42);                  // watchdog
    cpu_out_port(h, 7, 0xFF);                  // unmapped
    cpu_out_port(h, 0, 0xAA);                  // unmapped write
    CHECK(h.shift_reg == shift_before);
    CHECK(h.port1     == port1_before);
}

// ----------------------------------------------------------------------------
//  H8 — Scheduler RST 1 timing
// ----------------------------------------------------------------------------
static void H8_sched_rst1() {
    std::printf("H8  scheduler fires RST 1 at scanline 96\n");
    Hardware h;
    InterruptScheduler sched;
    sched.reset();
    // Just-before threshold: nothing fired.
    CHECK(!sched.tick(h, uint64_t(timing::CYCLES_TO_RST1 - 1)));
    CHECK(!h.interrupt_pending);
    // At threshold: RST 1 fires.
    CHECK(!sched.tick(h, uint64_t(timing::CYCLES_TO_RST1)));
    CHECK(h.interrupt_pending);
    CHECK(h.interrupt_opcode == Hardware::INT_RST_1);
    CHECK(sched.phase == 1);
}

// ----------------------------------------------------------------------------
//  H9 — Scheduler RST 2 timing
// ----------------------------------------------------------------------------
static void H9_sched_rst2() {
    std::printf("H9  scheduler fires RST 2 at scanline 224\n");
    Hardware h;
    InterruptScheduler sched;
    sched.reset();
    // Skip past RST 1.
    sched.tick(h, uint64_t(timing::CYCLES_TO_RST1));
    h.interrupt_pending = false;  // simulate CPU ack
    // Just before RST 2 threshold.
    CHECK(!sched.tick(h, uint64_t(timing::CYCLES_TO_RST2 - 1)));
    CHECK(!h.interrupt_pending);
    // At threshold.
    CHECK(!sched.tick(h, uint64_t(timing::CYCLES_TO_RST2)));
    CHECK(h.interrupt_pending);
    CHECK(h.interrupt_opcode == Hardware::INT_RST_2);
    CHECK(sched.phase == 2);
}

// ----------------------------------------------------------------------------
//  H10 — Frame boundary fires exactly once per frame
// ----------------------------------------------------------------------------
static void H10_frame_boundary() {
    std::printf("H10 scheduler returns frame-boundary exactly once\n");
    Hardware h;
    InterruptScheduler sched;
    sched.reset();
    sched.tick(h, uint64_t(timing::CYCLES_TO_RST1));   h.interrupt_pending = false;
    sched.tick(h, uint64_t(timing::CYCLES_TO_RST2));   h.interrupt_pending = false;
    // Just before end of frame.
    CHECK(!sched.tick(h, uint64_t(display::CYCLES_PER_FRAME - 1)));
    // Frame boundary.
    CHECK( sched.tick(h, uint64_t(display::CYCLES_PER_FRAME)));
    // Subsequent calls in the SAME frame: no second boundary.
    CHECK(!sched.tick(h, uint64_t(display::CYCLES_PER_FRAME + 100)));
    CHECK(sched.phase == 0);
}

// ----------------------------------------------------------------------------
//  H11 — Multi-frame rollover; no drift
// ----------------------------------------------------------------------------
static void H11_multi_frame() {
    std::printf("H11 scheduler runs 1000 frames without drift\n");
    Hardware h;
    InterruptScheduler sched;
    sched.reset();

    int rst1_count = 0, rst2_count = 0, frame_count = 0;
    uint64_t cycle = 0;
    const uint64_t step = 1000;     // coarse — every 1k cycles is fine
    const int FRAMES = 1000;

    while (frame_count < FRAMES) {
        cycle += step;
        // Snapshot whether an interrupt got raised this tick.
        bool was_pending = h.interrupt_pending;
        uint8_t prev_op  = h.interrupt_opcode;

        bool frame_done = sched.tick(h, cycle);

        // Count NEW raises (transition from not-pending or new opcode).
        if (h.interrupt_pending && (!was_pending || h.interrupt_opcode != prev_op)) {
            if (h.interrupt_opcode == Hardware::INT_RST_1) ++rst1_count;
            if (h.interrupt_opcode == Hardware::INT_RST_2) ++rst2_count;
            h.interrupt_pending = false;  // simulate CPU ack so we can detect the next raise
        }
        if (frame_done) ++frame_count;
    }

    CHECK(frame_count == FRAMES);
    CHECK(rst1_count  == FRAMES);
    CHECK(rst2_count  == FRAMES);
}

// ----------------------------------------------------------------------------
//  H12 — End-to-end: CPU OUT 4 / OUT 2 / IN 3 round-trip
//        Verifies that the dispatcher's 0xD3 / 0xDB opcodes correctly call
//        through cpu_out_port / cpu_in_port to the hardware implementation.
// ----------------------------------------------------------------------------
static void H12_cpu_io_roundtrip() {
    std::printf("H12 CPU OUT/IN routes through hardware (round-trip)\n");
    CPU c; Memory m; Hardware h;
    // MVI A, 0xCD ; OUT 4 ; MVI A, 0xAB ; OUT 4 ;
    // MVI A, 4    ; OUT 2 ; IN 3        ; HLT
    load_at(m, 0x0000, {
        0x3E, 0xCD, 0xD3, 0x04,
        0x3E, 0xAB, 0xD3, 0x04,
        0x3E, 0x04, 0xD3, 0x02,
        0xDB, 0x03,
        0x76,
    });
    while (!c.halted) cpu_step(c, m, h);
    CHECK(h.shift_reg    == 0xABCD);
    CHECK(h.shift_offset == 0x04);
    CHECK(c.a            == 0xBC);  // (0xABCD >> 4) & 0xFF
}

// ----------------------------------------------------------------------------
//  H13 — End-to-end: full frame with EI / HLT and RST handlers
//        Proves the whole chain: scheduler → cpu_request_interrupt →
//        cpu_step injects RST → handler executes → EI re-enables → next
//        interrupt also fires.
// ----------------------------------------------------------------------------
static void H13_full_frame() {
    std::printf("H13 full frame: EI/HLT + RST 1 + RST 2 handlers fire once each\n");
    CPU c; Memory m; Hardware h;
    InterruptScheduler sched;
    sched.reset();
    c.sp = 0x2400;

    // Main:  LXI SP,0x2400 ; EI ; HLT ; JMP 0x0004 (back to HLT)
    load_at(m, 0x0000, {
        0x31, 0x00, 0x24,        // 0x0000 LXI SP,0x2400
        0xFB,                    // 0x0003 EI
        0x76,                    // 0x0004 HLT
        0xC3, 0x04, 0x00,        // 0x0005 JMP 0x0004
    });
    // RST 1 handler @ 0x0008: increment [0x2300]; EI; RET
    load_at(m, 0x0008, {
        0xE5,                    // PUSH H
        0x21, 0x00, 0x23,        // LXI H, 0x2300
        0x34,                    // INR M
        0xE1,                    // POP H
        0xFB,                    // EI
        0xC9,                    // RET
    });
    // RST 2 handler @ 0x0010: increment [0x2301]; EI; RET
    load_at(m, 0x0010, {
        0xE5,
        0x21, 0x01, 0x23,
        0x34,
        0xE1,
        0xFB,
        0xC9,
    });

    // Pump until the scheduler reports a frame boundary.
    bool frame_done = false;
    int safety = 200000;
    while (!frame_done && safety-- > 0) {
        cpu_step(c, m, h);
        frame_done = sched.tick(h, c.cycles);
    }
    CHECK(frame_done);
    CHECK(m.read(0x2300) == 1);   // RST 1 ran exactly once
    CHECK(m.read(0x2301) == 1);   // RST 2 ran exactly once

    // Run another frame; counts should advance to 2.
    frame_done = false;
    safety = 200000;
    while (!frame_done && safety-- > 0) {
        cpu_step(c, m, h);
        frame_done = sched.tick(h, c.cycles);
    }
    CHECK(m.read(0x2300) == 2);
    CHECK(m.read(0x2301) == 2);
}

// ----------------------------------------------------------------------------
//  H14 — Linker correctness: strong cpu_in_port / cpu_out_port from
//        hardware.cpp must override the weak stubs in cpu.cpp. We can
//        detect this empirically: the weak stub returns 0 unconditionally;
//        the real impl returns 0x08 for port 1 (bit-3 hardwired). H12
//        already proves out the OUT-side. This one nails IN.
// ----------------------------------------------------------------------------
static void H14_strong_override() {
    std::printf("H14 strong cpu_in_port overrides weak stub\n");
    Hardware h;                     // port1 starts at 0x08
    uint8_t v = cpu_in_port(h, 1);
    CHECK(v == 0x08);               // weak stub would have returned 0
}

int main() {
    H1_empty_shift();
    H2_shift_slide();
    H3_barrel_shift();
    H4_offset_mask();
    H5_port1_hardwired();
    H6_input_helpers();
    H7_watchdog_unmapped();
    H8_sched_rst1();
    H9_sched_rst2();
    H10_frame_boundary();
    H11_multi_frame();
    H12_cpu_io_roundtrip();
    H13_full_frame();
    H14_strong_override();

    std::printf("\nPASS %d   FAIL %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
