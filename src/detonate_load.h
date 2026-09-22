#pragma once
#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif
RETRO_API bool detonate_load_game_async(const struct retro_game_info *game);
/* 0: loading, 1: complete, -1: failed (browser remains usable). */
RETRO_API int detonate_load_status(void);
#ifdef __cplusplus
}
#endif
