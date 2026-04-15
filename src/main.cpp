// Virtual Saran Wrap — CS184 Final Project
// Entry point: GLFW init, OpenGL 4.1 context, OpenCL probe, render loop.
//
// Include order (GLFW_INCLUDE_NONE is defined in CMakeLists.txt):
//   1. <OpenGL/gl3.h>  — must come before <GLFW/glfw3.h>
//   2. <OpenCL/opencl.h>
//   3. <GLFW/glfw3.h>
//   4. ImGui headers
//   5. Project headers

#include <OpenGL/gl3.h>
#include <OpenCL/opencl.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "gpu/gl_check.h"
#include "gpu/compute_pipeline.h"
#include "gpu/render_pipeline.h"
#include "gpu/buffer_manager.h"
#include "sim/cloth.h"
#include "sim/params.h"
#include "sim/interaction.h"
#include "render/cloth_mesh.h"
#include "render/bowl_mesh.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

// ── Debug output callback (GL_ARB_debug_output) ───────────────────────────────
// glDebugMessageCallback is OpenGL 4.3 core and unavailable on macOS 4.1.
// The ARB extension equivalent is used instead; loaded at runtime via GLFW.
#ifndef NDEBUG
static void setupGLDebugCallback()
{
    using DebugCB = void (*)(GLenum, GLenum, GLuint, GLenum, GLsizei,
                             const GLchar*, const void*);
    using SetCBFn = void (*)(DebugCB, const void*);

    auto fn = reinterpret_cast<SetCBFn>(
        glfwGetProcAddress("glDebugMessageCallbackARB"));

    if (fn) {
        fn([](GLenum /*src*/, GLenum type, GLuint /*id*/, GLenum severity,
              GLsizei /*len*/, const GLchar* msg, const void* /*user*/)
           {
               // Ignore notifications; log warnings and errors
               if (severity != 0x826Bu) // GL_DEBUG_SEVERITY_NOTIFICATION_ARB
                   fprintf(stderr, "[GL DEBUG type=0x%04x] %s\n", type, msg);
           },
           nullptr);
        printf("OpenGL debug output enabled (GL_ARB_debug_output)\n");
    } else {
        printf("OpenGL debug output unavailable on this context\n");
    }
}
#endif // NDEBUG

// ── OpenCL availability probe ─────────────────────────────────────────────────
static void probeOpenCL()
{
    cl_uint numPlatforms = 0;
    clGetPlatformIDs(0, nullptr, &numPlatforms);

    if (numPlatforms == 0) {
        printf("WARNING: No OpenCL GPU found\n");
        return;
    }

    std::vector<cl_platform_id> platforms(numPlatforms);
    clGetPlatformIDs(numPlatforms, platforms.data(), nullptr);

    for (cl_uint i = 0; i < numPlatforms; ++i) {
        cl_uint numDevices = 0;
        cl_int  err = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU,
                                     0, nullptr, &numDevices);
        if (err != CL_SUCCESS || numDevices == 0)
            continue;

        std::vector<cl_device_id> devices(numDevices);
        clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU,
                       numDevices, devices.data(), nullptr);

        char name[256] = {};
        clGetDeviceInfo(devices[0], CL_DEVICE_NAME, sizeof(name), name, nullptr);
        printf("OpenCL GPU found: %s\n", name);
        return;
    }

    printf("WARNING: No OpenCL GPU found\n");
}

// ── GLFW error callback ───────────────────────────────────────────────────────
static void glfwErrorCallback(int code, const char* desc)
{
    fprintf(stderr, "[GLFW] error %d: %s\n", code, desc);
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main()
{
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        fprintf(stderr, "GLFW init failed\n");
        return 1;
    }

    // Request OpenGL 4.1 core profile (macOS maximum; compute via OpenCL)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE); // required on macOS

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Virtual Saran Wrap",
                                          nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    // ── Print OpenGL version and renderer ─────────────────────────────────────
    printf("OpenGL %s — %s\n",
           reinterpret_cast<const char*>(glGetString(GL_VERSION)),
           reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    fflush(stdout);

    // ── Register debug callback (Debug builds only) ───────────────────────────
#ifndef NDEBUG
    setupGLDebugCallback();
    fflush(stdout);
#endif

    // ── OpenCL availability probe ─────────────────────────────────────────────
    probeOpenCL();
    fflush(stdout);

    // ── ImGui init (wired up but unused until Sprint 4) ───────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, /*install_callbacks=*/true);
    ImGui_ImplOpenGL3_Init("#version 410");

    // ── Render loop ───────────────────────────────────────────────────────────
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // TODO (Sprint 2): dispatch OpenCL compute passes here
        // TODO (Sprint 3): draw cloth mesh and bowl mesh here

        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ImGui frame (no UI yet — placeholder for Sprint 4 sliders)
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
