#pragma once
#include "libretro.h"
bool shader_renderer_init(retro_hw_get_proc_address_t proc, bool es2);
void shader_renderer_shutdown(bool context_lost);
