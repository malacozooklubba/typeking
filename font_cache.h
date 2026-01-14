#pragma once

#include <SDL3/SDL.h>

typedef struct {
    SDL_Texture *texture;
    int width, height;
    int xoff, yoff;
    int advance;
    int kerning;
} GlyphCacheEntry;

typedef struct {
    float ascent;
    float descent;
    float line_gap;
} FontMetricsCache;

bool fontCacheInit(SDL_Renderer *renderer, const char *font_path,
                   float font_size);

GlyphCacheEntry getGlyphCacheEntry(int codepoint);

GlyphCacheEntry *getGlyphCache();

FontMetricsCache getFontMetricsCache();

float fontCacheGetKerning(int codepoint1, int codepoint2);

void fontCacheQuit();
