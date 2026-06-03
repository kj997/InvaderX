# InvaderX — Cycle-Accurate Intel 8080 Space Invaders Emulator

A from-scratch emulator for the 1978 Taito *Space Invaders* arcade board.
Implements the complete Intel 8080 instruction set, the custom Taito I/O
hardware (shift register, interrupt timing, inputs), and renders the rotated
monochrome display through SDL2 at a wall-clock-locked 60 Hz.

No emulation frameworks. No external CPU cores. Raw C++17, built from the
ground up: register file, opcode dispatcher, memory bus, hardware ports,
renderer, and game loop are all original code.

```
  ┌──────────────────────────────────────────────────────────┐
  │  [ screenshot placeholder ]                               │
  │                                                           │
  │   Drop a 224x256 PNG/GIF of gameplay here once you have   │
  │   the real ROMs running. See "Capturing a screenshot".    │
  └──────────────────────────────────────────────────────────┘
```

---

## Table of contents

1. [Quick start](#quick-start)
2. [Architecture](#architecture)
3. [Memory map](#memory-map)
4. [Interrupt timing](#interrupt-timing)
5. [Hardware quirks](#hardware-quirks)
6. [CPU implementation notes](#cpu-implementation-notes)
7. [Display & rotation](#display--rotation)
8. [Input mapping](#input-mapping)
9. [Testing](#testing)
10. [Opcode coverage](#opcode-coverage)
11. [Project layout](#project-layout)
12. [Design decisions](#design-decisions)
13. [Troubleshooting](#troubleshooting)
14. [References](#references)
15. [Legal](#legal)

---

## Quick start

### Prerequisites (Windows)

- **MinGW-w64** with a C++17-capable GCC (MSYS2 distribution recommended).
- **CMake** 3.15 or newer.
- **SDL2 development libraries** for MinGW, extracted to `C:\SDL2`. The
  expected layout is:

  ```
  C:\SDL2\include\SDL.h
  C:\SDL2\lib\libSDL2.a
  C:\SDL2\lib\libSDL2main.a
  C:\SDL2\lib\SDL2.dll
  ```

  (Download the `SDL2-devel-x.y.z-mingw` package from libsdl.org.)

### Build

```bat
mkdir build
cd build
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

This produces `build\invaderx.exe` and copies `SDL2.dll` next to it
automatically.

If SDL2 is somewhere other than `C:\SDL2`:

```bat
cmake -G "MinGW Makefiles" -DSDL2_ROOT="D:/libs/SDL2" ..
```

### Run

Place your four ROM files in a directory (e.g. `roms\`):

```
roms\invaders.e
roms\invaders.f
roms\invaders.g
roms\invaders.h
```

Then:

```bat
.\invaderx.exe ..\roms
```

On startup the emulator prints a load report and disassembles the boot
vector. If that disassembly looks like sane 8080 code (`LXI SP`, `JMP`,
`MVI`, etc.) the ROMs loaded correctly. If it looks like garbage
(lots of `RST 7`, `*CALL`, random `ANI`), your dump uses the opposite chip
ordering — rerun with `--mame`:

```bat
.\invaderx.exe ..\roms --mame
```

### Run the tests

```bat
cd build
ctest --output-on-failure
```

All five suites should report `Passed` (2,050 assertions total).

---

## Architecture

The emulator is split into strictly-separated modules. State lives in plain
structs; behavior lives in free functions that operate on those structs. No
module reaches into another's internals — they communicate only through the
documented interfaces.

```
                          ┌─────────────────────────────┐
                          │           main.cpp          │
                          │   (composition / game loop) │
                          └───────────────┬─────────────┘
                                          │
        ┌──────────────┬──────────────────┼──────────────────┬──────────────┐
        │              │                  │                  │              │
        ▼              ▼                  ▼                  ▼              ▼
 ┌────────────┐ ┌────────────┐    ┌──────────────┐  ┌────────────┐  ┌─────────────┐
 │  rom_loader│ │   cpu      │    │  hardware    │  │  renderer  │  │ sdl_frontend│
 │            │ │ (dispatch) │    │ (ports, INT, │  │ (VRAM ->   │  │ (window,    │
 │ disk->RAM  │ │            │    │  shift reg)  │  │  fb, rot)  │  │  events,    │
 └─────┬──────┘ └─────┬──────┘    └──────┬───────┘  └─────┬──────┘  │  pacer)     │
       │              │                  │                │         └──────┬──────┘
       │              ▼                  │                │                │
       │      ┌────────────────┐         │                │                │
       └─────▶│     Memory      │◀────────┘                │                │
              │  (64KB bus,     │◀─────────────────────────┘                │
              │   ROM guard)    │                                           │
              └─────────────────┘                                           │
                                                                            │
              ┌─────────────────┐                                          │
              │     Display      │◀─────────────────────────────────────────┘
              │  (framebuffer +  │
              │   SDL handles)   │
              └─────────────────┘
```

### Module responsibilities

| Module | Files | Responsibility |
|---|---|---|
| **CPU** | `cpu.cpp`, `cpu.h`, `cpu_step.h` | 256-opcode dispatcher, flag math, interrupt injection, register file |
| **Memory** | `memory.h` | Flat 64 KB bus, ROM write-protection, little-endian 16-bit access |
| **Hardware** | `hardware.cpp`, `hardware.h`, `hardware_ops.h` | I/O ports, shift register, interrupt scheduler, input bits |
| **Renderer** | `renderer.cpp`, `renderer.h` | VRAM → framebuffer with 90° rotation; SDL-free |
| **SDL frontend** | `sdl_frontend.cpp`, `sdl_frontend.h` | Window/renderer/texture, event loop, 60 Hz pacer |
| **ROM loader** | `rom_loader.cpp`, `rom_loader.h` | Disk → memory, size validation, both chip orderings |
| **Disassembler** | `disassembler.cpp`, `disassembler.h` | Side-effect-free decode for verification & debugging |
| **Composition** | `main.cpp` | Wires everything into the game loop |

The CPU never includes SDL. The renderer never includes SDL. Only
`sdl_frontend.cpp` knows SDL exists — swapping the display backend touches
exactly one file.

---

## Memory map

| Range | Size | Region | Notes |
|---|---|---|---|
| `0x0000–0x07FF` | 2 KB | ROM `invaders.e` | read-only after load |
| `0x0800–0x0FFF` | 2 KB | ROM `invaders.f` | read-only after load |
| `0x1000–0x17FF` | 2 KB | ROM `invaders.g` | read-only after load |
| `0x1800–0x1FFF` | 2 KB | ROM `invaders.h` | read-only after load |
| `0x2000–0x23FF` | 1 KB | Work RAM | stack, game variables |
| `0x2400–0x3FFF` | 7 KB | Video RAM | 1 bpp framebuffer (256×224) |
| `0x4000–0xFFFF` | — | unused / mirror | not decoded on real PCB |

**ROM ordering caveat.** This emulator defaults to the ordering in the
project brief: `e→0x0000, f→0x0800, g→0x1000, h→0x1800`. Many ROM dumps in
the wild (MAME-derived) use the **reverse**: `h→0x0000, g→0x0800, f→0x1000,
e→0x1800`. If the boot disassembly looks wrong, switch with `--mame`. Both
orderings are fully supported; only the address-to-file mapping changes.

**Why allocate the full 64 KB?** The real board only decodes 14 address
lines for RAM, so `0x4000+` is undefined. We allocate the full address
space anyway: every `uint16_t` becomes a valid index, range checks vanish
from the hot path, and a runaway program counter can't read out of bounds.
The cost is 56 KB of zeroed memory — negligible.

**ROM write protection.** `Memory::write()` silently drops writes to
`0x0000–0x1FFF`, mirroring real silicon (the ROM chips ignore `/WR`). It
returns `false` on such writes so a debugger can flag stray stores. The ROM
loader uses a separate back-door (`load_byte()`) that bypasses the guard.

---

## Interrupt timing

Space Invaders is driven by two interrupts per video frame, both fired at
60 Hz. The CPU runs at 2 MHz, giving **33,333 T-states per frame** and
**~127 T-states per scanline** (262 scanlines: 224 visible + 38 VBLANK).

| Interrupt | Opcode | Vector | Scanline | Cycle offset | Purpose |
|---|---|---|---|---|---|
| **RST 1** | `0xCF` | `0x0008` | 96 | ~12,192 | mid-screen |
| **RST 2** | `0xD7` | `0x0010` | 224 | ~28,448 | start of VBLANK |

**Why two interrupts and why these scanlines?** The CRT is rotated 90° in
the cabinet, so the electron beam sweeps across what the player sees as
*vertical* columns. The game must update video RAM without the beam
overtaking the region it's drawing (which would tear the image). It splits
the work in half:

- **RST 1 (scanline 96, mid-screen):** the game updates one half of VRAM
  while the beam is painting the other half.
- **RST 2 (scanline 224, VBLANK start):** the beam has left the visible
  area entirely. The game has ~38 scanlines (~4,800 cycles) of VBLANK to
  finish the heavier draw work before the next frame begins.

The `InterruptScheduler` (in `hardware.cpp`) is a three-phase state machine
that fires each interrupt at its cycle threshold and signals frame
completion to the game loop. It rebases by a *fixed* delta
(`frame_start_cycle += 33,333`) rather than snapping to the current cycle
count, so timing never drifts. Verified over 1,000 consecutive frames:
exactly 1,000 RST 1s, 1,000 RST 2s, zero drift.

The CPU's `INTE` flag gates interrupts. `EI` sets it, `DI` clears it, and
acknowledging an interrupt clears it automatically (the handler re-enables
with `EI` before returning). If `INTE` is clear when an interrupt is
scheduled, the request is simply lost — matching the edge-triggered `INT`
pin on real hardware.

---

## Hardware quirks

### The shift register (ports 2, 4, 3)

The 8080 can only rotate one bit per instruction. Sprite blitting against
the rotated framebuffer requires shifting sprite data by an arbitrary
0–7 bit offset, which would be far too slow with `RAR`/`RAL`. Taito bolted
on an external 16-bit shift register with a programmable barrel-shift tap:

```
OUT 4, v   →  shift_reg   = (v << 8) | (shift_reg >> 8)
              (new byte enters the HIGH half; old HIGH slides to LOW;
               old LOW is discarded)

OUT 2, n   →  shift_offset = n & 0x07          (only low 3 bits wired)

IN  3      →  result       = (shift_reg >> (8 - shift_offset)) & 0xFF
```

Worked example with `shift_reg = 0xABCD`:

| Offset | `IN 3` result |
|:---:|:---:|
| 0 | `0xAB` |
| 1 | `0x57` |
| 2 | `0xAF` |
| 3 | `0x5E` |
| 4 | `0xBC` |
| 5 | `0x79` |
| 6 | `0xF3` |
| 7 | `0xE6` |

This is the single most failure-prone piece of the hardware emulation —
an off-by-one in the shift formula renders every sprite as garbage. It is
verified bit-exactly for all 8 offsets in `test_hardware.cpp` (H3).

### Display rotation

The CRT was mounted rotated 90° counter-clockwise inside the cabinet. VRAM
is stored in the 8080's native orientation (256 wide × 224 tall, each byte
holding 8 vertically-stacked pixels of one column). The renderer applies the
rotation when copying to the framebuffer:

```
(mem_x, mem_y)  →  (mem_y, 255 - mem_x)  =  (screen_x, screen_y)
```

so the framebuffer is already in the player's 224×256 orientation and can be
streamed straight to the GPU. See [Display & rotation](#display--rotation).

### Port 1 bit 3 hardwired high

On the real PCB, bit 3 of input port 1 is tied to +5 V, so it always reads
1. The `Hardware` struct initializes `port1` to `0x08` to reflect this.
Input handlers only ever set/clear the action bits, never bit 3.

### Watchdog (port 6)

The real board resets if no write to port 6 happens within ~250 ms (a
hang-detection watchdog). We accept and ignore writes to port 6 — there's
no benefit to emulating a reset-on-hang for a software emulator.

---

## CPU implementation notes

**Dispatch.** A flat 256-case `switch` on the opcode byte, which GCC
compiles into a jump table. No function-pointer indirection in the hot path.
The MOV r,r block (`0x40–0x7F`) and ALU r block (`0x80–0xBF`) use GCC's
case-range extension to collapse 128 regular cases into compact decode-by-
opcode-bits handlers.

**Flag math — three subtle correctness points:**

1. **Subtraction via the adder.** `SUB`/`SBB` are implemented as
   `a + (~b) + (1 - borrow)`, exactly as the silicon does it. This makes the
   Auxiliary Carry computation identical to addition (carry out of bit 3),
   and CY becomes the *negation* of the adder's carry-out (the 8080 sets CY
   on borrow).
2. **`ANA`/`ANI` Auxiliary Carry.** A documented 8080 quirk: AC is set to
   `((A | operand) & 0x08) != 0`, not unconditionally cleared. Emulators
   that "just clear AC for logicals" fail the 8080 exerciser ROM here.
3. **PSW hardwired bits.** `PUSH PSW`/`POP PSW` round-trip the flag byte
   with bit 1 forced to 1 and bits 3/5 forced to 0, matching the silicon.
   On reset the flag byte reads `0x02`, not `0x00`.

**Cycle counts** come from the Intel 8080 Programmer's Manual. Conditional
`CALL`/`RET` add 6 T-states when the condition is taken; conditional jumps
are always 10 (the 8080, unlike the Z80, doesn't vary jump timing). Measured
accuracy: after 60 emulated frames the cycle counter reads 1,999,983 vs the
ideal 2,000,000 — a 0.00085% deviation, entirely from the integer
truncation of 33,333.33 cycles/frame.

**Interrupt injection.** `cpu_step()` samples the interrupt line *before*
the fetch. If an interrupt is pending and `INTE` is set, it executes the
RST opcode in place of a fetch, clears `INTE`, and releases the `HLT` latch.
The RST pushes the current PC, so the handler returns to exactly where the
CPU would have continued.

---

## Display & rotation

- Native resolution (player orientation): **224 × 256**.
- Window: scaled **3×** to **672 × 768** via GPU nearest-neighbor.
- Pixel format: ARGB8888; white (`0xFFFFFFFF`) on black (`0xFF000000`).

The renderer walks all 7,168 VRAM bytes. For each byte it computes one base
framebuffer index, then writes 8 pixels striding by `-NATIVE_W` (because all
8 bits of a byte share the same screen column and descend by one row). This
keeps the inner loop tight: ~57k writes per frame, ~3.4M writes/second at
60 Hz.

The renderer is verified by writing single bits to all four VRAM corners and
confirming the lit pixel appears at exactly the predicted framebuffer
location, plus row→column and column→row inversion tests (1,223 assertions
in `test_renderer.cpp`).

> **Color overlay (not implemented):** the original cabinet glued a colored
> gel over the CRT — a red band near the top (UFO), a green band near the
> bottom (player + shields), white elsewhere. The current build renders pure
> white-on-black. Adding the overlay is a localized change in `renderer.cpp`:
> select `COLOR_ON` from a per-region lookup keyed on `screen_y`.

---

## Input mapping

| Key | Action | Port 1 bit |
|---|---|:---:|
| Enter / Keypad Enter | Coin insert | 0 |
| 1 | Player 1 start | 2 |
| Space | Shoot | 4 |
| Left arrow | Move left | 5 |
| Right arrow | Move right | 6 |
| Escape | Quit | — |

Inputs are press-and-hold: holding a key keeps the corresponding bit set.
The game ROM contains its own debounce logic, so the emulator faithfully
reports raw button state rather than synthesizing edges.

---

## Testing

Five standalone test suites, **2,050 assertions total**, all run under
`-Wall -Wextra -Wpedantic` with zero warnings:

| Suite | Assertions | Coverage |
|---|---:|---|
| `test_cpu` | 560 | All opcodes, flag boundaries, CALL/RET, interrupts, PSW round-trip |
| `test_loader` | 185 | Both ROM orderings, error paths, ROM write-protect sweep, RAM r/w, endianness |
| `test_hardware` | 56 | Shift register (bit-exact, all offsets), interrupt scheduler (1,000-frame drift), CPU IN/OUT round-trip, full-frame interrupt servicing |
| `test_renderer` | 1,223 | Rotation corners, row↔column inversions, pattern verification |
| `test_integration` | 26 | Full stack: ROM → CPU → interrupt handlers → VRAM → framebuffer |

Run them all via `ctest`:

```bat
cd build && ctest --output-on-failure
```

The integration test builds a synthetic chip-E ROM that mirrors the
structural shape of the real boot code (stack init → `EI` → `HLT` loop →
interrupt handlers painting VRAM), runs 60 emulated frames headless, and
verifies both the painted memory bytes and the resulting rotated framebuffer
pixels. If it passes, the real ROMs exercise the same code paths.

---

## Opcode coverage

All 256 opcode bytes are implemented (244 documented + 12 undocumented
alternates of NOP/JMP/RET/CALL). The full per-opcode table — mnemonic,
length, cycle count, category, documented status — is in
[`OPCODES.md`](OPCODES.md), generated directly from the implementation's own
decode and cycle tables.

### Regenerating the opcode report

```bat
g++ -std=c++17 -I include tools/gen_opcode_report.cpp ^
    src/cpu.cpp src/hardware.cpp src/disassembler.cpp ^
    src/renderer.cpp src/rom_loader.cpp -o gen_report
gen_report > OPCODES.md
```

Because the report is generated from the same tables the emulator uses, it
can never drift out of sync with the actual behavior.

---

## Project layout

```
InvaderX/
├── CMakeLists.txt          build system
├── README.md               this file
├── OPCODES.md              generated opcode coverage report
├── include/                all headers
│   ├── cpu.h, cpu_step.h
│   ├── memory.h
│   ├── hardware.h, hardware_ops.h
│   ├── display.h
│   ├── renderer.h
│   ├── rom_loader.h
│   ├── disassembler.h
│   └── sdl_frontend.h
├── src/                    production sources (ship in the binary)
│   ├── cpu.cpp
│   ├── disassembler.cpp
│   ├── hardware.cpp
│   ├── renderer.cpp
│   ├── rom_loader.cpp
│   ├── sdl_frontend.cpp
│   └── main.cpp
├── tests/                  test harnesses (not in the binary)
│   ├── test_cpu.cpp
│   ├── test_loader.cpp
│   ├── test_hardware.cpp
│   ├── test_renderer.cpp
│   └── test_integration.cpp
├── tools/                  dev tooling
│   └── gen_opcode_report.cpp
└── roms/                   place invaders.{e,f,g,h} here (not committed)
```

---

## Design decisions

**State and behavior are separated.** `CPU`, `Hardware`, and `Display` are
plain data structs. The dispatcher, scheduler, and renderer are free
functions operating on them by reference. No virtual dispatch in the hot
path, no hidden coupling.

**No dynamic allocation in the CPU hot path.** The 64 KB address space is a
`std::array`, the framebuffer is a `std::array`, the register file is plain
members. `cpu_step()` never touches the heap.

**Endianness is explicit.** Register pairs are accessed via inline
`bc()/de()/hl()` helpers that build the 16-bit value with shifts, rather
than type-punned unions (which are UB in C++ and host-endian dependent).
The compiler folds them into single moves.

**Weak-symbol I/O hooks.** `cpu.cpp` declares `cpu_in_port`/`cpu_out_port`
and ships *weak* stub implementations, so the CPU links and unit-tests in
isolation. `hardware.cpp` provides the strong definitions; the linker
prefers them automatically. The CPU never had to be modified when hardware
emulation landed.

**SDL confined to one translation unit.** Everything SDL-specific is in
`sdl_frontend.cpp`. The emulation core has no SDL dependency, which is why
the renderer and integration tests run headless in CI.

**Wall-clock pacing, not just vsync.** `FramePacer` uses
`SDL_GetPerformanceCounter` with sleep-then-spin and schedules each frame
from the *ideal* tick rather than the actual finish, so 60 Hz holds with
zero long-term drift regardless of monitor refresh rate or CPU speed.

---

## Troubleshooting

**Boot disassembly is garbage / black screen.** Wrong ROM ordering. Rerun
with the other flag: `invaderx ..\roms --mame` (or `--spec`).

**`0xc000007b` on launch (Windows).** `SDL2.dll` isn't next to the exe. The
build copies it automatically; if you moved the exe, copy `SDL2.dll` from
`C:\SDL2\lib` next to it.

**`undefined reference to WinMain@16` at link time.** SDL2 link order is
wrong. The CMake build handles this (`mingw32 SDL2main SDL2`); if you're
building by hand, that order is mandatory.

**CMake error "Cannot find SDL.h".** SDL2 isn't at `C:\SDL2`. Pass
`-DSDL2_ROOT="<your path>"`.

**ROM load fails with "wrong size".** Each ROM file must be exactly 2,048
bytes. Merged or padded dumps are rejected on purpose.

**Game runs too fast/slow.** The pacer locks to 60 Hz wall-clock. If it's
off, the host may be heavily loaded; the emulator never frame-skips, it just
runs slower than real-time under contention.

---

## References

- **Intel 8080 Microcomputer Systems User's Manual / 8080 Programmer's
  Manual** — opcode semantics, flag behavior, cycle counts.
- **Computer Archaeology — Space Invaders** — hardware reference: I/O port
  map, shift register behavior, interrupt timing, VRAM layout.
- **SDL2 documentation** (libsdl.org) — rendering and input API.

---

## Legal

This project contains **no copyrighted ROM data**. The *Space Invaders* ROM
files (`invaders.e/.f/.g/.h`) remain the intellectual property of Taito and
are **not distributed** with this emulator. You must supply your own legally
obtained ROM dumps to run the game.

The emulator source code itself is original work.
