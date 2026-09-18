/* Copyright (c) 2013-2014 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba-util/memory.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

void* anonymousMemoryMap(size_t size) {
	#ifdef _WIN32
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    void* result = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return result == MAP_FAILED ? NULL : result;
#endif
}

void mappedMemoryFree(void* memory, size_t size) {
	UNUSED(size);
	// size is not useful here because we're freeing the memory, not decommitting it
	#ifdef _WIN32
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    if (memory) munmap(memory, size);
#endif
}
