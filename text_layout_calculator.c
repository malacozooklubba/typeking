#include "text_layout_calculator.h"
#include "font_cache.h"
#include "text_render.h"
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_pixels.h>

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define CACHE_START_CHAR 32
#define CACHE_END_CHAR 126
#define CACHE_SIZE 95

static inline int getCacheIndex(int codepoint) {
    if (codepoint >= CACHE_START_CHAR && codepoint <= CACHE_END_CHAR) {
        return codepoint - CACHE_START_CHAR;
    }
    return -1;
}

int calculateTextLines(const char *text, int font_size, int layout_width,
                       int *out_line_starts) {

    if (!text || font_size <= 0 || layout_width <= 0)
        return 0;

    const int max_chars_per_line = layout_width / font_size;
    if (max_chars_per_line <= 0)
        return 0;

    int line_count = 0;
    int line_start = 0;
    int last_space = -1;
    int char_count = 0;

    out_line_starts[line_count++] = 0;

    for (int i = 0; text[i] != '\0'; i++) {
        char c = text[i];
        char_count++;

        if (c == ' ')
            last_space = i;

        if (c == '\n') {
            line_start = i + 1;
            out_line_starts[line_count++] = line_start;
            char_count = 0;
            last_space = -1;
            continue;
        }

        if (char_count >= max_chars_per_line) {
            if (last_space >= line_start) {
                // Wrap at last space
                line_start = last_space + 1;
                out_line_starts[line_count++] = line_start;
                char_count = i - line_start + 1;
                last_space = -1;
            } else {
                // Hard wrap
                line_start = i;
                out_line_starts[line_count++] = line_start;
                char_count = 1;
            }
        }
    }

    return line_count;
}

int calculateTextLayout(TextLayout *layout, const char *text, float max_width,
                        float font_size) {
    // Initialize layout
    layout->line_count = 0;
    layout->max_width = max_width;
    layout->font_size = font_size;
    layout->is_valid = true;

    // Handle empty text
    if (!text || text[0] == '\0') {
        return true;
    }

    // Measure space width once
    float space_width;
    textRenderMeasure(" ", font_size, &space_width, NULL);

    int line_start = 0;
    int line_length = 0;
    float current_line_width = 0.0f;

    const char *word_start = text;
    const char *p = text;

    while (*p) {
        // Word boundary detection (EXACT same as ui.c line 157)
        if (*p == ' ' || *(p + 1) == '\0') {
            int word_len = (p - word_start + 1);

            if (word_len > 0 && word_len < 64) {
                char word_buffer[64];
                strncpy(word_buffer, word_start, word_len);
                word_buffer[word_len] = '\0';

                float word_width;
                textRenderMeasure(word_buffer, font_size, &word_width, NULL);

                if (line_length > 0) {
                    // ALWAYS add space_width (matches ui.c line 175)
                    float required_width =
                        current_line_width + word_width + space_width;

                    if (required_width > max_width) {
                        // Save current line before wrapping
                        if (layout->line_count >= MAX_LINES) {
                            layout->is_valid = false;
                            return false;
                        }

                        layout->lines[layout->line_count].start_char_index =
                            line_start;
                        layout->lines[layout->line_count].end_char_index =
                            line_start + line_length;
                        layout->lines[layout->line_count].char_count =
                            line_length;
                        layout->line_count++;

                        // Start new line
                        line_start = line_start + line_length;
                        line_length = 0;
                        current_line_width = 0.0f;
                    }
                }

                line_length += word_len;
                current_line_width += word_width;
            }

            word_start = p + 1;
        }
        p++;
    }

    // Save final line
    if (line_length > 0 && layout->line_count < MAX_LINES) {
        layout->lines[layout->line_count].start_char_index = line_start;
        layout->lines[layout->line_count].end_char_index =
            line_start + line_length;
        layout->lines[layout->line_count].char_count = line_length;
        layout->line_count++;
    }

    return layout->line_count;
}

static void textRenderMeasureWidth(const char *text,
                                   GlyphCacheEntry *glyph_cache,
                                   float font_size, float *width) {

    if (width) {
        float total_width = 0;
        for (const char *p = text; *p; p++) {
            int codepoint = (int)(*p);

            // Use cached advance if available
            total_width += glyph_cache[codepoint].advance;

            if (*(p + 1)) {
                int kern = total_width += glyph_cache[codepoint].advance;
            }
        }
        *width = total_width;
    }
}
