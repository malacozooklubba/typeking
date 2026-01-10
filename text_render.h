#pragma once

#include <SDL3/SDL_render.h>
#include <stdbool.h>

// Initialize font rendering system with pre-rasterized glyph cache
// font_path: Path to TTF font file
// renderer: SDL_Renderer to create textures with
// font_size: Size in pixels to pre-rasterize (e.g., 42.0f)
// Returns: true on success, false on failure
bool textRenderInit(SDL_Renderer *renderer, const char *font_path,
                    float font_size);

// Cleanup font rendering resources
void textRenderQuit(void);

// Render a single line of text at position (x, y)
void textRenderDraw(SDL_Renderer *renderer, const char *text, float x, float y,
                    float font_size, SDL_Color color);

void typedTextRenderDraw(SDL_Renderer *renderer, const char *text, float x,
                         float y, float font_size, int char_states);

// Measure text dimensions without rendering (uses advance widths)
void textRenderMeasure(const char *text, float font_size, float *width,
                       float *height);

// Measure visible width of text (excludes trailing space advance)
float textRenderMeasureVisibleWidth(const char *text, float font_size);

// Get font metrics for layout calculations
void textRenderGetMetrics(float font_size, float *ascent, float *descent,
                          float *line_gap);

// Calculate line height (ascent - descent + line_gap)
float textRenderGetLineHeight(float font_size);
