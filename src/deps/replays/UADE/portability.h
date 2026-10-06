#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifndef _WIN32
#define _byteswap_ushort __builtin_bswap16
#define _byteswap_ulong __builtin_bswap32
#define __forceinline inline
#else
#include <intrin.h>
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define HAVE_UNISTD_H 1

#include "compat/memmemrep.h"
#include "compat/strlrep.h"
