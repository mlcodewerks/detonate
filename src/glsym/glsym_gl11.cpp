#include "glsym_gl11.h"

decltype(detonate_glBindTexture) detonate_glBindTexture = nullptr;
decltype(detonate_glClear) detonate_glClear = nullptr;
decltype(detonate_glClearColor) detonate_glClearColor = nullptr;
decltype(detonate_glColorMask) detonate_glColorMask = nullptr;
decltype(detonate_glDeleteTextures) detonate_glDeleteTextures = nullptr;
decltype(detonate_glDisable) detonate_glDisable = nullptr;
decltype(detonate_glDrawElements) detonate_glDrawElements = nullptr;
decltype(detonate_glEnable) detonate_glEnable = nullptr;
decltype(detonate_glGenTextures) detonate_glGenTextures = nullptr;
decltype(detonate_glGetError) detonate_glGetError = nullptr;
decltype(detonate_glGetIntegerv) detonate_glGetIntegerv = nullptr;
decltype(detonate_glGetString) detonate_glGetString = nullptr;
decltype(detonate_glIsEnabled) detonate_glIsEnabled = nullptr;
decltype(detonate_glPixelStorei) detonate_glPixelStorei = nullptr;
decltype(detonate_glPolygonMode) detonate_glPolygonMode = nullptr;
decltype(detonate_glScissor) detonate_glScissor = nullptr;
decltype(detonate_glTexImage2D) detonate_glTexImage2D = nullptr;
decltype(detonate_glTexParameteri) detonate_glTexParameteri = nullptr;
decltype(detonate_glTexSubImage2D) detonate_glTexSubImage2D = nullptr;
decltype(detonate_glViewport) detonate_glViewport = nullptr;

bool detonate_gl11_resolve(retro_hw_get_proc_address_t proc)
{
    detonate_glBindTexture = reinterpret_cast<decltype(detonate_glBindTexture)>(proc("glBindTexture"));
    if (!detonate_glBindTexture) return false;
    detonate_glClear = reinterpret_cast<decltype(detonate_glClear)>(proc("glClear"));
    if (!detonate_glClear) return false;
    detonate_glClearColor = reinterpret_cast<decltype(detonate_glClearColor)>(proc("glClearColor"));
    if (!detonate_glClearColor) return false;
    detonate_glColorMask = reinterpret_cast<decltype(detonate_glColorMask)>(proc("glColorMask"));
    if (!detonate_glColorMask) return false;
    detonate_glDeleteTextures = reinterpret_cast<decltype(detonate_glDeleteTextures)>(proc("glDeleteTextures"));
    if (!detonate_glDeleteTextures) return false;
    detonate_glDisable = reinterpret_cast<decltype(detonate_glDisable)>(proc("glDisable"));
    if (!detonate_glDisable) return false;
    detonate_glDrawElements = reinterpret_cast<decltype(detonate_glDrawElements)>(proc("glDrawElements"));
    if (!detonate_glDrawElements) return false;
    detonate_glEnable = reinterpret_cast<decltype(detonate_glEnable)>(proc("glEnable"));
    if (!detonate_glEnable) return false;
    detonate_glGenTextures = reinterpret_cast<decltype(detonate_glGenTextures)>(proc("glGenTextures"));
    if (!detonate_glGenTextures) return false;
    detonate_glGetError = reinterpret_cast<decltype(detonate_glGetError)>(proc("glGetError"));
    if (!detonate_glGetError) return false;
    detonate_glGetIntegerv = reinterpret_cast<decltype(detonate_glGetIntegerv)>(proc("glGetIntegerv"));
    if (!detonate_glGetIntegerv) return false;
    detonate_glGetString = reinterpret_cast<decltype(detonate_glGetString)>(proc("glGetString"));
    if (!detonate_glGetString) return false;
    detonate_glIsEnabled = reinterpret_cast<decltype(detonate_glIsEnabled)>(proc("glIsEnabled"));
    if (!detonate_glIsEnabled) return false;
    detonate_glPixelStorei = reinterpret_cast<decltype(detonate_glPixelStorei)>(proc("glPixelStorei"));
    if (!detonate_glPixelStorei) return false;
    detonate_glPolygonMode = reinterpret_cast<decltype(detonate_glPolygonMode)>(proc("glPolygonMode"));
    if (!detonate_glPolygonMode) return false;
    detonate_glScissor = reinterpret_cast<decltype(detonate_glScissor)>(proc("glScissor"));
    if (!detonate_glScissor) return false;
    detonate_glTexImage2D = reinterpret_cast<decltype(detonate_glTexImage2D)>(proc("glTexImage2D"));
    if (!detonate_glTexImage2D) return false;
    detonate_glTexParameteri = reinterpret_cast<decltype(detonate_glTexParameteri)>(proc("glTexParameteri"));
    if (!detonate_glTexParameteri) return false;
    detonate_glTexSubImage2D = reinterpret_cast<decltype(detonate_glTexSubImage2D)>(proc("glTexSubImage2D"));
    if (!detonate_glTexSubImage2D) return false;
    detonate_glViewport = reinterpret_cast<decltype(detonate_glViewport)>(proc("glViewport"));
    if (!detonate_glViewport) return false;
    return true;
}
