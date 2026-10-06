#include "shader_visualization.h"
#include "shader_presets.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <tuple>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#include "stb/stb_image.h"

namespace shader_view
{
    namespace
    {
        render_function renderer = nullptr;
        uint64_t generation = 0;
        std::string trim(std::string s)
        {
            const auto begin = s.find_first_not_of(" \t\r\n");
            return begin == std::string::npos ? std::string{} : s.substr(begin, s.find_last_not_of(" \t\r\n") - begin + 1);
        }
        bool builtin(const std::string &name)
        {
            return name == "audio" || name == "fft" || name == "waveform" || name == "spectrum" || name == "previous";
        }
        bool read_file(const std::filesystem::path &path, size_t limit, std::vector<uint8_t> &bytes)
        {
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (!in) return false;
            const auto n = in.tellg();
            if (n <= 0 || uint64_t(n) > limit) return false;
            bytes.resize(size_t(n)); in.seekg(0);
            return bool(in.read(reinterpret_cast<char *>(bytes.data()), bytes.size()));
        }
        bool read_image(const std::filesystem::path &path, image &out, std::string &error)
        {
            std::vector<uint8_t> file;
            if (!read_file(path, 16 * 1024 * 1024, file)) { error = "Cannot read texture file (limit 16 MiB)."; return false; }
            int channels;
            if (!stbi_info_from_memory(file.data(), int(file.size()), &out.width, &out.height, &channels) ||
                out.width < 1 || out.height < 1 || out.width > 2048 || out.height > 2048)
            { error = "Texture must be a PNG, JPEG, BMP or TGA image up to 2048 x 2048."; return false; }
            if (out.repeat && ((out.width & (out.width - 1)) || (out.height & (out.height - 1))))
            { error = "Repeating textures need power-of-two dimensions for GLES2."; return false; }
            // The supplied stb version's BMP offset check assumes callback buffers.
            struct input { const std::vector<uint8_t> &bytes; size_t offset = 0; } input{file};
            const stbi_io_callbacks io{
                [](void *user, char *buffer, int n) -> int {
                    auto &in = *static_cast<struct input *>(user);
                    const auto count = std::min(size_t(std::max(n, 0)), in.bytes.size() - in.offset);
                    std::memcpy(buffer, in.bytes.data() + in.offset, count); in.offset += count; return int(count);
                },
                [](void *user, int n) {
                    auto &in = *static_cast<struct input *>(user);
                    in.offset = size_t(std::clamp(int64_t(in.offset) + n, int64_t(0), int64_t(in.bytes.size())));
                },
                [](void *user) -> int { const auto &in = *static_cast<struct input *>(user); return in.offset == in.bytes.size(); }
            };
            auto *pixels = stbi_load_from_callbacks(&io, &input, &out.width, &out.height, &channels, 4);
            if (!pixels) { error = std::string("Cannot decode texture image: ") + stbi_failure_reason(); return false; }
            out.rgba.resize(size_t(out.width) * out.height * 4);
            const size_t stride = size_t(out.width) * 4;
            for (int y = 0; y < out.height; ++y)
                std::memcpy(out.rgba.data() + y * stride, pixels + (out.height - y - 1) * stride, stride);
            stbi_image_free(pixels);
            return true;
        }
        // Keep newlines when removing comments, for source diagnostics and uniform detection.
        std::string without_comments(std::string s)
        {
            bool block = false, line = false;
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (line && s[i] == '\n') line = false;
                if (!block && !line && i + 1 < s.size() && s[i] == '/' && (s[i + 1] == '/' || s[i + 1] == '*'))
                { block = s[i + 1] == '*'; line = !block; s[i] = s[i + 1] = ' '; ++i; continue; }
                if (block && i + 1 < s.size() && s[i] == '*' && s[i + 1] == '/')
                { block = false; s[i] = s[i + 1] = ' '; ++i; continue; }
                if ((block || line) && s[i] != '\n') s[i] = ' ';
            }
            return s;
        }
    }
    std::span<const preset> presets() { return shader_presets; }
    bool parse(const std::string &input, const std::filesystem::path &directory, graph &out, std::string &error)
    {
        error.clear();
        auto source = input;
        if (source.starts_with("\xef\xbb\xbf")) source.erase(0, 3);
        if (source.empty() || source.size() > 1024 * 1024 || source.find('\0') != std::string::npos) { error = "Shader file must contain 1 byte to 1 MiB of text without NUL bytes."; return false; }
        graph result;
        std::istringstream stream(source);
        std::string line; std::getline(stream, line);
        if (trim(line) != "stoy 1")
        {
            if (trim(line).starts_with("stoy ")) { error = "Unsupported stoy format version."; return false; }
            pass p; p.name = "image"; p.source = source;
            result.passes.push_back(std::move(p));
            out = std::move(result); return true;
        }
        enum { none, image_section, pass_section, shader_section } section = none;
        std::set<std::string> names;
        std::filesystem::path image_path;
        unsigned line_number = 1;
        auto fail = [&](const char *message) { error = "Line " + std::to_string(line_number) + ": " + message; return false; };
        while (std::getline(stream, line))
        {
            ++line_number;
            const auto text = trim(line);
            if (section == shader_section && text != "[end]") { result.passes.back().source += line + '\n'; continue; }
            if (text.empty() || text[0] == '#') continue;
            if (text == "[end]")
            {
                if (section == none) return fail("Unexpected [end].");
                if (section == pass_section) return fail("Pass is missing shader:.");
                if (section == shader_section && trim(result.passes.back().source).empty()) return fail("Pass shader is empty.");
                if (section == image_section)
                {
                    if (image_path.empty()) return fail("Texture is missing file=.");
                    if (!read_image(directory / image_path, result.images.back(), error)) return false;
                    size_t total = 0; for (const auto &im : result.images) total += im.rgba.size();
                    if (total > 64 * 1024 * 1024) return fail("Decoded textures exceed 64 MiB.");
                }
                section = none; continue;
            }
            if (text.starts_with("[texture ") || text.starts_with("[pass "))
            {
                if (section != none || text.back() != ']') return fail("Close the previous section with [end].");
                const bool texture = text.starts_with("[texture ");
                const auto name = text.substr(texture ? 9 : 6, text.size() - (texture ? 10 : 7));
                if (name.empty() || name.size() > 64 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos || builtin(name) || !names.insert(name).second)
                    return fail("Names must be unique identifiers and cannot use audio input names or previous.");
                if (texture)
                {
                    if (result.images.size() >= 8) return fail("Maximum eight image textures.");
                    result.images.emplace_back(); result.images.back().name = name; image_path.clear(); section = image_section;
                }
                else
                {
                    if (result.passes.size() >= 8) return fail("Maximum eight passes.");
                    result.passes.emplace_back(); result.passes.back().name = name; section = pass_section;
                }
                continue;
            }
            if (text == "shader:" && section == pass_section)
            { section = shader_section; result.passes.back().source_line = line_number + 1; continue; }
            const auto separator = text.find('=');
            if (separator == std::string::npos) return fail("Expected a section, key=value, shader:, or [end].");
            const auto key = trim(text.substr(0, separator)), value = trim(text.substr(separator + 1));
            if (section == image_section)
            {
                if (key == "file") image_path = std::filesystem::path(reinterpret_cast<const char8_t *>(value.c_str()));
                else if (key == "filter" && (value == "linear" || value == "nearest")) result.images.back().linear = value == "linear";
                else if (key == "wrap" && (value == "clamp" || value == "repeat")) result.images.back().repeat = value == "repeat";
                else return fail("Unknown texture option or value.");
            }
            else if (section == pass_section)
            {
                if (key == "scale" && (value == "1" || value == "1.0" || value == "0.5" || value == "0.25")) result.passes.back().scale = std::stof(value);
                else if (key.size() == 8 && key.starts_with("channel") && key[7] >= '0' && key[7] <= '3') result.passes.back().channels[key[7] - '0'] = value;
                else return fail("Unknown pass option or value.");
            }
            else return fail("Options need a texture or pass section.");
        }
        if (section != none) return fail("Unclosed section: expected [end].");
        if (result.passes.empty() || result.passes.back().name != "image" || result.passes.back().scale != 1)
            return fail("The final pass must be named image and use scale=1.");
        std::set<std::string> available;
        for (const auto &im : result.images) available.insert(im.name);
        for (const auto &p : result.passes)
        {
            for (const auto &channel : p.channels)
                if (!builtin(channel) && !available.contains(channel)) { error = p.name + ": input '" + channel + "' must name an image, an earlier pass, or an audio input. Use previous for feedback."; return false; }
            available.insert(p.name);
        }
        out = std::move(result); return true;
    }
    bool load(const std::filesystem::path &path, graph &out, std::string &error)
    {
        std::vector<uint8_t> bytes;
        if (!read_file(path, 1024 * 1024, bytes)) { error = "Cannot read shader file (limit 1 MiB)."; return false; }
        return parse(std::string(bytes.begin(), bytes.end()), path.parent_path(), out, error);
    }
    std::string fragment_source(const pass &p, bool es2)
    {
        std::string body;
        std::istringstream lines(p.source); std::string line;
        while (std::getline(lines, line)) body += (trim(line).starts_with("#version") ? "" : line) + '\n';
        const auto code = without_comments(body);
        std::string header = es2 ? "#version 100\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\nprecision mediump int;\n#define texture texture2D\n" :
            "#version 330 core\nout vec4 detonateFragColor;\n#define gl_FragColor detonateFragColor\n#define texture2D texture\n";
        for (const auto &[type, name, extent] : {std::tuple{"vec3", "iResolution", ""}, {"float", "iTime", ""}, {"float", "iGlobalTime", ""},
             {"float", "iTimeDelta", ""}, {"float", "iFrameRate", ""}, {"int", "iFrame", ""}, {"float", "iSampleRate", ""},
             {"vec4", "iMouse", ""}, {"vec4", "iDate", ""}, {"float", "iChannelTime", "[4]"}, {"vec3", "iChannelResolution", "[4]"},
             {"sampler2D", "iChannel0", ""}, {"sampler2D", "iChannel1", ""}, {"sampler2D", "iChannel2", ""}, {"sampler2D", "iChannel3", ""},
             {"sampler2D", "iAudio", ""}, {"sampler2D", "iFFT", ""}, {"sampler2D", "iWaveform", ""}, {"sampler2D", "iSpectrum", ""}})
        {
            const std::regex declaration(std::string("\\buniform\\s+(?:(?:lowp|mediump|highp)\\s+)?\\w+\\s+") + name + "\\b");
            if (!std::regex_search(code, declaration)) header += std::string("uniform ") + type + ' ' + name + extent + ";\n";
        }
        header += "#line " + std::to_string(p.source_line) + '\n' + body;
        if (!std::regex_search(code, std::regex("\\bvoid\\s+main\\s*\\(")))
            header += "\nvoid main() { vec4 c = vec4(0.0); mainImage(c, gl_FragCoord.xy); gl_FragColor = c; }\n";
        return header;
    }
    audio_textures pack_audio(const visualization_data &data)
    {
        audio_textures out;
        auto encode = [](float f) { return uint8_t(std::lround(std::clamp(f, 0.f, 1.f) * 255)); };
        for (size_t x = 0; x < 512; ++x)
        {
            const auto fft = encode(data.fft[x]);
            const auto left = encode(data.left[x] * .5f + .5f), right = encode(data.right[x] * .5f + .5f);
            const auto mono = encode((data.left[x] + data.right[x]) * .25f + .5f);
            for (unsigned c = 0; c < 4; ++c)
            {
                out.audio[x * 4 + c] = c == 3 ? 255 : fft;
                out.audio[(512 + x) * 4 + c] = c == 3 ? 255 : mono;
                out.waveform[x * 4 + c] = c == 0 ? left : c == 1 ? right : c == 2 ? mono : 255;
            }
        }
        for (size_t x = 0; x < 1024; ++x)
            for (unsigned c = 0; c < 4; ++c) out.fft[x * 4 + c] = c == 3 ? 255 : encode(data.fft[x]);
        for (size_t x = 0; x < 32; ++x)
            for (unsigned c = 0; c < 4; ++c) out.spectrum[x * 4 + c] = c == 3 ? 255 : encode(data.spectrum[x]);
        return out;
    }
    void set_renderer(render_function fn) { renderer = fn; ++generation; }
    bool available() { return renderer != nullptr; }
    uint64_t renderer_generation() { return generation; }
    bool render(const graph &g, uint64_t revision, const audio_textures &audio, const uniforms &u,
                unsigned width, unsigned height, uintptr_t &texture, std::string &error)
    { return renderer && renderer(g, revision, audio, u, width, height, texture, error); }
}
