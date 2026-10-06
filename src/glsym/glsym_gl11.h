// GL 1.1 entry points omitted by the vendored glsym table.
#pragma once
#include "rglgen_headers.h"
#include "libretro.h"

bool detonate_gl11_resolve(retro_hw_get_proc_address_t proc);
extern decltype(&glBindTexture) detonate_glBindTexture;
extern decltype(&glClear) detonate_glClear;
extern decltype(&glClearColor) detonate_glClearColor;
extern decltype(&glColorMask) detonate_glColorMask;
extern decltype(&glDeleteTextures) detonate_glDeleteTextures;
extern decltype(&glDisable) detonate_glDisable;
extern decltype(&glDrawElements) detonate_glDrawElements;
extern decltype(&glEnable) detonate_glEnable;
extern decltype(&glGenTextures) detonate_glGenTextures;
extern decltype(&glGetError) detonate_glGetError;
extern decltype(&glGetIntegerv) detonate_glGetIntegerv;
extern decltype(&glGetString) detonate_glGetString;
extern decltype(&glIsEnabled) detonate_glIsEnabled;
extern decltype(&glPixelStorei) detonate_glPixelStorei;
extern decltype(&glPolygonMode) detonate_glPolygonMode;
extern decltype(&glScissor) detonate_glScissor;
extern decltype(&glTexImage2D) detonate_glTexImage2D;
extern decltype(&glTexParameteri) detonate_glTexParameteri;
extern decltype(&glTexSubImage2D) detonate_glTexSubImage2D;
extern decltype(&glViewport) detonate_glViewport;

#define glBindTexture detonate_glBindTexture
#define glClear detonate_glClear
#define glClearColor detonate_glClearColor
#define glColorMask detonate_glColorMask
#define glDeleteTextures detonate_glDeleteTextures
#define glDisable detonate_glDisable
#define glDrawElements detonate_glDrawElements
#define glEnable detonate_glEnable
#define glGenTextures detonate_glGenTextures
#define glGetError detonate_glGetError
#define glGetIntegerv detonate_glGetIntegerv
#define glGetString detonate_glGetString
#define glIsEnabled detonate_glIsEnabled
#define glPixelStorei detonate_glPixelStorei
#define glPolygonMode detonate_glPolygonMode
#define glScissor detonate_glScissor
#define glTexImage2D detonate_glTexImage2D
#define glTexParameteri detonate_glTexParameteri
#define glTexSubImage2D detonate_glTexSubImage2D
#define glViewport detonate_glViewport
