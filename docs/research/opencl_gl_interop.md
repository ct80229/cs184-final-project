# OpenCL / OpenGL Interop on macOS

## Summary

macOS uses CGL (Core OpenGL) as the platform layer beneath GLFW. OpenCL gains
access to GL buffers by sharing the CGL share group at context creation time —
no data copies between CPU and GPU per frame. The acquire/release pattern
(`clEnqueueAcquireGLObjects` / `clEnqueueReleaseGLObjects`) enforces exclusive
ownership: CL and GL cannot both touch a shared buffer simultaneously, and
`clFinish` must complete before any GL draw that reads that buffer.

---

## Key Concepts

### CGL Share Group (macOS-specific)

GLFW creates a CGL context internally. You extract it at init time via:

```cpp
#include <OpenGL/OpenGL.h>   // CGLGetCurrentContext, CGLGetShareGroup
#include <OpenCL/opencl.h>

CGLContextObj   cgl_ctx = CGLGetCurrentContext();
CGLShareGroupObj cgl_sg  = CGLGetShareGroup(cgl_ctx);
```

The share group is then passed as a property when creating the CL context. This
tells the OpenCL runtime to use the same GPU device as the GL context and
enables `clCreateFromGLBuffer`.

### `CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE`

This Apple extension replaces the cross-platform
`CL_GL_CONTEXT_KHR` / `CL_CGL_SHAREGROUP_KHR` approach. On macOS you **must**
use the Apple variant — the Khronos variant is not supported:

```cpp
cl_context_properties props[] = {
    CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE,
    (cl_context_properties)cgl_sg,
    0
};
// Device count = 0, device list = NULL: Apple extension selects the device
// that matches the GL context automatically.
cl_int err;
cl_context cl_ctx = clCreateContext(props, 0, NULL, NULL, NULL, &err);
CHECK_CL_ERROR(err);
```

### Getting the Matched Device

After creating the CL context, retrieve the device associated with the current
GL virtual screen (needed to create the command queue):

```cpp
cl_device_id device;
err = clGetGLContextInfoAPPLE(
    cl_ctx, cgl_ctx,
    CL_CGL_DEVICE_FOR_CURRENT_VIRTUAL_SCREEN_APPLE,
    sizeof(device), &device, NULL);
CHECK_CL_ERROR(err);

cl_command_queue queue = clCreateCommandQueue(cl_ctx, device, 0, &err);
CHECK_CL_ERROR(err);
```

---

## Full Init Sequence (Minimal Working Pattern)

```cpp
// --- 1. GLFW creates the OpenGL 4.1 context (must happen first) ---
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
GLFWwindow* window = glfwCreateWindow(1280, 720, "cloth_sim", nullptr, nullptr);
glfwMakeContextCurrent(window);   // <-- GL context is now current

// --- 2. Extract CGL handles ---
CGLContextObj    cgl_ctx = CGLGetCurrentContext();
CGLShareGroupObj cgl_sg  = CGLGetShareGroup(cgl_ctx);

// --- 3. Create CL context sharing the GL share group ---
cl_context_properties cl_props[] = {
    CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE, (cl_context_properties)cgl_sg,
    0
};
cl_int err;
cl_context cl_ctx = clCreateContext(cl_props, 0, NULL, NULL, NULL, &err);
CHECK_CL_ERROR(err);

// --- 4. Get device matched to current GL screen ---
cl_device_id device;
err = clGetGLContextInfoAPPLE(
    cl_ctx, cgl_ctx,
    CL_CGL_DEVICE_FOR_CURRENT_VIRTUAL_SCREEN_APPLE,
    sizeof(device), &device, nullptr);
CHECK_CL_ERROR(err);

// --- 5. Create command queue ---
cl_command_queue queue = clCreateCommandQueue(cl_ctx, device, 0, &err);
CHECK_CL_ERROR(err);

// --- 6. Create a GL SSBO and wrap it as a CL buffer ---
GLuint gl_pos_a;
glGenBuffers(1, &gl_pos_a);
glBindBuffer(GL_ARRAY_BUFFER, gl_pos_a);
glBufferData(GL_ARRAY_BUFFER, particle_bytes, init_data, GL_DYNAMIC_DRAW);
glBindBuffer(GL_ARRAY_BUFFER, 0);

cl_mem cl_pos_a = clCreateFromGLBuffer(cl_ctx, CL_MEM_READ_WRITE, gl_pos_a, &err);
CHECK_CL_ERROR(err);
```

---

## Per-Frame Acquire / Release Pattern

```cpp
// ---- Compute phase ----
// All shared cl_mem objects that any kernel touches must be acquired together.
cl_mem shared_bufs[] = { cl_pos_a, cl_pos_b, cl_vel };
err = clEnqueueAcquireGLObjects(queue, 3, shared_bufs, 0, nullptr, nullptr);
CHECK_CL_ERROR(err);

// Dispatch integrate kernel
clEnqueueNDRangeKernel(queue, integrate_kernel, 1,
    nullptr, &global_size, &local_size, 0, nullptr, nullptr);

// Dispatch constraints kernel (repeated substeps)
for (int s = 0; s < substeps; s++) {
    clEnqueueNDRangeKernel(queue, constraints_kernel, ...);
    // swap ping-pong buffers via kernel arg update
}

// Dispatch thickness, adhesion kernels ...

// Release all shared objects before GL renders
err = clEnqueueReleaseGLObjects(queue, 3, shared_bufs, 0, nullptr, nullptr);
CHECK_CL_ERROR(err);

// CRITICAL: clFinish, not clFlush — must drain the queue before GL draws
err = clFinish(queue);
CHECK_CL_ERROR(err);

// ---- Render phase ----
glBindVertexArray(vao);          // vao references gl_pos_a
glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
```

---

## Workgroup Sizing — Intel UHD 630

| Property | Value |
|---|---|
| Max compute units | 24 EUs |
| Max workgroup size | 256 |
| Preferred workgroup multiple | 32 (SIMD-8 / SIMD-16 dispatch) |
| L1 / SLM per CU | 64 KB |

**Recommended local_size: 64.** This fits in one hardware workgroup, is a
multiple of 32, and leaves headroom for register spill. Matches the value in
CLAUDE.md.

```cpp
size_t local_size  = 64;
// Round particle count up to next multiple of local_size
size_t global_size = ((N * N + local_size - 1) / local_size) * local_size;
```

At 64×64 = 4096 particles: global_size = 4096, 64 workgroups of 64 threads —
well within device limits.

At 128×128 = 16384 particles: 256 workgroups of 64 — still fine.

---

## Gotchas

### 1. Context creation order is mandatory

The CGL context **must already be current** (`glfwMakeContextCurrent` called)
before you call `clCreateContext`. The share group handle is only valid after
the GL context is fully initialized.

### 2. `clFinish`, not `clFlush`

`clFlush` submits commands to the device but does not wait for completion.
If you call `glDrawElements` after `clFlush`, the GPU may still be running the
CL kernel — undefined behavior, likely visual corruption or a crash.
`clFinish` blocks until the queue is drained.

### 3. Acquire/Release must be symmetric and complete

You cannot partially release. If you acquire N objects, release all N before
the GL draw. Acquiring an already-acquired object (e.g., from a previous frame
that crashed before release) is a CL_INVALID_OPERATION error. In Debug builds,
track acquire state per buffer.

### 4. GL buffer must be unbound when creating the CL wrapper

Call `glBindBuffer(GL_ARRAY_BUFFER, 0)` before `clCreateFromGLBuffer`. Some
drivers reject creation if the buffer is currently bound.

### 5. VAO setup happens after both GL buffer and CL buffer are created

Set up your VAO (vertex attribute pointers) after the GL buffer is populated but
you do not need to re-bind or re-specify it each frame — the VAO retains the
attribute state, and the GL buffer object id is stable.

### 6. SimParams is a pure CL buffer, not a GL buffer

The `SimParams` struct does not need to be shared with GL (no vertex shader reads
it). Allocate it as a plain `cl_mem` with `clCreateBuffer` and upload with
`clEnqueueWriteBuffer` each frame. This avoids an unnecessary acquire/release.

### 7. Apple headers

On macOS, include order matters:
```cpp
#include <OpenGL/gl3.h>        // or use GLFW's include
#include <OpenGL/OpenGL.h>     // CGLGetCurrentContext, CGLGetShareGroup
#include <OpenCL/opencl.h>     // cl_* types and functions
```
Do NOT include `<GL/gl.h>` (old non-core header) alongside `<OpenGL/gl3.h>` —
symbol conflicts.

---

## CHECK_CL_ERROR Macro

```cpp
#define CHECK_CL_ERROR(err) \
    do { \
        if ((err) != CL_SUCCESS) { \
            fprintf(stderr, "CL error %d at %s:%d\n", (err), __FILE__, __LINE__); \
            std::abort(); \
        } \
    } while(0)
```
