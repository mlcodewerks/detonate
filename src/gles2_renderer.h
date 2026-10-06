#pragma once
#include "libretro.h"
bool gles2_init(retro_hw_get_proc_address_t proc);
void gles2_shutdown(bool lost);
void gles2_new_frame();
void gles2_render(uintptr_t framebuffer);
