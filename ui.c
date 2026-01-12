#include "ui.h"
#include "debug_ui.h"
#include "text_render.h"
#include "theme.h"
#include <SDL3/SDL.h>
#include <string.h>

UITextBox uiTextBoxCreate(float x, float y, float width, float height) {
    UITextBox box = {.x = x,
                     .y = y,
                     .width = width,
                     .height = height,
                     .padding = {.top = 10.0f,
                                 .right = 10.0f,
                                 .bottom = 10.0f,
                                 .left = 10.0f},
                     .bg_color = THEME_BACKGROUND,
                     .text_color = THEME_TEXT_TYPED,
                     .text = "",
                     .char_states = NULL,
                     .font_size = 42.0f,
                     .align = UI_ALIGN_START,
                     .caret_position = -1};
    return box;
}

static inline float calculateAlignedX(float base_x, float max_width,
                                      float line_width, UITextAlign align) {
    switch (align) {
    case UI_ALIGN_CENTER:
        return base_x + (max_width - line_width) / 2.0f;
    case UI_ALIGN_END:
        return base_x + (max_width - line_width);
    case UI_ALIGN_START:
    default:
        return base_x;
    }
}

static inline float calculateAlignedY(float base_y, float max_height,
                                      float line_height, UITextAlign align) {
    switch (align) {
    case UI_ALIGN_CENTER:
        return base_y + (max_height - line_height) / 2.0f;
    case UI_ALIGN_END:
        return base_y + (max_height - line_height);
    case UI_ALIGN_START:
    default:
        return base_y;
    }
}

// Helper function to render a line of text with alignment
static inline void renderAlignedLine(SDL_Renderer *renderer,
                                     const char *line_buffer, float base_x,
                                     float base_y, float max_width,
                                     float max_height, const UITextBox *box,
                                     const unsigned char *char_states,
                                     int char_offset, int line_length) {
    float line_width =
        textRenderMeasureVisibleWidth(line_buffer, box->font_size);
    float text_x = calculateAlignedX(base_x, max_width, line_width, box->align);

    float line_height = textRenderGetLineHeight(box->font_size);
    float text_y =
        calculateAlignedY(base_y, max_height, line_height, box->align);

    if (char_states != NULL) {
        typedTextRenderDraw(renderer, line_buffer, text_x, text_y,
                            box->font_size, char_states + char_offset);
    } else {
        SDL_Log("char_states is NULL");
    }

    // Draw caret if it's on this line
    if (box->caret_position >= char_offset &&
        box->caret_position < char_offset + line_length) {
        int caret_offset_in_line = box->caret_position - char_offset;

        float caret_x;

        // Use visual lerp position on first line only
        caret_x = text_x + box->caret_visual_x_offset;

        float ascent, descent;
        textRenderGetMetrics(box->font_size, &ascent, &descent, NULL);
        float caret_y = text_y;
        float caret_height = ascent - descent;

        // Draw caret as a vertical line
        SDL_Color caret_color = THEME_TEXT_TYPED;
        SDL_SetRenderDrawColor(renderer, caret_color.r, caret_color.g,
                               caret_color.b, caret_color.a);
        SDL_FRect caret_rect = {caret_x, caret_y, 2.0f, caret_height};
        SDL_RenderFillRect(renderer, &caret_rect);
        debugUIIncrementDrawCalls();
    }
}

void uiTextBoxDraw(SDL_Renderer *renderer, const UITextBox *box) {
    if (!renderer || !box) {
        return;
    }

    // Draw background
    SDL_FRect bg_rect = {
        .x = box->x, .y = box->y, .w = box->width, .h = box->height};

    SDL_RenderFillRect(renderer, &bg_rect);
    debugUIIncrementDrawCalls();

    // Draw text with automatic word wrapping
    if (box->text && box->text[0] != '\0') {
        float base_x = box->x + box->padding.left;
        float text_y = box->y + box->padding.top;
        float line_height = textRenderGetLineHeight(box->font_size);
        float max_width = box->width - box->padding.left - box->padding.right;
        float max_height = box->height - box->padding.top - box->padding.bottom;

        // Calculated space width
        float space_width;
        textRenderMeasure(" ", box->font_size, &space_width, NULL);

        char line_buffer[1024];
        int line_pos = 0;
        float current_line_width = 0.0f;
        int char_offset = 0; // Track position in original text

        const char *word_start = box->text;
        const char *p = box->text;

        while (*p) {
            // Check for word boundary (space or end of text)
            if (*p == ' ' || *(p + 1) == '\0') {
                // Calculate word length
                // Include word boundary as part of word
                int word_len = (p - word_start + 1);

                if (word_len > 0 && word_len < 64) {
                    char word_buffer[64];
                    strncpy(word_buffer, word_start, word_len);
                    word_buffer[word_len] = '\0';

                    // Measure word width
                    float word_width;
                    textRenderMeasure(word_buffer, box->font_size, &word_width,
                                      NULL);

                    if (line_pos > 0) {
                        // Check if word fits on current line
                        float required_width =
                            current_line_width + word_width + space_width;

                        if (required_width > max_width) {
                            // Draw current line and start new one
                            line_buffer[line_pos] = '\0';
                            renderAlignedLine(renderer, line_buffer, base_x,
                                              text_y, max_width, max_height,
                                              box, box->char_states,
                                              char_offset, line_pos);

                            text_y += line_height;
                            char_offset +=
                                line_pos; // Update offset for next line
                            line_pos = 0;
                            current_line_width = 0.0f;
                        }
                    }

                    // Add word to line
                    if (line_pos + word_len < 1024) {
                        strncpy(line_buffer + line_pos, word_buffer, word_len);
                        line_pos += word_len;
                        current_line_width += word_width;
                    }
                } else {
                    // Word is longer than max buffer size
                    SDL_Log("Word is longer than max buffer size!");
                }

                word_start = p + 1;
            }

            p++;
        }

        // Draw any remaining text
        if (line_pos > 0) {
            line_buffer[line_pos] = '\0';
            renderAlignedLine(renderer, line_buffer, base_x, text_y, max_width,
                              max_height, box, box->char_states, char_offset,
                              line_pos);
        }
    }
}
