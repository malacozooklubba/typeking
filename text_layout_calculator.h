#pragma once

#include <stdbool.h>
#define MAX_LINES 50

typedef struct {
    int start_char_index; // Character index where line starts (inclusive)
    int end_char_index;   // Character index where line ends (exclusive)
    int char_count;       // Number of characters on this line
} LineBreak;

typedef struct {
    LineBreak lines[MAX_LINES];
    int line_count;
    float max_width; // Width constraint used for calculation
    float font_size; // Font size used for calculation
    bool is_valid;   // Whether this layout is valid
} TextLayout;

typedef struct {
    int line_starts[MAX_LINES];
    int line_count;
} PrecalculatedTextLayout;

int calculateTextLayout(TextLayout *layout, const char *text, float max_width,
                        float font_size);

void calculateTextLayoutLineBreaks(char *target_text, int font_size,
                                   int layout_width);

PrecalculatedTextLayout getCalculatedTextLayout();
