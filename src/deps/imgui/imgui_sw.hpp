// By Emil Ernerfeldt 2018
// LICENSE:
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
// WHAT:
//   This is a software renderer for Dear ImGui.
//   It is decently fast, but has a lot of room for optimization.
#pragma once

#include <cstdint>

struct ImDrawData;
namespace imgui_sw {

struct Texture
{
  // User textures use the same packed 32-bit color layout as Dear ImGui
  // (RGBA byte order by default, BGRA with IMGUI_USE_BGRA_PACKED_COLOR).
  const uint32_t *pixels = nullptr;
  int width = 0;
  int height = 0;
  bool bilinear = false; // User images default to nearest; ImGui atlases require linear filtering.
  bool alpha8 = false;   // Alpha-only atlas storage, interpreted as white with coverage.
};


struct SwOptions
{
  bool optimize_text = true;       // Fast path for axis-aligned textured quads.
  bool optimize_rectangles = true; // Fast path for uniform filled rectangles.
};


void make_style_fast();

/// Undo what make_style_fast did.
void restore_style();


void bind_imgui_painting();


bool rebuild_font_texture();


void paint_imgui(uint32_t *pixels, ImDrawData *data, const SwOptions &options = {});


void unbind_imgui_painting();


bool show_options(SwOptions *io_options);


void show_stats();

}// namespace imgui_sw
