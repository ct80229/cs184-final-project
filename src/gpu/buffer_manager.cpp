#include "gpu/buffer_manager.h"
#include "gpu/gl_check.h"
#include "sim/params.h"   // for sizeof(SimParams)

#include <cstdio>
#include <utility>  // std::swap

BufferManager::BufferManager() = default;

BufferManager::~BufferManager()
{
    // Release CL wrappers first (they reference the GL objects)
    if (m_clPosA)      clReleaseMemObject(m_clPosA);
    if (m_clPosB)      clReleaseMemObject(m_clPosB);
    if (m_clSprings)   clReleaseMemObject(m_clSprings);
    if (m_clThickness) clReleaseMemObject(m_clThickness);

    // Delete GL buffers
    if (m_posA)      { glDeleteBuffers(1, &m_posA);      m_posA      = 0; }
    if (m_posB)      { glDeleteBuffers(1, &m_posB);      m_posB      = 0; }
    if (m_paramsUBO) { glDeleteBuffers(1, &m_paramsUBO); m_paramsUBO = 0; }
    if (m_springs)   { glDeleteBuffers(1, &m_springs);   m_springs   = 0; }
    if (m_thickness) { glDeleteBuffers(1, &m_thickness); m_thickness = 0; }
}

void BufferManager::allocateParticleBuffers(int numParticles)
{
    // Particle = vec4 pos (16 bytes) + vec4 vel (16 bytes) = 32 bytes.
    // GL_ARRAY_BUFFER used here — GL_SHADER_STORAGE_BUFFER requires OpenGL 4.3
    // (unavailable on macOS 4.1).  clCreateFromGLBuffer works with any GL buffer
    // object regardless of its last bind target.
    GLsizeiptr size = static_cast<GLsizeiptr>(numParticles) * 32;

    glGenBuffers(1, &m_posA);
    glBindBuffer(GL_ARRAY_BUFFER, m_posA);
    glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
    CHECK_GL_ERROR();

    glGenBuffers(1, &m_posB);
    glBindBuffer(GL_ARRAY_BUFFER, m_posB);
    glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
    CHECK_GL_ERROR();

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    printf("[BufferManager] particle buffers: 2 × %d bytes (posA/B)\n",
           (int)size);
}

void BufferManager::allocateParamsUBO()
{
    // Allocate as GL_ARRAY_BUFFER so clCreateFromGLBuffer can wrap it.
    // The UBO binding (binding 3) is set when a shader program binds it.
    // For Sprint 2, SimParams are passed directly as a CL buffer; this GL buffer
    // is a placeholder for the Sprint 3 vertex/fragment shader pipeline.
    glGenBuffers(1, &m_paramsUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, m_paramsUBO);
    glBufferData(GL_UNIFORM_BUFFER,
                 static_cast<GLsizeiptr>(sizeof(SimParams)),
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, m_paramsUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    CHECK_GL_ERROR();
    printf("[BufferManager] params UBO: %d bytes\n", (int)sizeof(SimParams));
}

void BufferManager::allocateSpringBuffer(int numSprings)
{
    // Spring element: int indexA + int indexB + float restLen + float pad = 16 bytes.
    GLsizeiptr size = static_cast<GLsizeiptr>(numSprings) * 16;

    glGenBuffers(1, &m_springs);
    glBindBuffer(GL_ARRAY_BUFFER, m_springs);
    glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    CHECK_GL_ERROR();
    printf("[BufferManager] spring buffer: %d bytes (%d springs)\n",
           (int)size, numSprings);
}

void BufferManager::allocateThicknessBuffer(int numFaces)
{
    // Per-face thickness: 1 float = 4 bytes each.
    GLsizeiptr size = static_cast<GLsizeiptr>(numFaces) * 4;

    glGenBuffers(1, &m_thickness);
    glBindBuffer(GL_ARRAY_BUFFER, m_thickness);
    glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    CHECK_GL_ERROR();
    printf("[BufferManager] thickness buffer: %d bytes (%d faces)\n",
           (int)size, numFaces);
}

void BufferManager::createCLBuffers(cl_context ctx)
{
    // Wrap existing GL SSBOs as CL memory objects.
    // GL buffers must already be fully allocated (glBufferData called).
    // These buffers are used in an interleaved acquire/release pattern each frame.
    cl_int err;

    m_clPosA = clCreateFromGLBuffer(ctx, CL_MEM_READ_WRITE, m_posA, &err);
    if (err != CL_SUCCESS)
        fprintf(stderr, "[CL] clCreateFromGLBuffer posA failed: %d\n", err);

    m_clPosB = clCreateFromGLBuffer(ctx, CL_MEM_READ_WRITE, m_posB, &err);
    if (err != CL_SUCCESS)
        fprintf(stderr, "[CL] clCreateFromGLBuffer posB failed: %d\n", err);

    m_clSprings = clCreateFromGLBuffer(ctx, CL_MEM_READ_ONLY, m_springs, &err);
    if (err != CL_SUCCESS)
        fprintf(stderr, "[CL] clCreateFromGLBuffer springs failed: %d\n", err);

    m_clThickness = clCreateFromGLBuffer(ctx, CL_MEM_WRITE_ONLY, m_thickness, &err);
    if (err != CL_SUCCESS)
        fprintf(stderr, "[CL] clCreateFromGLBuffer thickness failed: %d\n", err);

    printf("[BufferManager] CL interop buffers created\n");
}

void BufferManager::acquireForCL(cl_command_queue queue)
{
    // Transfer ownership of the four shared buffers from OpenGL to OpenCL.
    // No GL operations may touch these buffers until releaseFromCL().
    cl_mem objects[] = { m_clPosA, m_clPosB, m_clSprings, m_clThickness };
    cl_int err = clEnqueueAcquireGLObjects(queue, 4, objects, 0, nullptr, nullptr);
    CHECK_CL_ERROR(err);
}

void BufferManager::releaseFromCL(cl_command_queue queue)
{
    // Return ownership of the shared buffers back to OpenGL.
    // Call clFinish (or wait on an event) before glDrawElements.
    cl_mem objects[] = { m_clPosA, m_clPosB, m_clSprings, m_clThickness };
    cl_int err = clEnqueueReleaseGLObjects(queue, 4, objects, 0, nullptr, nullptr);
    CHECK_CL_ERROR(err);
}

void BufferManager::swapPingPong()
{
    // Exchange the A↔B roles so the last-written buffer becomes the next read.
    std::swap(m_posA,   m_posB);
    std::swap(m_clPosA, m_clPosB);
    // GL binding-point rebinding (SSBO) deferred to Sprint 3 render pipeline.
    // CL kernels use the cl_mem handles (m_clPosA / m_clPosB) directly.
}
