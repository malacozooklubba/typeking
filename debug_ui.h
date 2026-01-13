#pragma once

#include <SDL3/SDL_render.h>
#include <stdbool.h>

// Initialize debug UI system
void debugUIInit(void);

// Update frame timing (call once per frame)
void debugUIUpdateFrame(void);

// Draw the debug UI overlay
void debugUIDraw(SDL_Renderer *renderer, int window_width, int window_height);

// Cleanup resources
void debugUIQuit(void);
