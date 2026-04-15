#pragma once

// BufferManager — allocates and manages all GPU buffers for the simulation.
//
// OpenGL SSBOs hold particle data and are shared with OpenCL via CL/GL interop
// (clCreateFromGLBuffer). No CPU readback is needed per frame.
//
// OpenGL SSBO / UBO binding assignments (must match GLSL/CL declarations):
//   binding 0 — particle positions A  (ping-pong read)
//   binding 1 — particle positions B  (ping-pong write)
//   binding 2 — (unused here; velocities stored inline in particle struct)
//   binding 3 — SimParams UBO
//   binding 4 — springs index buffer
//   binding 5 — per-face thickness output

#include <OpenGL/gl3.h>
#include <OpenCL/opencl.h>

class BufferManager {
public:
    BufferManager();
    ~BufferManager();

    // TODO: Allocate two GL SSBOs (posA, posB) for ping-pong particle data.
    //       Each element is Particle { vec4 pos; vec4 vel; } = 32 bytes (std430).
    //       Binds posA to binding 0, posB to binding 1.
    void allocateParticleBuffers(int numParticles);

    // TODO: Allocate SimParams UBO (binding 3); size = sizeof(SimParams).
    void allocateParamsUBO();

    // TODO: Allocate springs SSBO (binding 4).
    //       Each element: int indexA, int indexB, float restLen, float pad = 16 bytes.
    void allocateSpringBuffer(int numSprings);

    // TODO: Allocate per-face thickness output SSBO (binding 5).
    //       Each element: float thickness_nm = 4 bytes.
    void allocateThicknessBuffer(int numFaces);

    // TODO: Create CL cl_mem wrappers from all GL SSBOs via clCreateFromGLBuffer.
    //       Must be called after all GL buffers are allocated and before first CL dispatch.
    void createCLBuffers(cl_context ctx);

    // TODO: Acquire GL buffers for CL use — call before each frame's CL dispatches.
    //       (clEnqueueAcquireGLObjects on posA, posB, springs, thickness)
    void acquireForCL(cl_command_queue queue);

    // TODO: Release GL buffers back to OpenGL — call after all CL dispatches,
    //       before glDrawElements. (clEnqueueReleaseGLObjects)
    void releaseFromCL(cl_command_queue queue);

    // TODO: Swap ping-pong roles — exchange posA↔posB (both GL names and CL handles).
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
