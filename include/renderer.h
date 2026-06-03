#pragma once
// ============================================================================
//  InvaderX — renderer.h
//  VRAM -> framebuffer pixel transformation. No SDL dependency.
//
//  The CRT in the original Space Invaders cabinet was rotated 90 degrees
//  counter-clockwise inside the cabinet enclosure. The 8080 writes VRAM
//  in its native (unrotated) orientation; the renderer applies the
//  rotation when copying bits into the framebuffer.
// ============================================================================

#include "memory.h"
#include "display.h"

namespace invaderx {

// ----------------------------------------------------------------------------
//  render_vram_to_framebuffer
//      Reads the entire VRAM region (0x2400-0x3FFF) from `mem` and writes
//      the rotated, color-mapped result into `disp.framebuffer`. Replaces
//      the full framebuffer every call — no partial updates.
//
//      Cost: ~57,344 framebuffer writes per call (one per pixel). At 60 fps
//      that's ~3.4M writes/sec — negligible on any host capable of running
//      a windowed SDL app.
// ----------------------------------------------------------------------------
void render_vram_to_framebuffer(const Memory& mem, Display& disp);

} // namespace invaderx
