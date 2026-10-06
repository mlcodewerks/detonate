#include "shader_visualization.h"
#include "libretro.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numbers>

retro_audio_sample_batch_t audio_batch_cb = nullptr;
retro_audio_sample_t audio_cb = nullptr;
static void check(bool ok, const char *message)
{ if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); } }
int main()
{
    using namespace shader_view;
    std::string error; graph g;
    for (const auto &preset : presets())
    {
        check(parse(preset.source, {}, g, error), "built-in preset parses");
        for (const auto &p : g.passes)
            check(fragment_source(p, true).starts_with("#version 100") && fragment_source(p, false).starts_with("#version 330 core"), "both shader dialects generated");
    }
    const auto count = g.passes.size();
    for (const auto &source : {"stoy 2\n", "stoy 1\n[pass image]\nchannel0=future\nshader:\nvoid mainImage(out vec4 c,in vec2 p){}\n[end]\n",
        "stoy 1\n[pass image]\nscale=0.3\n", "stoy 1\n[pass image]\nshader:\n", "stoy 1\n[pass waveform]\nshader:\nx\n[end]\n",
        "stoy 1\n[pass image]\nshader:\nx\n[end]\n[pass image]\nshader:\nx\n[end]\n"})
        check(!parse(source, {}, g, error) && !error.empty() && g.passes.size() == count, "invalid graph retains previous project");
    check(parse("#version 330\n// uniform vec3 iResolution;\nuniform float iTime;\nvoid mainImage(out vec4 c,in vec2 p){c=vec4(iTime);}", {}, g, error), "plain GLSL accepted");
    auto wrapped = fragment_source(g.passes[0], true);
    check(wrapped.find("uniform vec3 iResolution;") != wrapped.npos && wrapped.find("uniform float iTime;") == wrapped.rfind("uniform float iTime;"), "comments ignored and existing uniforms respected");
    check(parse("uniform vec3 iResolution;\nvoid main(){gl_FragColor=vec4(1.0);}", {}, g, error), "full fragment shader accepted");
    wrapped = fragment_source(g.passes[0], false);
    check(wrapped.find("void main()") == wrapped.rfind("void main()"), "existing main is not duplicated");
    // A bottom-up two by two BMP checks relative image loading and orientation.
    std::vector<uint8_t> bmp(70); bmp[0] = 'B'; bmp[1] = 'M'; bmp[2] = 70; bmp[10] = 54; bmp[14] = 40;
    bmp[18] = bmp[22] = 2; bmp[26] = 1; bmp[28] = 24;
    bmp[54] = 255; bmp[57] = bmp[58] = bmp[59] = 255; // blue, white bottom row
    bmp[64] = 255; bmp[66] = 255; // red, green top row
    { std::ofstream out("shader-format-texture.bmp", std::ios::binary); out.write(reinterpret_cast<const char *>(bmp.data()), bmp.size()); }
    const std::string source = "stoy 1\n[texture grid]\nfile=shader-format-texture.bmp\nfilter=nearest\nwrap=repeat\n[end]\n[pass image]\nchannel0=grid\nshader:\nvoid mainImage(out vec4 c,in vec2 p){c=texture(iChannel0,p/iResolution.xy);}\n[end]\n";
    const auto image_ok = parse(source, std::filesystem::current_path(), g, error);
    if (!image_ok) std::fprintf(stderr, "%s\n", error.c_str());
    check(image_ok, "texture section loads image");
    check(g.images[0].width == 2 && g.images[0].height == 2 && !g.images[0].linear && g.images[0].repeat && g.images[0].rgba[2] == 255 && g.images[0].rgba[8] == 255, "image sampler settings and bottom-left origin");
    { std::ofstream out("shader-format-project.stoy"); out << source; }
    check(load("shader-format-project.stoy", g, error), "load project and relative image from disk");
    audio_visualizer visualizer;
    auto packed = pack_audio(visualizer.snapshot());
    check(packed.audio[0] == 0 && packed.audio[512 * 4] == 128 && packed.waveform[0] == 128 && packed.waveform[1] == 128, "silent audio texture convention");
    std::vector<int16_t> tone(8192);
    for (size_t i = 0; i < tone.size() / 2; ++i)
    {
        tone[2 * i] = int16_t(16384 * std::sin(2 * std::numbers::pi * 1000 * i / 44100));
        tone[2 * i + 1] = -tone[2 * i];
    }
    visualizer.push(tone.data(), tone.size() / 2); const auto data = visualizer.snapshot();
    const auto peak = std::max_element(data.fft.begin(), data.fft.end());
    check(std::abs(int(peak - data.fft.begin()) - 46) <= 1 && *peak > .8f, "linear FFT locates frequency without stereo cancellation");
    packed = pack_audio(data);
    check(packed.fft[46 * 4] > 200 && packed.audio[46 * 4] == packed.fft[46 * 4], "FFT aliases match audio row zero");
    for (size_t i = 0; i < 512; ++i)
        check(packed.audio[(512 + i) * 4] == 128 && std::abs(int(packed.waveform[i * 4]) + int(packed.waveform[i * 4 + 1]) - 255) <= 1, "mono waveform and separate stereo waveform encoding");
    visualizer.reset();
    for (size_t i = 0; i < tone.size() / 2; ++i)
        tone[2 * i] = tone[2 * i + 1] = int16_t(16384 * std::sin(2 * std::numbers::pi * 20000 * i / 44100));
    visualizer.push(tone.data(), tone.size() / 2);
    const auto high = visualizer.snapshot();
    const auto high_peak = std::max_element(high.fft.begin(), high.fft.end());
    check(std::abs(int(high_peak - high.fft.begin()) - 929) <= 1 && *high_peak > .8f, "FFT covers high frequencies through Nyquist");
    std::filesystem::remove("shader-format-texture.bmp"); std::filesystem::remove("shader-format-project.stoy");
    std::puts("Shader format and audio texture checks passed.");
}
