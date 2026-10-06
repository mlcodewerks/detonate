#define IMGUI_IMPL_OPENGL_ES2
#include "imgui_gles2_names.h"
#include "deps/imgui/imgui_impl_opengl3.cpp"
#include "gles2_renderer.h"

bool gles2_init(retro_hw_get_proc_address_t proc)
{
    if (!detonate_gles2_resolve(proc) || !ImGui_ImplGLES2_Init("#version 100"))
        return false;
    if (ImGui_ImplGLES2_CreateDeviceObjects())
        return true;
    ImGui_ImplGLES2_Shutdown();
    return false;
}
void gles2_shutdown(bool lost)
{
    if (lost) ImGui_ImplGLES2_AbandonDeviceObjects();
    ImGui_ImplGLES2_Shutdown();
}
void gles2_new_frame() { ImGui_ImplGLES2_NewFrame(); }
void gles2_render(uintptr_t framebuffer)
{
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)framebuffer);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplGLES2_RenderDrawData(ImGui::GetDrawData());
}
