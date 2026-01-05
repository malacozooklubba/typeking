#include "text_render.h"
#include "debug_ui.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define CACHE_START_CHAR 32
#define CACHE_END_CHAR 126
#define CACHE_SIZE 95

typedef struct {
    SDL_Texture *texture;
    int width, height;
    int xoff, yoff;
    int advance;
} GlyphCacheEntry;

typedef struct {
    float scale;
    float ascent;
    float descent;
    float line_gap;
} FontMetricsCache;

static unsigned char *ttf_buffer = NULL;
static stbtt_fontinfo font;
static float cached_font_size = 0.0f;
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
    cached_metrics.scale = stbtt_ScaleForPixelHeight(&font, font_size);
    int a, d, lg;
    stbtt_GetFontVMetrics(&font, &a, &d, &lg);
    cached_metrics.ascent = a * cached_metrics.scale;
    cached_metrics.descent = d * cached_metrics.scale;
    cached_metrics.line_gap = lg * cached_metrics.scale;
}

static SDL_Texture *bitmapToTexture(SDL_Renderer *renderer,
                                    unsigned char *bitmap, int width,
                                    int height, SDL_Color color) {
    if (!bitmap || width <= 0 || height <= 0) {
        return NULL;
    }

    SDL_Surface *surface =
        SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        SDL_Log("bitmapToTexture: Failed to create surface: %s",
                SDL_GetError());
        return NULL;
    }

    if (!SDL_LockSurface(surface)) {
        SDL_Log("bitmapToTexture: Failed to lock surface: %s", SDL_GetError());
        SDL_DestroySurface(surface);
        return NULL;
    }

    unsigned char *pixels = (unsigned char *)surface->pixels;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int src_index = y * width + x;
            int dst_index = y * surface->pitch + x * 4;
            unsigned char alpha = bitmap[src_index];

            pixels[dst_index + 0] = color.r;
            pixels[dst_index + 1] = color.g;
            pixels[dst_index + 2] = color.b;
            pixels[dst_index + 3] = alpha;
        }
    }

    SDL_UnlockSurface(surface);

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);

    if (!texture) {
        SDL_Log("bitmapToTexture: Failed to create texture: %s",
                SDL_GetError());
        return NULL;
    }

    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    return texture;
}

bool textRenderInit(SDL_Renderer *renderer, const char *font_path,
                    float font_size) {
    if (!renderer || !font_path || font_size <= 0) {
        SDL_Log("textRenderInit: Invalid parameters");
        return false;
    }

    // Get actual file size
    FILE *font_file = fopen(font_path, "rb");
    if (!font_file) {
        SDL_Log("textRenderInit: Failed to open font file: %s", font_path);
        return false;
    }

    fseek(font_file, 0, SEEK_END);
    long file_size = ftell(font_file);
    fseek(font_file, 0, SEEK_SET);

    if (file_size <= 0) {
        SDL_Log("textRenderInit: Invalid font file size");
        fclose(font_file);
        return false;
    }

    // Allocate exact size needed
    ttf_buffer = malloc(file_size);
    if (!ttf_buffer) {
        SDL_Log("textRenderInit: Failed to allocate buffer");
        fclose(font_file);
        return false;
    }

    size_t bytes_read = fread(ttf_buffer, 1, file_size, font_file);
    fclose(font_file);

    if (bytes_read != (size_t)file_size) {
        SDL_Log("textRenderInit: Failed to read font file completely");
        free(ttf_buffer);
        ttf_buffer = NULL;
        return false;
    }

    if (!stbtt_InitFont(&font, ttf_buffer,
                        stbtt_GetFontOffsetForIndex(ttf_buffer, 0))) {
        SDL_Log("textRenderInit: Failed to initialize font");
        free(ttf_buffer);
        ttf_buffer = NULL;
        return false;
    }

    // Cache metrics for the pre-rasterized size
    cacheMetricsForSize(font_size);

    // Pre-rasterize glyph cache
    SDL_Color white = {255, 255, 255, 255};
    int cached_count = 0;

    for (int codepoint = CACHE_START_CHAR; codepoint <= CACHE_END_CHAR;
         codepoint++) {
        int idx = getCacheIndex(codepoint);
        if (idx < 0)
            continue;

        int width, height, xoff, yoff;
        unsigned char *bitmap =
            stbtt_GetCodepointBitmap(&font, 0, cached_metrics.scale, codepoint,
                                     &width, &height, &xoff, &yoff);

        if (bitmap) {
            SDL_Texture *tex =
                bitmapToTexture(renderer, bitmap, width, height, white);
            if (tex) {
                int advance, lsb;
                stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &lsb);

                glyph_cache[idx].texture = tex;
                glyph_cache[idx].width = width;
                glyph_cache[idx].height = height;
                glyph_cache[idx].xoff = xoff;
                glyph_cache[idx].yoff = yoff;
                glyph_cache[idx].advance = advance;
                cached_count++;
            }
            stbtt_FreeBitmap(bitmap, NULL);
        } else {
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &lsb);

            glyph_cache[idx].texture = NULL;
            glyph_cache[idx].width = 0;
            glyph_cache[idx].height = 0;
            glyph_cache[idx].xoff = 0;
            glyph_cache[idx].yoff = 0;
            glyph_cache[idx].advance = advance;
            cached_count++;
        }
    }

    cached_font_size = font_size;

    SDL_Log("Text rendering initialized: %d glyphs at %.1fpx (%ld bytes)",
            cached_count, font_size, file_size);
    return true;
}

void textRenderQuit(void) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (glyph_cache[i].texture) {
            SDL_DestroyTexture(glyph_cache[i].texture);
            glyph_cache[i].texture = NULL;
        }
    }

    if (ttf_buffer) {
        free(ttf_buffer);
        ttf_buffer = NULL;
    }

    cached_font_size = 0.0f;
}

void textRenderDraw(SDL_Renderer *renderer, const char *text, float x, float y,
                    float font_size, SDL_Color color) {
    // Use cached metrics if using the cached font size
    float scale, ascent;
    if (font_size == cached_font_size) {
        scale = cached_metrics.scale;
        ascent = cached_metrics.ascent;
    } else {
        scale = stbtt_ScaleForPixelHeight(&font, font_size);
        int a, d, lg;
        stbtt_GetFontVMetrics(&font, &a, &d, &lg);
        ascent = a * scale;
    }

    float size = font_size / cached_font_size;
    float baseline = y + ascent;

    float x_pos = x;
    for (const char *p = text; *p; p++) {
        int codepoint = (int)(*p);
        int cache_idx = getCacheIndex(codepoint);

        if (cache_idx >= 0) {
            GlyphCacheEntry *entry = &glyph_cache[cache_idx];

            // Render the glyph if it has a texture (spaces don't)
            if (entry->texture != NULL) {
                SDL_SetTextureColorMod(entry->texture, color.r, color.g,
                                       color.b);

                SDL_FRect dst = {
                    x_pos + entry->xoff * size, baseline + entry->yoff * size,
                    (float)entry->width * size, (float)entry->height * size};
                SDL_RenderTexture(renderer, entry->texture, NULL, &dst);
                debugUIIncrementDrawCalls();
            }

            // Always advance, even for spaces
            x_pos += entry->advance * scale;

            // Apply kerning if next character exists
            if (*(p + 1)) {
                int kern = stbtt_GetCodepointKernAdvance(&font, codepoint,
                                                         (int)(*(p + 1)));
                if (kern != 0) { // Only apply if non-zero
                    x_pos += kern * scale;
                }
            }
        }
    }
}

void textRenderMeasure(const char *text, float font_size, float *width,
                       float *height) {
    // Use cached metrics if using the cached font size
    float scale;
    if (font_size == cached_font_size) {
        scale = cached_metrics.scale;
    } else {
        scale = stbtt_ScaleForPixelHeight(&font, font_size);
    }

    if (width) {
        float total_width = 0;
        for (const char *p = text; *p; p++) {
            int codepoint = (int)(*p);
            int cache_idx = getCacheIndex(codepoint);

            // Use cached advance if available
            if (cache_idx >= 0) {
                total_width += glyph_cache[cache_idx].advance * scale;
            } else {
                int advance, lsb;
                stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &lsb);
                total_width += advance * scale;
            }

            if (*(p + 1)) {
                int kern = stbtt_GetCodepointKernAdvance(&font, codepoint,
                                                         (int)(*(p + 1)));
                if (kern != 0) { // Only apply if non-zero
                    total_width += kern * scale;
                }
            }
        }
        *width = total_width;
    }

    if (height) {
        if (font_size == cached_font_size) {
            *height = cached_metrics.ascent - cached_metrics.descent;
        } else {
            int ascent, descent, line_gap;
            stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
            *height = (ascent - descent) * scale;
        }
    }
}

void textRenderGetMetrics(float font_size, float *ascent, float *descent,
                          float *line_gap) {
    // Use cached metrics if using the cached font size
    if (font_size == cached_font_size) {
        if (ascent)
            *ascent = cached_metrics.ascent;
        if (descent)
            *descent = cached_metrics.descent;
        if (line_gap)
            *line_gap = cached_metrics.line_gap;
    } else {
        float scale = stbtt_ScaleForPixelHeight(&font, font_size);
        int a, d, lg;
        stbtt_GetFontVMetrics(&font, &a, &d, &lg);

        if (ascent)
            *ascent = a * scale;
        if (descent)
            *descent = d * scale;
        if (line_gap)
            *line_gap = lg * scale;
    }
}

float textRenderGetLineHeight(float font_size) {
    // Use cached metrics if using the cached font size
    if (font_size == cached_font_size) {
        return cached_metrics.ascent - cached_metrics.descent +
               cached_metrics.line_gap;
    } else {
        float scale = stbtt_ScaleForPixelHeight(&font, font_size);
        int ascent, descent, line_gap;
        stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
        return (ascent - descent + line_gap) * scale;
    }
}
