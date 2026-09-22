#include "imgui_libretro.h"

static ImGuiKey ImGui_ImplLibretro_KeyToImGuiKey(int key)
{
    switch (key)
    {
    case RETROK_TAB:
        return ImGuiKey_Tab;
    case RETROK_LEFT:
        return ImGuiKey_LeftArrow;
    case RETROK_RIGHT:
        return ImGuiKey_RightArrow;
    case RETROK_UP:
        return ImGuiKey_UpArrow;
    case RETROK_DOWN:
        return ImGuiKey_DownArrow;
    case RETROK_PAGEUP:
        return ImGuiKey_PageUp;
    case RETROK_PAGEDOWN:
        return ImGuiKey_PageDown;
    case RETROK_HOME:
        return ImGuiKey_Home;
    case RETROK_END:
        return ImGuiKey_End;
    case RETROK_INSERT:
        return ImGuiKey_Insert;
    case RETROK_DELETE:
        return ImGuiKey_Delete;
    case RETROK_BACKSPACE:
        return ImGuiKey_Backspace;
    case RETROK_SPACE:
        return ImGuiKey_Space;
    case RETROK_RETURN:
        return ImGuiKey_Enter;
    case RETROK_ESCAPE:
        return ImGuiKey_Escape;
    case RETROK_QUOTE:
        return ImGuiKey_Apostrophe;
    case RETROK_COMMA:
        return ImGuiKey_Comma;
    case RETROK_MINUS:
        return ImGuiKey_Minus;
    case RETROK_PERIOD:
        return ImGuiKey_Period;
    case RETROK_SLASH:
        return ImGuiKey_Slash;
    case RETROK_SEMICOLON:
        return ImGuiKey_Semicolon;
    case RETROK_EQUALS:
        return ImGuiKey_Equal;
    case RETROK_LEFTBRACKET:
        return ImGuiKey_LeftBracket;
    case RETROK_BACKSLASH:
        return ImGuiKey_Backslash;
    case RETROK_RIGHTBRACKET:
        return ImGuiKey_RightBracket;
    case RETROK_BACKQUOTE:
        return ImGuiKey_GraveAccent;
    case RETROK_CAPSLOCK:
        return ImGuiKey_CapsLock;
    case RETROK_SCROLLOCK:
        return ImGuiKey_ScrollLock;
    case RETROK_NUMLOCK:
        return ImGuiKey_NumLock;
    case RETROK_PRINT:
        return ImGuiKey_PrintScreen;
    case RETROK_PAUSE:
        return ImGuiKey_Pause;
    case RETROK_KP0:
        return ImGuiKey_Keypad0;
    case RETROK_KP1:
        return ImGuiKey_Keypad1;
    case RETROK_KP2:
        return ImGuiKey_Keypad2;
    case RETROK_KP3:
        return ImGuiKey_Keypad3;
    case RETROK_KP4:
        return ImGuiKey_Keypad4;
    case RETROK_KP5:
        return ImGuiKey_Keypad5;
    case RETROK_KP6:
        return ImGuiKey_Keypad6;
    case RETROK_KP7:
        return ImGuiKey_Keypad7;
    case RETROK_KP8:
        return ImGuiKey_Keypad8;
    case RETROK_KP9:
        return ImGuiKey_Keypad9;
    case RETROK_KP_PERIOD:
        return ImGuiKey_KeypadDecimal;
    case RETROK_KP_DIVIDE:
        return ImGuiKey_KeypadDivide;
    case RETROK_KP_MULTIPLY:
        return ImGuiKey_KeypadMultiply;
    case RETROK_KP_MINUS:
        return ImGuiKey_KeypadSubtract;
    case RETROK_KP_PLUS:
        return ImGuiKey_KeypadAdd;
    case RETROK_KP_ENTER:
        return ImGuiKey_KeypadEnter;
    case RETROK_KP_EQUALS:
        return ImGuiKey_KeypadEqual;
    case RETROK_LCTRL:
        return ImGuiKey_LeftCtrl;
    case RETROK_LSHIFT:
        return ImGuiKey_LeftShift;
    case RETROK_LALT:
        return ImGuiKey_LeftAlt;
    case RETROK_LSUPER:
    case RETROK_LMETA:
        return ImGuiKey_LeftSuper;
    case RETROK_RCTRL:
        return ImGuiKey_RightCtrl;
    case RETROK_RSHIFT:
        return ImGuiKey_RightShift;
    case RETROK_RALT:
        return ImGuiKey_RightAlt;
    case RETROK_RSUPER:
    case RETROK_RMETA:
        return ImGuiKey_RightSuper;
    case RETROK_MENU:
        return ImGuiKey_Menu;
    case RETROK_0:
        return ImGuiKey_0;
    case RETROK_1:
        return ImGuiKey_1;
    case RETROK_2:
        return ImGuiKey_2;
    case RETROK_3:
        return ImGuiKey_3;
    case RETROK_4:
        return ImGuiKey_4;
    case RETROK_5:
        return ImGuiKey_5;
    case RETROK_6:
        return ImGuiKey_6;
    case RETROK_7:
        return ImGuiKey_7;
    case RETROK_8:
        return ImGuiKey_8;
    case RETROK_9:
        return ImGuiKey_9;
    case RETROK_a:
        return ImGuiKey_A;
    case RETROK_b:
        return ImGuiKey_B;
    case RETROK_c:
        return ImGuiKey_C;
    case RETROK_d:
        return ImGuiKey_D;
    case RETROK_e:
        return ImGuiKey_E;
    case RETROK_f:
        return ImGuiKey_F;
    case RETROK_g:
        return ImGuiKey_G;
    case RETROK_h:
        return ImGuiKey_H;
    case RETROK_i:
        return ImGuiKey_I;
    case RETROK_j:
        return ImGuiKey_J;
    case RETROK_k:
        return ImGuiKey_K;
    case RETROK_l:
        return ImGuiKey_L;
    case RETROK_m:
        return ImGuiKey_M;
    case RETROK_n:
        return ImGuiKey_N;
    case RETROK_o:
        return ImGuiKey_O;
    case RETROK_p:
        return ImGuiKey_P;
    case RETROK_q:
        return ImGuiKey_Q;
    case RETROK_r:
        return ImGuiKey_R;
    case RETROK_s:
        return ImGuiKey_S;
    case RETROK_t:
        return ImGuiKey_T;
    case RETROK_u:
        return ImGuiKey_U;
    case RETROK_v:
        return ImGuiKey_V;
    case RETROK_w:
        return ImGuiKey_W;
    case RETROK_x:
        return ImGuiKey_X;
    case RETROK_y:
        return ImGuiKey_Y;
    case RETROK_z:
        return ImGuiKey_Z;
    case RETROK_F1:
        return ImGuiKey_F1;
    case RETROK_F2:
        return ImGuiKey_F2;
    case RETROK_F3:
        return ImGuiKey_F3;
    case RETROK_F4:
        return ImGuiKey_F4;
    case RETROK_F5:
        return ImGuiKey_F5;
    case RETROK_F6:
        return ImGuiKey_F6;
    case RETROK_F7:
        return ImGuiKey_F7;
    case RETROK_F8:
        return ImGuiKey_F8;
    case RETROK_F9:
        return ImGuiKey_F9;
    case RETROK_F10:
        return ImGuiKey_F10;
    case RETROK_F11:
        return ImGuiKey_F11;
    case RETROK_F12:
        return ImGuiKey_F12;
    case RETROK_UNKNOWN:
        return ImGuiKey_None;
    default:
        return ImGuiKey_None;
    }
}

void ImGui_ImpLibretro_ProcessKeys(bool down, unsigned keycode,
                                   uint32_t character, uint16_t key_modifiers)
{
    if (!ImGui::GetCurrentContext())
        return;
    ImGuiIO &io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, (key_modifiers & RETROKMOD_CTRL) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (key_modifiers & RETROKMOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (key_modifiers & RETROKMOD_ALT) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (key_modifiers & RETROKMOD_META) != 0);
    const ImGuiKey key = ImGui_ImplLibretro_KeyToImGuiKey(keycode);
    if (key != ImGuiKey_None)
        io.AddKeyEvent(key, down);
    if (down && character && character <= 0x10FFFF &&
        !(character >= 0xD800 && character <= 0xDFFF))
        io.AddInputCharacter(character);
}

void ImGui_ImplLibretro_ProcessMouse(int mouse_button, bool pressed, float x, float y)
{
    if (!ImGui::GetCurrentContext())
        return;
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent((float)x, (float)y);
    if (mouse_button >= 0 && mouse_button < 5)
        io.AddMouseButtonEvent(mouse_button, pressed);
}

void ImGui_ImplLibretro_ProcessMW(float mousewh)
{
    if (!ImGui::GetCurrentContext())
        return;
    ImGuiIO &io = ImGui::GetIO();
    if (mousewh != 0)
        io.AddMouseWheelEvent(0.0f, mousewh);
}

bool ImGui_ImplLibretro_Init()
{
    if (!ImGui::GetCurrentContext())
        return false;
    ImGuiIO &io = ImGui::GetIO();
    io.BackendPlatformName = "imgui_impl_libretro";
    io.MouseDrawCursor = true;
    return true;
}

void ImGui_ImplLibretro_Shutdown()
{
    if (!ImGui::GetCurrentContext())
        return;
    ImGuiIO &io = ImGui::GetIO();
    io.BackendPlatformName = nullptr;
}

void ImGui_ImplLibretro_NewFrame()
{
    // Setup time step
    if (!ImGui::GetCurrentContext())
        return;
    ImGuiIO &io = ImGui::GetIO();
    io.DeltaTime = 1 / 60.;
}
