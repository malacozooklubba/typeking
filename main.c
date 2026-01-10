#include "debug_ui.h"
#include "text_render.h"
#include "ui.h"
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_pixels.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define FONT_SIZE 32.0f
#define MAX_WORD_COUNT 5
#define MAX_WORD_LENGTH 32

typedef enum {
    LOBBY,
    TYPING,
    RESULTS,
} GameState;

const char game_name[] = "Type King";
const SDL_Color background_color = {0x1D, 0x23, 0x2F, 0xFF};
const SDL_Color untyped_text_color = {0x57, 0x65, 0x81, 0xFF};
const SDL_Color typed_text_color = {0xE9, 0xD7, 0xB1, 0xFF};
const SDL_Color error_text_color = {0xD4, 0x31, 0x31, 0xFF};

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static int window_width = 1024;
static int window_height = 720;
static bool debug_info = false;
static GameState game_state = LOBBY;

// Typing game state variables
static char input_buffer[MAX_WORD_LENGTH * MAX_WORD_COUNT];
// static char input_buffer[1024];
static int input_buffer_pos = 0;

void enterLobbyMode(void) {
    game_state = LOBBY;
    SDL_Log("Entering lobby mode");
    SDL_StopTextInput(window);
}

void enterTypingMode() {
    game_state = TYPING;
    SDL_Log("Entering typing mode");
    SDL_StartTextInput(window);
}

void enterResultsMode() {
    game_state = RESULTS;
    SDL_Log("Entering results mode");
    SDL_StopTextInput(window);
}

void renderLobbyGameState() {
    UITextBox hello_message =
        uiTextBoxCreate(0.0f, 0.0f, window_width, window_height);

    hello_message.text = "PRESS ENTER TO START";
    hello_message.font_size = 32.0f;
    hello_message.align = UI_ALIGN_CENTER;
    hello_message.text_color = typed_text_color;

    uiTextBoxDraw(renderer, &hello_message);
}

void renderTypingGameState() {
    const float window_padding = 50.0f;

    UITextBox box1 = uiTextBoxCreate(window_padding, 50.0f,
                                     window_width - window_padding * 2, 150.0f);

    box1.text = input_buffer;
    box1.font_size = 32.0f;
    box1.align = UI_ALIGN_START;
    box1.text_color = untyped_text_color;
    box1.bg_color = background_color;

    uiTextBoxDraw(renderer, &box1);
}

void resultsGameState() {}

void addWord(char *word) {}

void loadWords() {
    FILE *file = fopen("words/oxford_3000.txt", "r");

    if (file == NULL) {
        SDL_Log("Could not open words file");
        return;
    }

    // Seed random number generator
    srand(time(NULL));

    int total_lines = 0;
    char buffer[64];
    int current_line = 0;

    while (fgets(buffer, sizeof(buffer), file)) {
        total_lines++;
    }

    SDL_Log("File line when count: %d", total_lines);

    int random_line_indices[MAX_WORD_COUNT];
    for (int i = 0; i < MAX_WORD_COUNT; i++) {
        random_line_indices[i] = rand() % total_lines;
    }

    // TODO: Sort random_line_indices so this can be done in a single pass
    // Then randomize the order of the lines (words) before going into typing
    for (int i = 0; i < MAX_WORD_COUNT; i++) {
        rewind(file);
        int current_line = 0;

        while (current_line < random_line_indices[i] &&
               fgets(buffer, sizeof(buffer), file)) {
            current_line++;
        }

        // Read the target line
        if (fgets(buffer, sizeof(buffer), file)) {
            // Remove trailing newline if present
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len - 1] == '\n') {
                buffer[len - 1] = '\0';
            }

            // Append to output buffer
            if (i > 0) {
                strcat(input_buffer, " "); // Add space separator
            }
            strcat(input_buffer, buffer);
        }
    }

    printf("%s\n", input_buffer);

    fclose(file);
}

SDL_AppResult SDL_AppInit(__attribute__((unused)) void **appstate,
                          __attribute__((unused)) int argc,
                          __attribute__((unused)) char *argv[]) {

    SDL_SetAppMetadata(game_name, "1.0", "com.palm.treetyper");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Could not initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer(game_name, window_width, window_height,
                                     SDL_WINDOW_RESIZABLE, &window,
                                     &renderer)) {

        SDL_Log("Could not create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!textRenderInit(renderer, "./bin/font/JetBrainsMono-Regular.ttf",
                        FONT_SIZE)) {
        SDL_Log("Failed to initialize text rendering");
        return SDL_APP_FAILURE;
    }

    loadWords();
    debugUIInit();
    enterLobbyMode();

    SDL_Log("Press F3 to toggle debug UI");

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(__attribute__((unused)) void *appstate,
                           SDL_Event *event) {

    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        SDL_GetWindowSizeInPixels(window, &window_width, &window_height);
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_F3) {
            debug_info = !debug_info;
            SDL_Log("Debug UI %s", debug_info ? "enabled" : "disabled");
        }
    }

    switch (game_state) {
    case LOBBY:
        if (event->type == SDL_EVENT_KEY_DOWN) {
            if (event->key.key == SDLK_RETURN) {
                enterTypingMode();
            }
        }
        break;
    case TYPING:
        if (event->type == SDL_EVENT_KEY_DOWN) {
            if (event->key.key == SDLK_BACKSPACE) {
                // Remove last character from input buffer
                if (input_buffer_pos > 0) {
                    input_buffer_pos--;
                    input_buffer[input_buffer_pos] = '\0';
                }
            }
        }

        if (event->type == SDL_EVENT_TEXT_INPUT) {
            // Add text in event to input buffer
            if (input_buffer_pos < 1024) {
                input_buffer[input_buffer_pos++] = event->text.text[0];
                // SDL_Log("Input buffer: %s", input_buffer);
            }
        }
        break;
    case RESULTS:
        game_state = LOBBY;
        break;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    // Reset draw calls at the start of each frame
    debugUIResetDrawCalls();

    /* ==== Render Loop ==== */
    SDL_SetRenderDrawColor(renderer, background_color.r, background_color.g,
                           background_color.b, background_color.a);
    SDL_RenderClear(renderer);

    // Run game state machine
    switch (game_state) {
    case LOBBY:
        renderLobbyGameState();
        break;
    case TYPING:
        renderTypingGameState();
        break;
    case RESULTS:
        break;
    }

    // Draw debug UI (if enabled)
    if (debug_info) {
        debugUIDraw(renderer, window_width, window_height);
    }

    SDL_RenderPresent(renderer);
    /* ===================== */

    // Update frame timing at the end of frame
    if (debug_info) {
        debugUIUpdateFrame();
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(__attribute__((unused)) void *appstate,
                 __attribute__((unused)) SDL_AppResult result) {
    debugUIQuit();
    textRenderQuit();
}
