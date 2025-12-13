#include <stdlib.h>

#include "fps_counter.h"

#define STB_TRUETYPE_IMPLEMENTATION 1
#include "stb_truetype.h"

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

const char gameName[] = "Type King";
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static int width = 1024;
static int height = 720;

unsigned char temp_bitmap[512 * 512];
stbtt_fontinfo font;
unsigned char *bitmap;

void loadFont() {
  char ttf_buffer[1 << 25];
  fread(ttf_buffer, 1, 1 << 25,
        fopen("./bin/fonts/JetBrainsMono-Regular.ttf", "rb"));
  stbtt_InitFont(&font, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0));
}

int drawLetter(SDL_Renderer *renderer) {

  int w, h, i, j, c = ('S'), s = (20);
  bitmap = stbtt_GetCodepointBitmap(
      &font, 0, stbtt_ScaleForPixelHeight(&font, s), c, &w, &h, 0, 0);

  for (j = 0; j < h; ++j) {
    for (i = 0; i < w; ++i) {
      char asciiCharacter = " .:ioVM@"[bitmap[j * w + i] >> 5];
      if (!SDL_RenderDebugText(renderer, i * 10.0f, 80.0f + j * 10.0f,
                               &asciiCharacter)) {
        SDL_Log("Error drawing fps! %s \n", SDL_GetError());
      }
    }
  }

  return 0;
}

SDL_AppResult SDL_AppInit(__attribute__((unused)) void **appstate,
                          __attribute__((unused)) int argc,
                          __attribute__((unused)) char *argv[]) {

  SDL_SetAppMetadata(gameName, "1.0", "com.palm.treetyper");

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("Could not initialize SDL: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  if (!SDL_CreateWindowAndRenderer(gameName, width, height,
                                   SDL_WINDOW_RESIZABLE, &window, &renderer)) {

    SDL_Log("Could not create window/renderer: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  // Load Jet Brains Mono Font
  loadFont();

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

  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  SDL_FRect rect = {.x = 0, .y = 0, .w = width, .h = height};
  const double now = ((double)SDL_GetTicks()) / 1000.0;
  const float red = (float)(0.5 + 0.5 * SDL_sin(now));
  const float green = (float)(0.5 + 0.5 * SDL_sin(now + SDL_PI_D * 2 / 3));
  const float blue = (float)(0.5 + 0.5 * SDL_sin(now + SDL_PI_D * 4 / 3));

  /* ==== Render Loop ==== */
  SDL_RenderClear(renderer);

  SDL_SetRenderDrawColorFloat(renderer, red, green, blue,
                              SDL_ALPHA_OPAQUE_FLOAT);
  SDL_RenderFillRect(renderer, &rect);

  // DrawFps
  drawFps(renderer);

  // Draw letter as ascii
  drawLetter(renderer);

  // Black background
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderPresent(renderer);
  /* ===================== */

  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(__attribute__((unused)) void *appstate,
                 __attribute__((unused)) SDL_AppResult result) {
  // SDL clean up window and renderer
  free(bitmap);
}
