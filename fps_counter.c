#include "fps_counter.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>

static int countedFrames = 0;
static Uint64 lastTime, currentTime = 0;
static float fps = 0;

void updateFps() {
  currentTime = SDL_GetTicks();

  if (currentTime - lastTime >= 200) {
    lastTime = currentTime;
    fps = countedFrames * 5;
    countedFrames = 0;
  }

  countedFrames++;
}

void drawFps(SDL_Renderer *renderer) {
  updateFps();

  // 5 digits for fps is enough, surely
  char fpsText[5];

  SDL_itoa(fps, fpsText, 10);

  // Draw white fps text
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

  if (!SDL_RenderDebugText(renderer, 20.0f, 20.0f, fpsText)) {
    SDL_Log("Error drawing fps! %s \n", SDL_GetError());
  }
}
