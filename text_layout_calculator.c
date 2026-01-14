#include "text_layout_calculator.h"
#include "text_render.h"
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_pixels.h>
#include <time.h>

#define CACHE_START_CHAR 32
#define CACHE_END_CHAR 126
#define CACHE_SIZE 95

static PrecalculatedTextLayout calculated_text_layout = {.line_count = 0};

static inline int getCacheIndex(int codepoint) {
    if (codepoint >= CACHE_START_CHAR && codepoint <= CACHE_END_CHAR) {
        return codepoint - CACHE_START_CHAR;
    }
    return -1;
}

int calculateTextLines(const char *text, int font_size, int layout_width,
                       int *out_line_starts) {

    if (!text || !text[0] || font_size <= 0 || layout_width <= 0)
        return 0;

    // Measure space width once
    float space_width;
    textRenderMeasure(" ", (float)font_size, &space_width, NULL);

    int line_count = 0;
    int line_pos = 0; // Characters on current line
    float current_line_width = 0.0f;
    int char_offset = 0; // Start position of current line in original text

    // First line always starts at 0
    out_line_starts[line_count++] = 0;

    const char *word_start = text;
    const char *p = text;

    while (*p) {
        // Check for word boundary (space or end of text)
        if (*p == ' ' || *(p + 1) == '\0') {
            // Calculate word length (include word boundary as part of word)
            int word_len = (p - word_start + 1);

            if (word_len > 0 && word_len < 64) {
                char word_buffer[64];
                strncpy(word_buffer, word_start, word_len);
                word_buffer[word_len] = '\0';

                // Measure word width
                float word_width;
                textRenderMeasure(word_buffer, (float)font_size, &word_width,
                                  NULL);

                if (line_pos > 0) {
                    // Not first word on line - check if word fits
                    float required_width =
                        current_line_width + word_width + space_width;

                    if (required_width > layout_width) {
                        // Word doesn't fit - end current line, start new one
                        char_offset += line_pos; // Move to next line start
                        out_line_starts[line_count++] = char_offset;
                        line_pos = 0;
                        current_line_width = 0.0f;
                    }
                }

                // Add word to current line
                line_pos += word_len;
                current_line_width += word_width;
            }

            word_start = p + 1;
        }

        p++;
    }

    return line_count;
}

void calculateTextLayoutLineBreaks(char *target_text, int font_size,
                                   int layout_width) {
    int line_count;
    int line_starts[MAX_LINES];

    line_count =
        calculateTextLines(target_text, font_size, layout_width, line_starts);

    calculated_text_layout.line_count = line_count;
    for (int i = 0; i < line_count; i++) {
        calculated_text_layout.line_starts[i] = line_starts[i];
    }
}

PrecalculatedTextLayout getCalculatedTextLayout() {
    return calculated_text_layout;
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
