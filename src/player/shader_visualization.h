#pragma once
#include "visualization.h"
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace shader_view
{
    struct image
    {
        std::string name;
        int width = 0, height = 0;
        bool linear = true, repeat = false;
        std::vector<uint8_t> rgba; // bottom row first
    };
    struct pass
    {
        std::string name, source;
        unsigned source_line = 1;
        float scale = 1;
        std::array<std::string, 4> channels{"audio", "fft", "waveform", "spectrum"};
    };
    struct graph { std::vector<image> images; std::vector<pass> passes; };
    struct preset { const char *name, *source; };
    std::span<const preset> presets();
    bool parse(const std::string &source, const std::filesystem::path &directory, graph &out, std::string &error);
    bool load(const std::filesystem::path &path, graph &out, std::string &error);
    std::string fragment_source(const pass &pass, bool es2);
    struct audio_textures
    {
        std::array<uint8_t, 512 * 2 * 4> audio{};
        std::array<uint8_t, 1024 * 4> fft{};
        std::array<uint8_t, 512 * 4> waveform{};
        std::array<uint8_t, 32 * 4> spectrum{};
    };
    audio_textures pack_audio(const visualization_data &data);
    struct uniforms
    {
        float time = 0, delta = 0, audio_time = 0;
        std::array<float, 4> mouse{}, date{};
    };
    using render_function = bool (*)(const graph &, uint64_t revision, const audio_textures &,
                                    const uniforms &, unsigned width, unsigned height,
                                    uintptr_t &texture, std::string &error);
    void set_renderer(render_function renderer);
    bool available();
    uint64_t renderer_generation();
    bool render(const graph &, uint64_t revision, const audio_textures &, const uniforms &,
                unsigned width, unsigned height, uintptr_t &texture, std::string &error);
}
