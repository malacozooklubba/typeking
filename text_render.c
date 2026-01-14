#include "text_render.h"
#include "font_cache.h"
#include "theme.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <stdlib.h>

void textRenderDraw(SDL_Renderer *renderer, const char *text, float x, float y,
                    GlyphCacheEntry *glyph_cache, FontMetricsCache metrics,
                    SDL_Color color) {

    float baseline = y + metrics.ascent;
    float x_pos = x;

    for (const char *p = text; *p; p++) {
        int codepoint = (unsigned char)(*p);

        // Bounds check for glyph cache (32-126)
        if (codepoint < 32 || codepoint > 126) {
            continue;
        }

        GlyphCacheEntry glyph = glyph_cache[codepoint - 32];
        SDL_Color glyph_color = color;

        // Render the glyph if it has a texture (spaces don't)
        if (glyph.texture != NULL) {
            SDL_SetTextureColorMod(glyph.texture, glyph_color.r, glyph_color.g,
                                   glyph_color.b);

            SDL_FRect dst = {x_pos + glyph.xoff, baseline + glyph.yoff,
                             (float)glyph.width, (float)glyph.height};

            SDL_RenderTexture(renderer, glyph.texture, NULL, &dst);
        }

        x_pos += glyph.advance;

        // Apply kerning if next character exists
        if (*(p + 1)) {
            int codepoint1 = (int)(*p);
            int codepoint2 = (int)(*(p + 1));
            x_pos += fontCacheGetKerning(codepoint1, codepoint2);
        }
    }
}

void typedTextRenderDraw(SDL_Renderer *renderer, const char *text, float x,
                         float y, float font_size,
                         const unsigned char *char_states) {
    GlyphCacheEntry *glyph_cache = getGlyphCache();
    FontMetricsCache metrics = getFontMetricsCache();

    float baseline = y + metrics.ascent;
    float x_pos = x;

    int char_index = 0;
    for (const char *p = text; *p; p++) {
        int codepoint = (unsigned char)(*p);

        // Bounds check for glyph cache (32-126)
        if (codepoint < 32 || codepoint > 126) {
            char_index++;
            continue;
        }

        GlyphCacheEntry glyph = glyph_cache[codepoint - 32];  // CACHE_START_CHAR is 32
        SDL_Color glyph_color = {0x00, 0x00, 0x00, 0x00};

        switch (char_states[char_index]) {
        case 1:
            glyph_color = THEME_TEXT_TYPED;
            break;
        case 2:
            glyph_color = THEME_TEXT_ERROR;
            break;
        default:
            glyph_color = THEME_TEXT_UNTYPED;
            break;
        }

        // Render the glyph if it has a texture (spaces don't)
        if (glyph.texture != NULL) {
            SDL_SetTextureColorMod(glyph.texture, glyph_color.r,
                                   glyph_color.g, glyph_color.b);

            SDL_FRect dst = {
                x_pos + glyph.xoff, baseline + glyph.yoff,
                (float)glyph.width, (float)glyph.height};

            SDL_RenderTexture(renderer, glyph.texture, NULL, &dst);
        }

        x_pos += glyph.advance;

        // Apply kerning if next character exists
        if (*(p + 1)) {
            int codepoint2 = (unsigned char)(*(p + 1));
            if (codepoint2 >= 32 && codepoint2 <= 126) {
                x_pos += fontCacheGetKerning(codepoint, codepoint2);
            }
        }

        char_index++;
    }
}

void textRenderMeasure(const char *text, float font_size, float *width,
                       float *height) {
    if (width) {
        *width = textRenderMeasureVisibleWidth(text, font_size);
    }

    if (height) {
        FontMetricsCache metrics = getFontMetricsCache();
        *height = metrics.ascent - metrics.descent;
    }
}

float textRenderMeasureVisibleWidth(const char *text, float font_size) {
    if (!text || !text[0]) {
        return 0.0f;
    }

    GlyphCacheEntry *glyph_cache = getGlyphCache();
    float total_width = 0.0f;
    const char *p = text;

    for (; *p; p++) {
        int codepoint = (unsigned char)(*p);  // Use unsigned to avoid negative values

        // Bounds check for glyph cache (32-126)
        if (codepoint < 32 || codepoint > 126) {
            continue;
        }

        GlyphCacheEntry glyph = glyph_cache[codepoint - 32];  // CACHE_START_CHAR is 32
        total_width += glyph.advance;

        // Add kerning for consecutive characters
        if (*(p + 1)) {
            int codepoint2 = (unsigned char)(*(p + 1));
            if (codepoint2 >= 32 && codepoint2 <= 126) {
                total_width += fontCacheGetKerning(codepoint, codepoint2);
            }
        }
    }

    return total_width;
}

void textRenderGetMetrics(float font_size, float *ascent, float *descent,
                          float *line_gap) {
    // Get cached metrics
    FontMetricsCache metrics = getFontMetricsCache();

    if (ascent)
        *ascent = metrics.ascent;
    if (descent)
        *descent = metrics.descent;
    if (line_gap)
        *line_gap = metrics.line_gap;
}

float textRenderGetLineHeight(float font_size) {
    FontMetricsCache metrics = getFontMetricsCache();
    return metrics.ascent - metrics.descent + metrics.line_gap;
}
