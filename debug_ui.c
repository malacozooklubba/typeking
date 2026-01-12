#include "debug_ui.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define FRAME_HISTORY_SIZE 240
#define GRAPH_HEIGHT 60.0f
#define GRAPH_WIDTH 300.0f

typedef struct {
    double frame_times[FRAME_HISTORY_SIZE]; // in milliseconds
    int frame_index;
    Uint64 last_frame_time;
    Uint64 performance_frequency;

    int draw_calls;
    int draw_calls_history[FRAME_HISTORY_SIZE];

    double fps;
    int frame_count;
    Uint64 fps_last_time;

    bool visible;
} DebugUI;

static DebugUI debug = {0};

void debugUIInit(void) {
    memset(&debug, 0, sizeof(DebugUI));
    debug.performance_frequency = SDL_GetPerformanceFrequency();
    debug.last_frame_time = SDL_GetPerformanceCounter();
    debug.fps_last_time = SDL_GetTicks();
    debug.visible = true; // Start visible
}

void debugUIUpdateFrame(void) {
    Uint64 current_time = SDL_GetPerformanceCounter();
    double frame_time_ms = (current_time - debug.last_frame_time) * 1000.0 /
                           debug.performance_frequency;

    debug.frame_times[debug.frame_index] = frame_time_ms;
    debug.draw_calls_history[debug.frame_index] = debug.draw_calls;
    debug.frame_index = (debug.frame_index + 1) % FRAME_HISTORY_SIZE;

    debug.last_frame_time = current_time;

    // Update FPS counter
    debug.frame_count++;
    Uint64 current_ticks = SDL_GetTicks();
    if (current_ticks - debug.fps_last_time >= 200) {
        debug.fps = debug.frame_count * 5.0;
        debug.frame_count = 0;
        debug.fps_last_time = current_ticks;
    }
}

void debugUIIncrementDrawCalls(void) { debug.draw_calls++; }

void debugUIResetDrawCalls(void) { debug.draw_calls = 0; }

void debugUIToggle(void) { debug.visible = !debug.visible; }

bool debugUIIsVisible(void) { return debug.visible; }

void debugUIDraw(SDL_Renderer *renderer, int window_width, int window_height) {
    if (!debug.visible) {
        return;
    }

    const float padding = 10.0f;
    const float panel_width = GRAPH_WIDTH + padding * 2;
    const float panel_height = 200.0f;
    const float x = window_width - panel_width - 10.0f;
    const float y = 10.0f;

    // Draw semi-transparent background
    SDL_FRect bg = {x, y, panel_width, panel_height};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, &bg);

    // Draw border
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
    SDL_RenderRect(renderer, &bg);

    float text_y = y + padding;
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color green = {0, 255, 0, 255};

    // Calculate statistics
    double avg_frame_time = 0;
    double max_frame_time = 0;
    double min_frame_time = 999999;
    int avg_draw_calls = 0;

    for (int i = 0; i < FRAME_HISTORY_SIZE; i++) {
        if (debug.frame_times[i] > 0) {
            avg_frame_time += debug.frame_times[i];
            if (debug.frame_times[i] > max_frame_time)
                max_frame_time = debug.frame_times[i];
            if (debug.frame_times[i] < min_frame_time)
                min_frame_time = debug.frame_times[i];
        }
        avg_draw_calls += debug.draw_calls_history[i];
    }
    avg_frame_time /= FRAME_HISTORY_SIZE;
    avg_draw_calls /= FRAME_HISTORY_SIZE;

    // Draw FPS
    char text_buffer[128];
    snprintf(text_buffer, sizeof(text_buffer), "FPS: %.1f", debug.fps);
    // textRenderDraw(renderer, text_buffer, x + padding, text_y, 24.0f, green);
    text_y += 24.0f;

    // Draw frame time stats
    snprintf(text_buffer, sizeof(text_buffer), "Frame: %.2fms (avg)",
             avg_frame_time);
    // textRenderDraw(renderer, text_buffer, x + padding, text_y, 24.0f, white);
    text_y += 24.0f;

    snprintf(text_buffer, sizeof(text_buffer), "Min: %.2fms | Max: %.2fms",
             min_frame_time, max_frame_time);
    // textRenderDraw(renderer, text_buffer, x + padding, text_y, 24.0f, white);
    text_y += 24.0f;

    // Draw draw calls
    snprintf(text_buffer, sizeof(text_buffer), "Draw Calls: %d (avg: %d)",
             debug.draw_calls, avg_draw_calls);
    // textRenderDraw(renderer, text_buffer, x + padding, text_y, 24.0f, white);
    text_y += 24.0f;

    // Draw frame time graph
    float graph_x = x + padding;
    float graph_y = text_y;

    // Graph background
    SDL_FRect graph_bg = {graph_x, graph_y, GRAPH_WIDTH, GRAPH_HEIGHT};
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    SDL_RenderFillRect(renderer, &graph_bg);

    // Draw frame time graph
    float bar_width = GRAPH_WIDTH / (float)FRAME_HISTORY_SIZE;
    for (int i = 0; i < FRAME_HISTORY_SIZE; i++) {
        int idx = (debug.frame_index + i) % FRAME_HISTORY_SIZE;
        double frame_time = debug.frame_times[idx];

        if (frame_time > 0) {
            float normalized = (float)(frame_time / 1);
            if (normalized > 1.0f)
                normalized = 1.0f;

            float bar_height = normalized * GRAPH_HEIGHT;
            float bar_x = graph_x + i * bar_width;
            float bar_y = graph_y + GRAPH_HEIGHT - bar_height;

            SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);

            SDL_FRect bar = {bar_x, bar_y, bar_width, bar_height};
            SDL_RenderFillRect(renderer, &bar);
        }
    }

    // Graph border
    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
    SDL_RenderRect(renderer, &graph_bg);
}

void debugUIQuit(void) { memset(&debug, 0, sizeof(DebugUI)); }
