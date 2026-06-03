// ============================================================================
//  InvaderX — src/sdl_frontend.cpp
//
//  Everything SDL2-specific lives here. Other modules talk to this one
//  via small, SDL-free signatures (Display&, Hardware&, bool&).
//
//  Section guide:
//    §1  sdl_init / sdl_shutdown  — lifecycle of window/renderer/texture
//    §2  sdl_present              — streaming-texture upload + present
//    §3  sdl_process_events       — keyboard -> port 1 + quit
//    §4  FramePacer               — wall-clock 60 Hz lock
// ============================================================================

#include "sdl_frontend.h"
#include "hardware_ops.h"            // hw_input_set

// We provide our own main() in main.cpp WITHOUT including SDL.h there, so
// SDL must not try to rename main -> SDL_main and substitute its own entry
// point. SDL_MAIN_HANDLED tells SDL "the application owns main()"; we then
// call SDL_SetMainReady() ourselves inside sdl_init() before SDL_Init().
// This is the supported pattern for apps that keep main() SDL-free.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cstdio>

namespace invaderx {

// ============================================================================
//  §1  Window / renderer / texture lifecycle
// ============================================================================

bool sdl_init(Display& disp) {
    // Required because we own main() (see SDL_MAIN_HANDLED above). Must be
    // called before SDL_Init on platforms where SDL2main would normally
    // have done it for us.
    SDL_SetMainReady();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    disp.window = SDL_CreateWindow(
        "InvaderX",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        display::WINDOW_W, display::WINDOW_H,
        SDL_WINDOW_SHOWN);
    if (!disp.window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    // VSYNC is on — it's free anti-tearing insurance. The FramePacer
    // still does authoritative 60 Hz timing in case the monitor is at
    // 75/144/240 Hz. We fall back to SDL_RENDERER_SOFTWARE if no
    // accelerated driver is available (RDP, headless CI, weak GPU).
    disp.renderer = SDL_CreateRenderer(
        disp.window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!disp.renderer) {
        std::fprintf(stderr, "Accelerated renderer unavailable (%s); "
                             "falling back to software.\n", SDL_GetError());
        disp.renderer = SDL_CreateRenderer(
            disp.window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!disp.renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(disp.window);
        disp.window = nullptr;
        SDL_Quit();
        return false;
    }

    // Streaming texture sized to the NATIVE resolution. The GPU scales
    // it up to the window size via SDL_RenderCopy with nearest-neighbor
    // filtering by default — keeps pixel art crisp.
    disp.texture = SDL_CreateTexture(
        disp.renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        display::NATIVE_W, display::NATIVE_H);
    if (!disp.texture) {
        std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(disp.renderer); disp.renderer = nullptr;
        SDL_DestroyWindow  (disp.window);   disp.window   = nullptr;
        SDL_Quit();
        return false;
    }

    // Clear once so an uninitialized framebuffer doesn't ghost on screen.
    SDL_SetRenderDrawColor(disp.renderer, 0, 0, 0, 255);
    SDL_RenderClear(disp.renderer);
    SDL_RenderPresent(disp.renderer);
    return true;
}

void sdl_shutdown(Display& disp) {
    if (disp.texture)  { SDL_DestroyTexture (disp.texture);  disp.texture  = nullptr; }
    if (disp.renderer) { SDL_DestroyRenderer(disp.renderer); disp.renderer = nullptr; }
    if (disp.window)   { SDL_DestroyWindow  (disp.window);   disp.window   = nullptr; }
    SDL_Quit();
}

// ============================================================================
//  §2  Frame present — texture upload + scaled render
// ============================================================================

void sdl_present(Display& disp) {
    // SDL_UpdateTexture pushes the entire framebuffer to GPU memory.
    // Pitch = NATIVE_W * 4 bytes per pixel (ARGB8888).
    SDL_UpdateTexture(
        disp.texture,
        nullptr,
        disp.framebuffer.data(),
        display::NATIVE_W * int(sizeof(uint32_t)));

    SDL_RenderClear(disp.renderer);
    // nullptr/nullptr = full texture -> full window, scaled.
    SDL_RenderCopy(disp.renderer, disp.texture, nullptr, nullptr);
    SDL_RenderPresent(disp.renderer);
}

// ============================================================================
//  §3  Event handling — keyboard -> port 1 + quit
// ============================================================================

void sdl_process_events(Hardware& hw, bool& quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                quit = true;
                return;

            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                const bool pressed = (e.type == SDL_KEYDOWN);
                switch (e.key.keysym.sym) {
                    case SDLK_RETURN:    hw_input_set(hw, Hardware::P1_COIN,  pressed); break;
                    case SDLK_KP_ENTER:  hw_input_set(hw, Hardware::P1_COIN,  pressed); break;
                    case SDLK_1:         hw_input_set(hw, Hardware::P1_START, pressed); break;
                    case SDLK_SPACE:     hw_input_set(hw, Hardware::P1_FIRE,  pressed); break;
                    case SDLK_LEFT:      hw_input_set(hw, Hardware::P1_LEFT,  pressed); break;
                    case SDLK_RIGHT:     hw_input_set(hw, Hardware::P1_RIGHT, pressed); break;
                    case SDLK_ESCAPE:    if (pressed) quit = true;                       break;
                    default: break;
                }
                break;
            }

            default:
                break;
        }
    }
}

// ============================================================================
//  §4  FramePacer — sleep + spin to lock at 60 Hz wall-clock
// ============================================================================

void FramePacer::reset() {
    m_freq             = SDL_GetPerformanceFrequency();
    m_target_per_frame = m_freq / 60;          // ticks per 1/60 s
    m_last_tick        = SDL_GetPerformanceCounter();
    m_last_frame_ms    = 0.0;
}

void FramePacer::wait_for_next_frame() {
    const uint64_t target = m_last_tick + m_target_per_frame;

    // Coarse sleep — under-sleep by 1 ms to compensate for SDL_Delay's
    // imprecision (esp. on Windows).
    uint64_t now = SDL_GetPerformanceCounter();
    if (now < target) {
        uint64_t remaining_ticks = target - now;
        uint64_t ms = (remaining_ticks * 1000) / m_freq;
        if (ms > 1) SDL_Delay(uint32_t(ms - 1));
    }

    // Fine spin — busy-wait the last fraction. Costs <1 ms of CPU per
    // frame in the worst case; trivially better than missing vsync.
    while ((now = SDL_GetPerformanceCounter()) < target) {
        // intentionally empty
    }

    // Stats — useful when diagnosing slow frames.
    uint64_t elapsed = now - m_last_tick;
    m_last_frame_ms  = double(elapsed) * 1000.0 / double(m_freq);
    m_last_tick      = target;        // schedule next frame from the IDEAL
                                       // tick, not the actual, to prevent
                                       // drift from accumulating.
}

} // namespace invaderx
