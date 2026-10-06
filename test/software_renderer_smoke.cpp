#include "imgui.h"
#include "imgui_sw.hpp"
#include "imgui_font.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}
static unsigned callbacks = 0;
static void callback(const ImDrawList *, const ImDrawCmd *) { ++callbacks; }

static std::vector<uint32_t> paint(ImDrawData *data, const imgui_sw::SwOptions &options = {})
{
    const size_t count = size_t(data->DisplaySize.x * data->FramebufferScale.x) *
                         size_t(data->DisplaySize.y * data->FramebufferScale.y);
    constexpr uint32_t guard = 0x12345678;
    std::vector<uint32_t> pixels(count + 2, 0);
    pixels.front() = pixels.back() = guard;
    imgui_sw::paint_imgui(pixels.data() + 1, data, options);
    check(pixels.front() == guard && pixels.back() == guard, "framebuffer bounds");
    return {pixels.begin() + 1, pixels.end() - 1};
}

static void check_close(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, const char *what)
{
    check(a.size() == b.size(), what);
    for (size_t i = 0; i < a.size(); ++i)
        for (unsigned shift = 0; shift < 32; shift += 8)
            if (std::abs(int((a[i] >> shift) & 255) - int((b[i] >> shift) & 255)) > 1)
            {
                std::fprintf(stderr, "pixel %zu: %08x != %08x\n", i, a[i], b[i]);
                check(false, what);
            }
}

static void check_font_pixels(ImFont *font, float scale)
{
    ImGui::NewFrame();
    auto *draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect(ImVec2(3.2f, 4.2f), ImVec2(48.3f, 30.7f), false);
    draw->AddText(font, 18.0f, ImVec2(2.25f, 4.5f), IM_COL32_WHITE, "Ag9");
    draw->PopClipRect();
    ImGui::Render();
    auto *data = ImGui::GetDrawData();
    data->FramebufferScale = ImVec2(scale, scale);
    const auto actual = paint(data);
    const int width = int(data->DisplaySize.x * scale), height = int(data->DisplaySize.y * scale);
    std::vector<uint32_t> expected(width * height, 0);
    // Independent bilinear reference over the glyph atlas. Derive coordinates
    // from the emitted glyph quads, without using renderer sampling helpers.
    for (const auto &cmd : draw->CmdBuffer)
    {
        if (!cmd.ElemCount)
            continue;
        auto *atlas = cmd.TexRef._TexData;
        check(atlas && atlas->Format == ImTextureFormat_RGBA32, "font atlas reference");
        for (unsigned index = 0; index < cmd.ElemCount; index += 6)
        {
            const auto &a = draw->VtxBuffer[cmd.VtxOffset + draw->IdxBuffer[cmd.IdxOffset + index]];
            const auto &b = draw->VtxBuffer[cmd.VtxOffset + draw->IdxBuffer[cmd.IdxOffset + index + 2]];
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    const double px = (x + 0.5) / scale, py = (y + 0.5) / scale;
                    if (px < a.pos.x || px >= b.pos.x || py < a.pos.y || py >= b.pos.y ||
                        px < cmd.ClipRect.x || px >= cmd.ClipRect.z || py < cmd.ClipRect.y || py >= cmd.ClipRect.w)
                        continue;
                    const double u = a.uv.x + (px - a.pos.x) / (b.pos.x - a.pos.x) * (b.uv.x - a.uv.x);
                    const double v = a.uv.y + (py - a.pos.y) / (b.pos.y - a.pos.y) * (b.uv.y - a.uv.y);
                    const double tx = u * atlas->Width - 0.5, ty = v * atlas->Height - 0.5;
                    const int ix = int(std::floor(tx)), iy = int(std::floor(ty));
                    double channels[4] = {};
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx)
                        {
                            const auto *sample = reinterpret_cast<const uint32_t *>(atlas->GetPixelsAt(
                                std::clamp(ix + dx, 0, atlas->Width - 1), std::clamp(iy + dy, 0, atlas->Height - 1)));
                            const double weight = (dx ? tx - ix : 1 - (tx - ix)) * (dy ? ty - iy : 1 - (ty - iy));
                            for (int c = 0; c < 4; ++c)
                                channels[c] += ((*sample >> (c * 8)) & 255) * weight;
                        }
                    const unsigned alpha = unsigned(std::lround(channels[IM_COL32_A_SHIFT / 8]));
                    const uint32_t old = expected[y * width + x];
                    uint32_t result = 0;
                    for (int c = 0; c < 4; ++c)
                    {
                        const unsigned src = unsigned(std::lround(channels[c]));
                        const unsigned dst = (old >> (c * 8)) & 255;
                        const unsigned value = c * 8 == IM_COL32_A_SHIFT
                                                   ? alpha + dst * (255 - alpha) / 255
                                                   : (src * alpha + dst * (255 - alpha)) / 255;
                        result |= value << (c * 8);
                    }
                    expected[y * width + x] = result;
                }
        }
    }
    check_close(actual, expected, "oversampled glyphs match bilinear atlas coverage");
    check_close(paint(data, {false, false}), expected, "triangle glyph path matches atlas coverage");
}

static void check_primitives()
{
    const uint32_t texels[] = {IM_COL32(255, 0, 0, 255), IM_COL32(0, 255, 0, 255),
                               IM_COL32(0, 0, 255, 255), IM_COL32(255, 255, 255, 255)};
    imgui_sw::Texture texture{texels, 4, 1};
    const ImTextureRef ref(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(&texture)));
    for (bool linear : {false, true})
    {
        texture.bilinear = linear;
        ImGui::NewFrame();
        auto *draw = ImGui::GetBackgroundDrawList();
        draw->AddImage(ref, ImVec2(0, 0), ImVec2(16, 4));
        draw->AddImage(ref, ImVec2(0, 8), ImVec2(16, 12), ImVec2(1, 0), ImVec2(0, 1));
        ImGui::Render();
        const auto pixels = paint(ImGui::GetDrawData());
        check(pixels[16] == 0 && pixels[4 * 64] == 0, "textured quads exclude right and bottom edges");
        if (!linear)
            for (int x = 0; x < 16; ++x)
            {
                check(pixels[x] == texels[x / 4], "nearest texels have equal widths");
                check(pixels[8 * 64 + x] == texels[3 - x / 4], "flipped UV sampling");
            }
        else
        {
            check(pixels[3] == IM_COL32(159, 96, 0, 255), "bilinear interpolation at texel centers");
            check(pixels[0] == texels[0] && pixels[15] == texels[3], "linear clamp to edge");
        }
        check_close(pixels, paint(ImGui::GetDrawData(), {false, false}), "quad and triangle sampling agree");
    }
    for (bool reverse : {false, true})
    {
        ImGui::NewFrame();
        auto *draw = ImGui::GetBackgroundDrawList();
        draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        draw->AddRectFilled(ImVec2(4.5f, 4.5f), ImVec2(20.5f, 20.5f), IM_COL32(255, 0, 0, 128));
        // Two duplicate triangles must not be collapsed into a filled rectangle.
        for (int i = 0; i < 2; ++i)
            draw->AddTriangleFilled(ImVec2(30, 4), ImVec2(46, 4), ImVec2(46, 20), IM_COL32(255, 0, 0, 128));
        if (reverse)
            for (int i = 0; i < draw->IdxBuffer.Size; i += 3)
                std::swap(draw->IdxBuffer[i], draw->IdxBuffer[i + 2]);
        ImGui::Render();
        const auto pixels = paint(ImGui::GetDrawData());
        const auto triangles = paint(ImGui::GetDrawData(), {false, false});
        check_close(pixels, triangles, "quad detection and alpha match triangle coverage");
        check(pixels[10 * 64 + 10] == IM_COL32(128, 0, 0, 128), "source-over alpha across shared diagonal");
        check(pixels[4 * 64 + 4] == IM_COL32(128, 0, 0, 128) && pixels[20 * 64 + 20] == 0,
              "half-pixel top-left fill convention in either winding");
        check(pixels[16 * 64 + 32] == 0, "duplicate triangles leave uncovered corner empty");
        check(pixels[6 * 64 + 42] == IM_COL32(191, 0, 0, 191), "overlapping triangles blend twice");
    }

    ImGui::NewFrame();
    auto *draw = ImGui::GetBackgroundDrawList();
    draw->Flags |= ImDrawListFlags_AntiAliasedFill;
    draw->AddRectFilled(ImVec2(8, 8), ImVec2(56, 32), IM_COL32_WHITE, 4.5f);
    ImGui::Render();
    std::vector<uint32_t> expected(64 * 64, 0);
    const auto edge = [](ImVec2 a, ImVec2 b, double x, double y)
    {
        return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
    };
    for (int i = 0; i < draw->IdxBuffer.Size; i += 3)
    {
        const auto &a = draw->VtxBuffer[draw->IdxBuffer[i]], &b = draw->VtxBuffer[draw->IdxBuffer[i + 1]],
                   &c = draw->VtxBuffer[draw->IdxBuffer[i + 2]];
        const double area = edge(a.pos, b.pos, c.pos.x, c.pos.y);
        if (area == 0)
            continue;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
            {
                const double w0 = edge(b.pos, c.pos, x + 0.5, y + 0.5) / area;
                const double w1 = edge(c.pos, a.pos, x + 0.5, y + 0.5) / area;
                const double w2 = edge(a.pos, b.pos, x + 0.5, y + 0.5) / area;
                if (w0 < 0 || w1 < 0 || w2 < 0)
                    continue;
                const auto inclusive = [area](ImVec2 p, ImVec2 q)
                {
                    return (q.y - p.y) * area < 0 || (q.y == p.y && (q.x - p.x) * area > 0);
                };
                if ((w0 == 0 && !inclusive(b.pos, c.pos)) || (w1 == 0 && !inclusive(c.pos, a.pos)) ||
                    (w2 == 0 && !inclusive(a.pos, b.pos)))
                    continue;
                const unsigned alpha = unsigned(std::lround(w0 * ((a.col >> IM_COL32_A_SHIFT) & 255) +
                                                            w1 * ((b.col >> IM_COL32_A_SHIFT) & 255) + w2 * ((c.col >> IM_COL32_A_SHIFT) & 255)));
                const unsigned old = (expected[y * 64 + x] >> IM_COL32_A_SHIFT) & 255;
                const unsigned value = alpha + old * (255 - alpha) / 255;
                expected[y * 64 + x] = IM_COL32(value, value, value, value);
            }
    }
    check_close(paint(ImGui::GetDrawData()), expected, "rounded AA corners match triangle reference");
}

int main()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(64, 64);
    io.Fonts->AddFontDefault();
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    auto *roboto = io.Fonts->AddFontFromMemoryTTF((void *)Roboto_Regular, sizeof(Roboto_Regular), 18.0f, &config);
    imgui_sw::bind_imgui_painting();

    check_primitives();
    check_font_pixels(roboto, 1.0f);
    check_font_pixels(roboto, 1.5f);
    check_font_pixels(roboto, 2.0f);

    // Clip in logical coordinates, then translate and scale to framebuffer pixels.
    ImGui::NewFrame();
    auto *draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect(ImVec2(14, 24), ImVec2(26, 36), false);
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(60, 60), IM_COL32(220, 30, 70, 255));
    draw->PopClipRect();
    ImGui::Render();
    auto *data = ImGui::GetDrawData();
    data->DisplayPos = ImVec2(10, 20);
    data->FramebufferScale = ImVec2(3, 2);
    const auto clipped = paint(data);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 192; ++x)
            check(clipped[y * 192 + x] == (x >= 12 && x < 48 && y >= 8 && y < 32
                                               ? IM_COL32(220, 30, 70, 255)
                                               : 0),
                  "scaled translated clipping");

    // An image sampling at the atlas white UV still needs its own texture.
    const uint32_t blue = IM_COL32(10, 40, 230, 255);
    const imgui_sw::Texture texture{&blue, 1, 1};
    const ImTextureID texture_id = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(&texture));
    ImGui::NewFrame();
    draw = ImGui::GetBackgroundDrawList();
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    draw->AddImage(ImTextureRef(texture_id), ImVec2(4, 4), ImVec2(20, 20), uv, uv);
    draw->AddRectFilledMultiColor(ImVec2(24, 4), ImVec2(48, 20),
                                  IM_COL32(255, 0, 0, 255), IM_COL32(255, 0, 0, 255),
                                  IM_COL32(0, 0, 255, 255), IM_COL32(0, 0, 255, 255));
    draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    draw->AddCallback(callback, nullptr);
    ImGui::Render();
    const auto textured = paint(ImGui::GetDrawData());
    check(textured[10 * 64 + 10] == blue, "user texture at font white UV");
    check(textured[6 * 64 + 32] != textured[18 * 64 + 32], "gradient interpolation");
    check(callbacks == 1, "custom and reset callbacks");
    const auto unoptimized = paint(ImGui::GetDrawData(), {false, false});
    check(unoptimized[10 * 64 + 10] == blue, "triangle path samples user texture");

    // Force a second vertex base while keeping nearly all geometry offscreen.
    ImGui::NewFrame();
    draw = ImGui::GetBackgroundDrawList();
    for (int i = 0; i < 17000; ++i)
        draw->AddRectFilled(ImVec2(-20, -20), ImVec2(-10, -10), IM_COL32_WHITE);
    draw->AddRectFilled(ImVec2(4, 4), ImVec2(20, 20), blue);
    ImGui::Render();
    check(ImGui::GetDrawData()->TotalVtxCount > 65535, "large draw list fixture");
    const auto large = paint(ImGui::GetDrawData());
    check(large[10 * 64 + 10] == blue, "vertex and index offsets beyond 64K vertices");

    // Introduce new glyph sizes after rendering, forcing incremental atlas updates
    // and growth while old draw commands can still refer to retired textures.
    for (int frame = 0; frame < 24; ++frame)
    {
        ImGui::NewFrame();
        draw = ImGui::GetBackgroundDrawList();
        draw->AddText(io.FontDefault ? io.FontDefault : io.Fonts->Fonts[0],
                      12.0f + frame * 2.0f, ImVec2(0, 0), IM_COL32_WHITE, "Ag9!xyz");
        ImGui::Render();
        const auto text = paint(ImGui::GetDrawData());
        check(std::count_if(text.begin(), text.end(), [](uint32_t p)
                            { return (p & 0xffffffu) != 0; }) > 20,
              "dynamic font atlas renders new glyph sizes");
        for (const auto *tex : *ImGui::GetDrawData()->Textures)
            check(tex->Status == ImTextureStatus_OK || tex->Status == ImTextureStatus_Destroyed ||
                      (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames == 0),
                  "atlas updates serviced");
    }
    check_font_pixels(roboto, 1.0f); // same glyph coverage after atlas growth
    imgui_sw::unbind_imgui_painting();
    for (const auto *tex : ImGui::GetPlatformIO().Textures)
        check(tex->BackendUserData == nullptr && tex->TexID == ImTextureID_Invalid, "texture resources released");
    ImGui::DestroyContext();
    std::puts("Software clipping, textures, gradients, offsets, callbacks, and dynamic fonts passed.");
}
