#include <SDL3/SDL_main.h>
#include "standalone/sdl_input.h"
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char *what)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
int main(int, char **)
{
    mouse_input mouse;
    mouse.push(100, 200, 0, true);
    mouse.push(100, 200, 0, false);
    mouse.push(300, 400);
    mouse.poll();
    check(mouse.state(RETRO_DEVICE_ID_MOUSE_LEFT) && mouse.dx == -540 && mouse.dy == -160,
          "quick press survives until a core frame at the click position");
    mouse.poll();
    check(!mouse.state(RETRO_DEVICE_ID_MOUSE_LEFT) && mouse.dx == 0 && mouse.dy == 0,
          "release is delivered separately at the same position");
    mouse.poll();
    check(mouse.dx == 200 && mouse.dy == 200, "motion follows queued click");
    mouse.poll(); check(mouse.dx == 0 && mouse.dy == 0, "relative deltas do not repeat");
    mouse.push(-30, 20, 0, true); mouse.poll();
    check(!mouse.buttons[0], "letterbox press is ignored");
    mouse.push(30, 20, 0, true); mouse.poll();
    mouse.push(-30, 20, 0, false); mouse.poll();
    check(!mouse.buttons[0] && mouse.x == 0, "release outside window ends drag");
    mouse.push(30, 20, 0, true); mouse.poll();
    mouse.release(); mouse.poll(); check(!mouse.buttons[0], "focus loss releases mouse");
    mouse.pending_wheel = 0.5f; mouse.poll(); check(mouse.wheel == 0, "fractional wheel retained");
    mouse.pending_wheel += 0.5f; mouse.poll(); check(mouse.wheel == 1, "fractional wheel accumulates");
    mouse.poll(); check(mouse.wheel == 0, "wheel consumed once");
    mouse.reset(1280, 720); check(mouse.x == 640 && mouse.y == 360 && mouse.pending.empty(), "reload resets core cursor origin");
    check(retro_key(SDLK_V) == RETROK_v && retro_key(SDLK_KP_ENTER) == RETROK_KP_ENTER &&
          retro_key(SDLK_LEFT) == RETROK_LEFT && retro_key(SDLK_F5) == RETROK_F5 &&
          retro_key(SDLK_LCTRL) == RETROK_LCTRL, "keyboard mappings");
    check(retro_modifiers(SDL_KMOD_CTRL | SDL_KMOD_SHIFT) == (RETROKMOD_CTRL | RETROKMOD_SHIFT), "modifier mapping");
    check(SDL_Init(SDL_INIT_VIDEO), "initialize dummy video");
    auto *window = SDL_CreateWindow("input test", 1600, 1000, SDL_WINDOW_HIDDEN);
    check(window != nullptr, "create resized window");
    auto *renderer = SDL_CreateRenderer(window, "software");
    check(renderer && SDL_SetRenderLogicalPresentation(renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX), "set letterboxed UI");
    float x = 0, y = 0;
    check(SDL_RenderCoordinatesFromWindow(renderer, 800, 500, &x, &y) && std::abs(x - 640) < 0.1f && std::abs(y - 360) < 0.1f,
          "window center maps to core center after resizing");
    check(SDL_RenderCoordinatesFromWindow(renderer, 800, 0, &x, &y) && y < 0, "letterbox stays outside core coordinates");
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    std::puts("SDL input mapping and queued click checks passed.");
    return 0;
}
