/* Dynamically loaded EGL/GLES2 (for example ANGLE). No EGL import library. */
#include <EGL/egl.h>
static HMODULE egl_library, gles_library;
static EGLDisplay egl_display;
static EGLContext egl_context;
static EGLSurface egl_surface;
#define HOST_EGL(X) \
    X(PFNEGLGETDISPLAYPROC, eglGetDisplay) \
    X(PFNEGLINITIALIZEPROC, eglInitialize) \
    X(PFNEGLBINDAPIPROC, eglBindAPI) \
    X(PFNEGLCHOOSECONFIGPROC, eglChooseConfig) \
    X(PFNEGLCREATEWINDOWSURFACEPROC, eglCreateWindowSurface) \
    X(PFNEGLCREATECONTEXTPROC, eglCreateContext) \
    X(PFNEGLMAKECURRENTPROC, eglMakeCurrent) \
    X(PFNEGLSWAPINTERVALPROC, eglSwapInterval) \
    X(PFNEGLSWAPBUFFERSPROC, eglSwapBuffers) \
    X(PFNEGLDESTROYSURFACEPROC, eglDestroySurface) \
    X(PFNEGLDESTROYCONTEXTPROC, eglDestroyContext) \
    X(PFNEGLTERMINATEPROC, eglTerminate) \
    X(PFNEGLGETPROCADDRESSPROC, eglGetProcAddress)
#define DECLARE(type, name) static type host_##name;
HOST_EGL(DECLARE)
#undef DECLARE
static retro_proc_address_t RETRO_CALLCONV egl_proc(const char *name)
{
    retro_proc_address_t p = (retro_proc_address_t)GetProcAddress(gles_library, name);
    return p ? p : (retro_proc_address_t)host_eglGetProcAddress(name);
}
static BOOL start_egl(void)
{
    EGLConfig config;
    EGLint count;
    const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    egl_library = LoadLibraryW(L"libEGL.dll");
    gles_library = LoadLibraryW(L"libGLESv2.dll");
    if (!egl_library || !gles_library) return FALSE;
#define LOAD(type, name) host_##name = (type)(void *)GetProcAddress(egl_library, #name); if (!host_##name) return FALSE;
    HOST_EGL(LOAD)
#undef LOAD
    egl_display = host_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY || !host_eglInitialize(egl_display, NULL, NULL)) return FALSE;
    if (!host_eglBindAPI(EGL_OPENGL_ES_API) ||
        !host_eglChooseConfig(egl_display, config_attributes, &config, 1, &count) || !count) return FALSE;
    egl_surface = host_eglCreateWindowSurface(egl_display, config, (EGLNativeWindowType)window, NULL);
    egl_context = host_eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attributes);
    if (egl_surface == EGL_NO_SURFACE || egl_context == EGL_NO_CONTEXT ||
        !host_eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) return FALSE;
    host_eglSwapInterval(egl_display, 0);
    return gl_video_create(&gl, egl_proc, av.geometry.base_width, av.geometry.base_height, true);
}
static void stop_egl(void)
{
    if (egl_display != EGL_NO_DISPLAY)
    {
        if (egl_context != EGL_NO_CONTEXT) gl_video_destroy(&gl);
        host_eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_context != EGL_NO_CONTEXT) host_eglDestroyContext(egl_display, egl_context);
        if (egl_surface != EGL_NO_SURFACE) host_eglDestroySurface(egl_display, egl_surface);
        host_eglTerminate(egl_display);
    }
    if (gles_library) FreeLibrary(gles_library);
    if (egl_library) FreeLibrary(egl_library);
}
#undef HOST_EGL
