#include "ui.h"
#include "text_render.h"
#include "debug_ui.h"
#include <SDL3/SDL.h>
#include <string.h>

UITextBox uiTextBoxCreate(float x, float y, float width, float height) {
    UITextBox box = {
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .padding = {.top = 10.0f, .right = 10.0f, .bottom = 10.0f, .left = 10.0f},
        .bg_color = {30, 30, 30, 255},
        .text_color = {255, 255, 255, 255},
        .border_color = {100, 100, 100, 255},
        .border_width = 2.0f,
        .text = "",
        .font_size = 16.0f,
        .align = UI_ALIGN_START
    };
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
static inline void renderAlignedLine(SDL_Renderer *renderer, const char *line_buffer,
                                     float base_x, float text_y, float max_width,
                                     const UITextBox *box) {
    float line_width;
    textRenderMeasure(line_buffer, box->font_size, &line_width, NULL);
    float text_x = calculateAlignedX(base_x, max_width, line_width, box->align);
    textRenderDraw(renderer, line_buffer, text_x, text_y, box->font_size, box->text_color);
}

void uiTextBoxDraw(SDL_Renderer *renderer, const UITextBox *box) {
    if (!renderer || !box) {
        return;
    }

    // Draw background
    SDL_FRect bg_rect = {
        .x = box->x,
        .y = box->y,
        .w = box->width,
        .h = box->height
    };
    SDL_SetRenderDrawColor(renderer, box->bg_color.r, box->bg_color.g,
                          box->bg_color.b, box->bg_color.a);
    SDL_RenderFillRect(renderer, &bg_rect);
    debugUIIncrementDrawCalls();

    // Draw border using filled rectangles (more efficient than multiple strokes)
    if (box->border_width > 0) {
        SDL_SetRenderDrawColor(renderer, box->border_color.r, box->border_color.g,
                              box->border_color.b, box->border_color.a);

        float bw = box->border_width;
        // Top border
        SDL_FRect top = {box->x, box->y, box->width, bw};
        SDL_RenderFillRect(renderer, &top);
        debugUIIncrementDrawCalls();

        // Bottom border
        SDL_FRect bottom = {box->x, box->y + box->height - bw, box->width, bw};
        SDL_RenderFillRect(renderer, &bottom);
        debugUIIncrementDrawCalls();

        // Left border
        SDL_FRect left = {box->x, box->y, bw, box->height};
        SDL_RenderFillRect(renderer, &left);
        debugUIIncrementDrawCalls();

        // Right border
        SDL_FRect right = {box->x + box->width - bw, box->y, bw, box->height};
        SDL_RenderFillRect(renderer, &right);
        debugUIIncrementDrawCalls();
    }

    // Draw text with automatic word wrapping
    if (box->text && box->text[0] != '\0') {
        float base_x = box->x + box->padding.left;
        float text_y = box->y + box->padding.top;
        float line_height = textRenderGetLineHeight(box->font_size);
        float max_width = box->width - box->padding.left - box->padding.right;

        // Cache space width (calculate once)
        float space_width;
        textRenderMeasure(" ", box->font_size, &space_width, NULL);

        char line_buffer[1024];
        int line_pos = 0;
        float current_line_width = 0.0f;

        const char *word_start = box->text;
        const char *p = box->text;

        while (*p) {
            // Check for manual line break
            if (*p == '\n') {
                // Draw current line if it has content
                if (line_pos > 0) {
                    line_buffer[line_pos] = '\0';
                    renderAlignedLine(renderer, line_buffer, base_x, text_y, max_width, box);
                    line_pos = 0;
                    current_line_width = 0.0f;
                }
                text_y += line_height;
                word_start = p + 1;
                p++;
                continue;
            }

            // Check for word boundary (space or end of text)
            if (*p == ' ' || *(p + 1) == '\0') {
                // Calculate word length (include current char if not space)
                int word_len = (*p == ' ') ? (p - word_start) : (p - word_start + 1);

                if (word_len > 0 && word_len < 512) {
                    char word_buffer[512];
                    strncpy(word_buffer, word_start, word_len);
                    word_buffer[word_len] = '\0';

                    // Measure word width (cached measurement)
                    float word_width;
                    textRenderMeasure(word_buffer, box->font_size, &word_width, NULL);

                    // Check if word fits on current line
                    float required_width = current_line_width + word_width;
                    if (line_pos > 0) {
                        required_width += space_width;  // Add space if continuing line
                    }

                    if (line_pos > 0 && required_width > max_width) {
                        // Draw current line and start new one
                        line_buffer[line_pos] = '\0';
                        renderAlignedLine(renderer, line_buffer, base_x, text_y, max_width, box);
                        text_y += line_height;
                        line_pos = 0;
                        current_line_width = 0.0f;
                    }

                    // Add space if continuing line
                    if (line_pos > 0 && *p == ' ') {
                        line_buffer[line_pos++] = ' ';
                        current_line_width += space_width;
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
            renderAlignedLine(renderer, line_buffer, base_x, text_y, max_width, box);
        }
    }
}

float uiTextBoxMeasureHeight(const UITextBox *box) {
    if (!box || !box->text || box->text[0] == '\0') {
        return box->padding.top + box->padding.bottom;
    }

    float line_height = textRenderGetLineHeight(box->font_size);
    float max_width = box->width - box->padding.left - box->padding.right;
    int line_count = 0;

    // Cache space width
    float space_width;
    textRenderMeasure(" ", box->font_size, &space_width, NULL);

    char line_buffer[1024];
    int line_pos = 0;
    float current_line_width = 0.0f;

    const char *word_start = box->text;
    const char *p = box->text;

    // Simulate word wrapping to count actual lines
    while (*p) {
        if (*p == '\n') {
            line_count++;
            line_pos = 0;
            current_line_width = 0.0f;
            word_start = p + 1;
            p++;
            continue;
        }

        if (*p == ' ' || *(p + 1) == '\0') {
            int word_len = (*p == ' ') ? (p - word_start) : (p - word_start + 1);

            if (word_len > 0 && word_len < 512) {
                char word_buffer[512];
                strncpy(word_buffer, word_start, word_len);
                word_buffer[word_len] = '\0';

                float word_width;
                textRenderMeasure(word_buffer, box->font_size, &word_width, NULL);

                float required_width = current_line_width + word_width;
                if (line_pos > 0) {
                    required_width += space_width;
                }

                // Need to wrap to new line?
                if (line_pos > 0 && required_width > max_width) {
                    line_count++;
                    line_pos = 0;
                    current_line_width = 0.0f;
                }

                if (line_pos > 0 && *p == ' ') {
                    current_line_width += space_width;
                }

                if (line_pos + word_len < 1024) {
                    line_pos += word_len;
                    current_line_width += word_width;
                }
            }

            word_start = p + 1;
        }

        p++;
    }

    // Count the last line if there's content
    if (line_pos > 0) {
        line_count++;
    }

    return box->padding.top + box->padding.bottom + (line_count * line_height);
}
