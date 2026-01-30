#include "font_cache.h"
#include "fps_counter.h"
#include "text_layout_calculator.h"
#include "text_render.h"
#include "theme.h"
#include "ui.h"
#include <SDL3/SDL_filesystem.h>
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

// Difficulty modes
typedef enum {
    DIFFICULTY_EASY, // common_500.txt
    DIFFICULTY_HARD  // oxford_3000.txt
} DifficultyMode;

typedef enum {
    SPLASH,
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
const float window_padding = 50.0f;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static int window_width = 1024;
static int window_height = 720;
static bool debug_info = false;
static GameState game_state = SPLASH;

// Typing game state variables
static char target_text[MAX_WORD_LENGTH * MAX_WORD_COUNT];
static char user_input[MAX_WORD_LENGTH * MAX_WORD_COUNT];
static int user_input_pos = 0;
static TypingStats typing_stats = {0};
static int word_count = MODE_SHORT;
static DifficultyMode difficulty_mode = DIFFICULTY_EASY;
static bool position_had_error[MAX_WORD_LENGTH * MAX_WORD_COUNT] = {false};

// Precalculated text layouts (not used anymore - using simpler
// PrecalculatedTextLayout)
static PrecalculatedTextLayout user_input_layout = {0};

// Animation timing
static Uint64 last_frame_time = 0;

// Frame rate limiting
static float target_frame_time_ms = 16.666f; // Default to 60Hz
static Uint64 frame_start_time = 0;

// Dirty flag for rendering - only redraw when something changes
static bool needs_redraw = true;

// Splash screen timing
static Uint64 splash_start_time = 0;
#define SPLASH_DURATION_MS 2000.0f

// Dictionary for word loading
#define MAX_DICTIONARY_WORDS 5000
static char *dictionary[MAX_DICTIONARY_WORDS] = {NULL};
static int dictionary_loaded = 0;
static DifficultyMode dictionary_difficulty = DIFFICULTY_EASY; // Track which difficulty dictionary was loaded for

// State transition animation
static StateTransition state_transition = {
    .state = TRANSITION_NONE,
    .duration_ms = 100.0f // Each phase is 100ms (200ms total)
};

// Caret lerp animation
static float caret_visual_x = 0.0f;
static float caret_target_x = 0.0f;
static int caret_current_line = 0; // Track which line caret is on

// Reusable buffers to avoid stack allocation every frame
static unsigned char char_states_buffer[MAX_WORD_LENGTH * MAX_WORD_COUNT];
static char display_text_buffer[MAX_WORD_LENGTH * MAX_WORD_COUNT];

// Forward declarations
static void calculateTypingStats(void);
static void loadWords(int count);
static void freeDictionary(void);

static void beginTransition(GameState target) {
    state_transition.state = TRANSITION_FADE_IN;
    state_transition.start_time = SDL_GetPerformanceCounter();
    state_transition.target_state = target;
}

static void performStateChange(GameState new_state) {
    GameState old_state = game_state;
    game_state = new_state;

    switch (new_state) {
    case SPLASH:
        splash_start_time = SDL_GetPerformanceCounter();
        SDL_StopTextInput(window);
        break;

    case LOBBY:
        SDL_StopTextInput(window);
        break;

    case TYPING:
        // Load new words for this round
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

        // Initialize empty user input layout
        user_input_layout.line_count = 1;
        user_input_layout.line_starts[0] = 0;

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

// Returns true if transition is active (needs rendering)
static bool updateTransitionState(Uint64 current_time) {
    if (state_transition.state == TRANSITION_NONE)
        return false;

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
            return false;
        }
    }
    return true;  // Animation still active
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

// Find which line contains the caret using precalculated layout
static int findCaretLine(int caret_pos, const PrecalculatedTextLayout *layout,
                         int *line_start_offset) {
    if (!layout || layout->line_count == 0) {
        *line_start_offset = 0;
        return 0;
    }

    // Find which line contains the caret position
    // line_starts[i] is where line i begins
    // line_starts[i+1] is where line i+1 begins (which is where line i ends)
    for (int i = 0; i < layout->line_count; i++) {
        int line_start = layout->line_starts[i];
        int line_end;

        // Last line goes to end of text
        if (i == layout->line_count - 1) {
            line_end =
                999999; // Large number - caret is definitely on last line
        } else {
            line_end = layout->line_starts[i + 1];
        }

        // Caret is on this line if position is within range
        if (caret_pos >= line_start && caret_pos < line_end) {
            *line_start_offset = line_start;
            return i;
        }
    }

    // Caret is at the end (after last line) - use last line
    if (layout->line_count > 0) {
        int last_line = layout->line_count - 1;
        *line_start_offset = layout->line_starts[last_line];
        return last_line;
    }

    *line_start_offset = 0;
    return 0;
}

// Returns true if caret is still animating (not at target)
static bool updateCaretLerp(float delta_time) {
    if (game_state != TYPING) {
        caret_visual_x = 0.0f;
        caret_target_x = 0.0f;
        caret_current_line = 0;
        return false;
    }

    // Calculate target X position from current input
    if (user_input_pos == 0) {
        caret_target_x = 0.0f;
        caret_visual_x = 0.0f;
        caret_current_line = 0;
        return false;
    }

    // Use the SAME layout as rendering (target_text layout) to ensure
    // caret X position matches the rendered line boundaries
    PrecalculatedTextLayout text_layout = getCalculatedTextLayout();

    // Find which line the caret is on using target_text layout
    int line_start_offset = 0;
    int new_line =
        findCaretLine(user_input_pos, &text_layout, &line_start_offset);

    // Calculate target X: measure text from line start to caret position
    int chars_on_line = user_input_pos - line_start_offset;

    // Bounds check
    if (chars_on_line < 0)
        chars_on_line = 0;
    if (chars_on_line > 511)
        chars_on_line = 511;

    char temp_text[512];
    strncpy(temp_text, user_input + line_start_offset, chars_on_line);
    temp_text[chars_on_line] = '\0';

    float measured_width;
    textRenderMeasure(temp_text, FONT_SIZE, &measured_width, NULL);

    // Detect line change - snap immediately (no animation)
    if (new_line != caret_current_line) {
        caret_current_line = new_line;
        caret_target_x = measured_width;
        caret_visual_x = measured_width; // Snap instantly
        return false;  // No ongoing animation after snap
    }

    // Same line - update target
    caret_target_x = measured_width;

    // Exponential lerp toward target (smooth animation within line)
    const float lerp_factor = 30.0f;
    caret_visual_x +=
        (caret_target_x - caret_visual_x) * lerp_factor * delta_time;

    // Snap when very close (within 0.5 pixels)
    if (fabsf(caret_target_x - caret_visual_x) < 0.5f) {
        caret_visual_x = caret_target_x;
        return false;  // Animation complete
    }

    return true;  // Still animating
}

// Returns true if splash state changed (triggered transition)
static bool updateSplashState(Uint64 current_time) {
    if (game_state != SPLASH)
        return false;
    if (state_transition.state != TRANSITION_NONE)
        return false;

    float elapsed_ms = (current_time - splash_start_time) * 1000.0f /
                       (float)SDL_GetPerformanceFrequency();

    if (elapsed_ms >= SPLASH_DURATION_MS) {
        beginTransition(LOBBY);
        return true;
    }
    return false;
}

void enterLobbyMode(void) { beginTransition(LOBBY); }

void enterTypingMode() {
    loadWords(word_count);
    calculateTextLayoutLineBreaks(target_text, FONT_SIZE,
                                  window_width - window_padding * 2);
    beginTransition(TYPING);
}

void enterResultsMode() { beginTransition(RESULTS); }

static void renderSplashGameState() {
    const char *crown_art[] = {
        "*   *   *",
        "** *** **",
        "*********",
        "*********",
    };
    const int crown_lines = 4;
    float line_height = textRenderGetLineHeight(FONT_SIZE);

    // Calculate total height of crown + title
    float crown_height = crown_lines * line_height;
    float title_height = line_height;
    float gap = 30.0f;
    float total_height = crown_height + gap + title_height;

    // Center vertically
    float start_y = (window_height - total_height) / 2.0f;

    // Draw crown lines
    for (int i = 0; i < crown_lines; i++) {
        UITextBox crown_line = uiTextBoxCreate(0.0f, start_y + i * line_height,
                                               window_width, line_height);
        crown_line.text = crown_art[i];
        crown_line.align = UI_ALIGN_CENTER;
        crown_line.text_color = THEME_TEXT_TYPED;
        uiTextBoxDraw(renderer, &crown_line);
    }

    // Draw "Type King" below crown
    UITextBox title_box = uiTextBoxCreate(0.0f, start_y + crown_height + gap,
                                          window_width, title_height);
    title_box.text = game_name;
    title_box.align = UI_ALIGN_CENTER;
    title_box.text_color = THEME_TEXT_TYPED;
    uiTextBoxDraw(renderer, &title_box);
}

void renderLobbyGameState() {
    const float y_start = 200.0f;
    const float line_height = 50.0f;
    float current_y = y_start;

    // Difficulty display
    static char difficulty_text[64];
    const char *difficulty_name = (difficulty_mode == DIFFICULTY_EASY) ? "EASY" : "HARD";
    snprintf(difficulty_text, sizeof(difficulty_text), "DIFFICULTY: %s", difficulty_name);

    UITextBox difficulty_box = uiTextBoxCreate(0.0f, current_y, window_width, 50.0f);
    difficulty_box.text = difficulty_text;
    difficulty_box.align = UI_ALIGN_CENTER;
    difficulty_box.text_color = THEME_TEXT_TYPED;
    uiTextBoxDraw(renderer, &difficulty_box);
    current_y += line_height;

    // Difficulty instructions
    UITextBox difficulty_instructions_box =
        uiTextBoxCreate(0.0f, current_y, window_width, 50.0f);
    difficulty_instructions_box.text = "PRESS TAB TO TOGGLE DIFFICULTY";
    difficulty_instructions_box.align = UI_ALIGN_CENTER;
    difficulty_instructions_box.text_color = THEME_TEXT_UNTYPED;
    uiTextBoxDraw(renderer, &difficulty_instructions_box);
    current_y += line_height * 1.5f;

    // Word count display
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

    UITextBox mode_box = uiTextBoxCreate(0.0f, current_y, window_width, 50.0f);
    mode_box.text = mode_text;
    mode_box.align = UI_ALIGN_CENTER;
    mode_box.text_color = THEME_TEXT_TYPED;
    uiTextBoxDraw(renderer, &mode_box);
    current_y += line_height;

    // Word count instructions
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
    compareInputToTarget(target_text, user_input, char_states_buffer,
                         sizeof(char_states_buffer));

    typing_stats.correct_chars = 0;
    // NOTE: error_chars is already tracked in real-time, don't recalculate
    int target_len = strlen(target_text);
    for (int i = 0; i < target_len; i++) {
        if (char_states_buffer[i] == CHAR_STATE_CORRECT) {
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
    // Create state array and compare input to target
    compareInputToTarget(target_text, user_input, char_states_buffer,
                         sizeof(char_states_buffer));

    // Build display text: user input + remaining target text
    int input_len = strlen(user_input);
    int target_len = strlen(target_text);

    // Copy user input
    strncpy(display_text_buffer, user_input, input_len);

    // Append remaining target text
    if (input_len < target_len) {
        strcpy(display_text_buffer + input_len, target_text + input_len);
    } else {
        display_text_buffer[input_len] = '\0';
    }

    UITextBox box1 = uiTextBoxCreate(window_padding, 50.0f,
                                     window_width - window_padding * 2, 150.0f);

    box1.text = display_text_buffer;
    box1.char_states = char_states_buffer;
    box1.align = UI_ALIGN_START;
    box1.text_color = THEME_TEXT_UNTYPED;
    box1.bg_color = THEME_BACKGROUND;
    box1.caret_position = user_input_pos;
    box1.caret_visual_x_offset = caret_visual_x;

    drawPrecalculatedTextLayout(renderer, &box1);
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

static void loadWords(int count) {
    // Clamp count to valid range
    if (count < 1)
        count = 1;
    if (count > MAX_WORD_COUNT)
        count = MAX_WORD_COUNT;

    const char *basePath = SDL_GetBasePath();
    char path[1024];
    const char *word_file = (difficulty_mode == DIFFICULTY_EASY)
        ? "common_500.txt"
        : "oxford_3000.txt";
    snprintf(path, sizeof(path), "%s/words/%s", basePath, word_file);

    target_text[0] = '\0'; // Initialize empty string

    FILE *file = fopen(path, "r");

    if (file == NULL) {
        SDL_Log("Could not open words file");
        return;
    }

    // Seed random number generator
    srand(time(NULL));

    // Read all lines into memory (reload if difficulty changed)
    if (dictionary_loaded == 0 || dictionary_difficulty != difficulty_mode) {
        // Free existing dictionary if reloading
        for (int i = 0; i < dictionary_loaded; i++) {
            if (dictionary[i]) {
                free(dictionary[i]);
                dictionary[i] = NULL;
            }
        }
        dictionary_loaded = 0;
        dictionary_difficulty = difficulty_mode;
        char buffer[64];
        while (fgets(buffer, sizeof(buffer), file) &&
               dictionary_loaded < MAX_DICTIONARY_WORDS) {
            // Remove trailing newline if present
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len - 1] == '\n') {
                buffer[len - 1] = '\0';
                len--;
            }

            // Allocate and store word
            if (len > 0) {
                dictionary[dictionary_loaded] = malloc(len + 1);
                if (dictionary[dictionary_loaded]) {
                    strcpy(dictionary[dictionary_loaded], buffer);
                    dictionary_loaded++;
                }
            }
        }
        SDL_Log("Loaded %d words into dictionary", dictionary_loaded);
    }

    fclose(file);

    if (dictionary_loaded == 0) {
        SDL_Log("No words loaded from file");
        return;
    }

    // Pick random words from dictionary
    for (int i = 0; i < count; i++) {
        int random_index = rand() % dictionary_loaded;

        // Append to output buffer
        if (i > 0) {
            strcat(target_text, " "); // Add space separator
        }
        strcat(target_text, dictionary[random_index]);
    }

    SDL_Log("Selected %d words: %s", count, target_text);
}

static void freeDictionary(void) {
    for (int i = 0; i < dictionary_loaded; i++) {
        if (dictionary[i]) {
            free(dictionary[i]);
            dictionary[i] = NULL;
        }
    }
    dictionary_loaded = 0;
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

    const char *basePath = SDL_GetBasePath();
    char path[1024];
    snprintf(path, sizeof(path), "%s/font/JetBrainsMono-Regular.ttf", basePath);

    if (!fontCacheInit(renderer, path, FONT_SIZE)) {
        SDL_Log("Failed to initialize text rendering");
        return SDL_APP_FAILURE;
    }

    // Get display refresh rate and set target frame time
    SDL_DisplayID display_id = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(display_id);

    if (mode && mode->refresh_rate > 0) {
        target_frame_time_ms = 1000.0f / mode->refresh_rate;
        SDL_Log("Display refresh rate: %.2f Hz ", mode->refresh_rate);
    } else {
        SDL_Log("Could not get display refresh rate, defaulting to 60 Hz");
        target_frame_time_ms = 16.666f;
    }

    // Initialize typing stats performance frequency
    typing_stats.performance_frequency = SDL_GetPerformanceFrequency();

    // Initialize splash screen timer
    splash_start_time = SDL_GetPerformanceCounter();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(__attribute__((unused)) void *appstate,
                           SDL_Event *event) {
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        SDL_GetWindowSizeInPixels(window, &window_width, &window_height);
        needs_redraw = true;

        // Recalculate layouts if in typing mode
        if (game_state == TYPING) {
            // Recalculate target text layout
            calculateTextLayoutLineBreaks(target_text, FONT_SIZE,
                                          window_width - window_padding * 2);

            // Recalculate user input layout
            const float text_max_width = window_width - window_padding * 2;
            user_input_layout.line_count =
                calculateTextLines(user_input, FONT_SIZE, text_max_width,
                                   user_input_layout.line_starts);
        }
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_F3) {
            debug_info = !debug_info;
            needs_redraw = true;
            SDL_Log("Debug UI %s", debug_info ? "enabled" : "disabled");
        } else if (event->key.key == SDLK_ESCAPE) {
            needs_redraw = true;
            enterLobbyMode();
        }
    }

    switch (game_state) {
    case SPLASH:
        // No input handling during splash screen
        break;
    case LOBBY:
        if (event->type == SDL_EVENT_KEY_DOWN) {
            if (event->key.key == SDLK_RETURN) {
                needs_redraw = true;
                enterTypingMode();
            } else if (event->key.key == SDLK_TAB) {
                difficulty_mode = (difficulty_mode == DIFFICULTY_EASY)
                    ? DIFFICULTY_HARD
                    : DIFFICULTY_EASY;
                needs_redraw = true;
                SDL_Log("Difficulty set to %s",
                        (difficulty_mode == DIFFICULTY_EASY) ? "EASY" : "HARD");
            } else if (event->key.key == SDLK_1) {
                word_count = MODE_SHORT;
                needs_redraw = true;
                SDL_Log("Mode set to SHORT (%d words)", word_count);
            } else if (event->key.key == SDLK_2) {
                word_count = MODE_MEDIUM;
                needs_redraw = true;
                SDL_Log("Mode set to MEDIUM (%d words)", word_count);
            } else if (event->key.key == SDLK_3) {
                word_count = MODE_LONG;
                needs_redraw = true;
                SDL_Log("Mode set to LONG (%d words)", word_count);
            }
        }
        break;
    case TYPING:
        if (event->type == SDL_EVENT_KEY_DOWN &&
            event->key.key == SDLK_BACKSPACE) {
            // Remove last character from input buffer
            if (user_input_pos > 0) {
                user_input_pos--;
                user_input[user_input_pos] = '\0';
                needs_redraw = true;

                // Recalculate layout
                const float text_max_width = window_width - window_padding * 2;
                user_input_layout.line_count =
                    calculateTextLines(user_input, FONT_SIZE, text_max_width,
                                       user_input_layout.line_starts);
            }
        }

        if (event->type == SDL_EVENT_TEXT_INPUT) {

            // Start timer on first character typed
            if (!typing_stats.timer_started) {
                typing_stats.start_time = SDL_GetPerformanceCounter();
                typing_stats.timer_started = true;
            }

            // Add text in event to input buffer
            if (user_input_pos < 1024) {
                char typed_char = event->text.text[0];
                int current_pos = user_input_pos;

                // Add character to buffer
                user_input[user_input_pos++] = typed_char;
                user_input[user_input_pos] = '\0';
                needs_redraw = true;

                // Recalculate layout
                const float text_max_width = window_width - window_padding * 2;
                user_input_layout.line_count =
                    calculateTextLines(user_input, FONT_SIZE, text_max_width,
                                       user_input_layout.line_starts);

                // Check if this is an error and hasn't been counted yet
                if (current_pos < strlen(target_text) &&
                    typed_char != target_text[current_pos] &&
                    !position_had_error[current_pos]) {

                    typing_stats.error_chars++;
                    position_had_error[current_pos] = true;
                }

                // Check if user has typed all the text
                if (user_input_pos >= strlen(target_text)) {
                    enterResultsMode();
                }
            }
        }
        break;
    case RESULTS:
        if (event->type == SDL_EVENT_KEY_DOWN &&
            event->key.key == SDLK_RETURN) {
            needs_redraw = true;
            enterTypingMode();
        }
        break;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    // Record frame start time for frame rate limiting
    frame_start_time = SDL_GetPerformanceCounter();

    // Calculate delta time for animations
    Uint64 current_time = frame_start_time;
    float delta_time = 0.0f;

    if (last_frame_time != 0) {
        Uint64 delta_ticks = current_time - last_frame_time;
        delta_time = delta_ticks / (float)SDL_GetPerformanceFrequency();
    }
    last_frame_time = current_time;

    // Update animations and track if any are active
    bool animating = false;
    animating |= updateTransitionState(current_time);
    animating |= updateSplashState(current_time);
    animating |= updateCaretLerp(delta_time);

    // Only render if dirty or animating
    if (needs_redraw || animating) {
        /* ==== Render Loop ==== */
        SDL_SetRenderDrawColor(renderer, THEME_BACKGROUND.r, THEME_BACKGROUND.g,
                               THEME_BACKGROUND.b, THEME_BACKGROUND.a);
        SDL_RenderClear(renderer);

        // Run game state machine
        switch (game_state) {
        case SPLASH:
            renderSplashGameState();
            break;
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
            drawFps(renderer);
        }

        SDL_RenderPresent(renderer);
        /* ===================== */

        needs_redraw = false;

        // Frame rate limiting when rendering: sleep if frame finished too quickly
        Uint64 frame_end_time = SDL_GetPerformanceCounter();
        Uint64 frame_ticks = frame_end_time - frame_start_time;
        float frame_time_ms =
            (frame_ticks * 1000.0f) / SDL_GetPerformanceFrequency();

        if (frame_time_ms < target_frame_time_ms) {
            float sleep_time_ms = target_frame_time_ms - frame_time_ms;
            SDL_Delay((Uint32)sleep_time_ms);
        }
    } else {
        // Idle: use longer delay to save CPU while still polling events
        SDL_Delay(16);
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(__attribute__((unused)) void *appstate,
                 __attribute__((unused)) SDL_AppResult result) {
    freeDictionary();
    fontCacheQuit();
}
