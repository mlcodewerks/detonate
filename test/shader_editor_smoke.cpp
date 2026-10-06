// Exercise the real editor widgets and document lifecycle without a GL context.
#include "imgui_internal.h"
#include "imgui_sw.hpp"
#include "libretro.h"
#include "../src/player/imgui_shader_windows.cpp"
#include <cstdlib>

retro_audio_sample_batch_t audio_batch_cb = nullptr;
retro_audio_sample_t audio_cb = nullptr;
static void check(bool ok, const char *message)
{ if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); } }
static uint64_t rendered_revision;
static bool fake_render(const shader_view::graph &g, uint64_t rev,
    const shader_view::audio_textures &, const shader_view::uniforms &,
    unsigned w, unsigned h, uintptr_t &, std::string &error)
{
    check(!g.passes.empty() && w >= 64 && h >= 64, "editor submits a valid render graph");
    rendered_revision = rev; error = "0:7(3): deliberate compiler diagnostic";
    return false;
}
static void frame()
{
    ImGui::NewFrame(); ImGui::Begin("Controls"); ImGui::TextUnformatted("Visualization");
    shader_windows_toggle(); ImGui::End(); shader_windows_draw(); ImGui::Render();
    std::vector<uint32_t> pixels(1280 * 720);
    imgui_sw::paint_imgui(pixels.data(), ImGui::GetDrawData());
}
static void key(ImGuiKey k)
{
    ImGui::GetIO().AddKeyEvent(k, true); frame();
    ImGui::GetIO().AddKeyEvent(k, false); frame();
}
int main()
{
    ImGui::CreateContext();
    auto &io = ImGui::GetIO(); io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720); io.DeltaTime = 1.f / 60;
    shader_windows_init_font(1.5f); imgui_sw::bind_imgui_painting();
    frame(); key(ImGuiKey_F4);
    check(show_window && !selected.empty(), "F4 opens shader window in software mode");
    show_editor = true; frame(); frame();
    check(ImGui::FindWindowByName("Shader editor")->Active, "built-in editor opens");
    if (std::getenv("DETONATE_SMOKE_SCREENSHOTS"))
    {
        std::vector<uint32_t> pixels(1280 * 720);
        imgui_sw::paint_imgui(pixels.data(), ImGui::GetDrawData());
        std::ofstream out("shader-editor.ppm", std::ios::binary); out << "P6\n1280 720\n255\n";
        for (auto pixel : pixels) { const char rgb[] = {char(pixel >> 16), char(pixel >> 8), char(pixel)}; out.write(rgb, 3); }
    }
    const auto first = selected;
    auto &d = documents.at(first);
    // Select source with the real editor, focus its child, and type through ImGui.
    d.editor.SelectAll();
    auto *child = ImGui::FindWindowByName("Shader editor");
    for (auto *w : ImGui::GetCurrentContext()->Windows)
        if (w->ParentWindow == child && w->Name && std::string(w->Name).find("Shader source") != std::string::npos) child = w;
    check(child->ParentWindow != nullptr, "source editor child exists");
    ImGui::FocusWindow(child);
    const std::string replacement = "void mainImage(out vec4 c,in vec2 p){c=vec4(0.25,0.5,0.75,1.0);}";
    io.AddInputCharactersUTF8(replacement.c_str()); frame(); frame();
    check(text(d).find("0.25,0.5,0.75") != std::string::npos && d.editor.CanUndo(), "typing changes source and records undo");
    const auto draft = text(d);
    d.editor.Undo(); check(text(d) != draft && d.editor.CanRedo(), "undo restores prior source");
    d.editor.Redo(); check(text(d) == draft, "redo restores edited source");
    select_preset(1); frame(); frame();
    check(selected != first && preset_index == 1, "programmatic preset selection updates active editor tab");
    select_preset(0); frame(); frame();
    check(selected == first && text(documents.at(first)) == draft, "returning to preset retains draft");
    shader_view::set_renderer(fake_render); apply(d); frame(); frame();
    check(rendered_revision == revision && !compiler_log.empty(), "apply submits edits and shows compiler diagnostics");
    const auto good_revision = revision;
    d.editor.SetText("stoy 99\n"); apply(d); frame();
    check(revision == good_revision && !status.empty(), "parser failure retains current graph");
    d.editor.SetText(draft); apply(d);
    const auto path = std::filesystem::current_path() / "shader-editor-owned.stoy";
    std::snprintf(d.path.data(), d.path.size(), "%s", path.string().c_str()); save(d);
    check(std::filesystem::exists(path) && d.saved == text(d), "save writes editor source");
    d.editor.SetText("// unsaved\n"); reload(d);
    check(text(d) == draft, "reload restores saved source");
    std::snprintf(open_path.data(), open_path.size(), "%s", path.string().c_str()); open_file(); frame(); frame();
    check(preset_index == -1 && text(documents.at(selected)) == draft, "open file creates selected document tab");
    key(ImGuiKey_F4); check(!show_window, "F4 closes shader window while editing");
    shader_view::set_renderer(nullptr); shader_windows_shutdown();
    imgui_sw::unbind_imgui_painting(); ImGui::DestroyContext(); std::filesystem::remove(path);
    std::puts("Shader editor: typing, undo/redo, tabs, apply, diagnostics, save/reload and software fallback passed");
}
