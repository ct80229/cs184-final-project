#pragma once

// BufferManager — allocates and manages all GPU buffers for the simulation.
//
// All particle/spring/thickness data lives in GL_ARRAY_BUFFER objects (not SSBOs;
// GL_SHADER_STORAGE_BUFFER requires OpenGL 4.3 which is unavailable on macOS 4.1).
// Buffers are shared with OpenCL via clCreateFromGLBuffer — no CPU readback per frame.
//
// SimParams is a separate CL-only buffer (clCreateBuffer); it is NOT a GL UBO for
// the compute pipeline.  A GL_UNIFORM_BUFFER is allocated for binding 3 as a
// placeholder for future vertex/fragment shader access.
//
// GL buffer roles (no fixed binding slots — accessed by GL ID or CL handle):
//   posA / posB  — ping-pong particle buffers (Particle = vec4 pos + vec4 vel, 32 bytes)
//   springs      — spring topology (indexA, indexB, restLen, pad = 16 bytes each)
//   thickness    — per-face thickness output (float, 4 bytes each)
//   paramsUBO    — GL_UNIFORM_BUFFER binding 3 (placeholder)

#include <OpenGL/gl3.h>
#include <OpenCL/opencl.h>

class BufferManager {
public:
    BufferManager();
    ~BufferManager();

    // Allocates two GL_ARRAY_BUFFER objects (posA, posB) for ping-pong particle data.
    // Each element is Particle { vec4 pos; vec4 vel; } = 32 bytes.
    void allocateParticleBuffers(int numParticles);

    // Allocates a GL_UNIFORM_BUFFER for SimParams (binding 3).
    // SimParams are passed to CL kernels separately via clEnqueueWriteBuffer.
    void allocateParamsUBO();

    // Allocates a GL_ARRAY_BUFFER for spring data.
    // Each element: int indexA, int indexB, float restLen, float pad = 16 bytes.
    void allocateSpringBuffer(int numSprings);

    // Allocates a GL_ARRAY_BUFFER for per-face thickness output.
    // Each element: float thickness_nm = 4 bytes.
    void allocateThicknessBuffer(int numFaces);

    // Creates CL cl_mem wrappers from all shared GL buffers via clCreateFromGLBuffer.
    // Must be called after all GL buffers are allocated and before the first CL dispatch.
    void createCLBuffers(cl_context ctx);

    // Acquires the four shared GL buffers for CL use (clEnqueueAcquireGLObjects).
    // No GL operations may touch these buffers until releaseFromCL().
    void acquireForCL(cl_command_queue queue);

    // Returns the shared GL buffers back to OpenGL (clEnqueueReleaseGLObjects).
    // Call clFinish (via ComputePipeline::finish) before any glDraw* call.
    void releaseFromCL(cl_command_queue queue);

    // Swaps ping-pong roles — exchanges posA↔posB GL IDs and CL handles.
    // Must be called after each CL kernel write so posA always holds the latest result.
    void swapPingPong();

    // ── GL buffer accessors ───────────────────────────────────────────────────
    GLuint posBufferA()      const { return m_posA;      }
    GLuint posBufferB()      const { return m_posB;      }
    GLuint paramsUBO()       const { return m_paramsUBO; }
    GLuint springBuffer()    const { return m_springs;   }
    GLuint thicknessBuffer() const { return m_thickness; }

    // ── CL buffer accessors ───────────────────────────────────────────────────
    cl_mem clPosA()      const { return m_clPosA;     }
    cl_mem clPosB()      const { return m_clPosB;     }
    cl_mem clSprings()   const { return m_clSprings;  }
    cl_mem clThickness() const { return m_clThickness;}

private:
    GLuint m_posA      = 0;
    GLuint m_posB      = 0;
    GLuint m_paramsUBO = 0;
    GLuint m_springs   = 0;
    GLuint m_thickness = 0;

    cl_mem m_clPosA      = nullptr;
    cl_mem m_clPosB      = nullptr;
    cl_mem m_clSprings   = nullptr;
    cl_mem m_clThickness = nullptr;
};
