// The game's Smoothing (src/smoothing.cpp): what screen_layout.cpp's overrides of the same guest functions call.
#pragma once

#include <cstdint>

namespace futari::smoothing {

// The guest global holding the display block's address (the settings object + 1408): smoothing.cpp reads its
// Smoothing (+68), screen_layout.cpp its zoom and screen mode.
constexpr uint32_t kDisplayBlockPointer = 0x821F8264;

// futari_dev_smoothing: with it set, the display block's Smoothing (display_block + 68) becomes its value, under every
// renderer; the save keeps its own. From the override of the function that copies the settings into that block.
void ApplyDevSmoothing(uint8_t* base, uint32_t display_block);
// The generic draw-from-memory (count vertices at the guest address `vertices`): while futari_dev_smoothing is set
// and the draw is the gameplay blit's, logs its 4 vertices (x, y, u, v) once per change. Every other draw: nothing.
void NoteBlitDraw(uint8_t* base, uint32_t vertices, uint32_t count);
// The same draw: when it is the gameplay blit's own (under every Smoothing and cvar), the guest address of the blit's
// VS matrix (c10..c13: 4 rows of 4 big-endian f32, clip x first, then clip y); 0 for every other draw.
uint32_t BlitDrawMatrix(uint32_t vertices, uint32_t count);

}  // namespace futari::smoothing
