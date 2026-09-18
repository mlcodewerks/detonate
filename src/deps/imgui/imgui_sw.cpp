// By Emil Ernerfeldt 2018
// LICENSE:
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
#include "imgui_sw.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

#include <imgui.h>

namespace imgui_sw
{
  namespace
  {

    const Texture *fontTexture = nullptr;
#if !defined(IMGUI_HAS_TEXTURES)
    Texture legacyFontTexture;
#endif

#if defined(IMGUI_HAS_TEXTURES)
    struct BackendTexture
    {
      Texture texture;
    };
#endif

    static inline ImTextureID texture_to_imgui_id(const Texture *texture)
    {
#if IMGUI_VERSION_NUM >= 19104
      return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture));
#else
      return reinterpret_cast<ImTextureID>(const_cast<Texture *>(texture));
#endif
    }

    static inline const Texture *texture_from_imgui_id(ImTextureID texture_id)
    {
#if IMGUI_VERSION_NUM >= 19104
      return reinterpret_cast<const Texture *>(static_cast<uintptr_t>(texture_id));
#else
      return reinterpret_cast<const Texture *>(texture_id);
#endif
    }

    struct Stats
    {
      int uniform_triangle_pixels = 0;
      int textured_triangle_pixels = 0;
      int gradient_triangle_pixels = 0;
      int font_pixels = 0;
      double uniform_rectangle_pixels = 0;
      double textured_rectangle_pixels = 0;
      double gradient_rectangle_pixels = 0;
      double gradient_textured_rectangle_pixels = 0;
    };

    struct PaintTarget
    {
      uint32_t *pixels;
      int width;
      int height;
      ImVec2 scale; // Multiply ImGui coordinates by this to get framebuffer pixels.
      ImVec2 display_pos;
    };

    using Int = int64_t;
    static constexpr Int kFixedBias = 256;
    static constexpr float kInv255 = 1.0f / 255.0f;

    struct Point
    {
      Int x;
      Int y;
    };

    struct Barycentric
    {
      float w0;
      float w1;
      float w2;
    };

    struct EdgeWalker
    {
      Int row;
      Int dx;
      Int dy;
    };

    struct TriangleSetup
    {
      ImVec2 p0;
      ImVec2 p1;
      ImVec2 p2;
      float area;
      int min_x;
      int min_y;
      int max_x;
      int max_y;
      EdgeWalker e0;
      EdgeWalker e1;
      EdgeWalker e2;
    };

    static inline uint32_t channel_u8(uint32_t color, unsigned shift)
    {
      return (color >> shift) & 0xffu;
    }

    static inline uint32_t div255(uint32_t x)
    {
      return (x + 1u + (x >> 8u)) >> 8u;
    }

    static inline uint32_t blend_packed(uint32_t target, uint32_t source)
    {
      const uint32_t sa = channel_u8(source, IM_COL32_A_SHIFT);
      if (sa == 0u)
      {
        return target;
      }
      if (sa == 255u)
      {
        return source;
      }

      const uint32_t inv_a = 255u - sa;
      const uint32_t r = div255(channel_u8(source, IM_COL32_R_SHIFT) * sa + channel_u8(target, IM_COL32_R_SHIFT) * inv_a);
      const uint32_t g = div255(channel_u8(source, IM_COL32_G_SHIFT) * sa + channel_u8(target, IM_COL32_G_SHIFT) * inv_a);
      const uint32_t b = div255(channel_u8(source, IM_COL32_B_SHIFT) * sa + channel_u8(target, IM_COL32_B_SHIFT) * inv_a);

      const uint32_t a = sa + div255(channel_u8(target, IM_COL32_A_SHIFT) * inv_a);
      return (a << IM_COL32_A_SHIFT) | (r << IM_COL32_R_SHIFT) | (g << IM_COL32_G_SHIFT) | (b << IM_COL32_B_SHIFT);
    }

    static inline uint32_t modulate_packed(uint32_t lhs, uint32_t rhs)
    {
      const uint32_t r = div255(channel_u8(lhs, IM_COL32_R_SHIFT) * channel_u8(rhs, IM_COL32_R_SHIFT));
      const uint32_t g = div255(channel_u8(lhs, IM_COL32_G_SHIFT) * channel_u8(rhs, IM_COL32_G_SHIFT));
      const uint32_t b = div255(channel_u8(lhs, IM_COL32_B_SHIFT) * channel_u8(rhs, IM_COL32_B_SHIFT));
      const uint32_t a = div255(channel_u8(lhs, IM_COL32_A_SHIFT) * channel_u8(rhs, IM_COL32_A_SHIFT));
      return (r << IM_COL32_R_SHIFT) | (g << IM_COL32_G_SHIFT) | (b << IM_COL32_B_SHIFT) | (a << IM_COL32_A_SHIFT);
    }

    static inline ImVec4 color_convert_u32_to_float4(ImU32 in)
    {
      return ImVec4(channel_u8(in, IM_COL32_R_SHIFT) * kInv255,
                    channel_u8(in, IM_COL32_G_SHIFT) * kInv255,
                    channel_u8(in, IM_COL32_B_SHIFT) * kInv255,
                    channel_u8(in, IM_COL32_A_SHIFT) * kInv255);
    }

    static inline ImU32 color_convert_float4_to_u32(const ImVec4 &in)
    {
      return (uint32_t(std::clamp(in.x, 0.0f, 1.0f) * 255.0f + 0.5f) << IM_COL32_R_SHIFT) | (uint32_t(std::clamp(in.y, 0.0f, 1.0f) * 255.0f + 0.5f) << IM_COL32_G_SHIFT) | (uint32_t(std::clamp(in.z, 0.0f, 1.0f) * 255.0f + 0.5f) << IM_COL32_B_SHIFT) | (uint32_t(std::clamp(in.w, 0.0f, 1.0f) * 255.0f + 0.5f) << IM_COL32_A_SHIFT);
    }

    static inline void bary_add(Barycentric &a, const Barycentric &b)
    {
      a.w0 += b.w0;
      a.w1 += b.w1;
      a.w2 += b.w2;
    }

    static inline float min3(float a, float b, float c)
    {
      const float ab = a < b ? a : b;
      return ab < c ? ab : c;
    }

    static inline float max3(float a, float b, float c)
    {
      const float ab = a > b ? a : b;
      return ab > c ? ab : c;
    }

    static inline float barycentric(const ImVec2 &a, const ImVec2 &b, const ImVec2 &point)
    {
      return (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);
    }

    static inline Int orient2d(const Point &a, const Point &b, const Point &c)
    {
      return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    }

    static inline Int floor_fixed(float v)
    {
      const float scaled = v * float(kFixedBias);
      Int i = static_cast<Int>(scaled);
      i -= static_cast<Int>(scaled < static_cast<float>(i));
      return i;
    }

    static inline Point as_point(const ImVec2 &v)
    {
      return Point{floor_fixed(v.x), floor_fixed(v.y)};
    }

    static inline bool is_dominant_edge(const ImVec2 &edge)
    {
      return edge.y < 0.0f || (edge.y == 0.0f && edge.x > 0.0f);
    }

    static inline bool clip_edge_span(Int row, Int delta, int width, int &begin, int &end)
    {
      if (delta > 0)
      {
        if (row < 0)
        {
          const Int need = -row;
          const Int n = 1 + (need - 1) / delta;
          if (n >= width)
            return false;
          if (n > begin)
            begin = static_cast<int>(n);
        }
      }
      else if (delta < 0)
      {
        if (row < 0)
          return false;
        const Int exclusive = row / -delta + 1;
        if (exclusive < end)
          end = static_cast<int>(exclusive);
      }
      else if (row < 0)
      {
        return false;
      }
      return begin < end;
    }

    static inline bool clip_scanline_span(Int row0, Int dx0, Int row1, Int dx1, Int row2, Int dx2, int width, int &begin, int &end)
    {
      begin = 0;
      end = width;
      return clip_edge_span(row0, dx0, width, begin, end) && clip_edge_span(row1, dx1, width, begin, end) && clip_edge_span(row2, dx2, width, begin, end);
    }

    static inline uint32_t texel(const Texture &texture, int x, int y)
    {
      if (texture.alpha8)
        return IM_COL32(255, 255, 255, reinterpret_cast<const uint8_t *>(texture.pixels)[x + y * texture.width]);
      return texture.pixels[x + y * texture.width];
    }

    static inline uint32_t sample_texture(const Texture &texture, float u, float v)
    {
      if (!texture.bilinear)
      {
        const int x = static_cast<int>(std::clamp(u * texture.width, 0.0f, float(texture.width - 1)));
        const int y = static_cast<int>(std::clamp(v * texture.height, 0.0f, float(texture.height - 1)));
        return texel(texture, x, y);
      }
      const float x = std::clamp(u * texture.width - 0.5f, 0.0f, float(texture.width - 1));
      const float y = std::clamp(v * texture.height - 0.5f, 0.0f, float(texture.height - 1));
      const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
      const int x1 = std::min(x0 + 1, texture.width - 1), y1 = std::min(y0 + 1, texture.height - 1);
      const float fx = x - x0, fy = y - y0;
      const uint32_t c00 = texel(texture, x0, y0), c10 = texel(texture, x1, y0);
      const uint32_t c01 = texel(texture, x0, y1), c11 = texel(texture, x1, y1);
      uint32_t result = 0;
      for (unsigned shift = 0; shift < 32; shift += 8)
      {
        const float top = channel_u8(c00, shift) * (1.0f - fx) + channel_u8(c10, shift) * fx;
        const float bottom = channel_u8(c01, shift) * (1.0f - fx) + channel_u8(c11, shift) * fx;
        result |= uint32_t(top * (1.0f - fy) + bottom * fy + 0.5f) << shift;
      }
      return result;
    }

    static inline int pixel_bound(float coordinate, int limit)
    {
      return static_cast<int>(std::clamp(std::ceil(coordinate - 0.5f), 0.0f, float(limit)));
    }

    static bool setup_triangle(const PaintTarget &target,
                               const ImVec4 &clip_rect,
                               const ImDrawVert &v0,
                               const ImDrawVert &v1,
                               const ImDrawVert &v2,
                               TriangleSetup &out)
    {
      const float sx = target.scale.x;
      const float sy = target.scale.y;
      const float dx = target.display_pos.x;
      const float dy = target.display_pos.y;

      out.p0 = ImVec2(sx * (v0.pos.x - dx), sy * (v0.pos.y - dy));
      out.p1 = ImVec2(sx * (v1.pos.x - dx), sy * (v1.pos.y - dy));
      out.p2 = ImVec2(sx * (v2.pos.x - dx), sy * (v2.pos.y - dy));

      const Point p0i = as_point(out.p0), p1i = as_point(out.p1), p2i = as_point(out.p2);
      out.p0 = ImVec2(float(p0i.x) / kFixedBias, float(p0i.y) / kFixedBias);
      out.p1 = ImVec2(float(p1i.x) / kFixedBias, float(p1i.y) / kFixedBias);
      out.p2 = ImVec2(float(p2i.x) / kFixedBias, float(p2i.y) / kFixedBias);

      out.area = barycentric(out.p0, out.p1, out.p2);
      if (out.area == 0.0f)
      {
        return false;
      }

      float min_x = min3(out.p0.x, out.p1.x, out.p2.x);
      float min_y = min3(out.p0.y, out.p1.y, out.p2.y);
      float max_x = max3(out.p0.x, out.p1.x, out.p2.x);
      float max_y = max3(out.p0.y, out.p1.y, out.p2.y);

      const float clip_min_x = sx * (clip_rect.x - dx);
      const float clip_min_y = sy * (clip_rect.y - dy);
      const float clip_max_x = sx * (clip_rect.z - dx);
      const float clip_max_y = sy * (clip_rect.w - dy);

      min_x = std::max(min_x, clip_min_x);
      min_y = std::max(min_y, clip_min_y);
      max_x = std::min(max_x, clip_max_x);
      max_y = std::min(max_y, clip_max_y);

      out.min_x = pixel_bound(min_x, target.width);
      out.min_y = pixel_bound(min_y, target.height);
      out.max_x = pixel_bound(max_x, target.width);
      out.max_y = pixel_bound(max_y, target.height);

      if (out.min_x >= out.max_x || out.min_y >= out.max_y)
      {
        return false;
      }

      const int sign = out.area > 0.0f ? 1 : -1;
      const int bias0 = is_dominant_edge(ImVec2(sign * (out.p2.x - out.p1.x), sign * (out.p2.y - out.p1.y))) ? 0 : -1;
      const int bias1 = is_dominant_edge(ImVec2(sign * (out.p0.x - out.p2.x), sign * (out.p0.y - out.p2.y))) ? 0 : -1;
      const int bias2 = is_dominant_edge(ImVec2(sign * (out.p1.x - out.p0.x), sign * (out.p1.y - out.p0.y))) ? 0 : -1;

      const Point start{Int(out.min_x) * kFixedBias + kFixedBias / 2,
                        Int(out.min_y) * kFixedBias + kFixedBias / 2};

      out.e0.row = Int(sign) * orient2d(p1i, p2i, start) + bias0;
      out.e1.row = Int(sign) * orient2d(p2i, p0i, start) + bias1;
      out.e2.row = Int(sign) * orient2d(p0i, p1i, start) + bias2;

      out.e0.dx = Int(sign) * (p1i.y - p2i.y) * kFixedBias;
      out.e1.dx = Int(sign) * (p2i.y - p0i.y) * kFixedBias;
      out.e2.dx = Int(sign) * (p0i.y - p1i.y) * kFixedBias;

      out.e0.dy = Int(sign) * (p2i.x - p1i.x) * kFixedBias;
      out.e1.dy = Int(sign) * (p0i.x - p2i.x) * kFixedBias;
      out.e2.dy = Int(sign) * (p1i.x - p0i.x) * kFixedBias;

      return true;
    }

    static void paint_uniform_rectangle(const PaintTarget &target,
                                        float min_x,
                                        float min_y,
                                        float max_x,
                                        float max_y,
                                        uint32_t color)
    {
      int x0 = pixel_bound(target.scale.x * (min_x - target.display_pos.x), target.width);
      int y0 = pixel_bound(target.scale.y * (min_y - target.display_pos.y), target.height);
      int x1 = pixel_bound(target.scale.x * (max_x - target.display_pos.x), target.width);
      int y1 = pixel_bound(target.scale.y * (max_y - target.display_pos.y), target.height);

      x0 = std::max(x0, 0);
      y0 = std::max(y0, 0);
      x1 = std::min(x1, target.width);
      y1 = std::min(y1, target.height);
      if (x0 >= x1 || y0 >= y1)
      {
        return;
      }

      const uint32_t alpha = channel_u8(color, IM_COL32_A_SHIFT);
      if (alpha == 0u)
      {
        return;
      }

      const int count = x1 - x0;
      uint32_t *row = target.pixels + y0 * target.width + x0;

      if (alpha == 255u)
      {
        for (int y = y0; y < y1; ++y)
        {
          uint32_t *p = row;
          uint32_t *const end = p + count;
          while (p != end)
          {
            *p++ = color;
          }
          row += target.width;
        }
        return;
      }

      uint32_t last_target = *row;
      uint32_t last_output = blend_packed(last_target, color);

      for (int y = y0; y < y1; ++y)
      {
        uint32_t *p = row;
        uint32_t *const end = p + count;
        while (p != end)
        {
          const uint32_t target_pixel = *p;
          if (target_pixel == last_target)
          {
            *p = last_output;
          }
          else
          {
            last_target = target_pixel;
            last_output = blend_packed(target_pixel, color);
            *p = last_output;
          }
          ++p;
        }
        row += target.width;
      }
    }

    static void paint_uniform_textured_rectangle(const PaintTarget &target,
                                                 const Texture &texture,
                                                 const ImVec4 &clip_rect,
                                                 const ImDrawVert &min_v,
                                                 const ImDrawVert &max_v)
    {
      const float sx = target.scale.x;
      const float sy = target.scale.y;
      const float display_x = target.display_pos.x;
      const float display_y = target.display_pos.y;

      const ImVec2 min_p(sx * (min_v.pos.x - display_x), sy * (min_v.pos.y - display_y));
      const ImVec2 max_p(sx * (max_v.pos.x - display_x), sy * (max_v.pos.y - display_y));

      const float distance_x = max_p.x - min_p.x;
      const float distance_y = max_p.y - min_p.y;
      if (distance_x == 0.0f || distance_y == 0.0f)
      {
        return;
      }

      float min_x = std::max(min_p.x, sx * (clip_rect.x - display_x));
      float min_y = std::max(min_p.y, sy * (clip_rect.y - display_y));
      float max_x = std::min(max_p.x, sx * (clip_rect.z - display_x));
      float max_y = std::min(max_p.y, sy * (clip_rect.w - display_y));

      int x0 = pixel_bound(min_x, target.width);
      int y0 = pixel_bound(min_y, target.height);
      int x1 = pixel_bound(max_x, target.width);
      int y1 = pixel_bound(max_y, target.height);
      if (x0 >= x1 || y0 >= y1 || texture.width <= 0 || texture.height <= 0 || texture.pixels == nullptr)
      {
        return;
      }

      const ImVec2 delta_uv_per_pixel((max_v.uv.x - min_v.uv.x) / distance_x,
                                      (max_v.uv.y - min_v.uv.y) / distance_y);
      for (int y = y0; y < y1; ++y)
      {
        const float v = min_v.uv.y + (float(y) + 0.5f - min_p.y) * delta_uv_per_pixel.y;
        uint32_t *dst = target.pixels + y * target.width + x0;
        for (int x = x0; x < x1; ++x, ++dst)
        {
          const float u = min_v.uv.x + (float(x) + 0.5f - min_p.x) * delta_uv_per_pixel.x;
          *dst = blend_packed(*dst, modulate_packed(sample_texture(texture, u, v), min_v.col));
        }
      }
    }

    static void paint_uniform_triangle(const PaintTarget &target,
                                       const TriangleSetup &tri,
                                       uint32_t color)
    {
      const uint32_t alpha = channel_u8(color, IM_COL32_A_SHIFT);
      if (alpha == 0u)
      {
        return;
      }

      Int e0_row = tri.e0.row;
      Int e1_row = tri.e1.row;
      Int e2_row = tri.e2.row;

      uint32_t last_target = 0u;
      uint32_t last_output = blend_packed(last_target, color);
      const int box_width = tri.max_x - tri.min_x;

      for (int y = tri.min_y; y < tri.max_y; ++y)
      {
        int begin;
        int end;
        if (clip_scanline_span(e0_row, tri.e0.dx, e1_row, tri.e1.dx, e2_row, tri.e2.dx, box_width, begin, end))
        {
          uint32_t *dst = target.pixels + y * target.width + tri.min_x + begin;
          uint32_t *const dst_end = target.pixels + y * target.width + tri.min_x + end;

          if (alpha == 255u)
          {
            while (dst != dst_end)
            {
              *dst++ = color;
            }
          }
          else
          {
            while (dst != dst_end)
            {
              const uint32_t target_pixel = *dst;
              if (target_pixel == last_target)
              {
                *dst = last_output;
              }
              else
              {
                last_target = target_pixel;
                last_output = blend_packed(target_pixel, color);
                *dst = last_output;
              }
              ++dst;
            }
          }
        }

        e0_row += tri.e0.dy;
        e1_row += tri.e1.dy;
        e2_row += tri.e2.dy;
      }
    }

    static void paint_shaded_triangle(const PaintTarget &target,
                                      const TriangleSetup &tri,
                                      const Texture *texture,
                                      const ImDrawVert &v0,
                                      const ImDrawVert &v1,
                                      const ImDrawVert &v2,
                                      bool uniform_color)
    {
      const ImVec2 topleft(float(tri.min_x) + 0.5f, float(tri.min_y) + 0.5f);
      const ImVec2 one_x(topleft.x + 1.0f, topleft.y);
      const ImVec2 one_y(topleft.x, topleft.y + 1.0f);

      const float w0_topleft = barycentric(tri.p1, tri.p2, topleft);
      const float w1_topleft = barycentric(tri.p2, tri.p0, topleft);
      const float w2_topleft = barycentric(tri.p0, tri.p1, topleft);

      const float w0_dx = barycentric(tri.p1, tri.p2, one_x) - w0_topleft;
      const float w1_dx = barycentric(tri.p2, tri.p0, one_x) - w1_topleft;
      const float w2_dx = barycentric(tri.p0, tri.p1, one_x) - w2_topleft;

      const float w0_dy = barycentric(tri.p1, tri.p2, one_y) - w0_topleft;
      const float w1_dy = barycentric(tri.p2, tri.p0, one_y) - w1_topleft;
      const float w2_dy = barycentric(tri.p0, tri.p1, one_y) - w2_topleft;

      const float inv_area = 1.0f / tri.area;
      Barycentric bary_row{w0_topleft * inv_area, w1_topleft * inv_area, w2_topleft * inv_area};
      const Barycentric bary_dx{w0_dx * inv_area, w1_dx * inv_area, w2_dx * inv_area};
      const Barycentric bary_dy{w0_dy * inv_area, w1_dy * inv_area, w2_dy * inv_area};

      const ImVec4 c0 = color_convert_u32_to_float4(v0.col);
      ImVec4 c1;
      ImVec4 c2;
      if (!uniform_color)
      {
        c1 = color_convert_u32_to_float4(v1.col);
        c2 = color_convert_u32_to_float4(v2.col);
      }

      if (texture && (texture->width <= 0 || texture->height <= 0 || texture->pixels == nullptr))
      {
        return;
      }

      Int e0_row = tri.e0.row;
      Int e1_row = tri.e1.row;
      Int e2_row = tri.e2.row;
      const int box_width = tri.max_x - tri.min_x;

      for (int y = tri.min_y; y < tri.max_y; ++y)
      {
        int begin;
        int end;
        if (clip_scanline_span(e0_row, tri.e0.dx, e1_row, tri.e1.dx, e2_row, tri.e2.dx, box_width, begin, end))
        {
          Barycentric bary = bary_row;
          for (int n = 0; n < begin; ++n)
          {
            bary_add(bary, bary_dx);
          }

          uint32_t *dst = target.pixels + y * target.width + tri.min_x + begin;
          const int count = end - begin;
          for (int n = 0; n < count; ++n)
          {
            const float w0 = bary.w0;
            const float w1 = bary.w1;
            const float w2 = bary.w2;

            ImVec4 src_color;
            if (uniform_color)
            {
              src_color = c0;
            }
            else
            {
              src_color.x = w0 * c0.x + w1 * c1.x + w2 * c2.x;
              src_color.y = w0 * c0.y + w1 * c1.y + w2 * c2.y;
              src_color.z = w0 * c0.z + w1 * c1.z + w2 * c2.z;
              src_color.w = w0 * c0.w + w1 * c1.w + w2 * c2.w;
            }

            if (texture)
            {
              const float u = w0 * v0.uv.x + w1 * v1.uv.x + w2 * v2.uv.x;
              const float v = w0 * v0.uv.y + w1 * v1.uv.y + w2 * v2.uv.y;
              const ImVec4 tex_color = color_convert_u32_to_float4(sample_texture(*texture, u, v));
              src_color.x *= tex_color.x;
              src_color.y *= tex_color.y;
              src_color.z *= tex_color.z;
              src_color.w *= tex_color.w;
            }

            *dst = blend_packed(*dst, color_convert_float4_to_u32(src_color));

            ++dst;
            bary_add(bary, bary_dx);
          }
        }

        e0_row += tri.e0.dy;
        e1_row += tri.e1.dy;
        e2_row += tri.e2.dy;
        bary_add(bary_row, bary_dy);
      }
    }

    static void paint_triangle(const PaintTarget &target,
                               const Texture *texture,
                               const ImVec4 &clip_rect,
                               const ImDrawVert &v0,
                               const ImDrawVert &v1,
                               const ImDrawVert &v2)
    {
      TriangleSetup tri;
      if (!setup_triangle(target, clip_rect, v0, v1, v2, tri))
      {
        return;
      }

      const bool uniform_color = v0.col == v1.col && v0.col == v2.col;
      if (uniform_color && !texture)
      {
        paint_uniform_triangle(target, tri, v0.col);
        return;
      }

      paint_shaded_triangle(target, tri, texture, v0, v1, v2, uniform_color);
    }

    static inline bool uv_is_white(const ImVec2 &uv, const ImVec2 &white_uv)
    {
      return uv.x == white_uv.x && uv.y == white_uv.y;
    }

    static void paint_draw_cmd(const PaintTarget &target,
                               const ImDrawVert *vertices,
                               const ImDrawIdx *idx_buffer,
                               const ImDrawCmd &pcmd,
                               const ImVec2 &white_uv,
                               const SwOptions &options)
    {
      const Texture *const texture = texture_from_imgui_id(pcmd.GetTexID());
      assert(texture);
      if (!texture || texture->width <= 0 || texture->height <= 0 || texture->pixels == nullptr)
      {
        return;
      }

  
#if IMGUI_VERSION_NUM >= 19200
      const bool is_font_atlas = pcmd.GetTexID() == ImGui::GetIO().Fonts->TexRef.GetTexID();
#else
      const bool is_font_atlas = texture == fontTexture;
#endif

      const int elem_count = static_cast<int>(pcmd.ElemCount);

      for (int i = 0; i + 3 <= elem_count;)
      {
        const ImDrawVert &v0 = vertices[idx_buffer[i + 0]];
        const ImDrawVert &v1 = vertices[idx_buffer[i + 1]];
        const ImDrawVert &v2 = vertices[idx_buffer[i + 2]];

        if ((options.optimize_text || options.optimize_rectangles) && i + 6 <= elem_count && idx_buffer[i + 3] == idx_buffer[i] && idx_buffer[i + 4] == idx_buffer[i + 2])
        {
          const ImDrawVert &v3 = vertices[idx_buffer[i + 5]];
          const bool is_quad = v0.pos.x == v3.pos.x && v1.pos.x == v2.pos.x && v0.pos.y == v1.pos.y && v2.pos.y == v3.pos.y && v0.pos.x < v2.pos.x && v0.pos.y < v2.pos.y && v0.col == v1.col && v0.col == v2.col && v0.col == v3.col;
          const bool has_texture = !is_font_atlas || !uv_is_white(v0.uv, white_uv) || !uv_is_white(v1.uv, white_uv) || !uv_is_white(v2.uv, white_uv) || !uv_is_white(v3.uv, white_uv);
          if (is_quad && has_texture && options.optimize_text && v0.uv.x == v3.uv.x && v1.uv.x == v2.uv.x && v0.uv.y == v1.uv.y && v2.uv.y == v3.uv.y)
          {
            paint_uniform_textured_rectangle(target, *texture, pcmd.ClipRect, v0, v2);
            i += 6;
            continue;
          }
          if (is_quad && !has_texture && options.optimize_rectangles)
          {
            paint_uniform_rectangle(target, std::max(v0.pos.x, pcmd.ClipRect.x),
                                    std::max(v0.pos.y, pcmd.ClipRect.y), std::min(v2.pos.x, pcmd.ClipRect.z),
                                    std::min(v2.pos.y, pcmd.ClipRect.w), v0.col);
            i += 6;
            continue;
          }
        }

        const bool has_texture = !is_font_atlas || !uv_is_white(v0.uv, white_uv) || !uv_is_white(v1.uv, white_uv) || !uv_is_white(v2.uv, white_uv);
        paint_triangle(target, has_texture ? texture : nullptr, pcmd.ClipRect, v0, v1, v2);
        i += 3;
      }
    }

    static void paint_draw_list(
        const PaintTarget &target, const ImDrawList *cmd_list, const ImVec2 &white_uv, const SwOptions &options)
    {
      const ImDrawIdx *idx_buffer = cmd_list->IdxBuffer.Data;
      const ImDrawVert *vertices = cmd_list->VtxBuffer.Data;

      const int command_count = cmd_list->CmdBuffer.Size;
      for (int cmd_i = 0; cmd_i < command_count; ++cmd_i)
      {
        const ImDrawCmd &pcmd = cmd_list->CmdBuffer[cmd_i];
        if (pcmd.UserCallback)
        {

          if (pcmd.UserCallback != ImDrawCallback_ResetRenderState)
            pcmd.UserCallback(cmd_list, &pcmd);
        }
        else if (pcmd.ElemCount != 0)
        {
          paint_draw_cmd(target, vertices + pcmd.VtxOffset, idx_buffer + pcmd.IdxOffset, pcmd, white_uv, options);
        }
      }
    }

  } // namespace

  void make_style_fast()
  {
    ImGuiStyle &style = ImGui::GetStyle();
    style.AntiAliasedLines = false;
    style.AntiAliasedFill = false;
    style.WindowRounding = 0.0f;
  }

  void restore_style()
  {
    ImGuiStyle &style = ImGui::GetStyle();
    const ImGuiStyle default_style;
    style.AntiAliasedLines = default_style.AntiAliasedLines;
    style.AntiAliasedFill = default_style.AntiAliasedFill;
    style.WindowRounding = default_style.WindowRounding;
  }

#if defined(IMGUI_HAS_TEXTURES)
  static void update_dynamic_texture(ImTextureData *tex)
  {
    if (tex == nullptr || tex->Status == ImTextureStatus_OK || tex->Status == ImTextureStatus_Destroyed)
    {
      return;
    }

    if (tex->Status == ImTextureStatus_WantDestroy)
    {
      if (tex->UnusedFrames > 0)
      {
        BackendTexture *backend = static_cast<BackendTexture *>(tex->BackendUserData);
        delete backend;
        tex->BackendUserData = nullptr;
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
      }
      return;
    }

    assert(tex->Format == ImTextureFormat_RGBA32);
    if (tex->Format != ImTextureFormat_RGBA32 || tex->Width <= 0 || tex->Height <= 0 || tex->GetPixels() == nullptr)
    {
      return;
    }

    BackendTexture *backend = static_cast<BackendTexture *>(tex->BackendUserData);
    if (tex->Status == ImTextureStatus_WantCreate)
    {
      assert(backend == nullptr);
      backend = new BackendTexture;
      tex->BackendUserData = backend;
      tex->SetTexID(texture_to_imgui_id(&backend->texture));
    }

    if (backend == nullptr)
    {
      return;
    }

    backend->texture.pixels = reinterpret_cast<const uint32_t *>(tex->GetPixels());
    backend->texture.width = tex->Width;
    backend->texture.height = tex->Height;
    backend->texture.bilinear = true;
    tex->SetStatus(ImTextureStatus_OK);
  }

  static void update_dynamic_textures(ImDrawData *draw_data)
  {
    if (draw_data == nullptr || draw_data->Textures == nullptr)
    {
      return;
    }

    for (ImTextureData *tex : *draw_data->Textures)
    {
      update_dynamic_texture(tex);
    }
  }
#endif

  void bind_imgui_painting()
  {
    ImGuiIO &io = ImGui::GetIO();
    io.BackendRendererName = "imgui_sw";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
#if defined(IMGUI_HAS_TEXTURES)
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    fontTexture = nullptr;
#else
    rebuild_font_texture();
#endif
  }

  bool rebuild_font_texture()
  {
#if defined(IMGUI_HAS_TEXTURES)
    return true;
#else
    ImGuiIO &io = ImGui::GetIO();
    if (io.Fonts == nullptr)
    {
      return false;
    }

    uint8_t *tex_data = nullptr;
    int font_width = 0;
    int font_height = 0;
    io.Fonts->GetTexDataAsAlpha8(&tex_data, &font_width, &font_height);
    if (tex_data == nullptr || font_width <= 0 || font_height <= 0)
    {
      return false;
    }

    legacyFontTexture.pixels = reinterpret_cast<const uint32_t *>(tex_data);
    legacyFontTexture.width = font_width;
    legacyFontTexture.height = font_height;
    legacyFontTexture.bilinear = true;
    legacyFontTexture.alpha8 = true;
    io.Fonts->TexID = texture_to_imgui_id(&legacyFontTexture);
    fontTexture = &legacyFontTexture;
    return true;
#endif
  }

  static Stats s_stats;

  void paint_imgui(uint32_t *pixels, ImDrawData *drawData, const SwOptions &options)
  {
    if (drawData == nullptr)
      return;
#if defined(IMGUI_HAS_TEXTURES)
    update_dynamic_textures(drawData);
#endif

    const int fb_width = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const int fb_height = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if (!pixels || fb_width <= 0 || fb_height <= 0)
    {
      return;
    }

    const PaintTarget target{pixels, fb_width, fb_height, drawData->FramebufferScale, drawData->DisplayPos};
#if IMGUI_VERSION_NUM >= 19200
    const ImVec2 white_uv = ImGui::GetFontTexUvWhitePixel();
#else
    const ImVec2 white_uv = ImGui::GetIO().Fonts->TexUvWhitePixel;
#endif

    s_stats = Stats{};
#if IMGUI_VERSION_NUM >= 19200
    for (int i = 0; i < drawData->CmdLists.Size; ++i)
    {
      paint_draw_list(target, drawData->CmdLists[i], white_uv, options);
    }
#else
    for (int i = 0; i < drawData->CmdListsCount; ++i)
    {
      paint_draw_list(target, drawData->CmdLists[i], white_uv, options);
    }
#endif
  }

  void unbind_imgui_painting()
  {
    ImGuiIO &io = ImGui::GetIO();
    io.BackendRendererName = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;

#if defined(IMGUI_HAS_TEXTURES)
    ImGuiPlatformIO &platform_io = ImGui::GetPlatformIO();
    for (ImTextureData *tex : platform_io.Textures)
    {
      if (tex != nullptr && tex->BackendUserData != nullptr)
      {
        BackendTexture *backend = static_cast<BackendTexture *>(tex->BackendUserData);
        delete backend;
        tex->BackendUserData = nullptr;
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
      }
    }
    io.BackendFlags &= ~ImGuiBackendFlags_RendererHasTextures;
#else
    if (io.Fonts != nullptr && texture_from_imgui_id(io.Fonts->TexID) == &legacyFontTexture)
    {
      io.Fonts->TexID = texture_to_imgui_id(nullptr);
    }
    legacyFontTexture = Texture{};
#endif

    fontTexture = nullptr;
  }

  bool show_options(SwOptions *io_options)
  {
    assert(io_options);
    bool changed = false;
    changed |= ImGui::Checkbox("optimize_text", &io_options->optimize_text);
    changed |= ImGui::Checkbox("optimize_rectangles", &io_options->optimize_rectangles);
    return changed;
  }

  void show_stats()
  {
    ImGui::Text("uniform_triangle_pixels:            %7d", s_stats.uniform_triangle_pixels);
    ImGui::Text("textured_triangle_pixels:           %7d", s_stats.textured_triangle_pixels);
    ImGui::Text("gradient_triangle_pixels:           %7d", s_stats.gradient_triangle_pixels);
    ImGui::Text("font_pixels:                        %7d", s_stats.font_pixels);
    ImGui::Text("uniform_rectangle_pixels:           %7.0f", s_stats.uniform_rectangle_pixels);
    ImGui::Text("textured_rectangle_pixels:          %7.0f", s_stats.textured_rectangle_pixels);
    ImGui::Text("gradient_rectangle_pixels:          %7.0f", s_stats.gradient_rectangle_pixels);
    ImGui::Text("gradient_textured_rectangle_pixels: %7.0f", s_stats.gradient_textured_rectangle_pixels);
  }

} // namespace imgui_sw
