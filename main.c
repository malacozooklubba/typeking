#include "debug_ui.h"
#include "font_cache.h"
#include "text_layout_calculator.h"
#include "text_render.h"
#include "theme.h"
#include "ui.h"
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_pixels.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define FONT_SIZE 42.0f
#define MAX_WORD_COUNT 100
#define MAX_WORD_LENGTH 32

// Word count modes
#define MODE_SHORT 10
#define MODE_MEDIUM 30
#define MODE_LONG 80

typedef enum {
    LOBBY,
    TYPING,
    RESULTS,
} GameState;

typedef enum {
    TRANSITION_NONE,    // No transition active
    TRANSITION_FADE_IN, // Fading to black (hiding old state)
    TRANSITION_FADE_OUT // Fading from black (revealing new state)
} TransitionState;

typedef struct {
    TransitionState state;
    Uint64 start_time;
    float duration_ms;
    GameState target_state;
} StateTransition;

typedef enum {
    CHAR_STATE_UNTYPED = 0,
    CHAR_STATE_CORRECT = 1,
    CHAR_STATE_ERROR = 2
} CharState;

typedef struct {
    Uint64 start_time;
    Uint64 end_time;
    Uint64 performance_frequency;
    bool timer_started;
    int correct_words;
    int total_words;
    int correct_chars;
    int error_chars;       // Count of errors made
    int total_chars;       // Total chars in target_text
    int total_chars_typed; // Total chars user actually typed
    double elapsed_seconds;
    double wpm;
    double cps;
    double accuracy;
} TypingStats;

const char game_name[] = "Type King";

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static int window_width = 1024;
static int window_height = 720;
static bool debug_info = false;
static GameState game_state = LOBBY;

// Typing game state variables
static char target_text[MAX_WORD_LENGTH * MAX_WORD_COUNT];
static char user_input[MAX_WORD_LENGTH * MAX_WORD_COUNT];
static int user_input_pos = 0;
static TypingStats typing_stats = {0};
static int word_count = MODE_SHORT;
static bool position_had_error[MAX_WORD_LENGTH * MAX_WORD_COUNT] = {false};

// Precalculated text layouts
static TextLayout target_text_layout = {0};
static TextLayout user_input_layout = {0};

// Animation timing
static Uint64 last_frame_time = 0;

// State transition animation
static StateTransition state_transition = {
    .state = TRANSITION_NONE,
    .duration_ms = 100.0f // Each phase is 100ms (200ms total)
};

// Caret lerp animation
static float caret_visual_x = 0.0f;
static float caret_target_x = 0.0f;
static int caret_current_line = 0; // Track which line caret is on

// Forward declarations
static void calculateTypingStats(void);
static void loadWords(int count);
// static bool calculateTextLayout(TextLayout *layout, const char *text,
//                                 float max_width, float font_size);

static void beginTransition(GameState target) {
    state_transition.state = TRANSITION_FADE_IN;
    state_transition.start_time = SDL_GetPerformanceCounter();
    state_transition.target_state = target;
}

static void performStateChange(GameState new_state) {
    GameState old_state = game_state;
    game_state = new_state;

    SDL_Log("State change: %d -> %d", old_state, new_state);

    switch (new_state) {
    case LOBBY:
        SDL_StopTextInput(window);
        break;

    case TYPING:
        // Load new words for this round
        loadWords(word_count);

        user_input[0] = '\0';
        user_input_pos = 0;

        // Reset typing stats
        memset(&typing_stats, 0, sizeof(TypingStats));
        typing_stats.performance_frequency = SDL_GetPerformanceFrequency();
        typing_stats.timer_started = false;

        // Reset position error tracking
        memset(position_had_error, false, sizeof(position_had_error));

        // Count words and characters in target_text
        typing_stats.total_words = 0;
        typing_stats.total_chars = 0;
        for (const char *p = target_text; *p; p++) {
            if (*p == ' ')
                typing_stats.total_words++;
            typing_stats.total_chars++;
        }
        if (typing_stats.total_chars > 0)
            typing_stats.total_words++;

        // Reset caret animation
        caret_visual_x = 0.0f;
        caret_target_x = 0.0f;
        caret_current_line = 0;

        // Calculate text layouts
        const float window_padding = 50.0f;
        float text_max_width = window_width - window_padding * 2;
        calculateTextLayout(&target_text_layout, target_text, text_max_width,
                            FONT_SIZE);
        calculateTextLayout(&user_input_layout, user_input, text_max_width,
                            FONT_SIZE);

        SDL_StartTextInput(window);
        break;

    case RESULTS:
        // Capture end time and calculate all statistics
        typing_stats.end_time = SDL_GetPerformanceCounter();
        calculateTypingStats();
        SDL_StopTextInput(window);
        break;
    }
}

static void updateTransitionState(Uint64 current_time) {
    if (state_transition.state == TRANSITION_NONE)
        return;

    float elapsed_ms = (current_time - state_transition.start_time) * 1000.0f /
                       (float)SDL_GetPerformanceFrequency();

    if (state_transition.state == TRANSITION_FADE_IN) {
        if (elapsed_ms >= state_transition.duration_ms) {
            // Midpoint: switch to actual state
            performStateChange(state_transition.target_state);

            // Start fade-out
            state_transition.state = TRANSITION_FADE_OUT;
            state_transition.start_time = current_time;
        }
    } else if (state_transition.state == TRANSITION_FADE_OUT) {
        if (elapsed_ms >= state_transition.duration_ms) {
            // Transition complete
            state_transition.state = TRANSITION_NONE;
        }
    }
}

static void renderTransitionOverlay(SDL_Renderer *renderer,
                                    Uint64 current_time) {
    if (state_transition.state == TRANSITION_NONE)
        return;

    float elapsed_ms = (current_time - state_transition.start_time) * 1000.0f /
                       (float)SDL_GetPerformanceFrequency();

    float alpha_ratio = elapsed_ms / state_transition.duration_ms;
    if (alpha_ratio > 1.0f)
        alpha_ratio = 1.0f;

    int alpha;
    if (state_transition.state == TRANSITION_FADE_IN) {
        // Fade to black: alpha increases
        alpha = (int)(alpha_ratio * 255.0f);
    } else {
        // Fade from black: alpha decreases
        alpha = (int)((1.0f - alpha_ratio) * 255.0f);
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, THEME_BACKGROUND.r, THEME_BACKGROUND.g,
                           THEME_BACKGROUND.b, alpha);

    SDL_FRect overlay = {0, 0, window_width, window_height};
    SDL_RenderFillRect(renderer, &overlay);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

// Calculate line breaks for text with word wrapping
// static bool calculateTextLayout(TextLayout *layout, const char *text,
//                                 float max_width, float font_size) {
//     // Initialize layout
//     layout->line_count = 0;
//     layout->max_width = max_width;
//     layout->font_size = font_size;
//     layout->is_valid = true;
//
//     // Handle empty text
//     if (!text || text[0] == '\0') {
//         return true;
//     }
//
//     // Measure space width once
//     float space_width;
//     textRenderMeasure(" ", font_size, &space_width, NULL);
//
//     int line_start = 0;
//     int line_length = 0;
//     float current_line_width = 0.0f;
//
//     const char *word_start = text;
//     const char *p = text;
//
//     while (*p) {
//         // Word boundary detection (EXACT same as ui.c line 157)
//         if (*p == ' ' || *(p + 1) == '\0') {
//             int word_len = (p - word_start + 1);
//
//             if (word_len > 0 && word_len < 64) {
//                 char word_buffer[64];
//                 strncpy(word_buffer, word_start, word_len);
//                 word_buffer[word_len] = '\0';
//
//                 float word_width;
//                 textRenderMeasure(word_buffer, font_size, &word_width, NULL);
//
//                 if (line_length > 0) {
//                     // ALWAYS add space_width (matches ui.c line 175)
//                     float required_width =
//                         current_line_width + word_width + space_width;
//
//                     if (required_width > max_width) {
//                         // Save current line before wrapping
//                         if (layout->line_count >= MAX_LINES) {
//                             layout->is_valid = false;
//                             return false;
//                         }
//
//                         layout->lines[layout->line_count].start_char_index =
//                             line_start;
//                         layout->lines[layout->line_count].end_char_index =
//                             line_start + line_length;
//                         layout->lines[layout->line_count].char_count =
//                             line_length;
//                         layout->line_count++;
//
//                         // Start new line
//                         line_start = line_start + line_length;
//                         line_length = 0;
//                         current_line_width = 0.0f;
//                     }
//                 }
//
//                 line_length += word_len;
//                 current_line_width += word_width;
//             }
//
//             word_start = p + 1;
//         }
//         p++;
//     }
//
//     // Save final line
//     if (line_length > 0 && layout->line_count < MAX_LINES) {
//         layout->lines[layout->line_count].start_char_index = line_start;
//         layout->lines[layout->line_count].end_char_index =
//             line_start + line_length;
//         layout->lines[layout->line_count].char_count = line_length;
//         layout->line_count++;
//     }
//
//     return true;
// }

// Find which line contains the caret using precalculated layout
static int findCaretLine(int caret_pos, const TextLayout *layout,
                         int *line_start_offset) {
    if (!layout || !layout->is_valid || layout->line_count == 0) {
        *line_start_offset = 0;
        return 0;
    }

    // Find which line contains the caret position
    for (int i = 0; i < layout->line_count; i++) {
        const LineBreak *line = &layout->lines[i];

        // Caret is on this line if position is within range
        if (caret_pos >= line->start_char_index &&
            caret_pos < line->end_char_index) {
            *line_start_offset = line->start_char_index;
            return i;
        }
    }

    // Caret is at the end (after last line)
    if (layout->line_count > 0) {
        int last_line = layout->line_count - 1;
        *line_start_offset = layout->lines[last_line].start_char_index;
        return last_line;
    }

    *line_start_offset = 0;
    return 0;
}

static void updateCaretLerp(float delta_time) {
    if (game_state != TYPING) {
        caret_visual_x = 0.0f;
        caret_target_x = 0.0f;
        caret_current_line = 0;
        return;
    }

    // Calculate target X position from current input
    if (user_input_pos == 0) {
        caret_target_x = 0.0f;
        caret_visual_x = 0.0f;
        caret_current_line = 0;
        return;
    }

    // Find which line the caret is on using precalculated layout
    int line_start_offset = 0;
    int new_line =
        findCaretLine(user_input_pos, &user_input_layout, &line_start_offset);

    // Detect line change - snap immediately (no animation)
    if (new_line != caret_current_line) {
        caret_current_line = new_line;

        // Measure text from line start to caret position
        int chars_on_line = user_input_pos - line_start_offset;
        char temp_text[MAX_WORD_LENGTH * MAX_WORD_COUNT];
        strncpy(temp_text, user_input + line_start_offset, chars_on_line);
        temp_text[chars_on_line] = '\0';

        float measured_width;
        textRenderMeasure(temp_text, FONT_SIZE, &measured_width, NULL);

        // Snap instantly (no lerp)
        caret_target_x = measured_width;
        caret_visual_x = measured_width;
        return;
    }

    // Same line - measure text from line start to caret position
    int chars_on_line = user_input_pos - line_start_offset;
    char temp_text[MAX_WORD_LENGTH * MAX_WORD_COUNT];
    strncpy(temp_text, user_input + line_start_offset, chars_on_line);
    temp_text[chars_on_line] = '\0';

    float measured_width;
    textRenderMeasure(temp_text, FONT_SIZE, &measured_width, NULL);
    caret_target_x = measured_width;

    // Exponential lerp toward target (smooth animation within line)
    const float lerp_factor = 30.0f;
    caret_visual_x +=
        (caret_target_x - caret_visual_x) * lerp_factor * delta_time;

    // Snap when very close (within 0.5 pixels)
    if (fabsf(caret_target_x - caret_visual_x) < 0.5f) {
        caret_visual_x = caret_target_x;
    }
}

void enterLobbyMode(void) { beginTransition(LOBBY); }

void enterTypingMode() {
    // Calculate text box layout
    int *user_input_line_starts = malloc(sizeof(int) * MAX_LINES);
    int lineCount;

    lineCount = calculateTextLines(target_text, FONT_SIZE, window_width,
                                   user_input_line_starts);

    for (int i = 0; i < lineCount; i++) {
        SDL_Log("Line %d: %d", i, user_input_line_starts[i]);
    }

    beginTransition(TYPING);
}

void enterResultsMode() { beginTransition(RESULTS); }

void renderLobbyGameState() {
    const float y_start = 250.0f;
    const float line_height = 50.0f;
    float current_y = y_start;

    static char mode_text[64];
    const char *mode_name;
    if (word_count == MODE_SHORT) {
        mode_name = "SHORT";
    } else if (word_count == MODE_MEDIUM) {
        mode_name = "MEDIUM";
    } else {
        mode_name = "LONG";
    }
    snprintf(mode_text, sizeof(mode_text), "%s (%d WORDS)", mode_name,
             word_count);

    // Mode display
    UITextBox mode_box = uiTextBoxCreate(0.0f, current_y, window_width, 50.0f);
    mode_box.text = mode_text;
    mode_box.align = UI_ALIGN_CENTER;
    mode_box.text_color = THEME_TEXT_TYPED;
    uiTextBoxDraw(renderer, &mode_box);
    current_y += line_height * 1.5f;

    // Instructions
    UITextBox instructions_box =
        uiTextBoxCreate(0.0f, current_y, window_width, 50.0f);
    instructions_box.text = "PRESS 1, 2, OR 3 TO CHANGE MODE";
    instructions_box.align = UI_ALIGN_CENTER;
    instructions_box.text_color = THEME_TEXT_UNTYPED;
    uiTextBoxDraw(renderer, &instructions_box);
    current_y += line_height * 1.5f;

    // Start message
    UITextBox start_box = uiTextBoxCreate(0.0f, current_y, window_width, 50.0f);
    start_box.text = "PRESS ENTER TO START";
    start_box.align = UI_ALIGN_CENTER;
    start_box.text_color = THEME_TEXT_TYPED;
    uiTextBoxDraw(renderer, &start_box);
}

static void compareInputToTarget(const char *target, const char *input,
                                 unsigned char *states, int max_len) {
    int input_len = strlen(input);
    int target_len = strlen(target);

    for (int i = 0; i < target_len && i < max_len; i++) {
        if (i >= input_len) {
            states[i] = CHAR_STATE_UNTYPED;
        } else if (input[i] == target[i]) {
            states[i] = CHAR_STATE_CORRECT;
        } else {
            states[i] = CHAR_STATE_ERROR;
        }
    }
}

static void calculateTypingStats() {
    // Calculate elapsed time
    if (typing_stats.timer_started) {
        typing_stats.elapsed_seconds =
            (typing_stats.end_time - typing_stats.start_time) /
            (double)typing_stats.performance_frequency;
    } else {
        typing_stats.elapsed_seconds = 0.0;
    }

    // Prevent extreme values from very fast typing
    if (typing_stats.elapsed_seconds < 0.01) {
        typing_stats.elapsed_seconds = 0.01;
    }

    // Calculate correct words using strict character-by-character matching
    typing_stats.correct_words = 0;
    char target_word[MAX_WORD_LENGTH];
    char input_word[MAX_WORD_LENGTH];
    const char *target_p = target_text;
    const char *input_p = user_input;

    while (true) {
        // Extract target word
        int target_len = 0;
        while (*target_p && *target_p != ' ' &&
               target_len < MAX_WORD_LENGTH - 1) {
            target_word[target_len++] = *target_p++;
        }
        target_word[target_len] = '\0';
        if (*target_p == ' ')
            target_p++;

        // Extract input word
        int input_len = 0;
        while (*input_p && *input_p != ' ' && input_len < MAX_WORD_LENGTH - 1) {
            input_word[input_len++] = *input_p++;
        }
        input_word[input_len] = '\0';
        if (*input_p == ' ')
            input_p++;

        // Compare words strictly
        if (target_len > 0 && strcmp(target_word, input_word) == 0) {
            typing_stats.correct_words++;
        }

        // Break if no more words
        if (target_len == 0 && input_len == 0)
            break;
    }

    // Count correct characters using existing comparison logic
    unsigned char char_states[MAX_WORD_LENGTH * MAX_WORD_COUNT];
    compareInputToTarget(target_text, user_input, char_states,
                         sizeof(char_states));

    typing_stats.correct_chars = 0;
    // NOTE: error_chars is already tracked in real-time, don't recalculate
    int target_len = strlen(target_text);
    for (int i = 0; i < target_len; i++) {
        if (char_states[i] == CHAR_STATE_CORRECT) {
            typing_stats.correct_chars++;
        }
    }

    // Calculate accuracy as: 100 - (errors / total_target_chars * 100)
    typing_stats.total_chars_typed = strlen(user_input);
    if (target_len > 0) {
        double error_percentage =
            (typing_stats.error_chars / (double)target_len) * 100.0;
        typing_stats.accuracy = 100.0 - error_percentage;
    } else {
        typing_stats.accuracy = 100.0;
    }

    // Calculate WPM and CPS
    if (typing_stats.elapsed_seconds > 0) {
        typing_stats.wpm =
            typing_stats.correct_words / (typing_stats.elapsed_seconds / 60.0);
        typing_stats.cps =
            typing_stats.correct_chars / typing_stats.elapsed_seconds;
    } else {
        typing_stats.wpm = 0.0;
        typing_stats.cps = 0.0;
    }

    SDL_Log("Stats - Time: %.2fs, WPM: %.1f, CPS: %.1f, Accuracy: %.1f%%, "
            "Words: %d/%d",
            typing_stats.elapsed_seconds, typing_stats.wpm, typing_stats.cps,
            typing_stats.accuracy, typing_stats.correct_words,
            typing_stats.total_words);
}

void renderTypingGameState() {
    const float window_padding = 50.0f;

    // Create state array and compare input to target
    unsigned char char_states[MAX_WORD_LENGTH * MAX_WORD_COUNT];
    compareInputToTarget(target_text, user_input, char_states,
                         sizeof(char_states));

    // Build display text: user input + remaining target text
    static char display_text[MAX_WORD_LENGTH * MAX_WORD_COUNT];
    int input_len = strlen(user_input);
    int target_len = strlen(target_text);

    // Copy user input
    strncpy(display_text, user_input, input_len);

    // Append remaining target text
    if (input_len < target_len) {
        strcpy(display_text + input_len, target_text + input_len);
    } else {
        display_text[input_len] = '\0';
    }

    UITextBox box1 = uiTextBoxCreate(window_padding, 50.0f,
                                     window_width - window_padding * 2, 150.0f);

    box1.text = display_text;
    box1.char_states = char_states;
    box1.align = UI_ALIGN_START;
    box1.text_color = THEME_TEXT_UNTYPED;
    box1.bg_color = THEME_BACKGROUND;
    box1.caret_position = user_input_pos;
    box1.caret_visual_x_offset = caret_visual_x;

    uiTextBoxDraw(renderer, &box1);
}

void renderResultsGameState() {
    const float y_start = 180.0f;
    const float line_height = 60.0f;
    float current_y = y_start;

    // Use static buffers so text persists across draw calls
    static char wpm_text[64];
    static char cps_text[64];
    static char accuracy_text[64];

    // Format statistics
    snprintf(wpm_text, sizeof(wpm_text), "WPM: %.1f", typing_stats.wpm);
    snprintf(cps_text, sizeof(cps_text), "CPS: %.1f", typing_stats.cps);
    snprintf(accuracy_text, sizeof(accuracy_text), "ACCURACY: %.1f%%",
             typing_stats.accuracy);

    UITextBox wpm_box = uiTextBoxCreate(0.0f, current_y, window_width, 50.0f);
    wpm_box.text = wpm_text;
    wpm_box.align = UI_ALIGN_CENTER;
    wpm_box.text_color = THEME_TEXT_TYPED;
    uiTextBoxDraw(renderer, &wpm_box);
    current_y += line_height;

    UITextBox cps_box = uiTextBoxCreate(0.0f, current_y, window_width, 50.0f);
    cps_box.text = cps_text;
    cps_box.align = UI_ALIGN_CENTER;
    cps_box.text_color = THEME_TEXT_TYPED;
    uiTextBoxDraw(renderer, &cps_box);
    current_y += line_height;

    UITextBox accuracy_box =
        uiTextBoxCreate(0.0f, current_y, window_width, 50.0f);
    accuracy_box.text = accuracy_text;
    accuracy_box.align = UI_ALIGN_CENTER;
    accuracy_box.text_color = THEME_TEXT_TYPED;
    uiTextBoxDraw(renderer, &accuracy_box);
    current_y += line_height * 1.5f;

    // Continue instruction
    UITextBox continue_box =
        uiTextBoxCreate(0.0f, current_y, window_width, 50.0f);
    continue_box.text = "Enter to restart";
    continue_box.align = UI_ALIGN_CENTER;
    continue_box.text_color = THEME_TEXT_UNTYPED;
    uiTextBoxDraw(renderer, &continue_box);
    current_y += line_height;

    // Continue instruction
    UITextBox exit_box = uiTextBoxCreate(0.0f, current_y, window_width, 50.0f);
    exit_box.text = "Escape to return to lobby";
    exit_box.align = UI_ALIGN_CENTER;
    exit_box.text_color = THEME_TEXT_UNTYPED;
    uiTextBoxDraw(renderer, &exit_box);
}

void addWord(char *word) {}

static void loadWords(int count) {
    // Clamp count to valid range
    if (count < 1)
        count = 1;
    if (count > MAX_WORD_COUNT)
        count = MAX_WORD_COUNT;

    target_text[0] = '\0'; // Initialize empty string

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

    SDL_Log("Loading %d words from %d total lines", count, total_lines);

    int random_line_indices[MAX_WORD_COUNT];
    for (int i = 0; i < count; i++) {
        random_line_indices[i] = rand() % total_lines;
    }

    // TODO: Sort random_line_indices so this can be done in a single pass
    // Then randomize the order of the lines (words) before going into typing
    for (int i = 0; i < count; i++) {
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
                strcat(target_text, " "); // Add space separator
            }
            strcat(target_text, buffer);
        }
    }

    printf("%s\n", target_text);

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

    if (!fontCacheInit(renderer, "./bin/font/JetBrainsMono-Regular.ttf",
                       FONT_SIZE)) {
        SDL_Log("Failed to initialize text rendering");
        return SDL_APP_FAILURE;
    }

    loadWords(word_count);
    debugUIInit();

    // Initialize typing stats performance frequency
    typing_stats.performance_frequency = SDL_GetPerformanceFrequency();

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

        // Recalculate layouts if in typing mode
        if (game_state == TYPING) {
            const float window_padding = 50.0f;
            float text_max_width = window_width - window_padding * 2;
            calculateTextLayout(&target_text_layout, target_text,
                                text_max_width, FONT_SIZE);
            calculateTextLayout(&user_input_layout, user_input, text_max_width,
                                FONT_SIZE);
        }
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
            } else if (event->key.key == SDLK_1) {
                word_count = MODE_SHORT;
                SDL_Log("Mode set to SHORT (%d words)", word_count);
            } else if (event->key.key == SDLK_2) {
                word_count = MODE_MEDIUM;
                SDL_Log("Mode set to MEDIUM (%d words)", word_count);
            } else if (event->key.key == SDLK_3) {
                word_count = MODE_LONG;
                SDL_Log("Mode set to LONG (%d words)", word_count);
            }
        }
        break;
    case TYPING:
        if (event->type == SDL_EVENT_KEY_DOWN) {
            if (event->key.key == SDLK_BACKSPACE) {
                // Remove last character from input buffer
                if (user_input_pos > 0) {
                    user_input_pos--;
                    user_input[user_input_pos] = '\0';

                    // Recalculate layout
                    const float window_padding = 50.0f;
                    float text_max_width = window_width - window_padding * 2;
                    calculateTextLayout(&user_input_layout, user_input,
                                        text_max_width, FONT_SIZE);
                }
            }
        }

        if (event->type == SDL_EVENT_TEXT_INPUT) {
            // Start timer on first character typed
            if (!typing_stats.timer_started) {
                typing_stats.start_time = SDL_GetPerformanceCounter();
                typing_stats.timer_started = true;
                SDL_Log("Timer started");
            }

            // Add text in event to input buffer
            if (user_input_pos < 1024) {
                char typed_char = event->text.text[0];
                int current_pos = user_input_pos;

                // Add character to buffer
                user_input[user_input_pos++] = typed_char;
                user_input[user_input_pos] = '\0';

                // Recalculate layout
                const float window_padding = 50.0f;
                float text_max_width = window_width - window_padding * 2;
                calculateTextLayout(&user_input_layout, user_input,
                                    text_max_width, FONT_SIZE);

                // Check if this is an error and hasn't been counted yet
                if (current_pos < strlen(target_text)) {
                    if (typed_char != target_text[current_pos] &&
                        !position_had_error[current_pos]) {
                        typing_stats.error_chars++;
                        position_had_error[current_pos] = true;
                        SDL_Log("Error at position %d (total errors: %d)",
                                current_pos, typing_stats.error_chars);
                    }
                }

                // Check if user has typed all the text
                if (user_input_pos >= strlen(target_text)) {
                    enterResultsMode();
                }
            }
        }
        break;
    case RESULTS:
        if (event->type == SDL_EVENT_KEY_DOWN) {
            if (event->key.key == SDLK_RETURN) {
                enterTypingMode();
            } else if (event->key.key == SDLK_ESCAPE) {
                enterLobbyMode();
            }
        }
        break;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    // Calculate delta time for animations
    Uint64 current_time = SDL_GetPerformanceCounter();
    float delta_time = 0.0f;

    if (last_frame_time != 0) {
        Uint64 delta_ticks = current_time - last_frame_time;
        delta_time = delta_ticks / (float)SDL_GetPerformanceFrequency();
    }
    last_frame_time = current_time;

    // Update transition state
    updateTransitionState(current_time);

    // Update caret lerp animation
    updateCaretLerp(delta_time);

    // Reset draw calls at the start of each frame
    debugUIResetDrawCalls();

    /* ==== Render Loop ==== */
    SDL_SetRenderDrawColor(renderer, THEME_BACKGROUND.r, THEME_BACKGROUND.g,
                           THEME_BACKGROUND.b, THEME_BACKGROUND.a);
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
        renderResultsGameState();
        break;
    }

    // Render fade overlay on top
    renderTransitionOverlay(renderer, current_time);

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
    fontCacheQuit();
}
