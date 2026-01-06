#include "ui.h"
#include "debug_ui.h"
#include "text_render.h"
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
                     .bg_color = {30, 30, 30, 255},
                     .text_color = {255, 255, 255, 255},
                     .border_color = {100, 100, 100, 255},
                     .border_width = 2.0f,
                     .text = "",
                     .font_size = 16.0f,
                     .align = UI_ALIGN_START};
    return box;
}

// Helper function to calculate aligned X position
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

// Helper function to render a line of text with alignment
static inline void renderAlignedLine(SDL_Renderer *renderer,
                                     const char *line_buffer, float base_x,
                                     float text_y, float max_width,
                                     const UITextBox *box) {
    float line_width =
        textRenderMeasureVisibleWidth(line_buffer, box->font_size);
    float text_x = calculateAlignedX(base_x, max_width, line_width, box->align);
    textRenderDraw(renderer, line_buffer, text_x, text_y, box->font_size,
                   box->text_color);
}

void uiTextBoxDraw(SDL_Renderer *renderer, const UITextBox *box) {
    if (!renderer || !box) {
        return;
    }

    if (box->border_width > 0) {
        SDL_SetRenderDrawColor(renderer, box->border_color.r,
                               box->border_color.g, box->border_color.b,
                               box->border_color.a);

        // Top border
        SDL_FRect border_rect = {box->x, box->y, box->width, box->height};
        SDL_RenderFillRect(renderer, &border_rect);
        debugUIIncrementDrawCalls();
    }

    // Draw background
    SDL_FRect bg_rect = {.x = box->x + box->border_width,
                         .y = box->y + box->border_width,
                         .w = box->width - box->border_width * 2,
                         .h = box->height - box->border_width * 2};

    SDL_SetRenderDrawColor(renderer, box->bg_color.r, box->bg_color.g,
                           box->bg_color.b, box->bg_color.a);

    SDL_RenderFillRect(renderer, &bg_rect);
    debugUIIncrementDrawCalls();

    // Draw text with automatic word wrapping
    if (box->text && box->text[0] != '\0') {
        float base_x = box->x + box->padding.left;
        float text_y = box->y + box->padding.top;
        float line_height = textRenderGetLineHeight(box->font_size);
        float max_width = box->width - box->padding.left - box->padding.right;

        // Calculated space width
        float space_width;
        textRenderMeasure(" ", box->font_size, &space_width, NULL);

        char line_buffer[1024];
        int line_pos = 0;
        float current_line_width = 0.0f;

        const char *word_start = box->text;
        const char *p = box->text;

        while (*p) {
            // Check for word boundary (space or end of text)
            if (*p == ' ' || *(p + 1) == '\0') {
                // Calculate word length (include current char if not space)
                int word_len =
                    (*p == ' ') ? (p - word_start) : (p - word_start + 1);

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
                                              text_y, max_width, box);
                            text_y += line_height;
                            line_pos = 0;
                            current_line_width = 0.0f;
                        } else {
                            // Add space if continuing line
                            line_buffer[line_pos++] = ' ';
                            current_line_width += space_width;
                        }
                    }

                    // Add word to line
                    if (line_pos + word_len < 1024) {
                        strncpy(line_buffer + line_pos, word_buffer, word_len);
                        line_pos += word_len;
                        current_line_width += word_width;
                    }
                }

                word_start = p + 1;
            }

            p++;
        }

        // Draw any remaining text
        if (line_pos > 0) {
            line_buffer[line_pos] = '\0';
            renderAlignedLine(renderer, line_buffer, base_x, text_y, max_width,
                              box);
        }
    }
}
