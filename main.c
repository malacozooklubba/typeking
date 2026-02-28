#include "font_cache.h"
#include "framebuffer.h"
#include "text_layout_calculator.h"
#include "text_render.h"
#include "theme.h"
#include "ui.h"
#include <MiniFB.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define FONT_SIZE 64.0f
#define MAX_WORD_COUNT 100
#define MAX_WORD_LENGTH 32

// Word count modes
#define MODE_SHORT 15
#define MODE_MEDIUM 25
#define MODE_LONG 50

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
    double start_time; // seconds
    float duration_ms;
    GameState target_state;
} StateTransition;

typedef enum {
    CHAR_STATE_UNTYPED = 0,
    CHAR_STATE_CORRECT = 1,
    CHAR_STATE_ERROR = 2
} CharState;

typedef struct {
    double start_time; // seconds
    double end_time;   // seconds
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

static struct mfb_window *window = NULL;
static struct mfb_timer *timer = NULL;
static Framebuffer fb;
static int window_width = 1024;
static int window_height = 720;
static bool debug_info = false;
static GameState game_state = SPLASH;
static bool text_input_enabled = false;

// Typing game state variables
static char target_text[MAX_WORD_LENGTH * MAX_WORD_COUNT];
static char user_input[MAX_WORD_LENGTH * MAX_WORD_COUNT];
static int user_input_pos = 0;
static TypingStats typing_stats = {0};
static int word_count = MODE_SHORT;
static bool position_had_error[MAX_WORD_LENGTH * MAX_WORD_COUNT] = {false};

static PrecalculatedTextLayout user_input_layout = {0};

// Animation timing
static double last_frame_time = 0.0;

// Dirty flag for rendering - only redraw when something changes
static bool needs_redraw = true;

// Splash screen timing
static double splash_start_time = 0.0;
#define SPLASH_DURATION_MS 2000.0f

// Dictionary for word loading
#define MAX_DICTIONARY_WORDS 5000
static char *dictionary[MAX_DICTIONARY_WORDS] = {NULL};
static int dictionary_loaded = 0;

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

// Helper to get executable directory (replaces SDL_GetBasePath)
static char exe_dir[1024] = {0};
static const char *getBasePath(void) {
    if (exe_dir[0] == '\0') {
        ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
        if (len > 0) {
            exe_dir[len] = '\0';
            // Strip executable name to get directory
            char *last_slash = strrchr(exe_dir, '/');
            if (last_slash) {
                *(last_slash + 1) = '\0';
            }
        } else {
            strcpy(exe_dir, "./");
        }
    }
    return exe_dir;
}

static double now_seconds(void) { return mfb_timer_now(timer); }

static void beginTransition(GameState target) {
    state_transition.state = TRANSITION_FADE_IN;
    state_transition.start_time = now_seconds();
    state_transition.target_state = target;
}

static void performStateChange(GameState new_state) {
    game_state = new_state;

    switch (new_state) {
    case SPLASH:
        splash_start_time = now_seconds();
        text_input_enabled = false;
        break;

    case LOBBY:
        text_input_enabled = false;
        break;

    case TYPING:
        // Load new words for this round
        user_input[0] = '\0';
        user_input_pos = 0;

        // Reset typing stats
        memset(&typing_stats, 0, sizeof(TypingStats));
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

        text_input_enabled = true;
        break;

    case RESULTS:
        // Capture end time and calculate all statistics
        typing_stats.end_time = now_seconds();
        calculateTypingStats();
        text_input_enabled = false;
        break;
    }
}

// Returns true if transition is active (needs rendering)
static bool updateTransitionState(double current_time) {
    if (state_transition.state == TRANSITION_NONE)
        return false;

    float elapsed_ms =
        (float)((current_time - state_transition.start_time) * 1000.0);

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
    return true; // Animation still active
}

static void renderTransitionOverlay(double current_time) {
    if (state_transition.state == TRANSITION_NONE)
        return;

    float elapsed_ms =
        (float)((current_time - state_transition.start_time) * 1000.0);

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

    fb_fill_rect_alpha(&fb, 0, 0, fb.width, fb.height, THEME_BACKGROUND,
                       (uint8_t)alpha);
}

// Find which line contains the caret using precalculated layout
static int findCaretLine(int caret_pos, const PrecalculatedTextLayout *layout,
                         int *line_start_offset) {
    if (!layout || layout->line_count == 0) {
        *line_start_offset = 0;
        return 0;
    }

    for (int i = 0; i < layout->line_count; i++) {
        int line_start = layout->line_starts[i];
        int line_end;

        if (i == layout->line_count - 1) {
            line_end = 999999;
        } else {
            line_end = layout->line_starts[i + 1];
        }

        if (caret_pos >= line_start && caret_pos < line_end) {
            *line_start_offset = line_start;
            return i;
        }
    }

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

    if (user_input_pos == 0) {
        caret_target_x = 0.0f;
        caret_visual_x = 0.0f;
        caret_current_line = 0;
        return false;
    }

    PrecalculatedTextLayout text_layout = getCalculatedTextLayout();

    int line_start_offset = 0;
    int new_line =
        findCaretLine(user_input_pos, &text_layout, &line_start_offset);

    int chars_on_line = user_input_pos - line_start_offset;

    if (chars_on_line < 0)
        chars_on_line = 0;
    if (chars_on_line > 511)
        chars_on_line = 511;

    char temp_text[512];
    strncpy(temp_text, user_input + line_start_offset, chars_on_line);
    temp_text[chars_on_line] = '\0';

    float measured_width;
    textRenderMeasure(temp_text, FONT_SIZE, &measured_width, NULL);

    if (new_line != caret_current_line) {
        caret_current_line = new_line;
        caret_target_x = measured_width;
        caret_visual_x = measured_width;
        return false;
    }

    caret_target_x = measured_width;

    const float lerp_factor = 30.0f;
    caret_visual_x +=
        (caret_target_x - caret_visual_x) * lerp_factor * delta_time;

    if (fabsf(caret_target_x - caret_visual_x) < 0.5f) {
        caret_visual_x = caret_target_x;
        return false;
    }

    return true;
}

// Returns true if splash state changed (triggered transition)
static bool updateSplashState(double current_time) {
    if (game_state != SPLASH)
        return false;
    if (state_transition.state != TRANSITION_NONE)
        return false;

    float elapsed_ms = (float)((current_time - splash_start_time) * 1000.0);

    if (elapsed_ms >= SPLASH_DURATION_MS) {
        beginTransition(LOBBY);
        return true;
    }
    return false;
}

static void enterLobbyMode(void) { beginTransition(LOBBY); }

static void enterTypingMode(void) {
    loadWords(word_count);
    calculateTextLayoutLineBreaks(target_text, FONT_SIZE,
                                  window_width - window_padding * 2);
    beginTransition(TYPING);
}

static void enterResultsMode(void) { beginTransition(RESULTS); }

static void renderSplashGameState(void) {
    const char *crown_art[] = {
        "*   *   *",
        "** *** **",
        "*********",
        "*********",
    };
    const int crown_lines = 4;
    float line_height = textRenderGetLineHeight(FONT_SIZE);

    float crown_height = crown_lines * line_height;
    float title_height = line_height;
    float gap = 30.0f;
    float total_height = crown_height + gap + title_height;

    float start_y = (window_height - total_height) / 2.0f;

    for (int i = 0; i < crown_lines; i++) {
        UITextBox crown_line =
            uiTextBoxCreate(0.0f, start_y + i * line_height, window_width,
                            line_height, FONT_SIZE);
        crown_line.text = crown_art[i];
        crown_line.align = UI_ALIGN_CENTER;
        crown_line.text_color = THEME_TEXT_TYPED;
        uiTextBoxDraw(&fb, &crown_line);
    }

    UITextBox title_box =
        uiTextBoxCreate(0.0f, start_y + crown_height + gap, window_width,
                        title_height, FONT_SIZE);
    title_box.text = game_name;
    title_box.align = UI_ALIGN_CENTER;
    title_box.text_color = THEME_TEXT_TYPED;
    uiTextBoxDraw(&fb, &title_box);
}

static void renderLobbyGameState(void) {
    const float line_height = 80.0f;
    const float box_height = 50.0f;
    float total_height = line_height + line_height * 1.5f + box_height;
    float current_y = (window_height - total_height) / 2.0f;

    static char mode_text[64];
    const char *mode_name;
    if (word_count == MODE_SHORT) {
        mode_name = "Short";
    } else if (word_count == MODE_MEDIUM) {
        mode_name = "Medium";
    } else {
        mode_name = "Long";
    }
    snprintf(mode_text, sizeof(mode_text), "%s (%d words)", mode_name,
             word_count);

    UITextBox mode_box =
        uiTextBoxCreate(0.0f, current_y, window_width, 50.0f, FONT_SIZE);
    mode_box.text = mode_text;
    mode_box.align = UI_ALIGN_CENTER;
    mode_box.text_color = THEME_TEXT_TYPED;
    uiTextBoxDraw(&fb, &mode_box);
    current_y += line_height;

    UITextBox instructions_box =
        uiTextBoxCreate(0.0f, current_y, window_width, 50.0f, FONT_SIZE);
    instructions_box.text = "1, 2, or 3 to change mode";
    instructions_box.align = UI_ALIGN_CENTER;
    instructions_box.text_color = THEME_TEXT_UNTYPED;
    uiTextBoxDraw(&fb, &instructions_box);
    current_y += line_height * 1.5f;

    UITextBox start_box =
        uiTextBoxCreate(0.0f, current_y, window_width, 50.0f, FONT_SIZE);
    start_box.text = "ENTER to start";
    start_box.align = UI_ALIGN_CENTER;
    start_box.text_color = THEME_TEXT_TYPED;
    uiTextBoxDraw(&fb, &start_box);
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

static void calculateTypingStats(void) {
    if (typing_stats.timer_started) {
        typing_stats.elapsed_seconds =
            typing_stats.end_time - typing_stats.start_time;
    } else {
        typing_stats.elapsed_seconds = 0.0;
    }

    typing_stats.correct_words = 0;
    char target_word[MAX_WORD_LENGTH];
    char input_word[MAX_WORD_LENGTH];
    const char *target_p = target_text;
    const char *input_p = user_input;

    while (true) {
        int target_len = 0;
        while (*target_p && *target_p != ' ' &&
               target_len < MAX_WORD_LENGTH - 1) {
            target_word[target_len++] = *target_p++;
        }
        target_word[target_len] = '\0';
        if (*target_p == ' ')
            target_p++;

        int input_len = 0;
        while (*input_p && *input_p != ' ' && input_len < MAX_WORD_LENGTH - 1) {
            input_word[input_len++] = *input_p++;
        }
        input_word[input_len] = '\0';
        if (*input_p == ' ')
            input_p++;

        if (target_len > 0 && strcmp(target_word, input_word) == 0) {
            typing_stats.correct_words++;
        }

        if (target_len == 0 && input_len == 0)
            break;
    }

    compareInputToTarget(target_text, user_input, char_states_buffer,
                         sizeof(char_states_buffer));

    typing_stats.correct_chars = 0;
    int target_len = strlen(target_text);
    for (int i = 0; i < target_len; i++) {
        if (char_states_buffer[i] == CHAR_STATE_CORRECT) {
            typing_stats.correct_chars++;
        }
    }

    typing_stats.total_chars_typed = strlen(user_input);
    if (target_len > 0) {
        double error_percentage =
            (typing_stats.error_chars / (double)target_len) * 100.0;
        typing_stats.accuracy = 100.0 - error_percentage;
    } else {
        typing_stats.accuracy = 100.0;
    }

    if (typing_stats.elapsed_seconds > 0) {
        typing_stats.wpm =
            typing_stats.correct_words / (typing_stats.elapsed_seconds / 60.0);
        typing_stats.cps =
            typing_stats.correct_chars / typing_stats.elapsed_seconds;
    } else {
        typing_stats.wpm = 0.0;
        typing_stats.cps = 0.0;
    }

    fprintf(stderr,
            "Stats - Time: %.2fs, WPM: %.1f, CPS: %.1f, Accuracy: %.1f%%, "
            "Words: %d/%d\n",
            typing_stats.elapsed_seconds, typing_stats.wpm, typing_stats.cps,
            typing_stats.accuracy, typing_stats.correct_words,
            typing_stats.total_words);
}

static void renderTypingGameState(void) {
    compareInputToTarget(target_text, user_input, char_states_buffer,
                         sizeof(char_states_buffer));

    int input_len = strlen(user_input);
    int target_len = strlen(target_text);

    strncpy(display_text_buffer, user_input, input_len);

    if (input_len < target_len) {
        strcpy(display_text_buffer + input_len, target_text + input_len);
    } else {
        display_text_buffer[input_len] = '\0';
    }

    UITextBox box1 =
        uiTextBoxCreate(window_padding, 50.0f,
                        window_width - window_padding * 2, 150.0f, FONT_SIZE);

    box1.text = display_text_buffer;
    box1.char_states = char_states_buffer;
    box1.align = UI_ALIGN_START;
    box1.text_color = THEME_TEXT_UNTYPED;
    box1.bg_color = THEME_BACKGROUND;
    box1.caret_position = user_input_pos;
    box1.caret_visual_x_offset = caret_visual_x;

    drawPrecalculatedTextLayout(&fb, &box1);
}

static void renderResultsGameState(void) {
    const float line_height = 60.0f;
    const float box_height = 50.0f;
    float total_height =
        line_height * 2 + line_height * 1.5f + line_height + box_height;
    float current_y = (window_height - total_height) / 2.0f;

    static char wpm_text[64];
    static char cps_text[64];
    static char accuracy_text[64];

    snprintf(wpm_text, sizeof(wpm_text), "WPM: %.1f", typing_stats.wpm);
    snprintf(cps_text, sizeof(cps_text), "CPS: %.1f", typing_stats.cps);
    snprintf(accuracy_text, sizeof(accuracy_text), "ACCURACY: %.1f%%",
             typing_stats.accuracy);

    UITextBox wpm_box =
        uiTextBoxCreate(0.0f, current_y, window_width, 50.0f, FONT_SIZE);
    wpm_box.text = wpm_text;
    wpm_box.align = UI_ALIGN_CENTER;
    wpm_box.text_color = THEME_TEXT_TYPED;
    uiTextBoxDraw(&fb, &wpm_box);
    current_y += line_height;

    UITextBox cps_box =
        uiTextBoxCreate(0.0f, current_y, window_width, 50.0f, FONT_SIZE);
    cps_box.text = cps_text;
    cps_box.align = UI_ALIGN_CENTER;
    cps_box.text_color = THEME_TEXT_TYPED;
    uiTextBoxDraw(&fb, &cps_box);
    current_y += line_height;

    UITextBox accuracy_box =
        uiTextBoxCreate(0.0f, current_y, window_width, 50.0f, FONT_SIZE);
    accuracy_box.text = accuracy_text;
    accuracy_box.align = UI_ALIGN_CENTER;
    accuracy_box.text_color = THEME_TEXT_TYPED;
    uiTextBoxDraw(&fb, &accuracy_box);
    current_y += line_height * 1.5f;

    UITextBox continue_box =
        uiTextBoxCreate(0.0f, current_y, window_width, 50.0f, FONT_SIZE);
    continue_box.text = "Enter to restart";
    continue_box.align = UI_ALIGN_CENTER;
    continue_box.text_color = THEME_TEXT_UNTYPED;
    uiTextBoxDraw(&fb, &continue_box);
    current_y += line_height;

    UITextBox exit_box =
        uiTextBoxCreate(0.0f, current_y, window_width, 50.0f, FONT_SIZE);
    exit_box.text = "Escape to return to lobby";
    exit_box.align = UI_ALIGN_CENTER;
    exit_box.text_color = THEME_TEXT_UNTYPED;
    uiTextBoxDraw(&fb, &exit_box);
}

static void loadWords(int count) {
    if (count < 1)
        count = 1;
    if (count > MAX_WORD_COUNT)
        count = MAX_WORD_COUNT;

    const char *basePath = getBasePath();
    char path[1024];
    const char *word_file = "common_500.txt";
    snprintf(path, sizeof(path), "%swords/%s", basePath, word_file);

    target_text[0] = '\0';

    FILE *file = fopen(path, "r");

    if (file == NULL) {
        fprintf(stderr, "Could not open words file: %s\n", path);
        return;
    }

    srand(time(NULL));

    if (dictionary_loaded == 0) {
        for (int i = 0; i < dictionary_loaded; i++) {
            if (dictionary[i]) {
                free(dictionary[i]);
                dictionary[i] = NULL;
            }
        }
        dictionary_loaded = 0;
        char buffer[64];
        while (fgets(buffer, sizeof(buffer), file) &&
               dictionary_loaded < MAX_DICTIONARY_WORDS) {
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len - 1] == '\n') {
                buffer[len - 1] = '\0';
                len--;
            }

            if (len > 0) {
                dictionary[dictionary_loaded] = malloc(len + 1);
                if (dictionary[dictionary_loaded]) {
                    strcpy(dictionary[dictionary_loaded], buffer);
                    dictionary_loaded++;
                }
            }
        }
        fprintf(stderr, "Loaded %d words into dictionary\n", dictionary_loaded);
    }

    fclose(file);

    if (dictionary_loaded == 0) {
        fprintf(stderr, "No words loaded from file\n");
        return;
    }

    for (int i = 0; i < count; i++) {
        int random_index = rand() % dictionary_loaded;

        if (i > 0) {
            strcat(target_text, " ");
        }
        strcat(target_text, dictionary[random_index]);
    }

    fprintf(stderr, "Selected %d words: %s\n", count, target_text);
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

// --- MiniFB callbacks ---

static void keyboard_cb(struct mfb_window *win, mfb_key key, mfb_key_mod mod,
                        bool is_pressed) {
    if (!is_pressed)
        return;

    needs_redraw = true;

    // Global keys
    if (key == KB_KEY_ESCAPE) {
        enterLobbyMode();
        return;
    }

    switch (game_state) {
    case SPLASH:
        if (state_transition.state == TRANSITION_NONE) {
            beginTransition(LOBBY);
        }
        break;

    case LOBBY:
        if (key == KB_KEY_ENTER) {
            enterTypingMode();
        } else if (key == KB_KEY_1) {
            word_count = MODE_SHORT;
            fprintf(stderr, "Mode set to SHORT (%d words)\n", word_count);
        } else if (key == KB_KEY_2) {
            word_count = MODE_MEDIUM;
            fprintf(stderr, "Mode set to MEDIUM (%d words)\n", word_count);
        } else if (key == KB_KEY_3) {
            word_count = MODE_LONG;
            fprintf(stderr, "Mode set to LONG (%d words)\n", word_count);
        }
        break;

    case TYPING:
        if (key == KB_KEY_BACKSPACE) {
            if (user_input_pos > 0) {
                user_input_pos--;
                user_input[user_input_pos] = '\0';

                const float text_max_width = window_width - window_padding * 2;
                user_input_layout.line_count =
                    calculateTextLines(user_input, FONT_SIZE, text_max_width,
                                       user_input_layout.line_starts);
            }
        }
        break;

    case RESULTS:
        if (key == KB_KEY_ENTER) {
            enterTypingMode();
        }
        break;
    }
}

// MiniFB bug: char_input_func is called twice per keypress for printable
// ASCII (lines 78 and 84 in X11MiniFB.c). Deduplicate with a simple guard.
static unsigned int last_char_codepoint = 0;
static double last_char_time = 0.0;

static void char_input_cb(struct mfb_window *win, unsigned int codepoint) {
    if (!text_input_enabled)
        return;
    if (game_state != TYPING)
        return;
    if (codepoint < 0x20 || codepoint > 0x7e)
        return;

    // Deduplicate: ignore if same codepoint arrives within 1ms
    double now = now_seconds();
    if (codepoint == last_char_codepoint && (now - last_char_time) < 0.001) {
        return;
    }
    last_char_codepoint = codepoint;
    last_char_time = now;

    // Start timer on first character typed
    if (!typing_stats.timer_started) {
        typing_stats.start_time = now_seconds();
        typing_stats.timer_started = true;
    }

    if (user_input_pos < 1024 && codepoint < 128) {
        char typed_char = (char)codepoint;
        int current_pos = user_input_pos;

        user_input[user_input_pos++] = typed_char;
        user_input[user_input_pos] = '\0';
        needs_redraw = true;

        const float text_max_width = window_width - window_padding * 2;
        user_input_layout.line_count =
            calculateTextLines(user_input, FONT_SIZE, text_max_width,
                               user_input_layout.line_starts);

        if (current_pos < (int)strlen(target_text) &&
            typed_char != target_text[current_pos] &&
            !position_had_error[current_pos]) {

            typing_stats.error_chars++;
            position_had_error[current_pos] = true;
        }

        if (user_input_pos >= (int)strlen(target_text)) {
            enterResultsMode();
        }
    }
}

static void resize_cb(struct mfb_window *win, int width, int height) {
    window_width = width;
    window_height = height;
    fb_resize(&fb, width, height);
    needs_redraw = true;

    if (game_state == TYPING) {
        calculateTextLayoutLineBreaks(target_text, FONT_SIZE,
                                      window_width - window_padding * 2);

        const float text_max_width = window_width - window_padding * 2;
        user_input_layout.line_count =
            calculateTextLines(user_input, FONT_SIZE, text_max_width,
                               user_input_layout.line_starts);
    }
}

int main(void) {
    window = mfb_open_ex(game_name, window_width, window_height, WF_RESIZABLE);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        return 1;
    }

    timer = mfb_timer_create();

    mfb_set_keyboard_callback(window, keyboard_cb);
    mfb_set_char_input_callback(window, char_input_cb);
    mfb_set_resize_callback(window, resize_cb);
    mfb_set_target_fps(60);

    fb_init(&fb, window_width, window_height);

    const char *basePath = getBasePath();
    char path[1024];
    snprintf(path, sizeof(path), "%sfont/JetBrainsMono-Regular.ttf", basePath);

    if (!fontCacheInit(path, FONT_SIZE)) {
        fprintf(stderr, "Failed to initialize text rendering\n");
        fb_destroy(&fb);
        return 1;
    }

    // Initialize splash screen timer
    splash_start_time = now_seconds();

    while (true) {
        double current_time = now_seconds();
        float delta_time = 0.0f;

        if (last_frame_time > 0.0) {
            delta_time = (float)(current_time - last_frame_time);
        }
        last_frame_time = current_time;

        // Update animations
        bool animating = false;
        animating |= updateTransitionState(current_time);
        animating |= updateSplashState(current_time);
        animating |= updateCaretLerp(delta_time);

        bool did_render = needs_redraw || animating;

        if (did_render) {
            fb_clear(&fb, THEME_BACKGROUND);

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

            renderTransitionOverlay(current_time);

            needs_redraw = false;
        }

        // Only upload framebuffer when we actually rendered
        mfb_update_state state;
        if (did_render) {
            state = mfb_update_ex(window, fb.pixels, fb.width, fb.height);
            mfb_wait_sync(window);
        } else {
            state = mfb_update_events(window);
            usleep(16000); // ~60fps when idle
        }
        if (state != STATE_OK)
            break;
    }

    freeDictionary();
    fontCacheQuit();
    fb_destroy(&fb);
    mfb_timer_destroy(timer);

    return 0;
}
