#pragma once
// ============================================================================
//  InvaderX — sdl_frontend.h
//  SDL2 wiring: window/renderer/texture lifecycle, frame present, keyboard
//  event translation, and wall-clock 60 Hz pacer.
//
//  All SDL2-specific code is confined to this translation unit and
//  sdl_frontend.cpp. The CPU, memory, hardware, and renderer modules
//  do NOT include any SDL headers.
// ============================================================================

#include <cstdint>
#include "display.h"
#include "hardware.h"

namespace invaderx {

// ----------------------------------------------------------------------------
//  sdl_init / sdl_shutdown
//      Lifecycle for the SDL_Window, SDL_Renderer, and SDL_Texture stored
//      in `disp`. On success, disp's pointers are non-null; the texture is
//      a streaming ARGB8888 of native (NATIVE_W x NATIVE_H) size.
//      Returns true on success; on failure, writes a descriptive line to
//      stderr and leaves disp's pointers null (safe for sdl_shutdown).
// ----------------------------------------------------------------------------
bool sdl_init(Display& disp);
void sdl_shutdown(Display& disp);

// ----------------------------------------------------------------------------
//  sdl_present
//      Streams the framebuffer to the texture and presents it to the
//      window. Performs nearest-neighbor scaling from 224x256 -> WINDOW_W x
//      WINDOW_H via SDL_RenderCopy (GPU-side scale, near-free).
// ----------------------------------------------------------------------------
void sdl_present(Display& disp);

// ----------------------------------------------------------------------------
//  sdl_process_events
//      Drains the SDL event queue. Maps keyboard to port-1 bits via
//      hw_input_set(). Sets `quit` to true on SDL_QUIT or Escape.
//
//      Key map (per project brief):
//          Enter       -> P1_COIN   (bit 0)
//          1           -> P1_START  (bit 2)
//          Space       -> P1_FIRE   (bit 4)
//          Left arrow  -> P1_LEFT   (bit 5)
//          Right arrow -> P1_RIGHT  (bit 6)
//          Escape      -> quit
// ----------------------------------------------------------------------------
void sdl_process_events(Hardware& hw, bool& quit);

// ----------------------------------------------------------------------------
//  FramePacer
//      Wall-clock 60 Hz limiter. Call `wait_for_next_frame()` AFTER
//      rendering one frame; it blocks (sleep + spin) until 16.667 ms
//      have elapsed since the previous return.
//
//      Sleep + spin: SDL_Delay()'s precision is ~1 ms on Windows and
//      ~10 ms on bad kernels. We under-sleep by 1 ms, then spin on
//      SDL_GetPerformanceCounter() for the final stretch. This gives
//      sub-100 us pacing accuracy.
//
//      The pacer is independent of CPU emulation speed: even if the host
//      runs the 33,333 emulated cycles in 0.5 ms, the next frame still
//      starts at t = 16.667 ms.
// ----------------------------------------------------------------------------
class FramePacer {
public:
    void  reset();                       // call once at game-loop start
    void  wait_for_next_frame();         // call once per emulated frame

    // Diagnostics — useful when tracing why a frame ran long.
    double last_frame_ms() const { return m_last_frame_ms; }

private:
    uint64_t m_freq            = 0;      // ticks per second
    uint64_t m_target_per_frame= 0;      // ticks per 1/60 s
    uint64_t m_last_tick       = 0;
    double   m_last_frame_ms   = 0.0;
};

} // namespace invaderx
