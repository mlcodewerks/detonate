/* Included by win32_loader.c after its host state. */
#include "gl_video.h"
static HMODULE opengl;
static HDC gl_dc;
static HGLRC gl_context;
static gl_video gl;
static struct retro_hw_render_callback hw;
static BOOL context_ready;
static HGLRC (WINAPI *create_context)(HDC);
static BOOL (WINAPI *delete_context)(HGLRC);
static BOOL (WINAPI *make_current)(HDC, HGLRC);
static PROC (WINAPI *get_proc)(LPCSTR);
#ifdef DETONATE_GLES2
#include "win32_egl.h"
#endif

static retro_proc_address_t RETRO_CALLCONV gl_proc(const char *name)
{
#ifdef DETONATE_GLES2
    if (use_gles2) return egl_proc(name);
#endif
    PROC p = get_proc(name);
    if (!p || (INT_PTR)p == 1 || (INT_PTR)p == 2 || (INT_PTR)p == 3 || (INT_PTR)p == -1)
        p = GetProcAddress(opengl, name);
    return (retro_proc_address_t)p;
}
static uintptr_t RETRO_CALLCONV gl_framebuffer(void) { return gl.framebuffer; }

static BOOL start_gl(void)
{
#ifdef DETONATE_GLES2
    if (use_gles2) return start_egl();
#endif
    PIXELFORMATDESCRIPTOR format = {0};
    HGLRC legacy;
    HGLRC (WINAPI *create_attribs)(HDC, HGLRC, const int *);
    BOOL (WINAPI *swap_interval)(int);
    const int attributes[] = {0x2091, 3, 0x2092, 3, 0x9126, 1, 0};
    int index;
    opengl = LoadLibraryW(L"opengl32.dll");
    if (!opengl) return FALSE;
    create_context = (void *)GetProcAddress(opengl, "wglCreateContext");
    delete_context = (void *)GetProcAddress(opengl, "wglDeleteContext");
    make_current = (void *)GetProcAddress(opengl, "wglMakeCurrent");
    get_proc = (void *)GetProcAddress(opengl, "wglGetProcAddress");
    if (!create_context || !delete_context || !make_current || !get_proc) return FALSE;
    gl_dc = GetDC(window);
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    index = ChoosePixelFormat(gl_dc, &format);
    if (!index || !SetPixelFormat(gl_dc, index, &format)) return FALSE;
    legacy = create_context(gl_dc);
    if (!legacy) return FALSE;
    if (!make_current(gl_dc, legacy)) { delete_context(legacy); return FALSE; }
    create_attribs = (void *)gl_proc("wglCreateContextAttribsARB");
    if (create_attribs) gl_context = create_attribs(gl_dc, NULL, attributes);
    make_current(NULL, NULL);
    delete_context(legacy);
    if (!gl_context || !make_current(gl_dc, gl_context)) return FALSE;
    swap_interval = (void *)gl_proc("wglSwapIntervalEXT");
    if (swap_interval) swap_interval(0);
    return gl_video_create(&gl, gl_proc, av.geometry.base_width, av.geometry.base_height, false);
}
static void stop_gl(void)
{
#ifdef DETONATE_GLES2
    if (use_gles2) { stop_egl(); return; }
#endif
    if (gl_context)
    {
        gl_video_destroy(&gl);
        make_current(NULL, NULL);
        delete_context(gl_context);
    }
    if (gl_dc) ReleaseDC(window, gl_dc);
    if (opengl) FreeLibrary(opengl);
}
static void present_gl(void)
{
    RECT client;
    GetClientRect(window, &client);
    gl_video_present(&gl, client.right, client.bottom, hw.bottom_left_origin);
#ifdef DETONATE_GLES2
    if (use_gles2)
    {
        if (!host_eglSwapBuffers(egl_display, egl_surface)) failure = L"Unable to present GLES2 video.";
        return;
    }
#endif
    if (!SwapBuffers(gl_dc)) failure = L"Unable to present OpenGL video.";
}
