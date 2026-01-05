#include "debug_ui.h"
#include "text_render.h"
#include "ui.h"

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

const char gameName[] = "Type King";
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static int width = 1024;
static int height = 720;
static int font_size = 24.0f;

SDL_AppResult SDL_AppInit(__attribute__((unused)) void **appstate,
                          __attribute__((unused)) int argc,
                          __attribute__((unused)) char *argv[]) {

    SDL_SetAppMetadata(gameName, "1.0", "com.palm.treetyper");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Could not initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer(gameName, width, height,
                                     SDL_WINDOW_RESIZABLE, &window,
                                     &renderer)) {

        SDL_Log("Could not create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!textRenderInit(renderer, "./bin/font/JetBrainsMono-Regular.ttf",
                        font_size)) {
        SDL_Log("Failed to initialize text rendering");
        return SDL_APP_FAILURE;
    }

    debugUIInit();

    SDL_Log("Press F3 to toggle debug UI");

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(__attribute__((unused)) void *appstate,
                           SDL_Event *event) {

    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        SDL_GetWindowSizeInPixels(window, &width, &height);
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_F3) {
            debugUIToggle();
            SDL_Log("Debug UI %s", debugUIIsVisible() ? "enabled" : "disabled");
        }
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    // Reset draw calls at the start of each frame
    debugUIResetDrawCalls();

    const float window_padding = 50.0f;

    /* ==== Render Loop ==== */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    // Text box with START alignment (default)
    UITextBox box1 = uiTextBoxCreate(window_padding, 50.0f,
                                     width - window_padding * 2, 150.0f);
    box1.text = "Start Alignment (Default)\n\nThis text is aligned to the "
                "start (left). Long lines wrap naturally at word boundaries.";
    box1.font_size = 24.0f;
    box1.align = UI_ALIGN_START;
    uiTextBoxDraw(renderer, &box1);

    // Text box with CENTER alignment
    UITextBox box2 = uiTextBoxCreate(window_padding, 220.0f,
                                     width - window_padding * 2, 150.0f);
    box2.text = "Center Alignment\n\nThis text is centered within the box. "
                "Each line is individually centered based on its width.";
    box2.font_size = 24.0f;
    box2.align = UI_ALIGN_CENTER;
    box2.bg_color = (SDL_Color){20, 40, 60, 255};
    box2.border_color = (SDL_Color){80, 120, 160, 255};
    uiTextBoxDraw(renderer, &box2);

    // Text box with END alignment
    UITextBox box3 = uiTextBoxCreate(window_padding, 390.0f,
                                     width - window_padding * 2, 150.0f);
    box3.text = "End Alignment\n\nThis text is aligned to the end (right). "
                "Perfect for right-to-left text or special layouts!";
    box3.font_size = 24.0f;
    box3.align = UI_ALIGN_END;
    box3.bg_color = (SDL_Color){40, 20, 60, 255};
    box3.border_color = (SDL_Color){120, 80, 160, 255};
    uiTextBoxDraw(renderer, &box3);

    // Draw debug UI (if enabled)
    debugUIDraw(renderer, width, height);

    SDL_RenderPresent(renderer);
    /* ===================== */

    // Update frame timing at the end of frame
    debugUIUpdateFrame();

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(__attribute__((unused)) void *appstate,
                 __attribute__((unused)) SDL_AppResult result) {
    debugUIQuit();
    textRenderQuit();
}
