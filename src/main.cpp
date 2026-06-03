// ============================================================================
//  InvaderX — src/main.cpp
//  Composition layer. No new algorithms here; just wires the tested modules
//  into the master game loop:
//
//      ┌────────────────────────────────────────────────────────────────┐
//      │ load ROMs -> sanity-disassemble -> init SDL                    │
//      │                                                                │
//      │ while (!quit):                                                 │
//      │   poll SDL events  -> hardware.port1 bits                      │
//      │   step CPU until scheduler reports a frame boundary            │
//      │   render VRAM -> framebuffer                                   │
//      │   present framebuffer to SDL                                   │
//      │   pace to next 60 Hz wall-clock tick                           │
//      │                                                                │
//      │ shutdown SDL                                                   │
//      └────────────────────────────────────────────────────────────────┘
// ============================================================================

#include "cpu.h"
#include "memory.h"
#include "hardware.h"
#include "display.h"
#include "cpu_step.h"
#include "hardware_ops.h"
#include "rom_loader.h"
#include "renderer.h"
#include "sdl_frontend.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace invaderx;

// ----------------------------------------------------------------------------
//  Tiny CLI
//      Usage:  invaderx [rom_dir]  [--mame]
//      Defaults: rom_dir = "roms", order = Spec
//      The brief specifies positional arg + simple flags, no full parser
//      dependency.
// ----------------------------------------------------------------------------
struct CliArgs {
    std::string rom_dir = "roms";
    RomOrder    order   = RomOrder::Spec;
    bool        ok      = true;
};

static CliArgs parse_cli(int argc, char** argv) {
    CliArgs a;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--mame") == 0) {
            a.order = RomOrder::Mame;
        } else if (std::strcmp(arg, "--spec") == 0) {
            a.order = RomOrder::Spec;
        } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            std::printf("Usage: invaderx [rom_dir]  [--mame | --spec]\n"
                        "  rom_dir   directory containing invaders.{e,f,g,h}  (default: roms)\n"
                        "  --mame    use MAME ROM ordering   (h@0x0000, g@0x0800, f@0x1000, e@0x1800)\n"
                        "  --spec    use spec ROM ordering   (e@0x0000, f@0x0800, g@0x1000, h@0x1800)  [default]\n"
                        "\n"
                        "Keys:\n"
                        "  Enter / KP Enter   coin insert\n"
                        "  1                  Player 1 start\n"
                        "  Space              shoot\n"
                        "  Left / Right       move\n"
                        "  Escape             quit\n");
            a.ok = false;
        } else if (arg[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", arg);
            a.ok = false;
        } else {
            a.rom_dir = arg;
        }
    }
    return a;
}

// ----------------------------------------------------------------------------
//  Banner — short identification line so users can tell which build / mode
//  they're running.
// ----------------------------------------------------------------------------
static void print_banner() {
    std::printf("InvaderX — Intel 8080 Space Invaders emulator\n"
                "  CPU @ %d Hz, %d cycles/frame, RST 1 @ scanline 96, RST 2 @ scanline 224\n",
                display::CPU_HZ,
                display::CYCLES_PER_FRAME);
}

// ----------------------------------------------------------------------------
//  Game loop — extracted so the integration test can call it with a step
//  limit instead of running forever.
//
//      max_frames < 0  -> run until SDL_QUIT / Escape
//      max_frames >= 0 -> run for exactly that many frames then return
//
//  Returns the number of frames actually rendered.
// ----------------------------------------------------------------------------
int run_game_loop(CPU& cpu, Memory& mem, Hardware& hw, Display& disp,
                  int max_frames = -1) {
    InterruptScheduler sched;
    FramePacer         pacer;
    sched.reset(cpu.cycles);
    pacer.reset();

    int frames = 0;
    bool quit  = false;
    while (!quit) {
        // 1. Drain SDL events; map keys to port-1 bits, set quit on close.
        sdl_process_events(hw, quit);

        // 2. Run the CPU until the scheduler reports a frame boundary.
        //    Inside this loop the scheduler also fires RST 1 / RST 2 at
        //    their scanline thresholds.
        while (!sched.tick(hw, cpu.cycles) && !quit) {
            cpu_step(cpu, mem, hw);
        }

        // 3. Translate VRAM into the rotated framebuffer, then present.
        render_vram_to_framebuffer(mem, disp);
        sdl_present(disp);

        // 4. Sleep + spin until the next 60 Hz wall-clock tick.
        pacer.wait_for_next_frame();

        ++frames;
        if (max_frames >= 0 && frames >= max_frames) break;
    }
    return frames;
}

// ----------------------------------------------------------------------------
//  main — load, init, run, shutdown.
// ----------------------------------------------------------------------------
int main(int argc, char** argv) {
    CliArgs args = parse_cli(argc, argv);
    if (!args.ok) return 0;

    print_banner();

    // --- 1. Load ROMs ------------------------------------------------------
    CPU      cpu;
    Memory   mem;
    Hardware hw;
    Display  disp;

    RomLoadResult lr = load_invaders_roms(mem, args.rom_dir, args.order);
    print_load_report(lr, args.order);
    if (!lr.ok) {
        std::fprintf(stderr,
            "\nROM load failed. Place invaders.{e,f,g,h} in '%s' "
            "(each exactly 2048 bytes), or try the other ordering:\n"
            "  invaderx %s %s\n",
            args.rom_dir.c_str(),
            args.rom_dir.c_str(),
            args.order == RomOrder::Spec ? "--mame" : "--spec");
        return 1;
    }

    // --- 2. Boot sanity check via disassembly ------------------------------
    //  Real Space Invaders boots with a NOP pad, then LXI SP, then code.
    //  If this prints garbage (e.g. mostly RST 7 / *CALL), the ordering is
    //  wrong and the user should rerun with the other flag.
    std::printf("\nBoot vector disassembly:\n");
    dump_disassembly(mem, 0x0000, 12);

    // --- 3. Initialize SDL -------------------------------------------------
    if (!sdl_init(disp)) {
        std::fprintf(stderr, "SDL init failed; cannot continue.\n");
        return 1;
    }
    std::printf("\nSDL initialized — entering game loop. Press Escape to quit.\n");

    // --- 4. Run the master game loop ---------------------------------------
    int frames = run_game_loop(cpu, mem, hw, disp);
    std::printf("Game loop exited after %d frames.\n", frames);

    // --- 5. Shutdown -------------------------------------------------------
    sdl_shutdown(disp);
    return 0;
}
