#include "text_render.h"
#include "font_cache.h"
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
        GlyphCacheEntry glyph = glyph_cache[*p];
        int codepoint = (int)(*p);
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
            x_pos += glyph.kerning;
        }
    }
}

void typedTextRenderDraw(SDL_Renderer *renderer, const char *text, float x,
                         float y, float font_size,
                         const unsigned char *char_states) {

    // int a, d, lg;
    // stbtt_GetFontVMetrics(&font, &a, &d, &lg);

    // float scale = stbtt_ScaleForPixelHeight(&font, font_size);
    // float ascent = a * scale;
    // float size = font_size / cached_font_size;
    // float baseline = y + ascent;
    // float x_pos = x;

    // int char_index = 0;
    // for (const char *p = text; *p; p++) {
    //     int codepoint = (int)(*p);
    //     int cache_idx = getCacheIndex(codepoint);
    //     SDL_Color glyph_color = {0x00, 0x00, 0x00, 0x00};

    //     switch (char_states[char_index]) {
    //     case 1:
    //         glyph_color = THEME_TEXT_TYPED;
    //         break;
    //     case 2:
    //         glyph_color = THEME_TEXT_ERROR;
    //         break;
    //     default:
    //         glyph_color = THEME_TEXT_UNTYPED;
    //         break;
    //     }

    //     if (cache_idx >= 0) {
    //         GlyphCacheEntry *entry = &glyph_cache[cache_idx];

    //         // Render the glyph if it has a texture (spaces don't)
    //         if (entry->texture != NULL) {
    //             SDL_SetTextureColorMod(entry->texture, glyph_color.r,
    //                                    glyph_color.g, glyph_color.b);

    //             SDL_FRect dst = {
    //                 x_pos + entry->xoff * size, baseline + entry->yoff *
    //                 size, (float)entry->width * size, (float)entry->height *
    //                 size};

    //             SDL_RenderTexture(renderer, entry->texture, NULL, &dst);
    //             debugUIIncrementDrawCalls();
    //         }

    //         x_pos += entry->advance * scale;

    //         // Apply kerning if next character exists
    //         if (*(p + 1)) {
    //             int kern = stbtt_GetCodepointKernAdvance(&font, codepoint,
    //                                                      (int)(*(p + 1)));
    //             if (kern != 0) { // Only apply if non-zero
    //                 x_pos += kern * scale;
    //             }
    //         }
    //     }

    //     char_index++;
    // }
}

void textRenderMeasure(const char *text, float font_size, float *width,
                       float *height) {
    // float scale = stbtt_ScaleForPixelHeight(&font, font_size);

    // if (width) {
    //     float total_width = 0;
    //     for (const char *p = text; *p; p++) {
    //         int codepoint = (int)(*p);
    //         int cache_idx = getCacheIndex(codepoint);

    //         // Use cached advance if available
    //         if (cache_idx >= 0) {
    //             total_width += glyph_cache[cache_idx].advance * scale;
    //         } else {
    //             int advance, lsb;
    //             stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &lsb);
    //             total_width += advance * scale;
    //         }

    //         if (*(p + 1)) {
    //             int kern = stbtt_GetCodepointKernAdvance(&font, codepoint,
    //                                                      (int)(*(p + 1)));
    //             if (kern != 0) { // Only apply if non-zero
    //                 total_width += kern * scale;
    //             }
    //         }
    //     }
    //     *width = total_width;
    // }

    // if (height) {
    //     if (font_size == cached_font_size) {
    //         *height = cached_metrics.ascent - cached_metrics.descent;
    //     } else {
    //         int ascent, descent, line_gap;
    //         stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    //         *height = (ascent - descent) * scale;
    //     }
    // }
}

float textRenderMeasureVisibleWidth(const char *text, float font_size) {
    // if (!text || !text[0]) {
    //     return 0.0f;
    // }

    // // Use cached metrics if using the cached font size
    // float scale = stbtt_ScaleForPixelHeight(&font, font_size);

    float total_width = 0;
    // const char *p = text;

    // for (; *p; p++) {
    //     int codepoint = (int)(*p);
    //     int cache_idx = getCacheIndex(codepoint);

    //     // Use cached advance if available
    //     if (cache_idx >= 0) {
    //         total_width += glyph_cache[cache_idx].advance * scale;
    //     } else {
    //         int advance, lsb;
    //         stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &lsb);
    //         total_width += advance * scale;
    //     }

    //     if (*(p + 1)) {
    //         int kern = stbtt_GetCodepointKernAdvance(&font, codepoint,
    //                                                  (int)(*(p + 1)));
    //         if (kern != 0) { // Only apply if non-zero
    //             total_width += kern * scale;
    //         }
    //     }
    // }

    return total_width;
}

void textRenderGetMetrics(float font_size, float *ascent, float *descent,
                          float *line_gap) {
    // Use cached metrics if using the cached font size
    // if (font_size == cached_font_size) {
    //     if (ascent)
    //         *ascent = cached_metrics.ascent;
    //     if (descent)
    //         *descent = cached_metrics.descent;
    //     if (line_gap)
    //         *line_gap = cached_metrics.line_gap;
    // } else {
    //     float scale = stbtt_ScaleForPixelHeight(&font, font_size);
    //     int a, d, lg;
    //     stbtt_GetFontVMetrics(&font, &a, &d, &lg);

    //     if (ascent)
    //         *ascent = a * scale;
    //     if (descent)
    //         *descent = d * scale;
    //     if (line_gap)
    //         *line_gap = lg * scale;
    // }
}

float textRenderGetLineHeight(float font_size) {
    // Use cached metrics if using the cached font size
    // if (font_size == cached_font_size) {
    //     return cached_metrics.ascent - cached_metrics.descent +
    //            cached_metrics.line_gap;
    // } else {
    //     float scale = stbtt_ScaleForPixelHeight(&font, font_size);
    //     int ascent, descent, line_gap;
    //     stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    //     return (ascent - descent + line_gap) * scale;
    // }

    return 0.0f;
}
