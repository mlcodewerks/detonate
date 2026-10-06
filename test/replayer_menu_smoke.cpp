#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_sw.hpp"
#include "replayer_settings.h"
#include "libretro.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

retro_audio_sample_batch_t audio_batch_cb = [](const int16_t *, size_t n) { return n; };
retro_audio_sample_t audio_cb = nullptr;
extern void menus_init(float, int, int);
extern void menus_run();
extern void menus_shutdown();
static void check(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void frame()
{
    menus_run();
    std::vector<uint32_t> pixels(1280 * 720);
    imgui_sw::paint_imgui(pixels.data(), ImGui::GetDrawData());
    check(ImGui::GetDrawData()->TotalVtxCount > 0, "menu renders");
}
static void key(ImGuiKey key)
{
    auto &io = ImGui::GetIO();
    io.AddKeyEvent(key, true); frame();
    io.AddKeyEvent(key, false); frame();
}
static void click(ImVec2 pos)
{
    auto &io = ImGui::GetIO();
    io.AddMousePosEvent(pos.x, pos.y); frame();
    io.AddMouseButtonEvent(0, true); frame();
    io.AddMouseButtonEvent(0, false); frame();
}
int main()
{
    const auto config = std::filesystem::current_path() / "replayer-menu-smoke.ini";
    std::filesystem::remove(config);
    std::string error;
    check(replayer_settings::load(config, error), "configure defaults path");
    menus_init(1.5f, 1280, 720);
    imgui_sw::bind_imgui_painting();
    frame(); frame();
    for (unsigned i = 0; i < 4; ++i) key(ImGuiKey_Tab);
    key(ImGuiKey_Enter);
    auto &context = *ImGui::GetCurrentContext();
    check(context.OpenPopupStack.Size == 1 && context.OpenPopupStack[0].Window, "keyboard opens replayer options");
    // With no song loaded, Restart is disabled and Save defaults receives focus.
    key(ImGuiKey_Enter);
    check(std::filesystem::exists(config), "menu saves defaults");
    key(ImGuiKey_Tab); // Restore all defaults
    key(ImGuiKey_Tab); // OpenMPT group
    key(ImGuiKey_RightArrow); // Expand controls
    frame();
    check(context.OpenPopupStack.Size == 1, "engine controls remain in options menu");
    key(ImGuiKey_Tab); // Group restore button
    key(ImGuiKey_Tab); // Interpolation combo
    key(ImGuiKey_Enter);
    key(ImGuiKey_DownArrow);
    key(ImGuiKey_Enter);
    check(replayer_settings::snapshot()[replayer_settings::mpt_interpolation] == 1, "menu changes native interpolation setting");
    const auto popup_position = context.OpenPopupStack[0].Window->Pos;
    click(ImVec2(popup_position.x + 230, popup_position.y + 68));
    replayer_settings::defaults();
    check(replayer_settings::load(config, error) && replayer_settings::snapshot()[replayer_settings::mpt_interpolation] == 1, "menu persists edited setting");
    if (std::getenv("DETONATE_SMOKE_SCREENSHOTS"))
    {
        std::vector<uint32_t> pixels(1280 * 720);
        imgui_sw::paint_imgui(pixels.data(), ImGui::GetDrawData());
        auto *file = std::fopen("replayer-menu.ppm", "wb");
        check(file != nullptr, "menu screenshot opens");
        std::fprintf(file, "P6\n1280 720\n255\n");
        for (auto pixel : pixels)
        {
            const unsigned char rgb[] = {uint8_t(pixel >> 16), uint8_t(pixel >> 8), uint8_t(pixel)};
            std::fwrite(rgb, 1, 3, file);
        }
        std::fclose(file);
    }
    key(ImGuiKey_Escape);
    if (context.OpenPopupStack.Size) key(ImGuiKey_Escape); // First Escape can leave the child navigation scope.
    check(context.OpenPopupStack.Size == 0, "Escape closes options");
    menus_shutdown();
    imgui_sw::unbind_imgui_painting();
    ImGui::DestroyContext();
    std::filesystem::remove(config);
    std::puts("Replayer menu checks passed.");
}
