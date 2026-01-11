#pragma once

#include <SDL3/SDL_render.h>
#include <stdbool.h>

typedef enum {
    UI_ALIGN_START,  // Left alignment
    UI_ALIGN_CENTER, // Center alignment
    UI_ALIGN_END     // Right alignment
} UITextAlign;

typedef struct {
    float top;
    float right;
    float bottom;
    float left;
} UIPadding;

typedef struct {
    float x;
    float y;
    float width;
    float height;
    UIPadding padding;
    SDL_Color bg_color;
    SDL_Color text_color;
    const char *text;
    const unsigned char *char_states;
    float font_size;
    UITextAlign align;
    int caret_position; // Character position for caret (-1 to disable)
} UITextBox;

// Initialize a text box with default values
UITextBox uiTextBoxCreate(float x, float y, float width, float height);

// Draw a text box with background, border, padding, and multi-line text
void uiTextBoxDraw(SDL_Renderer *renderer, const UITextBox *box);
