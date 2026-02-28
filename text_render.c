#include "text_render.h"
#include "font_cache.h"
#include "theme.h"
#include <stdlib.h>

void textRenderDraw(Framebuffer *fb, const char *text, float x, float y,
                    GlyphCacheEntry *glyph_cache, FontMetricsCache metrics,
                    Color color) {

    float baseline = y + metrics.ascent;
    float x_pos = x;

    for (const char *p = text; *p; p++) {
        int codepoint = (unsigned char)(*p);

        // Bounds check for glyph cache (32-126)
        if (codepoint < 32 || codepoint > 126) {
            continue;
        }

        GlyphCacheEntry glyph = glyph_cache[codepoint - 32];

        // Render the glyph if it has a bitmap (spaces don't)
        if (glyph.bitmap != NULL) {
            int gx = (int)(x_pos + glyph.xoff + 0.5f);
            int gy = (int)(baseline + glyph.yoff + 0.5f);
            fb_blit_glyph(fb, gx, gy, glyph.bitmap, glyph.width, glyph.height, color);
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

void typedTextRenderDraw(Framebuffer *fb, const char *text, float x,
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

        GlyphCacheEntry glyph = glyph_cache[codepoint - 32];

        // If space has error state, show underscore instead so error is visible
        if (codepoint == ' ' && char_states[char_index] == 2) {
            glyph = glyph_cache['_' - 32];
        }

        Color glyph_color = {0x00, 0x00, 0x00, 0x00};

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

        // Render the glyph if it has a bitmap (spaces don't)
        if (glyph.bitmap != NULL) {
            int gx = (int)(x_pos + glyph.xoff + 0.5f);
            int gy = (int)(baseline + glyph.yoff + 0.5f);
            fb_blit_glyph(fb, gx, gy, glyph.bitmap, glyph.width, glyph.height, glyph_color);
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
        int codepoint = (unsigned char)(*p);

        // Bounds check for glyph cache (32-126)
        if (codepoint < 32 || codepoint > 126) {
            continue;
        }

        GlyphCacheEntry glyph = glyph_cache[codepoint - 32];
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
