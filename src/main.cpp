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

#include <glm/gtc/matrix_transform.hpp>  // glm::perspective, glm::lookAt, glm::radians

#include <cstdio>
#include <cstdlib>
#include <vector>

// ── Global state ─────────────────────────────────────────────────────────────
// Keyboard state
static bool g_paused          = false;
static bool g_wireframe       = false;
static bool g_resetFlag       = false;   // set by 'R' key, consumed in render loop
static bool g_debugThickness  = false;   // set by 'T' key, toggles heatmap

// n_film ImGui slider — updated per frame so clothPipeline.setFloat picks it up
static float g_nFilm          = 1.5f;

// Mouse interaction — pointers set after objects are constructed, before render loop.
// Callbacks fire from glfwPollEvents() which is always outside acquireForCL.
static Interaction*    g_interaction = nullptr;
static SimParams*      g_simParams   = nullptr;
static Cloth*          g_cloth       = nullptr;
static BufferManager*  g_buffers     = nullptr;
// Camera matrices updated each frame so callbacks can unproject with current values.
static glm::mat4       g_proj{1.0f};
static glm::mat4       g_view{1.0f};

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

// ── GLFW key callback ─────────────────────────────────────────────────────────
static void keyCallback(GLFWwindow* window, int key, int /*scancode*/,
                        int action, int /*mods*/)
{
    if (action != GLFW_PRESS) return;

    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        break;
    case GLFW_KEY_P:
        g_paused = !g_paused;
        printf("[Input] Simulation %s\n", g_paused ? "paused" : "resumed");
        break;
    case GLFW_KEY_W:
        g_wireframe = !g_wireframe;
        glPolygonMode(GL_FRONT_AND_BACK, g_wireframe ? GL_LINE : GL_FILL);
        printf("[Input] Wireframe %s\n", g_wireframe ? "ON" : "OFF");
        break;
    case GLFW_KEY_R:
        g_resetFlag = true;
        printf("[Input] Reset cloth to rest\n");
        break;
    case GLFW_KEY_T:
        g_debugThickness = !g_debugThickness;
        printf("[Input] Thickness heatmap %s\n", g_debugThickness ? "ON" : "OFF");
        break;
    default:
        break;
    }
}

// ── GLFW mouse button callback ────────────────────────────────────────────────
// Registered BEFORE ImGui_ImplGlfw_InitForOpenGL so ImGui chains to us correctly.
// Fires from glfwPollEvents() — always outside the CL acquire/release window,
// so glGetBufferSubData (called by syncPositionsFromGPU) is safe.
static void mouseButtonCallback(GLFWwindow* window, int button,
                                int action, int /*mods*/)
{
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (!g_interaction || !g_simParams || !g_cloth || !g_buffers) return;

    // Always process release — drag must end cleanly even if the cursor moved
    // over the ImGui overlay before the button was released.
    if (action == GLFW_RELEASE) {
        g_interaction->onMouseRelease(*g_simParams);
        return;
    }

    // For new grabs (press): suppress if ImGui wants the click.
    if (ImGui::GetIO().WantCaptureMouse) return;

    if (action == GLFW_PRESS) {
        // Sync CPU positions while GL owns the buffer (before acquireForCL this frame)
        g_cloth->syncPositionsFromGPU(*g_buffers);

        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        int winW, winH;
        glfwGetWindowSize(window, &winW, &winH);

        g_interaction->onMouseDown(mx, my, winW, winH,
                                   g_proj, g_view,
                                   g_cloth->cpuPositions(),
                                   g_cloth->numParticles(),
                                   g_cloth->gridSize(),
                                   *g_simParams);
    }
}

// ── GLFW cursor position callback ────────────────────────────────────────────
static void cursorPosCallback(GLFWwindow* window, double x, double y)
{
    if (!g_interaction || !g_interaction->isGrabbing()) return;
    if (!g_simParams) return;
    // No WantCaptureMouse check: once a drag is active the grab target must
    // follow the cursor even when it passes over the ImGui overlay.
    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);
    g_interaction->onMouseMove(x, y, winW, winH, g_proj, g_view, *g_simParams);
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
    glfwSetKeyCallback(window, keyCallback);
    // Register mouse callbacks BEFORE ImGui init so ImGui chains to them
    // (install_callbacks=true saves existing callbacks, calls them from its own handler)
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);

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

    // ── ImGui init ────────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, /*install_callbacks=*/true);
    ImGui_ImplOpenGL3_Init("#version 410");

    // ═════════════════════════════════════════════════════════════════════════
    // Sprint 2 — Cloth Mesh & Physics Core initialisation
    // ═════════════════════════════════════════════════════════════════════════

    // ── 1. Build cloth topology on CPU ────────────────────────────────────────
    Cloth cloth(64);
    cloth.init();
    cloth.buildSprings();

    // ── Interaction object — must outlive the render loop ─────────────────────
    Interaction interaction;

    // ── 2. Allocate GL buffers (SSBOs must exist before CL interop wraps them) ─
    BufferManager buffers;
    buffers.allocateParticleBuffers(cloth.numParticles());
    buffers.allocateParamsUBO();
    buffers.allocateSpringBuffer(cloth.numSprings());
    buffers.allocateThicknessBuffer(cloth.numFaces());

    // ── 3. Upload rest state to GPU ───────────────────────────────────────────
    cloth.uploadToGPU(buffers);
    CHECK_GL_ERROR();

    // ── 4. Initialise OpenCL pipeline (creates CL context sharing current CGL) ─
    ComputePipeline compute;
    if (!compute.init(nullptr)) {
        fprintf(stderr, "ComputePipeline init failed — aborting\n");
        glfwTerminate();
        return 1;
    }

    // ── 5. Create CL interop wrappers from the existing GL buffers ───────────
    buffers.createCLBuffers(compute.context());

    // ── 5b. Build and upload per-face data (face indices + rest areas) ────────
    // These are CL-only buffers (no GL access); never acquired/released with the
    // GL interop objects.  Computed once from the rest state and reused every frame.
    cloth.buildFaceData();
    cl_mem clFaceIndices = nullptr;
    cl_mem clRestAreas   = nullptr;
    cloth.uploadFaceDataToCL(compute.context(), clFaceIndices, clRestAreas);

    // ── 6. SimParams as a CL buffer (LEARNINGS.md: not a GL UBO for compute) ─
    SimParams simParams = defaultSimParams();
    cl_int clErr;
    cl_mem clParams = clCreateBuffer(
        compute.context(),
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(SimParams), &simParams, &clErr);
    if (clErr != CL_SUCCESS) {
        fprintf(stderr, "[CL] Failed to create SimParams buffer: %d\n", clErr);
        return 1;
    }

    printf("Sprint 2 init complete: cloth=%dx%d, springs=%d, faces=%d\n",
           cloth.gridSize(), cloth.gridSize(),
           cloth.numSprings(), cloth.numFaces());
    fflush(stdout);

    // ═════════════════════════════════════════════════════════════════════════
    // Sprint 3 — Rendering init
    // ═════════════════════════════════════════════════════════════════════════

    // ── Cloth shader pipeline ─────────────────────────────────────────────────
    RenderPipeline clothPipeline;
    if (!clothPipeline.loadShaders("src/shaders/cloth.vert",
                                   "src/shaders/cloth.frag")) {
        fprintf(stderr, "Failed to load cloth shaders — aborting\n");
        return 1;
    }

    // ── Cloth mesh: index buffer + TBO for particle positions ─────────────────
    // TBO wraps posBufferA at init time; rebindTBO() updates it each frame
    // to handle ping-pong ID alternation.
    ClothMesh clothMesh;
    clothMesh.init(cloth.gridSize(),
                   buffers.posBufferA(),
                   buffers.thicknessBuffer());

    // ── Bowl mesh: procedural upper hemisphere + Phong shader ─────────────────
    BowlMesh bowl;
    bowl.init();

    // ── Set uniforms that never change ────────────────────────────────────────
    // Sampler uniforms (texture unit indices, not GL object IDs):
    //   uPosTBO      = 0: particle TBO (GL_TEXTURE0)
    //   uThicknessTBO= 1: per-face thickness TBO (GL_TEXTURE1)
    // Grid and film uniforms (constant for now; ImGui sliders are Sprint 4):
    //   uGridSize    = 64
    //   uNFilm       = 1.5 (dry saran wrap refractive index)
    clothPipeline.bind();
    clothPipeline.setInt  ("uPosTBO",       0);
    clothPipeline.setInt  ("uThicknessTBO", 1);
    clothPipeline.setInt  ("uGridSize",     cloth.gridSize());
    // uNFilm is set per-frame so the ImGui n_film slider takes effect immediately
    clothPipeline.unbind();

    printf("Sprint 3 init complete: cloth and bowl rendering pipelines ready\n");
    fflush(stdout);

    // ── Wire up global pointers for mouse callbacks ───────────────────────────
    // Callbacks fire from glfwPollEvents() which is always outside acquireForCL,
    // so these pointers are safe to dereference there.
    g_interaction = &interaction;
    g_simParams   = &simParams;
    g_cloth       = &cloth;
    g_buffers     = &buffers;

    // ── Render loop ───────────────────────────────────────────────────────────
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // ── Handle cloth reset ('R' key) ───────────────────────────────────
        if (g_resetFlag) {
            g_resetFlag = false;
            // Must happen while GL has ownership (before acquire)
            cloth.resetToRest(buffers);
        }

        if (!g_paused) {
            // ── Push updated SimParams to CL ──────────────────────────────
            CHECK_CL_ERROR(clEnqueueWriteBuffer(
                compute.queue(), clParams, CL_FALSE, 0,
                sizeof(SimParams), &simParams, 0, nullptr, nullptr));

            // ── Transfer buffer ownership: GL → CL ────────────────────────
            buffers.acquireForCL(compute.queue());

            // ── Integrate: posA → posB, then swap so posA = result ────────
            // The `m_errorCount` buffer in ComputePipeline is passed via the
            // `vel` parameter (velocities are inline; vel repurposed as error counter).
            compute.dispatchIntegrate(
                buffers.clPosA(), buffers.clPosB(),
                compute.errorCountBuffer(),   // repurposed as error counter
                clParams, cloth.numParticles());
            buffers.swapPingPong();  // posA now holds integrated positions

            // ── Constraint solve: substeps × (forward + reverse pass) ─────
            for (int s = 0; s < simParams.substeps; ++s) {
                // Forward pass: posA → posB, swap
                compute.dispatchConstraints(
                    buffers.clPosA(), buffers.clPosB(),
                    buffers.clSprings(), clParams,
                    cloth.numSprings(), false);
                buffers.swapPingPong();

                // Reverse pass: posA → posB, swap
                compute.dispatchConstraints(
                    buffers.clPosA(), buffers.clPosB(),
                    buffers.clSprings(), clParams,
                    cloth.numSprings(), true);
                buffers.swapPingPong();
            }

            // ── Thickness: per-face area ratio → thickness_nm ────────────
            // Reads posA (constraint-solved positions), writes to thicknessBuffer.
            // clFaceIndices/clRestAreas are CL-only — not in acquire/release pair.
            compute.dispatchThickness(
                buffers.clPosA(), clFaceIndices, clRestAreas,
                buffers.clThickness(), cloth.numFaces());

            // ── Adhesion: Park & Byun §3.2 cohesion springs ───────────────
            // Reads pos.xyz from neighbours; writes vel.xyz only for contact particles.
            // Each work item writes only to its own index — no write races.
            compute.dispatchAdhesion(buffers.clPosA(), clParams,
                                     cloth.numParticles());

            // ── Transfer buffer ownership back: CL → GL ───────────────────
            buffers.releaseFromCL(compute.queue());

            // ── Block until all CL work completes before drawing ──────────
            compute.finish();

            // ── Update TBO to current posA ────────────────────────────────
            // ping-pong swaps the GL buffer ID behind posBufferA() each frame;
            // the TBO must be re-pointed at the new ID before drawing.
            clothMesh.rebindTBO(buffers.posBufferA());

        }

        // ── Clear frame ────────────────────────────────────────────────────
        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.82f, 0.84f, 0.86f, 1.0f);  // light grey — wrap reads as tint against bright bg
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        CHECK_GL_ERROR();

        // ── Camera / MVP ───────────────────────────────────────────────────
        // Static camera for the milestone; mouse orbit is Sprint 4.
        float aspect = (fbH > 0) ? static_cast<float>(fbW) / static_cast<float>(fbH)
                                  : 1.0f;
        glm::vec3 cameraEye(0.0f, 1.5f, 2.5f);  // extracted for uCameraPos uniform
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.01f, 100.0f);
        glm::mat4 view = glm::lookAt(
            cameraEye,
            glm::vec3(0.0f, -0.3f, 0.0f),   // target: slightly below cloth centre
            glm::vec3(0.0f,  1.0f, 0.0f));  // up
        glm::mat4 mvp  = proj * view;        // model = identity for cloth

        // Keep globals in sync so mouse callbacks can unproject with current matrices
        g_proj = proj;
        g_view = view;

        glEnable(GL_DEPTH_TEST);

        // ── Draw bowl (Phong, grey-blue) ───────────────────────────────────
        // Enable face culling for the bowl (full sphere) so the back faces
        // of the lower hemisphere don't bleed through the upper hemisphere.
        glm::vec3 lightPos(2.0f, 3.0f, 2.0f);
        glEnable(GL_CULL_FACE);
        bowl.draw(glm::mat4(1.0f), view, proj, lightPos);
        glDisable(GL_CULL_FACE);
        CHECK_GL_ERROR();

        // ── Draw cloth ────────────────────────────────────────────────────
        // Alpha blending: cloth is semi-transparent (alpha 0.75–0.92).
        // Bowl is drawn first and depth-tested correctly; blending shows it through.
        // Wireframe: glPolygonMode (W key) + GL_TRIANGLES draws all 3 edges.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        clothPipeline.bind();
        clothPipeline.setMat4 ("uMVP",            mvp);
        clothPipeline.setVec3 ("uCameraPos",      cameraEye);
        clothPipeline.setInt  ("uDebugThickness", g_debugThickness ? 1 : 0);
        clothPipeline.setFloat("uNFilm",          g_nFilm);
        clothMesh.draw(GL_TRIANGLES);
        clothPipeline.unbind();

        glDisable(GL_BLEND);
        CHECK_GL_ERROR();

        // ── ImGui frame ────────────────────────────────────────────────────
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ── ImGui overlay ──────────────────────────────────────────────────
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(340, 225), ImGuiCond_Always);
        ImGui::Begin("Sim", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse);

        ImGui::Text("Cloth: %dx%d  Springs: %d",
                    cloth.gridSize(), cloth.gridSize(), cloth.numSprings());
        ImGui::Text("Status: %s  (P=pause W=wire R=reset T=heatmap)",
                    g_paused ? "PAUSED" : "running");
        ImGui::Text("Grab: %s",
                    g_interaction && g_interaction->isGrabbing() ? "ACTIVE" : "none");
        ImGui::Separator();

        // Stiffness: low values (~5) let cloth stretch far enough to produce
        // visible iridescence (requires ~15x area strain → thickness < 800nm).
        if (ImGui::SliderFloat("Stiffness", &simParams.stiffness, 5.0f, 200.0f, "%.0f")) {
            simParams.adhesion_k = 0.1f * simParams.stiffness;
        }
        ImGui::SliderFloat("Damping",   &simParams.damping,   0.95f, 0.999f, "%.3f");
        ImGui::SliderInt  ("Substeps",  &simParams.substeps,  4,     32);
        // n_film: 1.5 = dry saran wrap; 1.33 = wet — shifts iridescence hue bands
        ImGui::SliderFloat("n_film",    &g_nFilm,             1.33f, 1.6f,   "%.2f");

        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    clReleaseMemObject(clFaceIndices);
    clReleaseMemObject(clRestAreas);
    clReleaseMemObject(clParams);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
