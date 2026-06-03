// ============================================================================
//  InvaderX — src/cpu.cpp
//  Intel 8080 instruction dispatcher.
//
//  Layout of this file:
//    §1  Helpers : parity, flag updaters, ALU primitives.
//    §2  Register access (read_reg / write_reg, M = memory at HL).
//    §3  Stack helpers (push16 / pop16).
//    §4  Cycle-count table (cycles_8080[256], "not taken" baseline).
//    §5  I/O port hooks (weak stubs; Step 4 supplies real ones).
//    §6  Interrupt-request helper.
//    §7  execute()        — the 256-case dispatcher.
//    §8  cpu_step()       — public entry; handles interrupt injection.
//
//  References:
//    • Intel 8080 Programmer's Manual (1975), opcode reference tables.
//    • Computer Archaeology — Space Invaders hardware reference.
//    • superzazu/8080 (passes 8080EXM.COM); cross-checked our flag math.
// ============================================================================

#include "cpu_step.h"
#include <cstdint>

namespace invaderx {

// ============================================================================
//  §1  Flag and ALU helpers
// ============================================================================

// Even-parity test — 8080 sets P when the result has an EVEN number of 1-bits.
// XOR-fold trick: O(1), branchless.
static inline bool parity_even(uint8_t v) {
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (v & 1) == 0;
}

// Update Sign / Zero / Parity from an 8-bit result.
// Carry and Aux-Carry are NOT touched — they're op-specific.
static inline void update_szp(CPU& cpu, uint8_t result) {
    cpu.f.s = (result & 0x80) != 0;
    cpu.f.z = (result == 0);
    cpu.f.p = parity_even(result);
}

// 8-bit add with carry-in. Sets S, Z, AC, P, CY. Returns the truncated result.
// AC = carry out of bit 3 of (a_low + b_low + cy_in). Standard 8080 formula.
static inline uint8_t add8(CPU& cpu, uint8_t a, uint8_t b, uint8_t cy_in) {
    uint16_t r16 = uint16_t(a) + uint16_t(b) + uint16_t(cy_in);
    cpu.f.cy = r16 > 0xFF;
    cpu.f.ac = ((a & 0x0F) + (b & 0x0F) + cy_in) > 0x0F;
    uint8_t r = uint8_t(r16);
    update_szp(cpu, r);
    return r;
}

// 8-bit subtract with borrow-in. Implemented as a + (~b) + (1 - borrow)
// because that's exactly how the silicon does it — and using the additive
// form keeps the AC computation identical to ADD.
static inline uint8_t sub8(CPU& cpu, uint8_t a, uint8_t b, uint8_t borrow) {
    uint8_t  bcompl = uint8_t(~b);
    uint8_t  cin    = 1 - borrow;
    uint16_t r16    = uint16_t(a) + uint16_t(bcompl) + uint16_t(cin);
    // On the 8080, CY for SUB is set when there IS a borrow, i.e.
    // when the adder did NOT produce a carry out. Hence the negation.
    cpu.f.cy = !(r16 > 0xFF);
    cpu.f.ac = ((a & 0x0F) + (bcompl & 0x0F) + cin) > 0x0F;
    uint8_t r = uint8_t(r16);
    update_szp(cpu, r);
    return r;
}

// ANA — bitwise AND.
// 8080 quirk: AC is set to ((A | operand) & 0x08) != 0. This is documented
// in the Intel manual and is the result of the AND being implemented via
// AND-then-OR gates that leak the high bit of bit 3 into AC.
static inline void ana8(CPU& cpu, uint8_t v) {
    uint8_t r = cpu.a & v;
    cpu.f.cy = false;
    cpu.f.ac = ((cpu.a | v) & 0x08) != 0;
    cpu.a    = r;
    update_szp(cpu, r);
}

// XRA — bitwise XOR. CY and AC are always cleared.
static inline void xra8(CPU& cpu, uint8_t v) {
    cpu.a    ^= v;
    cpu.f.cy  = false;
    cpu.f.ac  = false;
    update_szp(cpu, cpu.a);
}

// ORA — bitwise OR. CY and AC are always cleared.
static inline void ora8(CPU& cpu, uint8_t v) {
    cpu.a    |= v;
    cpu.f.cy  = false;
    cpu.f.ac  = false;
    update_szp(cpu, cpu.a);
}

// CMP — compare A with operand. Identical to SUB but the result is discarded;
// only flags are updated.
static inline void cmp8(CPU& cpu, uint8_t v) {
    (void)sub8(cpu, cpu.a, v, 0);
}

// INR — 8-bit increment. Sets S, Z, AC, P. CY is preserved.
static inline uint8_t inr8(CPU& cpu, uint8_t v) {
    uint8_t r = v + 1;
    cpu.f.ac  = (v & 0x0F) == 0x0F;     // low nibble was 0xF -> carry to bit 4
    update_szp(cpu, r);
    return r;
}

// DCR — 8-bit decrement. Sets S, Z, AC, P. CY is preserved.
// AC for DCR: cleared when the low nibble had to borrow (was 0).
static inline uint8_t dcr8(CPU& cpu, uint8_t v) {
    uint8_t r = v - 1;
    cpu.f.ac  = !((v & 0x0F) == 0x00);  // no borrow from bit 4 unless low nibble was 0
    update_szp(cpu, r);
    return r;
}

// DAD — 16-bit add to HL. Only CY is updated; S/Z/AC/P are preserved.
static inline void dad16(CPU& cpu, uint16_t v) {
    uint32_t r32 = uint32_t(cpu.hl()) + uint32_t(v);
    cpu.f.cy = r32 > 0xFFFF;
    cpu.set_hl(uint16_t(r32));
}

// DAA — Decimal Adjust Accumulator. Fixes A after a BCD addition.
//   Algorithm (Intel manual, p. 17):
//     1. If (A_low > 9) OR AC: A += 0x06, AC updated.
//     2. If (A_high > 9) OR CY (after step 1): A += 0x60, CY set.
//     S, Z, P updated from the final A.
static inline void daa(CPU& cpu) {
    uint8_t correction = 0;
    bool    set_cy     = cpu.f.cy;

    uint8_t lsn = cpu.a & 0x0F;
    if (lsn > 9 || cpu.f.ac) {
        correction |= 0x06;
    }

    uint8_t msn = (cpu.a >> 4) & 0x0F;
    if (msn > 9 || cpu.f.cy || (msn >= 9 && lsn > 9)) {
        correction |= 0x60;
        set_cy = true;
    }

    // AC reflects the carry-out of bit 3 of the correction add.
    cpu.f.ac = ((cpu.a & 0x0F) + (correction & 0x0F)) > 0x0F;
    cpu.a   += correction;
    cpu.f.cy = set_cy;
    update_szp(cpu, cpu.a);
}

// ============================================================================
//  §2  Register access by 3-bit index (the 8080's standard r-field encoding)
//      0 = B, 1 = C, 2 = D, 3 = E, 4 = H, 5 = L, 6 = M (mem[HL]), 7 = A.
//      MOV / ALU groups all use this encoding.
// ============================================================================

static inline uint8_t read_reg(CPU& cpu, Memory& mem, int idx) {
    switch (idx) {
        case 0: return cpu.b;
        case 1: return cpu.c;
        case 2: return cpu.d;
        case 3: return cpu.e;
        case 4: return cpu.h;
        case 5: return cpu.l;
        case 6: return mem.read(cpu.hl());   // M
        case 7: return cpu.a;
    }
    return 0;  // unreachable
}

static inline void write_reg(CPU& cpu, Memory& mem, int idx, uint8_t v) {
    switch (idx) {
        case 0: cpu.b = v; break;
        case 1: cpu.c = v; break;
        case 2: cpu.d = v; break;
        case 3: cpu.e = v; break;
        case 4: cpu.h = v; break;
        case 5: cpu.l = v; break;
        case 6: mem.write(cpu.hl(), v); break;
        case 7: cpu.a = v; break;
    }
}

// ============================================================================
//  §3  Stack helpers — 8080 push/pop are 16-bit, big-endian on the wire
//      (high byte at SP-1, low byte at SP-2). Stack grows DOWNWARD.
// ============================================================================

static inline void push16(CPU& cpu, Memory& mem, uint16_t v) {
    mem.write(uint16_t(cpu.sp - 1), uint8_t(v >> 8));
    mem.write(uint16_t(cpu.sp - 2), uint8_t(v & 0xFF));
    cpu.sp -= 2;
}

static inline uint16_t pop16(CPU& cpu, Memory& mem) {
    uint16_t lo = mem.read(cpu.sp);
    uint16_t hi = mem.read(uint16_t(cpu.sp + 1));
    cpu.sp += 2;
    return uint16_t((hi << 8) | lo);
}

// ============================================================================
//  §4  Cycle count baseline for every opcode.
//      Conditional CALL / RET cases ADD extra cycles when the condition is
//      taken; we patch those inside the case.
//
//      Source: Intel 8080 Programmer's Manual, "Instruction Set" appendix,
//      cross-checked against superzazu/8080 (passes 8080EXM.COM).
// ============================================================================

static const uint8_t cycles_8080[256] = {
//  +0  +1  +2  +3  +4  +5  +6  +7  +8  +9  +A  +B  +C  +D  +E  +F
    4, 10,  7,  5,  5,  5,  7,  4,  4, 10,  7,  5,  5,  5,  7,  4, // 0x00
    4, 10,  7,  5,  5,  5,  7,  4,  4, 10,  7,  5,  5,  5,  7,  4, // 0x10
    4, 10, 16,  5,  5,  5,  7,  4,  4, 10, 16,  5,  5,  5,  7,  4, // 0x20
    4, 10, 13,  5, 10, 10, 10,  4,  4, 10, 13,  5,  5,  5,  7,  4, // 0x30
    5,  5,  5,  5,  5,  5,  7,  5,  5,  5,  5,  5,  5,  5,  7,  5, // 0x40
    5,  5,  5,  5,  5,  5,  7,  5,  5,  5,  5,  5,  5,  5,  7,  5, // 0x50
    5,  5,  5,  5,  5,  5,  7,  5,  5,  5,  5,  5,  5,  5,  7,  5, // 0x60
    7,  7,  7,  7,  7,  7,  7,  7,  5,  5,  5,  5,  5,  5,  7,  5, // 0x70
    4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4, // 0x80
    4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4, // 0x90
    4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4, // 0xA0
    4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4, // 0xB0
    5, 10, 10, 10, 11, 11,  7, 11,  5, 10, 10, 10, 11, 17,  7, 11, // 0xC0
    5, 10, 10, 10, 11, 11,  7, 11,  5, 10, 10, 10, 11, 17,  7, 11, // 0xD0
    5, 10, 10, 18, 11, 11,  7, 11,  5,  5, 10,  5, 11, 17,  7, 11, // 0xE0
    5, 10, 10,  4, 11, 11,  7, 11,  5,  5, 10,  4, 11, 17,  7, 11, // 0xF0
};

// ============================================================================
//  §5  I/O port hooks — WEAK STUBS provided here so cpu.cpp links standalone.
//      Step 4's hardware.cpp will provide strong definitions and the linker
//      will prefer those. (GCC weak attribute — supported by MinGW.)
// ============================================================================

__attribute__((weak)) uint8_t cpu_in_port(Hardware& /*hw*/, uint8_t /*port*/) {
    return 0;
}
__attribute__((weak)) void cpu_out_port(Hardware& /*hw*/, uint8_t /*port*/, uint8_t /*v*/) {
}

// ============================================================================
//  §6  Interrupt-request helper. Step 4 (frame timer) calls this twice per
//      frame — once with RST 1 at scanline 96 and once with RST 2 at 224.
// ============================================================================

void cpu_request_interrupt(Hardware& hw, uint8_t rst_opcode) {
    hw.interrupt_opcode  = rst_opcode;
    hw.interrupt_pending = true;
}

// Read-only accessor over the cycle table (for tooling / docs / tracing).
int opcode_base_cycles(uint8_t opcode) {
    return int(cycles_8080[opcode]);
}

// ============================================================================
//  §7  Condition test for Jcc / Ccc / Rcc.
//      The opcode's bits 5..3 encode the condition (NZ, Z, NC, C, PO, PE, P, M).
// ============================================================================

static inline bool check_cond(const CPU& cpu, uint8_t op) {
    switch ((op >> 3) & 0x07) {
        case 0: return !cpu.f.z;     // NZ
        case 1: return  cpu.f.z;     // Z
        case 2: return !cpu.f.cy;    // NC
        case 3: return  cpu.f.cy;    // C
        case 4: return !cpu.f.p;     // PO (parity odd)
        case 5: return  cpu.f.p;     // PE (parity even)
        case 6: return !cpu.f.s;     // P  (positive)
        case 7: return  cpu.f.s;     // M  (negative)
    }
    return false; // unreachable
}

// ============================================================================
//  §8  execute() — the 256-case dispatcher.
//      Returns the cycle count consumed (cycles_8080[op] plus any
//      conditional bonus).
//
//      The MOV r,r block (0x40-0x7F) and the ALU r block (0x80-0xBF) are
//      collapsed using GCC's case-range extension because (a) the project
//      explicitly targets GCC via MinGW-w64 and (b) the alternative — 128
//      near-identical case labels — actively hurts readability.
// ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"   // suppress case-range warning

static int execute(CPU& cpu, Memory& mem, Hardware& hw, uint8_t op) {
    int extra_cycles = 0;     // added for "condition taken" on Ccc / Rcc

    switch (op) {

    // --------------------------------------------------------------------
    //  0x00-0x0F  : NOP, LXI B, STAX B, INX B, INR/DCR B, MVI B,
    //               RLC, [alt NOP], DAD B, LDAX B, DCX B, INR/DCR C, MVI C, RRC
    // --------------------------------------------------------------------
    case 0x00: /* NOP */                                                  break;
    case 0x01: cpu.set_bc(mem.read16(cpu.pc)); cpu.pc += 2;               break; // LXI B,d16
    case 0x02: mem.write(cpu.bc(), cpu.a);                                break; // STAX B
    case 0x03: cpu.set_bc(uint16_t(cpu.bc() + 1));                        break; // INX B
    case 0x04: cpu.b = inr8(cpu, cpu.b);                                  break; // INR B
    case 0x05: cpu.b = dcr8(cpu, cpu.b);                                  break; // DCR B
    case 0x06: cpu.b = mem.read(cpu.pc++);                                break; // MVI B,d8
    case 0x07: { // RLC — rotate A left, bit 7 goes to CY and to bit 0
        uint8_t bit7 = (cpu.a >> 7) & 1;
        cpu.a = uint8_t((cpu.a << 1) | bit7);
        cpu.f.cy = bit7 != 0;
        break;
    }
    case 0x08: /* undocumented NOP */                                     break;
    case 0x09: dad16(cpu, cpu.bc());                                      break; // DAD B
    case 0x0A: cpu.a = mem.read(cpu.bc());                                break; // LDAX B
    case 0x0B: cpu.set_bc(uint16_t(cpu.bc() - 1));                        break; // DCX B
    case 0x0C: cpu.c = inr8(cpu, cpu.c);                                  break; // INR C
    case 0x0D: cpu.c = dcr8(cpu, cpu.c);                                  break; // DCR C
    case 0x0E: cpu.c = mem.read(cpu.pc++);                                break; // MVI C,d8
    case 0x0F: { // RRC — rotate A right, bit 0 goes to CY and to bit 7
        uint8_t bit0 = cpu.a & 1;
        cpu.a = uint8_t((cpu.a >> 1) | (bit0 << 7));
        cpu.f.cy = bit0 != 0;
        break;
    }

    // --------------------------------------------------------------------
    //  0x10-0x1F  : DE pair counterparts of 0x00-0x0F, plus RAL/RAR
    // --------------------------------------------------------------------
    case 0x10: /* undocumented NOP */                                     break;
    case 0x11: cpu.set_de(mem.read16(cpu.pc)); cpu.pc += 2;               break; // LXI D,d16
    case 0x12: mem.write(cpu.de(), cpu.a);                                break; // STAX D
    case 0x13: cpu.set_de(uint16_t(cpu.de() + 1));                        break; // INX D
    case 0x14: cpu.d = inr8(cpu, cpu.d);                                  break; // INR D
    case 0x15: cpu.d = dcr8(cpu, cpu.d);                                  break; // DCR D
    case 0x16: cpu.d = mem.read(cpu.pc++);                                break; // MVI D,d8
    case 0x17: { // RAL — rotate A left THROUGH carry
        uint8_t old_cy = cpu.f.cy ? 1 : 0;
        cpu.f.cy = (cpu.a & 0x80) != 0;
        cpu.a = uint8_t((cpu.a << 1) | old_cy);
        break;
    }
    case 0x18: /* undocumented NOP */                                     break;
    case 0x19: dad16(cpu, cpu.de());                                      break; // DAD D
    case 0x1A: cpu.a = mem.read(cpu.de());                                break; // LDAX D
    case 0x1B: cpu.set_de(uint16_t(cpu.de() - 1));                        break; // DCX D
    case 0x1C: cpu.e = inr8(cpu, cpu.e);                                  break; // INR E
    case 0x1D: cpu.e = dcr8(cpu, cpu.e);                                  break; // DCR E
    case 0x1E: cpu.e = mem.read(cpu.pc++);                                break; // MVI E,d8
    case 0x1F: { // RAR — rotate A right THROUGH carry
        uint8_t old_cy = cpu.f.cy ? 1 : 0;
        cpu.f.cy = (cpu.a & 0x01) != 0;
        cpu.a = uint8_t((cpu.a >> 1) | (old_cy << 7));
        break;
    }

    // --------------------------------------------------------------------
    //  0x20-0x2F  : HL pair, SHLD, LHLD, CMA (complement A)
    // --------------------------------------------------------------------
    case 0x20: /* undocumented NOP */                                     break;
    case 0x21: cpu.set_hl(mem.read16(cpu.pc)); cpu.pc += 2;               break; // LXI H,d16
    case 0x22: { // SHLD addr — store HL to memory
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        mem.write(a,                  cpu.l);
        mem.write(uint16_t(a + 1),    cpu.h);
        break;
    }
    case 0x23: cpu.set_hl(uint16_t(cpu.hl() + 1));                        break; // INX H
    case 0x24: cpu.h = inr8(cpu, cpu.h);                                  break; // INR H
    case 0x25: cpu.h = dcr8(cpu, cpu.h);                                  break; // DCR H
    case 0x26: cpu.h = mem.read(cpu.pc++);                                break; // MVI H,d8
    case 0x27: daa(cpu);                                                  break; // DAA
    case 0x28: /* undocumented NOP */                                     break;
    case 0x29: dad16(cpu, cpu.hl());                                      break; // DAD H (HL += HL)
    case 0x2A: { // LHLD addr — load HL from memory
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        cpu.l = mem.read(a);
        cpu.h = mem.read(uint16_t(a + 1));
        break;
    }
    case 0x2B: cpu.set_hl(uint16_t(cpu.hl() - 1));                        break; // DCX H
    case 0x2C: cpu.l = inr8(cpu, cpu.l);                                  break; // INR L
    case 0x2D: cpu.l = dcr8(cpu, cpu.l);                                  break; // DCR L
    case 0x2E: cpu.l = mem.read(cpu.pc++);                                break; // MVI L,d8
    case 0x2F: cpu.a = uint8_t(~cpu.a);                                   break; // CMA

    // --------------------------------------------------------------------
    //  0x30-0x3F  : SP pair, STA, LDA, STC, CMC
    // --------------------------------------------------------------------
    case 0x30: /* undocumented NOP */                                     break;
    case 0x31: cpu.sp = mem.read16(cpu.pc); cpu.pc += 2;                  break; // LXI SP,d16
    case 0x32: { // STA addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        mem.write(a, cpu.a);
        break;
    }
    case 0x33: cpu.sp = uint16_t(cpu.sp + 1);                             break; // INX SP
    case 0x34: { // INR M
        uint8_t v = mem.read(cpu.hl());
        mem.write(cpu.hl(), inr8(cpu, v));
        break;
    }
    case 0x35: { // DCR M
        uint8_t v = mem.read(cpu.hl());
        mem.write(cpu.hl(), dcr8(cpu, v));
        break;
    }
    case 0x36: mem.write(cpu.hl(), mem.read(cpu.pc++));                   break; // MVI M,d8
    case 0x37: cpu.f.cy = true;                                           break; // STC
    case 0x38: /* undocumented NOP */                                     break;
    case 0x39: dad16(cpu, cpu.sp);                                        break; // DAD SP
    case 0x3A: { // LDA addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        cpu.a = mem.read(a);
        break;
    }
    case 0x3B: cpu.sp = uint16_t(cpu.sp - 1);                             break; // DCX SP
    case 0x3C: cpu.a = inr8(cpu, cpu.a);                                  break; // INR A
    case 0x3D: cpu.a = dcr8(cpu, cpu.a);                                  break; // DCR A
    case 0x3E: cpu.a = mem.read(cpu.pc++);                                break; // MVI A,d8
    case 0x3F: cpu.f.cy = !cpu.f.cy;                                      break; // CMC

    // --------------------------------------------------------------------
    //  0x40-0x7F  : MOV r1, r2   (with 0x76 = HLT)
    //      Bits 5..3 = dst, bits 2..0 = src; "6" means memory at HL.
    //      0x76 would be "MOV M, M" which is reused as HLT.
    // --------------------------------------------------------------------
    case 0x40 ... 0x7F: {
        if (op == 0x76) {
            // HLT — halt until interrupt or reset. We just set the flag;
            // cpu_step() handles the actual freeze.
            cpu.halted = true;
            break;
        }
        int dst = (op >> 3) & 0x07;
        int src =  op       & 0x07;
        write_reg(cpu, mem, dst, read_reg(cpu, mem, src));
        break;
    }

    // --------------------------------------------------------------------
    //  0x80-0xBF  : ALU on register/M.
    //      Bits 5..3 select the operation (ADD/ADC/SUB/SBB/ANA/XRA/ORA/CMP),
    //      bits 2..0 the source register.
    // --------------------------------------------------------------------
    case 0x80 ... 0xBF: {
        int     src = op & 0x07;
        uint8_t v   = read_reg(cpu, mem, src);
        switch ((op >> 3) & 0x07) {
            case 0: cpu.a = add8(cpu, cpu.a, v, 0);                    break; // ADD
            case 1: cpu.a = add8(cpu, cpu.a, v, cpu.f.cy ? 1 : 0);     break; // ADC
            case 2: cpu.a = sub8(cpu, cpu.a, v, 0);                    break; // SUB
            case 3: cpu.a = sub8(cpu, cpu.a, v, cpu.f.cy ? 1 : 0);     break; // SBB
            case 4: ana8(cpu, v);                                      break; // ANA
            case 5: xra8(cpu, v);                                      break; // XRA
            case 6: ora8(cpu, v);                                      break; // ORA
            case 7: cmp8(cpu, v);                                      break; // CMP
        }
        break;
    }

    // --------------------------------------------------------------------
    //  0xC0-0xCF  : RNZ / POP B / JNZ / JMP / CNZ / PUSH B / ADI / RST 0
    //               RZ  / RET   / JZ  / [JMP alt] / CZ / CALL / ACI / RST 1
    // --------------------------------------------------------------------
    case 0xC0: if (check_cond(cpu, op)) { cpu.pc = pop16(cpu, mem); extra_cycles = 6; } break; // RNZ
    case 0xC1: cpu.set_bc(pop16(cpu, mem));                               break; // POP B
    case 0xC2: { // JNZ addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        if (check_cond(cpu, op)) cpu.pc = a;
        break;
    }
    case 0xC3: cpu.pc = mem.read16(cpu.pc);                               break; // JMP addr
    case 0xC4: { // CNZ addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        if (check_cond(cpu, op)) { push16(cpu, mem, cpu.pc); cpu.pc = a; extra_cycles = 6; }
        break;
    }
    case 0xC5: push16(cpu, mem, cpu.bc());                                break; // PUSH B
    case 0xC6: cpu.a = add8(cpu, cpu.a, mem.read(cpu.pc++), 0);           break; // ADI d8
    case 0xC7: push16(cpu, mem, cpu.pc); cpu.pc = 0x0000;                 break; // RST 0
    case 0xC8: if (check_cond(cpu, op)) { cpu.pc = pop16(cpu, mem); extra_cycles = 6; } break; // RZ
    case 0xC9: cpu.pc = pop16(cpu, mem);                                  break; // RET
    case 0xCA: { // JZ addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        if (check_cond(cpu, op)) cpu.pc = a;
        break;
    }
    case 0xCB: cpu.pc = mem.read16(cpu.pc);                               break; // alt JMP (undoc)
    case 0xCC: { // CZ addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        if (check_cond(cpu, op)) { push16(cpu, mem, cpu.pc); cpu.pc = a; extra_cycles = 6; }
        break;
    }
    case 0xCD: { // CALL addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        push16(cpu, mem, cpu.pc);
        cpu.pc = a;
        break;
    }
    case 0xCE: cpu.a = add8(cpu, cpu.a, mem.read(cpu.pc++), cpu.f.cy ? 1 : 0); break; // ACI d8
    case 0xCF: push16(cpu, mem, cpu.pc); cpu.pc = 0x0008;                 break; // RST 1

    // --------------------------------------------------------------------
    //  0xD0-0xDF  : RNC / POP D / JNC / OUT / CNC / PUSH D / SUI / RST 2
    //               RC  / [RET alt] / JC / IN / CC / [CALL alt] / SBI / RST 3
    // --------------------------------------------------------------------
    case 0xD0: if (check_cond(cpu, op)) { cpu.pc = pop16(cpu, mem); extra_cycles = 6; } break; // RNC
    case 0xD1: cpu.set_de(pop16(cpu, mem));                               break; // POP D
    case 0xD2: { // JNC addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        if (check_cond(cpu, op)) cpu.pc = a;
        break;
    }
    case 0xD3: cpu_out_port(hw, mem.read(cpu.pc++), cpu.a);               break; // OUT p
    case 0xD4: { // CNC addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        if (check_cond(cpu, op)) { push16(cpu, mem, cpu.pc); cpu.pc = a; extra_cycles = 6; }
        break;
    }
    case 0xD5: push16(cpu, mem, cpu.de());                                break; // PUSH D
    case 0xD6: cpu.a = sub8(cpu, cpu.a, mem.read(cpu.pc++), 0);           break; // SUI d8
    case 0xD7: push16(cpu, mem, cpu.pc); cpu.pc = 0x0010;                 break; // RST 2
    case 0xD8: if (check_cond(cpu, op)) { cpu.pc = pop16(cpu, mem); extra_cycles = 6; } break; // RC
    case 0xD9: cpu.pc = pop16(cpu, mem);                                  break; // alt RET (undoc)
    case 0xDA: { // JC addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        if (check_cond(cpu, op)) cpu.pc = a;
        break;
    }
    case 0xDB: cpu.a = cpu_in_port(hw, mem.read(cpu.pc++));               break; // IN p
    case 0xDC: { // CC addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        if (check_cond(cpu, op)) { push16(cpu, mem, cpu.pc); cpu.pc = a; extra_cycles = 6; }
        break;
    }
    case 0xDD: { // alt CALL (undoc) — behaves identically to CD
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        push16(cpu, mem, cpu.pc);
        cpu.pc = a;
        break;
    }
    case 0xDE: cpu.a = sub8(cpu, cpu.a, mem.read(cpu.pc++), cpu.f.cy ? 1 : 0); break; // SBI d8
    case 0xDF: push16(cpu, mem, cpu.pc); cpu.pc = 0x0018;                 break; // RST 3

    // --------------------------------------------------------------------
    //  0xE0-0xEF  : RPO / POP H / JPO / XTHL / CPO / PUSH H / ANI / RST 4
    //               RPE / PCHL  / JPE / XCHG / CPE / [CALL alt] / XRI / RST 5
    // --------------------------------------------------------------------
    case 0xE0: if (check_cond(cpu, op)) { cpu.pc = pop16(cpu, mem); extra_cycles = 6; } break; // RPO
    case 0xE1: cpu.set_hl(pop16(cpu, mem));                               break; // POP H
    case 0xE2: { // JPO addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        if (check_cond(cpu, op)) cpu.pc = a;
        break;
    }
    case 0xE3: { // XTHL — exchange HL with the 16-bit word at SP
        uint8_t lo = mem.read(cpu.sp);
        uint8_t hi = mem.read(uint16_t(cpu.sp + 1));
        mem.write(cpu.sp,                cpu.l);
        mem.write(uint16_t(cpu.sp + 1),  cpu.h);
        cpu.l = lo; cpu.h = hi;
        break;
    }
    case 0xE4: { // CPO addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        if (check_cond(cpu, op)) { push16(cpu, mem, cpu.pc); cpu.pc = a; extra_cycles = 6; }
        break;
    }
    case 0xE5: push16(cpu, mem, cpu.hl());                                break; // PUSH H
    case 0xE6: ana8(cpu, mem.read(cpu.pc++));                             break; // ANI d8
    case 0xE7: push16(cpu, mem, cpu.pc); cpu.pc = 0x0020;                 break; // RST 4
    case 0xE8: if (check_cond(cpu, op)) { cpu.pc = pop16(cpu, mem); extra_cycles = 6; } break; // RPE
    case 0xE9: cpu.pc = cpu.hl();                                         break; // PCHL
    case 0xEA: { // JPE addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        if (check_cond(cpu, op)) cpu.pc = a;
        break;
    }
    case 0xEB: { // XCHG — swap DE and HL
        uint8_t td = cpu.d, te = cpu.e;
        cpu.d = cpu.h; cpu.e = cpu.l;
        cpu.h = td;    cpu.l = te;
        break;
    }
    case 0xEC: { // CPE addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        if (check_cond(cpu, op)) { push16(cpu, mem, cpu.pc); cpu.pc = a; extra_cycles = 6; }
        break;
    }
    case 0xED: { // alt CALL (undoc)
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        push16(cpu, mem, cpu.pc);
        cpu.pc = a;
        break;
    }
    case 0xEE: xra8(cpu, mem.read(cpu.pc++));                             break; // XRI d8
    case 0xEF: push16(cpu, mem, cpu.pc); cpu.pc = 0x0028;                 break; // RST 5

    // --------------------------------------------------------------------
    //  0xF0-0xFF  : RP / POP PSW / JP / DI / CP / PUSH PSW / ORI / RST 6
    //               RM / SPHL    / JM / EI / CM / [CALL alt] / CPI / RST 7
    // --------------------------------------------------------------------
    case 0xF0: if (check_cond(cpu, op)) { cpu.pc = pop16(cpu, mem); extra_cycles = 6; } break; // RP
    case 0xF1: { // POP PSW — pop into A : flags
        uint16_t v = pop16(cpu, mem);
        cpu.f.unpack(uint8_t(v & 0xFF));
        cpu.a = uint8_t(v >> 8);
        break;
    }
    case 0xF2: { // JP addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        if (check_cond(cpu, op)) cpu.pc = a;
        break;
    }
    case 0xF3: cpu.int_enable = false;                                    break; // DI
    case 0xF4: { // CP addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        if (check_cond(cpu, op)) { push16(cpu, mem, cpu.pc); cpu.pc = a; extra_cycles = 6; }
        break;
    }
    case 0xF5: push16(cpu, mem, uint16_t((uint16_t(cpu.a) << 8) | cpu.f.pack())); break; // PUSH PSW
    case 0xF6: ora8(cpu, mem.read(cpu.pc++));                             break; // ORI d8
    case 0xF7: push16(cpu, mem, cpu.pc); cpu.pc = 0x0030;                 break; // RST 6
    case 0xF8: if (check_cond(cpu, op)) { cpu.pc = pop16(cpu, mem); extra_cycles = 6; } break; // RM
    case 0xF9: cpu.sp = cpu.hl();                                         break; // SPHL
    case 0xFA: { // JM addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        if (check_cond(cpu, op)) cpu.pc = a;
        break;
    }
    case 0xFB: cpu.int_enable = true;                                     break; // EI
    case 0xFC: { // CM addr
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        if (check_cond(cpu, op)) { push16(cpu, mem, cpu.pc); cpu.pc = a; extra_cycles = 6; }
        break;
    }
    case 0xFD: { // alt CALL (undoc)
        uint16_t a = mem.read16(cpu.pc); cpu.pc += 2;
        push16(cpu, mem, cpu.pc);
        cpu.pc = a;
        break;
    }
    case 0xFE: cmp8(cpu, mem.read(cpu.pc++));                             break; // CPI d8
    case 0xFF: push16(cpu, mem, cpu.pc); cpu.pc = 0x0038;                 break; // RST 7

    } // end switch

    return cycles_8080[op] + extra_cycles;
}

#pragma GCC diagnostic pop

// ============================================================================
//  Public entry — fetch (or inject), then execute.
// ============================================================================

int cpu_step(CPU& cpu, Memory& mem, Hardware& hw) {
    // ----- 1. Interrupt sampling (happens BEFORE the fetch) -----
    if (hw.interrupt_pending && cpu.int_enable) {
        cpu.int_enable       = false;   // ACK clears INTE on the 8080
        cpu.halted           = false;   // INT releases the HLT latch
        uint8_t opcode       = hw.interrupt_opcode;
        hw.interrupt_pending = false;
        int c = execute(cpu, mem, hw, opcode);
        cpu.cycles += uint64_t(c);
        return c;
    }

    // ----- 2. Honor HLT — sleep one machine cycle and bail -----
    if (cpu.halted) {
        cpu.cycles += 4;
        return 4;
    }

    // ----- 3. Normal fetch / execute -----
    uint8_t op = mem.read(cpu.pc++);
    int c = execute(cpu, mem, hw, op);
    cpu.cycles += uint64_t(c);
    return c;
}

} // namespace invaderx
