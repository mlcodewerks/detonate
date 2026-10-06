#include "imgui_shader_windows.h"
#include "shader_visualization.h"
#include "audiodecode.h"
#include "ImGuiColorTextEdit/TextEditor.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

namespace
{
    struct document
    {
        TextEditor editor;
        std::string saved, applied;
        std::filesystem::path directory;
        std::array<char, 2048> path{};
    };
    std::map<std::string, document> documents;
    std::string selected, status, compiler_log, marked_log;
    shader_view::graph project;
    uint64_t revision = 0;
    int preset_index = 0;
    bool show_window = false, show_editor = false, frozen = false, focus_selected = false;
    uint64_t generation = 0;
    ImFont *font = nullptr;
    shader_view::uniforms uniforms;
    uintptr_t texture = 0;
    std::array<char, 2048> open_path{};
    std::string text(const document &d) { return d.editor.GetSectionText({}, {d.editor.GetLineCount(), 0}); }
    void init(document &d, const std::string &source)
    {
        d.editor.SetLanguage(TextEditor::Language::Glsl()); d.editor.SetTabSize(4);
        d.editor.SetInsertSpacesOnTabs(true); d.editor.SetShowWhitespacesEnabled(false);
        d.editor.SetText(source); d.saved = text(d);
    }
    void apply(document &d)
    {
        shader_view::graph next;
        const auto source = text(d);
        d.applied = source; compiler_log.clear(); marked_log.clear(); d.editor.ClearMarkers();
        if (!shader_view::parse(source, d.directory, next, status)) return;
        project = std::move(next); ++revision; uniforms = {}; frozen = false;
        d.applied = source; compiler_log.clear(); marked_log.clear(); d.editor.ClearMarkers();
        status = "Applied. Save to keep your edits.";
    }
    void select_preset(int index)
    {
        preset_index = index;
        selected = std::string("preset:") + shader_view::presets()[index].name;
        focus_selected = true;
        auto [it, fresh] = documents.try_emplace(selected);
        if (fresh) init(it->second, shader_view::presets()[index].source);
        apply(it->second);
    }
    bool read(const std::filesystem::path &path, std::string &source)
    {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in || in.tellg() <= 0 || in.tellg() > 1024 * 1024) { status = "Cannot read shader file (limit 1 MiB)."; return false; }
        source.resize(size_t(in.tellg())); in.seekg(0);
        if (!in.read(source.data(), source.size())) { status = "Cannot read shader file."; return false; }
        if (source.find('\0') != std::string::npos) { status = "Shader source contains NUL bytes."; return false; }
        return true;
    }
    void open_file()
    {
        const auto path = std::filesystem::path(reinterpret_cast<const char8_t *>(open_path.data()));
        std::error_code ec;
        const auto absolute = std::filesystem::absolute(path, ec);
        if (ec) { status = ec.message(); return; }
        const auto utf8 = absolute.lexically_normal().u8string();
        const std::string key(utf8.begin(), utf8.end());
        auto found = documents.find(key);
        if (found == documents.end())
        {
            std::string source; if (!read(absolute, source)) return;
            auto &d = documents[key]; init(d, source); d.directory = absolute.parent_path();
            std::snprintf(d.path.data(), d.path.size(), "%s", key.c_str());
        }
        selected = key; preset_index = -1; show_editor = true; focus_selected = true; apply(documents.at(key));
    }
    void save(document &d)
    {
        if (!d.path[0]) { status = "Enter a file path to save a copy of this preset."; return; }
        const auto path = std::filesystem::path(reinterpret_cast<const char8_t *>(d.path.data()));
        const auto source = text(d);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(source.data(), source.size()); out.close();
        if (!out) { status = "Cannot save shader file."; return; }
        d.saved = source; d.directory = path.parent_path(); status = "Shader saved.";
    }
    void reload(document &d)
    {
        std::string source;
        if (!d.path[0])
        {
            if (preset_index < 0) return;
            source = shader_view::presets()[preset_index].source;
        }
        else if (!read(std::filesystem::path(reinterpret_cast<const char8_t *>(d.path.data())), source)) return;
        init(d, source); apply(d);
    }
    void markers(document &d)
    {
        const auto source = text(d);
        const auto log = source == d.applied ? compiler_log + status : std::string{};
        if (marked_log == log) return;
        marked_log = log; d.editor.ClearMarkers();
        std::istringstream lines(log); std::string line;
        const std::regex diagnostic("(?:\\b\\d+:|Line\\s+)(\\d+)(?:[:(])");
        while (std::getline(lines, line))
        {
            std::smatch match;
            if (std::regex_search(line, match, diagnostic))
            {
                const auto number = std::strtoul(match[1].str().c_str(), nullptr, 10);
                if (number && number <= d.editor.GetLineCount())
                    d.editor.AddMarker(number - 1, IM_COL32(255, 90, 90, 255), IM_COL32(255, 60, 60, 35), line, line);
            }
        }
    }
    void editor_window()
    {
        if (!show_editor) return;
        ImGui::SetNextWindowPos(ImVec2(std::max(20.f, ImGui::GetIO().DisplaySize.x - 740.f), 60), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(720, 570), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Shader editor", &show_editor))
        {
            // Cache drafts by preset/file, including undo history, when changing tabs.
            if (ImGui::BeginTabBar("Shader documents", ImGuiTabBarFlags_TabListPopupButton | ImGuiTabBarFlags_FittingPolicyScroll))
            {
                const auto requested = selected;
                bool requested_visible = false;
                for (auto &[key, d] : documents)
                {
                    const bool dirty = text(d) != d.saved;
                    const auto title = key.starts_with("preset:") ? key.substr(7) : std::filesystem::path(reinterpret_cast<const char8_t *>(key.c_str())).filename().string();
                    ImGuiTabItemFlags flags = dirty ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
                    // Selection changes from the visualization's preset selector are mirrored here.
                    if (focus_selected && key == requested) flags |= ImGuiTabItemFlags_SetSelected;
                    if (ImGui::BeginTabItem((title + "###" + key).c_str(), nullptr, flags))
                    {
                        if (key == requested) requested_visible = true;
                        if (selected != key && (!focus_selected || key == requested)) { selected = key; preset_index = -1; for (size_t i = 0; i < shader_view::presets().size(); ++i) if (key == std::string("preset:") + shader_view::presets()[i].name) preset_index = int(i); apply(d); }
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
                focus_selected = focus_selected && !requested_visible;
            }
            auto &d = documents.at(selected);
            const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            if (ImGui::Button("Apply / compile") || (focused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter))) { apply(d); show_window = true; }
            ImGui::SameLine();
            if (ImGui::Button("Save") || (focused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))) save(d);
            ImGui::SameLine();
            if (ImGui::Button("Reload / reset"))
            {
                if (text(d) != d.saved) ImGui::OpenPopup("Discard edits?"); else reload(d);
            }
            ImGui::SameLine(); ImGui::BeginDisabled(!d.editor.CanUndo()); if (ImGui::Button("Undo")) d.editor.Undo(); ImGui::EndDisabled();
            ImGui::SameLine(); ImGui::BeginDisabled(!d.editor.CanRedo()); if (ImGui::Button("Redo")) d.editor.Redo(); ImGui::EndDisabled();
            if (ImGui::BeginPopupModal("Discard edits?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("Reloading discards this document's unsaved edits.");
                if (ImGui::Button("Discard and reload")) { reload(d); ImGui::CloseCurrentPopup(); }
                ImGui::SameLine(); if (ImGui::Button("Keep editing")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::SetNextItemWidth(-100); ImGui::InputText("Save path", d.path.data(), d.path.size());
            ImGui::SetItemTooltip("Choose a .stoy file path. Saving a built-in preset creates your own copy.");
            const auto cursor = d.editor.GetCursorPosition(0);
            ImGui::Text("Line %zu, column %zu | %zu lines | %s", cursor.line + 1, cursor.index + 1, d.editor.GetLineCount(), text(d) == d.saved ? "Saved" : "Unsaved changes");
            if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
            if (!compiler_log.empty())
            {
                ImGui::TextColored(ImVec4(1, .45f, .35f, 1), "Compilation failed; the last working visualization is still running.");
                ImGui::BeginChild("Compiler log", ImVec2(0, 85), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::TextUnformatted(compiler_log.c_str()); ImGui::EndChild();
            }
            markers(d);
            ImGui::PushFont(font); d.editor.Render("Shader source", ImGui::GetContentRegionAvail()); ImGui::PopFont();
        }
        ImGui::End();
    }
    void visualization_window()
    {
        if (!show_window) return;
        ImGui::SetNextWindowPos(ImVec2(20, 90), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(680, 460), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Shader visualization", &show_window))
        {
            ImGui::SetNextItemWidth(175);
            if (ImGui::BeginCombo("Preset", preset_index >= 0 ? shader_view::presets()[preset_index].name : "Custom shader"))
            {
                for (size_t i = 0; i < shader_view::presets().size(); ++i)
                    if (ImGui::Selectable(shader_view::presets()[i].name, preset_index == int(i))) { preset_index = int(i); select_preset(preset_index); }
                ImGui::EndCombo();
            }
            ImGui::SameLine(); if (ImGui::Button("Edit shader")) show_editor = true;
            ImGui::SameLine(); ImGui::Checkbox("Freeze", &frozen);
            ImGui::SetNextItemWidth(-75); ImGui::InputText("##Open shader path", open_path.data(), open_path.size());
            ImGui::SameLine(); if (ImGui::Button("Open")) open_file();
            ImGui::SetItemTooltip("Open a .stoy project or a plain mainImage GLSL file; image paths are relative to that file.");
            if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
            if (!compiler_log.empty()) ImGui::TextWrapped("%s", compiler_log.c_str());
            if (!shader_view::available())
                ImGui::TextWrapped("Shader visualizations require the OpenGL 3.3 or GLES2 renderer. Start the standalone player with --gl33 or --gles2, or select a hardware renderer in your frontend and restart.");
            else
            {
                if (generation != shader_view::renderer_generation()) { generation = shader_view::renderer_generation(); texture = 0; }
                auto size = ImGui::GetContentRegionAvail(); size.x = std::max(size.x, 64.f); size.y = std::max(size.y, 64.f);
                const auto origin = ImGui::GetCursorScreenPos(); const auto mouse = ImGui::GetIO().MousePos;
                if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && mouse.x >= origin.x && mouse.y >= origin.y)
                {
                    uniforms.mouse[0] = std::clamp(mouse.x - origin.x, 0.f, size.x);
                    uniforms.mouse[1] = std::clamp(size.y - (mouse.y - origin.y), 0.f, size.y);
                    if (ImGui::IsMouseClicked(0)) { uniforms.mouse[2] = std::max(1.f, uniforms.mouse[0]); uniforms.mouse[3] = std::max(1.f, uniforms.mouse[1]); }
                }
                if (!ImGui::IsMouseDown(0)) { uniforms.mouse[2] = -std::abs(uniforms.mouse[2]); uniforms.mouse[3] = -std::abs(uniforms.mouse[3]); }
                if (!frozen || !texture)
                {
                    uniforms.delta = std::min(ImGui::GetIO().DeltaTime, .1f); uniforms.time += uniforms.delta;
                    uniforms.audio_time = music_getposition() / 1000.f;
                    const auto now = std::time(nullptr); const auto *date = std::localtime(&now);
                    if (date) uniforms.date = {float(date->tm_year + 1900), float(date->tm_mon + 1), float(date->tm_mday), float(date->tm_hour * 3600 + date->tm_min * 60 + date->tm_sec)};
                    uintptr_t next = 0;
                    if (shader_view::render(project, revision, shader_view::pack_audio(music_visualization()), uniforms,
                        unsigned(size.x), unsigned(size.y), next, compiler_log)) texture = next;
                    else texture = 0;
                }
                if (texture) ImGui::Image(ImTextureRef(ImTextureID(texture)), size, ImVec2(0, 1), ImVec2(1, 0));
            }
        }
        ImGui::End();
    }
}
void shader_windows_init_font(float scale)
{
    ImFontConfig config; config.SizePixels = 12 * scale;
    font = ImGui::GetIO().Fonts->AddFontDefault(&config);
}
void shader_windows_toggle()
{
    if (ImGui::Shortcut(ImGuiKey_F4, ImGuiInputFlags_RouteGlobal)) show_window = !show_window;
    ImGui::SameLine(); ImGui::Checkbox("Shader window", &show_window);
    ImGui::SetItemTooltip("F4 toggles the movable shader visualization window.");
}
void shader_windows_draw()
{
    if (selected.empty() && (show_window || show_editor)) select_preset(0);
    editor_window(); visualization_window();
}
void shader_windows_shutdown()
{
    documents.clear(); selected.clear(); project = {}; status.clear(); compiler_log.clear(); marked_log.clear();
    revision = 0; preset_index = 0; show_window = show_editor = frozen = focus_selected = false; font = nullptr; uniforms = {}; texture = 0; open_path = {};
}
