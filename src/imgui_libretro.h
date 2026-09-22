#pragma once
#include "libretro.h"
#include "imgui.h"
#include <cstdint>
void ImGui_ImpLibretro_ProcessKeys(bool down, unsigned keycode, uint32_t character, uint16_t modifiers);
void ImGui_ImplLibretro_ProcessMouse(int button, bool pressed, float x, float y);
void ImGui_ImplLibretro_ProcessMW(float wheel);
bool ImGui_ImplLibretro_Init();
void ImGui_ImplLibretro_Shutdown();
void ImGui_ImplLibretro_NewFrame();
