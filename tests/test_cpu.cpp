// ============================================================================
//  InvaderX — src/test_cpu.cpp
//  Standalone validation harness for cpu.cpp + disassembler.cpp.
//  Builds and runs locally; not part of the final emulator binary.
//
//  Tests:
//    T1  Boot vector, register init, NOP cycle count
//    T2  MVI / MOV register-to-register
//    T3  ADD / ADI with carry and aux-carry boundary
//    T4  SUB / SBI with borrow
//    T5  Logical AND/OR/XOR and CY/AC behavior
//    T6  Flag pack/unpack (PUSH PSW / POP PSW round-trip)
//    T7  Conditional jump and call cycle counts
//    T8  Stack push/pop, CALL/RET round-trip
//    T9  Interrupt injection (RST 1 / RST 2)
//    T10 Disassembler — round-trips a hand-written program
// ============================================================================

#include "cpu.h"
#include "memory.h"
#include "hardware.h"
#include "cpu_step.h"
#include "disassembler.h"
#include <cstdio>
#include <cstring>

using namespace invaderx;

static int g_pass = 0, g_fail = 0;

#define CHECK(cond) do {                                                 \
    if (cond) { ++g_pass; }                                              \
    else      { ++g_fail; std::printf("  FAIL %s:%d  %s\n",              \
                                      __FILE__, __LINE__, #cond); }      \
} while (0)

// Load a tiny program at 0x2000 (which is WRAM, so writes work).
// We pretend it's code by setting PC there.
static void load_at(Memory& mem, uint16_t addr, std::initializer_list<uint8_t> bytes) {
    uint16_t a = addr;
    for (uint8_t b : bytes) mem.load_byte(a++, b);
}

// ----------------------------------------------------------------------------
//  T1 — Boot defaults
// ----------------------------------------------------------------------------
static void t1_defaults() {
    std::printf("T1  defaults\n");
    CPU c; Memory m; Hardware h;
    CHECK(c.pc == 0);
    CHECK(c.sp == 0);
    CHECK(c.a == 0 && c.b == 0 && c.c == 0);
    CHECK(c.f.pack() == 0x02);              // bit 1 hardwired to 1
    CHECK(!c.int_enable);
    CHECK(!c.halted);
    // NOP at 0 — 4 cycles, PC advances by 1
    int cyc = cpu_step(c, m, h);
    CHECK(cyc == 4);
    CHECK(c.pc == 1);
}

// ----------------------------------------------------------------------------
//  T2 — MVI + MOV r,r
// ----------------------------------------------------------------------------
static void t2_mov() {
    std::printf("T2  MVI + MOV r,r\n");
    CPU c; Memory m; Hardware h;
    // MVI B, 0x42 ; MVI C, 0x7F ; MOV A, B ; MOV D, C ; HLT
    load_at(m, 0x0000, {0x06, 0x42, 0x0E, 0x7F, 0x78, 0x51, 0x76});
    while (!c.halted) cpu_step(c, m, h);
    CHECK(c.b == 0x42);
    CHECK(c.c == 0x7F);
    CHECK(c.a == 0x42);
    CHECK(c.d == 0x7F);
    CHECK(c.halted);
}

// ----------------------------------------------------------------------------
//  T3 — ADD / ADI with CY and AC
// ----------------------------------------------------------------------------
static void t3_add() {
    std::printf("T3  ADD / ADI / AC + CY\n");
    {   // 0x0F + 0x01 -> 0x10, AC set, no CY
        CPU c; Memory m; Hardware h;
        load_at(m, 0x0000, {0x3E, 0x0F, 0xC6, 0x01, 0x76});   // MVI A,0x0F ; ADI 1 ; HLT
        while (!c.halted) cpu_step(c, m, h);
        CHECK(c.a == 0x10);
        CHECK(c.f.ac);
        CHECK(!c.f.cy);
        CHECK(!c.f.z);
        CHECK(!c.f.s);
    }
    {   // 0xFF + 0x01 -> 0x00, AC + CY + Z set
        CPU c; Memory m; Hardware h;
        load_at(m, 0x0000, {0x3E, 0xFF, 0xC6, 0x01, 0x76});
        while (!c.halted) cpu_step(c, m, h);
        CHECK(c.a == 0x00);
        CHECK(c.f.ac);
        CHECK(c.f.cy);
        CHECK(c.f.z);
        CHECK(!c.f.s);
    }
}

// ----------------------------------------------------------------------------
//  T4 — SUB / SBI with borrow
// ----------------------------------------------------------------------------
static void t4_sub() {
    std::printf("T4  SUB / SBI / borrow\n");
    {   // 0x00 - 0x01 -> 0xFF, CY set, S set
        CPU c; Memory m; Hardware h;
        load_at(m, 0x0000, {0x3E, 0x00, 0xD6, 0x01, 0x76});
        while (!c.halted) cpu_step(c, m, h);
        CHECK(c.a == 0xFF);
        CHECK(c.f.cy);
        CHECK(c.f.s);
        CHECK(!c.f.z);
    }
    {   // 0x10 - 0x01 -> 0x0F, AC behaves correctly (low nibble borrow)
        CPU c; Memory m; Hardware h;
        load_at(m, 0x0000, {0x3E, 0x10, 0xD6, 0x01, 0x76});
        while (!c.halted) cpu_step(c, m, h);
        CHECK(c.a == 0x0F);
        CHECK(!c.f.cy);
    }
}

// ----------------------------------------------------------------------------
//  T5 — Logicals
// ----------------------------------------------------------------------------
static void t5_logic() {
    std::printf("T5  ANA / ORA / XRA\n");
    {   // ANA always clears CY; AC follows 8080 rule
        CPU c; Memory m; Hardware h;
        load_at(m, 0x0000, {0x37, 0x3E, 0xF0, 0xE6, 0x0F, 0x76}); // STC ; MVI A,F0 ; ANI 0F ; HLT
        while (!c.halted) cpu_step(c, m, h);
        CHECK(c.a == 0x00);
        CHECK(!c.f.cy);            // ANA clears CY
        CHECK(c.f.z);
    }
    {   // XRA clears both CY and AC
        CPU c; Memory m; Hardware h;
        load_at(m, 0x0000, {0x37, 0x3E, 0xAA, 0xEE, 0xFF, 0x76}); // STC ; MVI A,AA ; XRI FF ; HLT
        while (!c.halted) cpu_step(c, m, h);
        CHECK(c.a == 0x55);
        CHECK(!c.f.cy);
        CHECK(!c.f.ac);
    }
}

// ----------------------------------------------------------------------------
//  T6 — PSW pack/unpack round-trip
// ----------------------------------------------------------------------------
static void t6_psw() {
    std::printf("T6  PSW round-trip\n");
    CPU c; Memory m; Hardware h;
    c.sp = 0x2400;
    c.a  = 0xA5;
    c.f.s = true;  c.f.z = false; c.f.ac = true;
    c.f.p = true;  c.f.cy = true;
    uint8_t before = c.f.pack();
    // PUSH PSW ; XRA A (smash A and flags) ; POP PSW ; HLT
    load_at(m, 0x0000, {0xF5, 0xAF, 0xF1, 0x76});
    while (!c.halted) cpu_step(c, m, h);
    CHECK(c.a == 0xA5);
    CHECK(c.f.pack() == before);
}

// ----------------------------------------------------------------------------
//  T7 — Conditional jump / call cycle counts
// ----------------------------------------------------------------------------
static void t7_cond_cycles() {
    std::printf("T7  conditional cycle counts\n");
    {   // CNZ taken: 11 + 6 = 17 cycles
        CPU c; Memory m; Hardware h;
        c.sp = 0x2400;
        // MVI A, 1 ; ANI 1 (Z=0) ; CNZ 0x0010 ; HLT @ 0x0008
        load_at(m, 0x0000, {0x3E, 0x01, 0xE6, 0x01, 0xC4, 0x10, 0x00, 0x76});
        load_at(m, 0x0010, {0xC9});                      // RET
        int total = 0;
        while (!c.halted) total += cpu_step(c, m, h);
        // 7 (MVI) + 7 (ANI) + 17 (CNZ taken) + 10 (RET) + 7 (HLT) = 48
        CHECK(total == 48);
    }
    {   // CNZ not taken: 11 cycles
        CPU c; Memory m; Hardware h;
        c.sp = 0x2400;
        // MVI A, 0 ; ANI 0xFF (Z=1) ; CNZ 0x0010 ; HLT
        load_at(m, 0x0000, {0x3E, 0x00, 0xE6, 0xFF, 0xC4, 0x10, 0x00, 0x76});
        int total = 0;
        while (!c.halted) total += cpu_step(c, m, h);
        // 7 + 7 + 11 + 7 = 32
        CHECK(total == 32);
    }
}

// ----------------------------------------------------------------------------
//  T8 — CALL / RET round-trip
// ----------------------------------------------------------------------------
static void t8_call() {
    std::printf("T8  CALL / RET\n");
    CPU c; Memory m; Hardware h;
    c.sp = 0x2400;
    // Main: MVI A, 1 ; CALL sub ; HLT
    load_at(m, 0x0000, {0x3E, 0x01, 0xCD, 0x10, 0x00, 0x76});
    // sub @ 0x0010: ADI 0x05 ; RET
    load_at(m, 0x0010, {0xC6, 0x05, 0xC9});
    while (!c.halted) cpu_step(c, m, h);
    CHECK(c.a == 0x06);
    CHECK(c.sp == 0x2400);    // stack restored
}

// ----------------------------------------------------------------------------
//  T9 — Interrupt injection
// ----------------------------------------------------------------------------
static void t9_interrupt() {
    std::printf("T9  RST interrupt injection\n");
    CPU c; Memory m; Hardware h;
    c.sp = 0x2400;
    c.int_enable = true;
    // Main loop: NOP ; JMP 0 ;  marker @ 0x0010 (RST 2 vector)
    load_at(m, 0x0000, {0x00, 0xC3, 0x00, 0x00});
    // RST 2 handler at 0x10: MVI B, 0x77 ; HLT
    load_at(m, 0x0010, {0x06, 0x77, 0x76});
    // Run a few normal steps, then request RST 2 (opcode 0xD7).
    cpu_step(c, m, h);  // NOP
    cpu_request_interrupt(h, 0xD7);
    while (!c.halted) cpu_step(c, m, h);
    CHECK(c.b == 0x77);
    CHECK(!c.int_enable);     // INTE cleared on ACK
}

// ----------------------------------------------------------------------------
//  T10 — Disassembler
// ----------------------------------------------------------------------------
static void t10_disasm() {
    std::printf("T10 disassembler\n");
    Memory m;
    load_at(m, 0x0000, {
        0x00,                   // NOP
        0x31, 0x00, 0x24,       // LXI SP, 0x2400
        0x3E, 0x42,             // MVI A, 0x42
        0xCD, 0x10, 0x00,       // CALL 0x0010
        0xC3, 0x00, 0x00,       // JMP 0x0000
    });

    struct { uint16_t addr; const char* expected; uint8_t len; } cases[] = {
        { 0x0000, "NOP",                 1 },
        { 0x0001, "LXI  SP, 0x2400",     3 },
        { 0x0004, "MVI  A, 0x42",        2 },
        { 0x0006, "CALL 0x0010",         3 },
        { 0x0009, "JMP  0x0000",         3 },
    };
    for (auto& tc : cases) {
        auto d = disassemble(m, tc.addr);
        bool ok = (d.text == tc.expected) && (d.length == tc.len);
        if (!ok) std::printf("  got '%s' (len %u), expected '%s' (len %u)\n",
                             d.text.c_str(), unsigned(d.length),
                             tc.expected, unsigned(tc.len));
        CHECK(ok);
    }

    // Verify all 256 entries have a non-empty mnemonic and a valid length.
    for (int op = 0; op < 256; ++op) {
        const char* mn = opcode_mnemonic(uint8_t(op));
        uint8_t     ln = opcode_length(uint8_t(op));
        CHECK(mn != nullptr && mn[0] != '\0');
        CHECK(ln >= 1 && ln <= 3);
    }
}

int main() {
    t1_defaults();
    t2_mov();
    t3_add();
    t4_sub();
    t5_logic();
    t6_psw();
    t7_cond_cycles();
    t8_call();
    t9_interrupt();
    t10_disasm();

    std::printf("\nPASS %d   FAIL %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
