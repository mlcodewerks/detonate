# Detonate

A libretro audio player with a software-rendered Dear ImGui file browser.
Requires a frontend accepting XRGB8888 software video; no hardware graphics
context is requested. Video is 1280 x 720 at 60 Hz, with stereo 44.1 kHz audio.
Windows/MSYS2 and Linux builds use C++20, Meson and Ninja.
