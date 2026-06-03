# InvaderX — Intel 8080 Opcode Coverage Report

Auto-generated from the implementation's own decode and cycle
tables (`src/disassembler.cpp`, `src/cpu.cpp`). Every one of the
256 possible opcode bytes is implemented in the dispatcher.

## Summary

| Metric | Count |
|---|---|
| Total opcode bytes | 256 |
| Implemented | 256 (100%) |
| Documented | 244 |
| Undocumented (executed as documented equivalents) | 12 |

Undocumented opcodes are the 8080's alternate encodings of NOP, JMP, RET, and CALL. They are marked with `*` below and execute identically to their documented counterparts, matching real silicon. Space Invaders does not use them, but they are handled so a runaway PC never hits an unimplemented opcode.

## Full opcode table

Cycles are the baseline T-states. Conditional CALL/RET add 6 T-states when the condition is taken (e.g. CZ = 11 not taken, 17 taken).

| Opcode | Mnemonic | Bytes | Cycles | Category | Status |
|--------|----------|:-----:|:------:|----------|--------|
| 0x00 | `NOP` | 1 | 4 | Stack / I/O / machine | documented |
| 0x01 | `LXI  B, %W` | 3 | 10 | Data transfer | documented |
| 0x02 | `STAX B` | 1 | 7 | Data transfer | documented |
| 0x03 | `INX  B` | 1 | 5 | Arithmetic | documented |
| 0x04 | `INR  B` | 1 | 5 | Arithmetic | documented |
| 0x05 | `DCR  B` | 1 | 5 | Arithmetic | documented |
| 0x06 | `MVI  B, %B` | 2 | 7 | Data transfer | documented |
| 0x07 | `RLC` | 1 | 4 | Logical / rotate | documented |
| 0x08 | `NOP` | 1 | 4 | Stack / I/O / machine | undocumented |
| 0x09 | `DAD  B` | 1 | 10 | Arithmetic | documented |
| 0x0A | `LDAX B` | 1 | 7 | Data transfer | documented |
| 0x0B | `DCX  B` | 1 | 5 | Arithmetic | documented |
| 0x0C | `INR  C` | 1 | 5 | Arithmetic | documented |
| 0x0D | `DCR  C` | 1 | 5 | Arithmetic | documented |
| 0x0E | `MVI  C, %B` | 2 | 7 | Data transfer | documented |
| 0x0F | `RRC` | 1 | 4 | Logical / rotate | documented |
| 0x10 | `NOP` | 1 | 4 | Stack / I/O / machine | undocumented |
| 0x11 | `LXI  D, %W` | 3 | 10 | Data transfer | documented |
| 0x12 | `STAX D` | 1 | 7 | Data transfer | documented |
| 0x13 | `INX  D` | 1 | 5 | Arithmetic | documented |
| 0x14 | `INR  D` | 1 | 5 | Arithmetic | documented |
| 0x15 | `DCR  D` | 1 | 5 | Arithmetic | documented |
| 0x16 | `MVI  D, %B` | 2 | 7 | Data transfer | documented |
| 0x17 | `RAL` | 1 | 4 | Logical / rotate | documented |
| 0x18 | `NOP` | 1 | 4 | Stack / I/O / machine | undocumented |
| 0x19 | `DAD  D` | 1 | 10 | Arithmetic | documented |
| 0x1A | `LDAX D` | 1 | 7 | Data transfer | documented |
| 0x1B | `DCX  D` | 1 | 5 | Arithmetic | documented |
| 0x1C | `INR  E` | 1 | 5 | Arithmetic | documented |
| 0x1D | `DCR  E` | 1 | 5 | Arithmetic | documented |
| 0x1E | `MVI  E, %B` | 2 | 7 | Data transfer | documented |
| 0x1F | `RAR` | 1 | 4 | Logical / rotate | documented |
| 0x20 | `NOP` | 1 | 4 | Stack / I/O / machine | undocumented |
| 0x21 | `LXI  H, %W` | 3 | 10 | Data transfer | documented |
| 0x22 | `SHLD %W` | 3 | 16 | Data transfer | documented |
| 0x23 | `INX  H` | 1 | 5 | Arithmetic | documented |
| 0x24 | `INR  H` | 1 | 5 | Arithmetic | documented |
| 0x25 | `DCR  H` | 1 | 5 | Arithmetic | documented |
| 0x26 | `MVI  H, %B` | 2 | 7 | Data transfer | documented |
| 0x27 | `DAA` | 1 | 4 | Arithmetic | documented |
| 0x28 | `NOP` | 1 | 4 | Stack / I/O / machine | undocumented |
| 0x29 | `DAD  H` | 1 | 10 | Arithmetic | documented |
| 0x2A | `LHLD %W` | 3 | 16 | Data transfer | documented |
| 0x2B | `DCX  H` | 1 | 5 | Arithmetic | documented |
| 0x2C | `INR  L` | 1 | 5 | Arithmetic | documented |
| 0x2D | `DCR  L` | 1 | 5 | Arithmetic | documented |
| 0x2E | `MVI  L, %B` | 2 | 7 | Data transfer | documented |
| 0x2F | `CMA` | 1 | 4 | Logical / rotate | documented |
| 0x30 | `NOP` | 1 | 4 | Stack / I/O / machine | undocumented |
| 0x31 | `LXI  SP, %W` | 3 | 10 | Data transfer | documented |
| 0x32 | `STA  %W` | 3 | 13 | Data transfer | documented |
| 0x33 | `INX  SP` | 1 | 5 | Arithmetic | documented |
| 0x34 | `INR  M` | 1 | 10 | Arithmetic | documented |
| 0x35 | `DCR  M` | 1 | 10 | Arithmetic | documented |
| 0x36 | `MVI  M, %B` | 2 | 10 | Data transfer | documented |
| 0x37 | `STC` | 1 | 4 | Logical / rotate | documented |
| 0x38 | `NOP` | 1 | 4 | Stack / I/O / machine | undocumented |
| 0x39 | `DAD  SP` | 1 | 10 | Arithmetic | documented |
| 0x3A | `LDA  %W` | 3 | 13 | Data transfer | documented |
| 0x3B | `DCX  SP` | 1 | 5 | Arithmetic | documented |
| 0x3C | `INR  A` | 1 | 5 | Arithmetic | documented |
| 0x3D | `DCR  A` | 1 | 5 | Arithmetic | documented |
| 0x3E | `MVI  A, %B` | 2 | 7 | Data transfer | documented |
| 0x3F | `CMC` | 1 | 4 | Logical / rotate | documented |
| 0x40 | `MOV  B, B` | 1 | 5 | Data transfer | documented |
| 0x41 | `MOV  B, C` | 1 | 5 | Data transfer | documented |
| 0x42 | `MOV  B, D` | 1 | 5 | Data transfer | documented |
| 0x43 | `MOV  B, E` | 1 | 5 | Data transfer | documented |
| 0x44 | `MOV  B, H` | 1 | 5 | Data transfer | documented |
| 0x45 | `MOV  B, L` | 1 | 5 | Data transfer | documented |
| 0x46 | `MOV  B, M` | 1 | 7 | Data transfer | documented |
| 0x47 | `MOV  B, A` | 1 | 5 | Data transfer | documented |
| 0x48 | `MOV  C, B` | 1 | 5 | Data transfer | documented |
| 0x49 | `MOV  C, C` | 1 | 5 | Data transfer | documented |
| 0x4A | `MOV  C, D` | 1 | 5 | Data transfer | documented |
| 0x4B | `MOV  C, E` | 1 | 5 | Data transfer | documented |
| 0x4C | `MOV  C, H` | 1 | 5 | Data transfer | documented |
| 0x4D | `MOV  C, L` | 1 | 5 | Data transfer | documented |
| 0x4E | `MOV  C, M` | 1 | 7 | Data transfer | documented |
| 0x4F | `MOV  C, A` | 1 | 5 | Data transfer | documented |
| 0x50 | `MOV  D, B` | 1 | 5 | Data transfer | documented |
| 0x51 | `MOV  D, C` | 1 | 5 | Data transfer | documented |
| 0x52 | `MOV  D, D` | 1 | 5 | Data transfer | documented |
| 0x53 | `MOV  D, E` | 1 | 5 | Data transfer | documented |
| 0x54 | `MOV  D, H` | 1 | 5 | Data transfer | documented |
| 0x55 | `MOV  D, L` | 1 | 5 | Data transfer | documented |
| 0x56 | `MOV  D, M` | 1 | 7 | Data transfer | documented |
| 0x57 | `MOV  D, A` | 1 | 5 | Data transfer | documented |
| 0x58 | `MOV  E, B` | 1 | 5 | Data transfer | documented |
| 0x59 | `MOV  E, C` | 1 | 5 | Data transfer | documented |
| 0x5A | `MOV  E, D` | 1 | 5 | Data transfer | documented |
| 0x5B | `MOV  E, E` | 1 | 5 | Data transfer | documented |
| 0x5C | `MOV  E, H` | 1 | 5 | Data transfer | documented |
| 0x5D | `MOV  E, L` | 1 | 5 | Data transfer | documented |
| 0x5E | `MOV  E, M` | 1 | 7 | Data transfer | documented |
| 0x5F | `MOV  E, A` | 1 | 5 | Data transfer | documented |
| 0x60 | `MOV  H, B` | 1 | 5 | Data transfer | documented |
| 0x61 | `MOV  H, C` | 1 | 5 | Data transfer | documented |
| 0x62 | `MOV  H, D` | 1 | 5 | Data transfer | documented |
| 0x63 | `MOV  H, E` | 1 | 5 | Data transfer | documented |
| 0x64 | `MOV  H, H` | 1 | 5 | Data transfer | documented |
| 0x65 | `MOV  H, L` | 1 | 5 | Data transfer | documented |
| 0x66 | `MOV  H, M` | 1 | 7 | Data transfer | documented |
| 0x67 | `MOV  H, A` | 1 | 5 | Data transfer | documented |
| 0x68 | `MOV  L, B` | 1 | 5 | Data transfer | documented |
| 0x69 | `MOV  L, C` | 1 | 5 | Data transfer | documented |
| 0x6A | `MOV  L, D` | 1 | 5 | Data transfer | documented |
| 0x6B | `MOV  L, E` | 1 | 5 | Data transfer | documented |
| 0x6C | `MOV  L, H` | 1 | 5 | Data transfer | documented |
| 0x6D | `MOV  L, L` | 1 | 5 | Data transfer | documented |
| 0x6E | `MOV  L, M` | 1 | 7 | Data transfer | documented |
| 0x6F | `MOV  L, A` | 1 | 5 | Data transfer | documented |
| 0x70 | `MOV  M, B` | 1 | 7 | Data transfer | documented |
| 0x71 | `MOV  M, C` | 1 | 7 | Data transfer | documented |
| 0x72 | `MOV  M, D` | 1 | 7 | Data transfer | documented |
| 0x73 | `MOV  M, E` | 1 | 7 | Data transfer | documented |
| 0x74 | `MOV  M, H` | 1 | 7 | Data transfer | documented |
| 0x75 | `MOV  M, L` | 1 | 7 | Data transfer | documented |
| 0x76 | `HLT` | 1 | 7 | Stack / I/O / machine | documented |
| 0x77 | `MOV  M, A` | 1 | 7 | Data transfer | documented |
| 0x78 | `MOV  A, B` | 1 | 5 | Data transfer | documented |
| 0x79 | `MOV  A, C` | 1 | 5 | Data transfer | documented |
| 0x7A | `MOV  A, D` | 1 | 5 | Data transfer | documented |
| 0x7B | `MOV  A, E` | 1 | 5 | Data transfer | documented |
| 0x7C | `MOV  A, H` | 1 | 5 | Data transfer | documented |
| 0x7D | `MOV  A, L` | 1 | 5 | Data transfer | documented |
| 0x7E | `MOV  A, M` | 1 | 7 | Data transfer | documented |
| 0x7F | `MOV  A, A` | 1 | 5 | Data transfer | documented |
| 0x80 | `ADD  B` | 1 | 4 | Arithmetic | documented |
| 0x81 | `ADD  C` | 1 | 4 | Arithmetic | documented |
| 0x82 | `ADD  D` | 1 | 4 | Arithmetic | documented |
| 0x83 | `ADD  E` | 1 | 4 | Arithmetic | documented |
| 0x84 | `ADD  H` | 1 | 4 | Arithmetic | documented |
| 0x85 | `ADD  L` | 1 | 4 | Arithmetic | documented |
| 0x86 | `ADD  M` | 1 | 7 | Arithmetic | documented |
| 0x87 | `ADD  A` | 1 | 4 | Arithmetic | documented |
| 0x88 | `ADC  B` | 1 | 4 | Arithmetic | documented |
| 0x89 | `ADC  C` | 1 | 4 | Arithmetic | documented |
| 0x8A | `ADC  D` | 1 | 4 | Arithmetic | documented |
| 0x8B | `ADC  E` | 1 | 4 | Arithmetic | documented |
| 0x8C | `ADC  H` | 1 | 4 | Arithmetic | documented |
| 0x8D | `ADC  L` | 1 | 4 | Arithmetic | documented |
| 0x8E | `ADC  M` | 1 | 7 | Arithmetic | documented |
| 0x8F | `ADC  A` | 1 | 4 | Arithmetic | documented |
| 0x90 | `SUB  B` | 1 | 4 | Arithmetic | documented |
| 0x91 | `SUB  C` | 1 | 4 | Arithmetic | documented |
| 0x92 | `SUB  D` | 1 | 4 | Arithmetic | documented |
| 0x93 | `SUB  E` | 1 | 4 | Arithmetic | documented |
| 0x94 | `SUB  H` | 1 | 4 | Arithmetic | documented |
| 0x95 | `SUB  L` | 1 | 4 | Arithmetic | documented |
| 0x96 | `SUB  M` | 1 | 7 | Arithmetic | documented |
| 0x97 | `SUB  A` | 1 | 4 | Arithmetic | documented |
| 0x98 | `SBB  B` | 1 | 4 | Arithmetic | documented |
| 0x99 | `SBB  C` | 1 | 4 | Arithmetic | documented |
| 0x9A | `SBB  D` | 1 | 4 | Arithmetic | documented |
| 0x9B | `SBB  E` | 1 | 4 | Arithmetic | documented |
| 0x9C | `SBB  H` | 1 | 4 | Arithmetic | documented |
| 0x9D | `SBB  L` | 1 | 4 | Arithmetic | documented |
| 0x9E | `SBB  M` | 1 | 7 | Arithmetic | documented |
| 0x9F | `SBB  A` | 1 | 4 | Arithmetic | documented |
| 0xA0 | `ANA  B` | 1 | 4 | Logical / rotate | documented |
| 0xA1 | `ANA  C` | 1 | 4 | Logical / rotate | documented |
| 0xA2 | `ANA  D` | 1 | 4 | Logical / rotate | documented |
| 0xA3 | `ANA  E` | 1 | 4 | Logical / rotate | documented |
| 0xA4 | `ANA  H` | 1 | 4 | Logical / rotate | documented |
| 0xA5 | `ANA  L` | 1 | 4 | Logical / rotate | documented |
| 0xA6 | `ANA  M` | 1 | 7 | Logical / rotate | documented |
| 0xA7 | `ANA  A` | 1 | 4 | Logical / rotate | documented |
| 0xA8 | `XRA  B` | 1 | 4 | Logical / rotate | documented |
| 0xA9 | `XRA  C` | 1 | 4 | Logical / rotate | documented |
| 0xAA | `XRA  D` | 1 | 4 | Logical / rotate | documented |
| 0xAB | `XRA  E` | 1 | 4 | Logical / rotate | documented |
| 0xAC | `XRA  H` | 1 | 4 | Logical / rotate | documented |
| 0xAD | `XRA  L` | 1 | 4 | Logical / rotate | documented |
| 0xAE | `XRA  M` | 1 | 7 | Logical / rotate | documented |
| 0xAF | `XRA  A` | 1 | 4 | Logical / rotate | documented |
| 0xB0 | `ORA  B` | 1 | 4 | Logical / rotate | documented |
| 0xB1 | `ORA  C` | 1 | 4 | Logical / rotate | documented |
| 0xB2 | `ORA  D` | 1 | 4 | Logical / rotate | documented |
| 0xB3 | `ORA  E` | 1 | 4 | Logical / rotate | documented |
| 0xB4 | `ORA  H` | 1 | 4 | Logical / rotate | documented |
| 0xB5 | `ORA  L` | 1 | 4 | Logical / rotate | documented |
| 0xB6 | `ORA  M` | 1 | 7 | Logical / rotate | documented |
| 0xB7 | `ORA  A` | 1 | 4 | Logical / rotate | documented |
| 0xB8 | `CMP  B` | 1 | 4 | Logical / rotate | documented |
| 0xB9 | `CMP  C` | 1 | 4 | Logical / rotate | documented |
| 0xBA | `CMP  D` | 1 | 4 | Logical / rotate | documented |
| 0xBB | `CMP  E` | 1 | 4 | Logical / rotate | documented |
| 0xBC | `CMP  H` | 1 | 4 | Logical / rotate | documented |
| 0xBD | `CMP  L` | 1 | 4 | Logical / rotate | documented |
| 0xBE | `CMP  M` | 1 | 7 | Logical / rotate | documented |
| 0xBF | `CMP  A` | 1 | 4 | Logical / rotate | documented |
| 0xC0 | `RNZ` | 1 | 5 | Branch / call / return | documented |
| 0xC1 | `POP  B` | 1 | 10 | Stack / I/O / machine | documented |
| 0xC2 | `JNZ  %W` | 3 | 10 | Branch / call / return | documented |
| 0xC3 | `JMP  %W` | 3 | 10 | Branch / call / return | documented |
| 0xC4 | `CNZ  %W` | 3 | 11 | Branch / call / return | documented |
| 0xC5 | `PUSH B` | 1 | 11 | Stack / I/O / machine | documented |
| 0xC6 | `ADI  %B` | 2 | 7 | Arithmetic | documented |
| 0xC7 | `RST  0` | 1 | 11 | Branch / call / return | documented |
| 0xC8 | `RZ` | 1 | 5 | Branch / call / return | documented |
| 0xC9 | `RET` | 1 | 10 | Branch / call / return | documented |
| 0xCA | `JZ   %W` | 3 | 10 | Branch / call / return | documented |
| 0xCB | `JMP %W` | 3 | 10 | Branch / call / return | undocumented |
| 0xCC | `CZ   %W` | 3 | 11 | Branch / call / return | documented |
| 0xCD | `CALL %W` | 3 | 17 | Branch / call / return | documented |
| 0xCE | `ACI  %B` | 2 | 7 | Arithmetic | documented |
| 0xCF | `RST  1` | 1 | 11 | Branch / call / return | documented |
| 0xD0 | `RNC` | 1 | 5 | Branch / call / return | documented |
| 0xD1 | `POP  D` | 1 | 10 | Stack / I/O / machine | documented |
| 0xD2 | `JNC  %W` | 3 | 10 | Branch / call / return | documented |
| 0xD3 | `OUT  %B` | 2 | 10 | Stack / I/O / machine | documented |
| 0xD4 | `CNC  %W` | 3 | 11 | Branch / call / return | documented |
| 0xD5 | `PUSH D` | 1 | 11 | Stack / I/O / machine | documented |
| 0xD6 | `SUI  %B` | 2 | 7 | Arithmetic | documented |
| 0xD7 | `RST  2` | 1 | 11 | Branch / call / return | documented |
| 0xD8 | `RC` | 1 | 5 | Branch / call / return | documented |
| 0xD9 | `RET` | 1 | 10 | Branch / call / return | undocumented |
| 0xDA | `JC   %W` | 3 | 10 | Branch / call / return | documented |
| 0xDB | `IN   %B` | 2 | 10 | Stack / I/O / machine | documented |
| 0xDC | `CC   %W` | 3 | 11 | Branch / call / return | documented |
| 0xDD | `CALL %W` | 3 | 17 | Branch / call / return | undocumented |
| 0xDE | `SBI  %B` | 2 | 7 | Arithmetic | documented |
| 0xDF | `RST  3` | 1 | 11 | Branch / call / return | documented |
| 0xE0 | `RPO` | 1 | 5 | Branch / call / return | documented |
| 0xE1 | `POP  H` | 1 | 10 | Stack / I/O / machine | documented |
| 0xE2 | `JPO  %W` | 3 | 10 | Branch / call / return | documented |
| 0xE3 | `XTHL` | 1 | 18 | Stack / I/O / machine | documented |
| 0xE4 | `CPO  %W` | 3 | 11 | Branch / call / return | documented |
| 0xE5 | `PUSH H` | 1 | 11 | Stack / I/O / machine | documented |
| 0xE6 | `ANI  %B` | 2 | 7 | Logical / rotate | documented |
| 0xE7 | `RST  4` | 1 | 11 | Branch / call / return | documented |
| 0xE8 | `RPE` | 1 | 5 | Branch / call / return | documented |
| 0xE9 | `PCHL` | 1 | 5 | Branch / call / return | documented |
| 0xEA | `JPE  %W` | 3 | 10 | Branch / call / return | documented |
| 0xEB | `XCHG` | 1 | 5 | Data transfer | documented |
| 0xEC | `CPE  %W` | 3 | 11 | Branch / call / return | documented |
| 0xED | `CALL %W` | 3 | 17 | Branch / call / return | undocumented |
| 0xEE | `XRI  %B` | 2 | 7 | Logical / rotate | documented |
| 0xEF | `RST  5` | 1 | 11 | Branch / call / return | documented |
| 0xF0 | `RP` | 1 | 5 | Branch / call / return | documented |
| 0xF1 | `POP  PSW` | 1 | 10 | Stack / I/O / machine | documented |
| 0xF2 | `JP   %W` | 3 | 10 | Branch / call / return | documented |
| 0xF3 | `DI` | 1 | 4 | Stack / I/O / machine | documented |
| 0xF4 | `CP   %W` | 3 | 11 | Branch / call / return | documented |
| 0xF5 | `PUSH PSW` | 1 | 11 | Stack / I/O / machine | documented |
| 0xF6 | `ORI  %B` | 2 | 7 | Logical / rotate | documented |
| 0xF7 | `RST  6` | 1 | 11 | Branch / call / return | documented |
| 0xF8 | `RM` | 1 | 5 | Branch / call / return | documented |
| 0xF9 | `SPHL` | 1 | 5 | Stack / I/O / machine | documented |
| 0xFA | `JM   %W` | 3 | 10 | Branch / call / return | documented |
| 0xFB | `EI` | 1 | 4 | Stack / I/O / machine | documented |
| 0xFC | `CM   %W` | 3 | 11 | Branch / call / return | documented |
| 0xFD | `CALL %W` | 3 | 17 | Branch / call / return | undocumented |
| 0xFE | `CPI  %B` | 2 | 7 | Logical / rotate | documented |
| 0xFF | `RST  7` | 1 | 11 | Branch / call / return | documented |

## Flags affected (summary)

| Instruction class | S | Z | AC | P | CY |
|---|:-:|:-:|:-:|:-:|:-:|
| ADD/ADC/SUB/SBB/ADI/ACI/SUI/SBI | yes | yes | yes | yes | yes |
| INR/DCR | yes | yes | yes | yes | - |
| ANA/ANI | yes | yes | special* | yes | 0 |
| XRA/XRI/ORA/ORI | yes | yes | 0 | yes | 0 |
| CMP/CPI | yes | yes | yes | yes | yes |
| DAD | - | - | - | - | yes |
| INX/DCX | - | - | - | - | - |
| RLC/RRC/RAL/RAR | - | - | - | - | yes |
| DAA | yes | yes | yes | yes | yes |
| STC | - | - | - | - | 1 |
| CMC | - | - | - | - | toggled |

\* ANA/ANI set AC to `((A | operand) & 0x08) != 0`, a documented 8080 quirk rather than the always-clear behavior some references assume.
