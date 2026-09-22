#pragma once
#include <SDL3/SDL.h>
#include "libretro.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>

inline unsigned retro_key(SDL_Keycode key)
{
    if (key < 128) return unsigned(key);
    if (key >= SDLK_F1 && key <= SDLK_F12) return RETROK_F1 + unsigned(key - SDLK_F1);
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) return RETROK_KP1 + unsigned(key - SDLK_KP_1);
    switch (key)
    {
#define KEY(sdl, retro) case SDLK_##sdl: return RETROK_##retro
        KEY(LEFT, LEFT); KEY(RIGHT, RIGHT); KEY(UP, UP); KEY(DOWN, DOWN);
        KEY(HOME, HOME); KEY(END, END); KEY(PAGEUP, PAGEUP); KEY(PAGEDOWN, PAGEDOWN);
        KEY(INSERT, INSERT); KEY(CAPSLOCK, CAPSLOCK); KEY(SCROLLLOCK, SCROLLOCK);
        KEY(NUMLOCKCLEAR, NUMLOCK); KEY(PRINTSCREEN, PRINT); KEY(PAUSE, PAUSE);
        KEY(LCTRL, LCTRL); KEY(RCTRL, RCTRL); KEY(LSHIFT, LSHIFT); KEY(RSHIFT, RSHIFT);
        KEY(LALT, LALT); KEY(RALT, RALT); KEY(LGUI, LSUPER); KEY(RGUI, RSUPER);
        KEY(APPLICATION, MENU); KEY(KP_0, KP0); KEY(KP_PERIOD, KP_PERIOD);
        KEY(KP_DIVIDE, KP_DIVIDE); KEY(KP_MULTIPLY, KP_MULTIPLY);
        KEY(KP_MINUS, KP_MINUS); KEY(KP_PLUS, KP_PLUS); KEY(KP_ENTER, KP_ENTER); KEY(KP_EQUALS, KP_EQUALS);
#undef KEY
        default: return RETROK_UNKNOWN;
    }
}
inline uint16_t retro_modifiers(SDL_Keymod mod)
{
    return ((mod & SDL_KMOD_SHIFT) ? RETROKMOD_SHIFT : 0) |
           ((mod & SDL_KMOD_CTRL) ? RETROKMOD_CTRL : 0) |
           ((mod & SDL_KMOD_ALT) ? RETROKMOD_ALT : 0) |
           ((mod & SDL_KMOD_GUI) ? RETROKMOD_META : 0) |
           ((mod & SDL_KMOD_CAPS) ? RETROKMOD_CAPSLOCK : 0) |
           ((mod & SDL_KMOD_NUM) ? RETROKMOD_NUMLOCK : 0);
}


struct mouse_input
{
    struct event { int x, y, button; bool down; };
    std::deque<event> pending;
    std::array<bool, 3> buttons{};
    int width = 1280, height = 720, x = 640, y = 360, dx = 0, dy = 0, wheel = 0;
    float pending_wheel = 0;
    void reset(int w, int h)
    {
        pending.clear(); buttons.fill(false);
        width = w; height = h; x = w / 2; y = h / 2;
        dx = dy = wheel = 0; pending_wheel = 0;
    }
    void push(float px, float py, int button = -1, bool down = false)
    {
        if (!std::isfinite(px) || !std::isfinite(py)) return;
        const bool outside = px < 0 || py < 0 || px >= width || py >= height;
        if (outside && button >= 0 && down) return; // Letterbox clicks are not UI clicks.
        event next{int(std::clamp(px, 0.0f, float(width - 1))), int(std::clamp(py, 0.0f, float(height - 1))), button, down};
        if (button < 0 && !pending.empty() && pending.back().button < 0) pending.back() = next;
        else pending.push_back(next);
    }
    void release()
    {
        pending.clear(); buttons.fill(false); pending_wheel = 0;
    }
    void poll()
    {
        const int old_x = x, old_y = y;
        while (!pending.empty())
        {
            const auto next = pending.front(); pending.pop_front();
            x = next.x; y = next.y;
            if (next.button >= 0 && next.button < 3)
            {
                buttons[next.button] = next.down;
                break;
            }
        }
        dx = x - old_x; dy = y - old_y;
        wheel = int(std::clamp(pending_wheel, -32.0f, 32.0f));
        pending_wheel -= float(wheel);
    }
    int16_t state(unsigned id) const
    {
        switch (id)
        {
            case RETRO_DEVICE_ID_MOUSE_X: return int16_t(dx);
            case RETRO_DEVICE_ID_MOUSE_Y: return int16_t(dy);
            case RETRO_DEVICE_ID_MOUSE_LEFT: return buttons[0];
            case RETRO_DEVICE_ID_MOUSE_RIGHT: return buttons[1];
            case RETRO_DEVICE_ID_MOUSE_MIDDLE: return buttons[2];
            case RETRO_DEVICE_ID_MOUSE_WHEELUP: return int16_t(std::max(wheel, 0));
            case RETRO_DEVICE_ID_MOUSE_WHEELDOWN: return int16_t(std::max(-wheel, 0));
            default: return 0;
        }
    }
};
