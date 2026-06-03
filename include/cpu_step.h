#pragma once
// ============================================================================
//  InvaderX — cpu_step.h
//  Public interface to the CPU dispatcher and interrupt-injection helper.
//  Implementation lives in src/cpu.cpp.
// ============================================================================

#include <cstdint>
#include "cpu.h"
#include "memory.h"
#include "hardware.h"

namespace invaderx {

// ----------------------------------------------------------------------------
//  cpu_step
//      Fetch (or inject), decode, execute one instruction.
//      Returns the number of T-states (clock cycles) consumed — used by
//      the Step 5 frame pump to pace the CPU at ~2 MHz.
//
//      Interrupt sampling order matches the real 8080:
//          1. If hw.interrupt_pending && cpu.int_enable, the pending
//             interrupt opcode is executed INSTEAD of fetching from PC,
//             INTE is cleared, the HLT latch is released.
//          2. Otherwise, if cpu.halted, the CPU consumes 4 cycles waiting.
//          3. Otherwise, fetch (mem[PC++]) and dispatch.
// ----------------------------------------------------------------------------
int cpu_step(CPU& cpu, Memory& mem, Hardware& hw);

// ----------------------------------------------------------------------------
//  cpu_request_interrupt
//      Called by Step 4's interrupt scheduler. Stamps the RST opcode the
//      next cpu_step will execute and raises the pending flag. Idempotent
//      if a second interrupt arrives before the first is serviced — the
//      newer one wins (matches the real 8080's edge-triggered INT pin).
// ----------------------------------------------------------------------------
void cpu_request_interrupt(Hardware& hw, uint8_t rst_opcode);

// ----------------------------------------------------------------------------
//  I/O port hooks (provided by hardware.cpp in Step 4).
//      The CPU dispatcher's IN/OUT cases call these. We declare them here
//      so cpu.cpp doesn't have to include any hardware implementation
//      header. Step 2 ships weak stub implementations in cpu.cpp so the
//      CPU is unit-testable in isolation; Step 4 replaces them.
// ----------------------------------------------------------------------------
uint8_t cpu_in_port (Hardware& hw, uint8_t port);
void    cpu_out_port(Hardware& hw, uint8_t port, uint8_t value);

// ----------------------------------------------------------------------------
//  opcode_base_cycles
//      Returns the baseline T-state count for an opcode (the "condition not
//      taken" value for conditional CALL/RET; taken conditionals add 6).
//      Read-only; intended for documentation/coverage tooling and debug
//      tracing, not the hot path.
// ----------------------------------------------------------------------------
int opcode_base_cycles(uint8_t opcode);

} // namespace invaderx
