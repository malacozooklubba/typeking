#include "font_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define CACHE_START_CHAR 32
#define CACHE_END_CHAR 126
#define CACHE_SIZE 95

static unsigned char *ttf_buffer = NULL;
static stbtt_fontinfo font;
static float cached_font_size = 0.0f;
static float cached_scale = 0.0f;
static GlyphCacheEntry glyph_cache[CACHE_SIZE];

// Cached font metrics for the pre-rasterized size
static FontMetricsCache cached_metrics = {0};

static inline int getCacheIndex(int codepoint) {
    if (codepoint >= CACHE_START_CHAR && codepoint <= CACHE_END_CHAR) {
        return codepoint - CACHE_START_CHAR;
    }
    return -1;
}

static inline void cacheMetricsForSize(float font_size) {
    float scale = stbtt_ScaleForPixelHeight(&font, font_size);
    int a, d, lg;
    stbtt_GetFontVMetrics(&font, &a, &d, &lg);
    cached_metrics.ascent = a * scale;
    cached_metrics.descent = d * scale;
    cached_metrics.line_gap = lg * scale;
}

GlyphCacheEntry getGlyphCacheEntry(int codepoint) {
    return glyph_cache[getCacheIndex(codepoint)];
}

GlyphCacheEntry *getGlyphCache() { return glyph_cache; }

FontMetricsCache getFontMetricsCache() { return cached_metrics; }

float fontCacheGetKerning(int codepoint1, int codepoint2) {
    int kern = stbtt_GetCodepointKernAdvance(&font, codepoint1, codepoint2);
    return kern * cached_scale;
}

bool fontCacheInit(const char *font_path, float font_size) {
    if (!font_path || font_size <= 0) {
        fprintf(stderr, "fontCacheInit: Invalid parameters\n");
        return false;
    }

    // Get actual file size
    FILE *font_file = fopen(font_path, "rb");
    if (!font_file) {
        fprintf(stderr, "fontCacheInit: Failed to open font file: %s\n", font_path);
        return false;
    }

    fseek(font_file, 0, SEEK_END);
    long file_size = ftell(font_file);
    fseek(font_file, 0, SEEK_SET);

    if (file_size <= 0) {
        fprintf(stderr, "fontCacheInit: Invalid font file size\n");
        fclose(font_file);
        return false;
    }

    // Allocate exact size needed
    ttf_buffer = malloc(file_size);
    if (!ttf_buffer) {
        fprintf(stderr, "fontCacheInit: Failed to allocate buffer\n");
        fclose(font_file);
        return false;
    }

    size_t bytes_read = fread(ttf_buffer, 1, file_size, font_file);
    fclose(font_file);

    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "fontCacheInit: Failed to read font file completely\n");
        free(ttf_buffer);
        ttf_buffer = NULL;
        return false;
    }

    if (!stbtt_InitFont(&font, ttf_buffer,
                        stbtt_GetFontOffsetForIndex(ttf_buffer, 0))) {
        fprintf(stderr, "fontCacheInit: Failed to initialize font\n");
        free(ttf_buffer);
        ttf_buffer = NULL;
        return false;
    }

    // Cache metrics for the pre-rasterized size
    cacheMetricsForSize(font_size);

    // Pre-rasterize glyph cache
    int cached_count = 0;
    float scale = stbtt_ScaleForPixelHeight(&font, font_size);
    cached_scale = scale;

    for (int codepoint = CACHE_START_CHAR; codepoint <= CACHE_END_CHAR;
         codepoint++) {
        int idx = getCacheIndex(codepoint);
        if (idx < 0)
            continue;

        int width, height, xoff, yoff;
        unsigned char *bitmap = stbtt_GetCodepointBitmap(
            &font, 0, scale, codepoint, &width, &height, &xoff, &yoff);

        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &lsb);

        glyph_cache[idx].bitmap = bitmap; // Keep raw bitmap (NULL for spaces)
        glyph_cache[idx].width = width;
        glyph_cache[idx].height = height;
        glyph_cache[idx].xoff = xoff;
        glyph_cache[idx].yoff = yoff;
        glyph_cache[idx].advance = (int)(advance * scale + 0.5f);
        cached_count++;
    }

    cached_font_size = font_size;

    fprintf(stderr, "Text rendering initialized: %d glyphs at %.1fpx (%ld bytes)\n",
            cached_count, font_size, file_size);
    return true;
}

void fontCacheQuit() {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (glyph_cache[i].bitmap) {
            stbtt_FreeBitmap(glyph_cache[i].bitmap, NULL);
            glyph_cache[i].bitmap = NULL;
        }
    }

    if (ttf_buffer) {
        free(ttf_buffer);
        ttf_buffer = NULL;
    }
}
