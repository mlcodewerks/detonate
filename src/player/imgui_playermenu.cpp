#include "imgui.h"
#include "imgui_font.h"
#include "forkawesome.h"
#include "IconsForkAwesome.h"
#include "audiodecode.h"
#include "visualization.h"
#include "file_browser.h"
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#include <future>
#include <functional>
#include <chrono>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace fs = std::filesystem;
namespace
{
    file_browser browser;
    directory_playlist playlist;
    void play_item(const playlist_item &item)
    {
        if (item.members)
            music_play_archive_async(item.members, item.member, item.track);
        else
            music_play_async(item.source.empty() ? item.key : item.source, item.track);
    }
    struct navigation_result
    {
        bool content = false, success = true;
        std::string audio, error;
        uint64_t generation = 0;
        std::vector<playlist_item> songs;
    };
    using navigation = std::function<navigation_result()>;
    std::future<navigation_result> browser_job;
    navigation pending_navigation;
    std::string browser_job_error;
    int content_status = 1;
    uint64_t content_generation = 0;
    bool content_audio = false;
    void launch_navigation(navigation work)
    {
        browser_job_error.clear();
        try
        {
            browser_job = std::async(std::launch::async, std::move(work));
        }
        catch (const std::exception &e)
        {
            browser_job_error = e.what();
            content_status = -1;
        }
    }
    void request_navigation(navigation work)
    {
        if (browser_job.valid())
            pending_navigation = std::move(work);
        else
            launch_navigation(std::move(work));
    }
    void finish_browser_job()
    {
        if (!browser_job.valid())
            return;
        navigation_result result;
        try
        {
            result = browser_job.get();
        }
        catch (const std::exception &e)
        {
            result.success = false;
            result.error = e.what();
        }
        if (pending_navigation)
        {
            auto next = std::move(pending_navigation);
            pending_navigation = {};
            launch_navigation(std::move(next));
            return; // An obsolete request must never start its song.
        }
        if (result.content && result.generation != content_generation)
            return;
        browser_job_error = std::move(result.error);
        if (result.content || !result.success)
        {
            content_status = result.success ? 1 : -1;
            if (result.success && !result.audio.empty())
            {
                if (const auto *item = playlist.start(std::move(result.songs), result.audio))
                    play_item(*item);
                else
                    music_play_async(std::move(result.audio));
                content_audio = true;
                content_status = 0;
            }
        }
    }
    std::string selected, selected_location;
    int visualization = 1;
    void draw_visualization()
    {
        if (ImGui::Shortcut(ImGuiKey_V))
            visualization = (visualization + 1) % 3;
        ImGui::SetNextItemWidth(180.0f);
        ImGui::Combo("Visualization", &visualization, "Off\0Oscilloscope\0Spectrum bars\0");
        ImGui::SetItemTooltip("Press V to cycle views.");
        if (!visualization)
            return;

        const auto &data = music_visualization();
        const float height = std::clamp(ImGui::GetIO().DisplaySize.y * 0.25f, 100.0f, 200.0f);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(std::max(1.0f, ImGui::GetContentRegionAvail().x), height);
        ImGui::InvisibleButton("##visualization", size);
        auto *draw = ImGui::GetWindowDrawList();
        const ImVec2 end(origin.x + size.x, origin.y + size.y);
        draw->AddRectFilled(origin, end, IM_COL32(12, 18, 26, 255), 4.0f);
        draw->PushClipRect(origin, end, true);
        const float left = origin.x + 30.0f, right = end.x - 10.0f;
        const float top = origin.y + 10.0f, bottom = end.y - 24.0f;
        const ImU32 grid = IM_COL32(40, 53, 65, 255);
        for (int i = 0; i <= 4; ++i)
        {
            float y = top + (bottom - top) * i / 4.0f;
            draw->AddLine(ImVec2(left, y), ImVec2(right, y), grid);
        }
        if (visualization == 1)
        {
            const ImU32 colors[] = {IM_COL32(69, 221, 187, 255), IM_COL32(100, 171, 255, 255)};
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto &wave = channel ? data.right : data.left;
                const float center = top + (bottom - top) * (channel ? 0.75f : 0.25f);
                draw->AddText(ImVec2(origin.x + 8, center - 8), colors[channel], channel ? "R" : "L");
                std::array<ImVec2, visualization_data::waveform_size> points;
                for (size_t i = 0; i < wave.size(); ++i)
                    points[i] = ImVec2(left + (right - left) * float(i) / float(wave.size() - 1),
                                       center - wave[i] * (bottom - top) * 0.23f);
                draw->AddPolyline(points.data(), int(points.size()), colors[channel], ImDrawFlags_None, 1.5f);
            }
            draw->AddText(ImVec2(left, bottom + 4), IM_COL32(155, 169, 182, 255), "Stereo waveform - 11.6 ms");
        }
        else
        {
            const float step = (right - left) / data.spectrum.size();
            for (size_t i = 0; i < data.spectrum.size(); ++i)
            {
                const float x = left + float(i) * step;
                const float y = bottom - data.spectrum[i] * (bottom - top);
                draw->AddRectFilledMultiColor(ImVec2(x + 1, y), ImVec2(x + step - 2, bottom),
                                              IM_COL32(90, 228, 183, 255), IM_COL32(90, 228, 183, 255),
                                              IM_COL32(43, 105, 182, 255), IM_COL32(43, 105, 182, 255));
            }
            draw->AddText(ImVec2(left, bottom + 4), IM_COL32(155, 169, 182, 255), "22 Hz");
            const char *label = "20 kHz | -60 to 0 dBFS";
            draw->AddText(ImVec2(right - ImGui::CalcTextSize(label).x, bottom + 4), IM_COL32(155, 169, 182, 255), label);
        }
        draw->PopClipRect();
    }
    std::string path_text(const fs::path &p)
    {
        const auto utf8 = p.u8string();
        return std::string(utf8.begin(), utf8.end());
    }
    std::string time_text(unsigned ms)
    {
        unsigned seconds = ms / 1000;
        char text[40];
        std::snprintf(text, sizeof(text), "%u:%02u", seconds / 60, seconds % 60);
        return text;
    }
}
void menus_poll()
{
    music_poll();
    if (const auto *next = playlist.poll(music_loading(), music_isplaying(), music_ispaused(), !music_error().empty()))
        play_item(*next);
    if (browser_job.valid() && browser_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        finish_browser_job();
    if (content_audio && !music_loading())
    {
        content_audio = false;
        content_status = music_error().empty() ? 1 : -1;
    }
}
void menu_request_content(const char *path)
{
    std::string name = path ? path : "";
    selected = name;
    selected_location.clear();
    content_status = 0;
    content_audio = false;
    playlist.stop();
    music_stop_async();
    request_navigation([name = std::move(name), generation = ++content_generation]
                       {
        navigation_result result;
        result.content = true;
        result.generation = generation;
        const fs::path target = name.empty() ? fs::current_path() : fs::path(reinterpret_cast<const char8_t *>(name.c_str()));
        result.success = browser.open(target);
        result.error = browser.error();
        if (result.success && !browser.in_subsongs() && !name.empty() && !fs::is_directory(target) &&
            (!is_music_archive(name) || auddecode_supports(name.c_str()))) {
            result.audio = path_text(fs::absolute(target).lexically_normal());
            result.songs = browser.playlist();
        }
        return result; });
}
int menu_load_status()
{
    menus_poll();
    return content_status;
}
void menus_wait()
{
    while (browser_job.valid())
        finish_browser_job();
    music_wait();
    menus_poll();
}
void menus_shutdown()
{
    playlist.stop();
    ++content_generation;
    pending_navigation = {};
    while (browser_job.valid())
        finish_browser_job();
    music_wait();
    browser = file_browser{};
    content_audio = false;
    content_status = 1;
}
void menus_init(float scale, int width, int height)
{
    menus_wait();
    browser_job_error.clear();
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize = ImVec2(float(width), float(height));
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void *)Roboto_Regular, sizeof(Roboto_Regular), scale * 12.0f, &config);
    static const ImWchar ranges[] = {ICON_MIN_FK, ICON_MAX_FK, 0};
    config.MergeMode = true;
    config.GlyphMinAdvanceX = scale * 12.0f;
    io.Fonts->AddFontFromMemoryCompressedTTF(forkawesome_compressed_data, forkawesome_compressed_size, scale * 12.0f, &config, ranges);
    ImGui::StyleColorsDark();
    ImGui::GetStyle().FrameRounding = 3.0f;
    ImGui::GetStyle().ScaleAllSizes(scale);
    selected.clear();
    selected_location.clear();
    browser = file_browser{};

    playlist.stop();
    music_repeat(playlist.playback == directory_playlist::mode::repeat_song);
}
void menus_run()
{
    menus_poll();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Detonate", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::Button(music_ispaused() ? ICON_FK_PLAY " Resume" : ICON_FK_PAUSE " Pause"))
        music_pause(!music_ispaused());
    ImGui::SameLine();
    if (ImGui::Button(ICON_FK_STOP " Stop"))
    {
        ++content_generation;
        content_audio = false;
        content_status = 1;
        playlist.stop();
        music_stop_async();
    }
    ImGui::SameLine();
    int mode = int(playlist.playback);
    if (playlist.shuffled() && mode >= 2)
        mode += 2;
    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::Combo("Playback", &mode, "Play song once\0Repeat song\0Play directory once\0Repeat directory\0Shuffle directory once\0Repeat shuffled directory\0"))
    {
        playlist.playback = directory_playlist::mode(mode >= 4 ? mode - 2 : mode);
        if (playlist.shuffled() != (mode >= 4))
            playlist.shuffle(mode >= 4);
        music_repeat(playlist.playback == directory_playlist::mode::repeat_song);
    }
    ImGui::SetItemTooltip("Directory playback uses the folder where the song was opened. Play once stops after the last song.\nShuffle plays remaining songs without duplicates; repeat reshuffles each new pass.\nRepeat song follows native loops where available.");
    ImGui::BeginDisabled(music_loading());
    const unsigned duration = music_getduration();
    if (music_islooping() || !duration)
    {
        ImGui::SameLine();
        if (ImGui::Button("Restart"))
            music_setposition_async(0);
        ImGui::SameLine();
        ImGui::Text("%s%s", time_text(music_getposition()).c_str(), music_islooping() ? " (looping)" : "");
    }
    else
    {
        ImGui::SameLine();
        static unsigned position = 0;
        static bool seeking = false;
        if (!seeking)
            position = music_getposition();
        const unsigned minimum = 0;
        ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - 200.0f));
        if (ImGui::SliderScalar("##position", ImGuiDataType_U32, &position, &minimum, &duration, ""))
            seeking = true;
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            music_setposition_async(position);
            seeking = false;
        }
        else if (!ImGui::IsItemActive())
            seeking = false;
        ImGui::SameLine();
        ImGui::Text("%s / %s", time_text(position).c_str(), time_text(duration).c_str());
    }
    const auto title = music_title();
    if (!title.empty())
        ImGui::TextUnformatted(title.c_str());
    else if (!selected.empty())
        ImGui::TextUnformatted(selected.c_str());
    const unsigned tracks = music_trackcount();
    if (tracks > 1)
    {
        const unsigned current = music_currenttrack();
        ImGui::BeginDisabled(current == 0);
        if (ImGui::Button("Previous track"))
            music_settrack_async(current - 1);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(current + 1 >= tracks);
        if (ImGui::Button("Next track"))
            music_settrack_async(current + 1);
        ImGui::EndDisabled();
    }
    ImGui::EndDisabled();
    if (music_loading())
        ImGui::TextUnformatted("Loading audio...");
    if (!music_error().empty())
        ImGui::TextWrapped("%s", music_error().c_str());
    draw_visualization();
    ImGui::Separator();
    // Only the worker accesses browser while a navigation job is outstanding.
    if (browser_job.valid())
    {
        ImGui::TextUnformatted("Loading browser...");
        ImGui::End();
        ImGui::Render();
        return;
    }
    if (!browser_job_error.empty())
        ImGui::TextWrapped("%s", browser_job_error.c_str());
    std::function<void()> navigate;
    if (ImGui::Button(ICON_FK_FOLDER " Up"))
        navigate = []
        { browser.up(); };
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        navigate = []
        { browser.refresh(); };
#ifdef _WIN32
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::BeginCombo("##drive", path_text(browser.directory().root_name()).c_str()))
    {
        const DWORD mask = GetLogicalDrives();
        for (unsigned i = 0; i < 26; ++i)
            if (mask & (1u << i))
            {
                char root[] = {char('A' + i), ':', '/', 0};
                if (ImGui::Selectable(root))
                    navigate = [path = fs::path(root)]
                    { browser.open(path); };
            }
        ImGui::EndCombo();
    }
#endif
    ImGui::SameLine();
    ImGui::TextUnformatted(browser.location().c_str());
    if (!browser.error().empty())
        ImGui::TextWrapped("%s", browser.error().c_str());
    int activate = -1;
    const auto location = browser.location();
    const auto &files = browser.entries();
    if (ImGui::BeginChild("browser", ImVec2(0, 0), ImGuiChildFlags_Borders))
    {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(files.size()));
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const auto &file = files[i];
                ImGui::PushID(i);
                const auto label = std::string(file.directory || file.archive ? ICON_FK_FOLDER " " : ICON_FK_MUSIC " ") + file.name;
                if (ImGui::Selectable(label.c_str(), selected_location == location + "/" + file.key, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    selected = file.name;
                    selected_location = location + "/" + file.key;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter))
                    {
                        activate = i;
                    }
                }
                if (file.track >= 0 && ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(file.tags.title.c_str());
                    if (!file.tags.artist.empty())
                        ImGui::Text("Artist: %s", file.tags.artist.c_str());
                    if (!file.tags.album.empty())
                        ImGui::Text("Album: %s", file.tags.album.c_str());
                    if (file.tags.duration)
                        ImGui::Text("Duration: %u:%02u", file.tags.duration / 60000, file.tags.duration / 1000 % 60);
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
    }
    ImGui::EndChild();
    ImGui::End();
    ImGui::Render();
    if (activate >= 0)
    {
        if (files[activate].directory || files[activate].archive)
            navigate = [activate]
            { browser.activate(size_t(activate)); };
        else
        {
            ++content_generation;
            content_audio = true;
            content_status = 0;
            if (const auto *item = playlist.start(browser.playlist(), files[activate].key))
                play_item(*item);
        }
    }
    if (navigate)
    {
        request_navigation([navigate = std::move(navigate)]
                           {
            navigate();
            return navigation_result{false, browser.error().empty(), {}, browser.error(), 0, {}}; });
    }
}
