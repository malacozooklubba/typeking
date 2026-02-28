#include "ui.h"
#include "text_layout_calculator.h"
#include "text_render.h"
#include "theme.h"
#include <stdio.h>
#include <string.h>

UITextBox uiTextBoxCreate(float x, float y, float width, float height,
                          float font_size) {
    UITextBox box = {.x = x,
                     .y = y,
                     .width = width,
                     .height = height,
                     .padding = {.top = 50.0f,
                                 .right = 50.0f,
                                 .bottom = 50.0f,
                                 .left = 50.0f},
                     .bg_color = THEME_BACKGROUND,
                     .text_color = THEME_TEXT_TYPED,
                     .text = "",
                     .char_states = NULL,
                     .font_size = font_size,
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
static inline void renderAlignedLine(Framebuffer *fb,
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
        // Validate char_offset is reasonable (prevent out-of-bounds access)
        if (char_offset >= 0 && char_offset <= 3200) {
            typedTextRenderDraw(fb, line_buffer, text_x, text_y,
                                box->font_size, char_states + char_offset);
        } else {
            // Render without char states if offset is invalid
            textRenderDraw(fb, line_buffer, text_x, text_y,
                           getGlyphCache(), getFontMetricsCache(),
                           box->text_color);
        }
    } else {
        textRenderDraw(fb, line_buffer, text_x, text_y, getGlyphCache(),
                       getFontMetricsCache(), box->text_color);
    }

    // Draw caret if it's on this line
    if (box->caret_position >= char_offset &&
        box->caret_position < char_offset + line_length) {
        float caret_x = text_x + box->caret_visual_x_offset;

        float ascent, descent;
        textRenderGetMetrics(box->font_size, &ascent, &descent, NULL);
        float caret_y = text_y;
        float caret_height = ascent - descent;

        // Draw caret as a vertical line
        Color caret_color = THEME_TEXT_TYPED;
        fb_fill_rect(fb, (int)(caret_x + 0.5f), (int)(caret_y + 0.5f),
                     2, (int)(caret_height + 0.5f), caret_color);
    }
}

void drawPrecalculatedTextLayout(Framebuffer *fb, const UITextBox *box) {

    if (!fb || !box) {
        return;
    }

    // Draw background
    fb_fill_rect(fb, (int)box->x, (int)box->y,
                 (int)box->width, (int)box->height, box->bg_color);

    if (!box->text || box->text[0] == '\0') {
        return;
    }

    float base_x = box->x + box->padding.left;
    float text_y = box->y + box->padding.top;
    float line_height = textRenderGetLineHeight(box->font_size);
    float max_width = box->width - box->padding.left - box->padding.right;
    float max_height = box->height - box->padding.top - box->padding.bottom;

    PrecalculatedTextLayout text_layout = getCalculatedTextLayout();

    if (text_layout.line_count <= 0) {
        uiTextBoxDraw(fb, box);
        return;
    }

    int text_len = strlen(box->text);

    for (int i = 0; i < text_layout.line_count; i++) {
        int line_start = text_layout.line_starts[i];

        // Validate line_start is within text bounds
        if (line_start < 0 || line_start > text_len) {
            fprintf(stderr, "Invalid line_start %d at line %d (text_len=%d)\n",
                    line_start, i, text_len);
            continue;
        }
        int line_end;

        // For the last line, go to end of text; otherwise use next line's start
        if (i == text_layout.line_count - 1) {
            line_end = strlen(box->text);
        } else {
            line_end = text_layout.line_starts[i + 1];
        }

        int line_length = line_end - line_start;

        // Bounds checking to prevent VLA overflow/underflow
        if (line_length < 0 || line_length > 2048) {
            continue;
        }

        char line_buffer[line_length + 1];

        // Copy line text out of original text buffer
        strncpy(line_buffer, box->text + line_start, line_length);
        line_buffer[line_length] = '\0'; // Manually null-terminate

        // End current line
        renderAlignedLine(fb, line_buffer, base_x, text_y, max_width,
                          max_height, box, box->char_states, line_start,
                          line_length);

        text_y += line_height;
    }
}

void uiTextBoxDraw(Framebuffer *fb, const UITextBox *box) {
    if (!fb || !box) {
        return;
    }

    // Draw background
    fb_fill_rect(fb, (int)box->x, (int)box->y,
                 (int)box->width, (int)box->height, box->bg_color);

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
                            renderAlignedLine(fb, line_buffer, base_x,
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
                    fprintf(stderr, "Word is longer than max buffer size!\n");
                }

                word_start = p + 1;
            }

            p++;
        }

        // Draw any remaining text
        if (line_pos > 0) {
            line_buffer[line_pos] = '\0';
            renderAlignedLine(fb, line_buffer, base_x, text_y, max_width,
                              max_height, box, box->char_states, char_offset,
                              line_pos);
        }
    }
}
