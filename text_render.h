#pragma once

#include "font_cache.h"
#include "framebuffer.h"
#include "theme.h"
#include <stdbool.h>

// Render a single line of text at position (x, y)
void textRenderDraw(Framebuffer *fb, const char *text, float x, float y,
                    GlyphCacheEntry *glyph_cache, FontMetricsCache metrics,
                    Color color);

void typedTextRenderDraw(Framebuffer *fb, const char *text, float x,
                         float y, float font_size,
                         const unsigned char *char_states);

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
