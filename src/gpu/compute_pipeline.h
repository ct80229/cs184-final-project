#pragma once

// ComputePipeline — wraps OpenCL kernel loading and dispatch for all compute passes.
//
// All GPU compute work uses OpenCL (.cl kernels), not GLSL compute shaders.
// macOS caps OpenGL at 4.1; GL 4.3 compute shaders are unavailable.
// CL/GL buffer interop is handled via clCreateFromGLBuffer — no CPU readback per frame.
//
// Kernel dispatch order each frame (see CLAUDE.md GPU Pipeline section):
//   1. dispatchIntegrate
//   2. dispatchConstraints  ×(2 × substeps)
//   3. dispatchThickness
//   4. dispatchAdhesion
//   finish() / releaseFromCL before glDrawElements

#include <OpenCL/opencl.h>
#include <string>

class ComputePipeline {
public:
    ComputePipeline();
    ~ComputePipeline();

    // Creates CL context sharing the current CGL OpenGL context,
    // creates command queue, loads and compiles all .cl kernels from src/shaders/.
    // Returns false and prints diagnostics on any CL error.
    bool init(cl_device_id device);

    // Dispatches integrate.cl — semi-implicit Verlet integration,
    // sphere SDF collision, NaN guard with atomic error counter.
    // vel is repurposed as the error counter buffer (velocities are inline
    // in the Particle struct). Reads posIn, writes posOut.
    void dispatchIntegrate(cl_mem posIn, cl_mem posOut, cl_mem vel,
                           cl_mem paramsUBO, int numParticles);

    // Dispatches constraints.cl — serial Gauss-Seidel PBD spring corrections.
    // Copies posIn→posOut first, then iterates all springs in-place.
    // Call twice per substep: forward (reverseOrder=false) then reverse (true).
    void dispatchConstraints(cl_mem posIn, cl_mem posOut, cl_mem springs,
                             cl_mem paramsUBO, int numSprings, bool reverseOrder);

    // Dispatches thickness.cl — per-face area ratio → thickness_nm.
    // Stub until Sprint 3 wires up restAreas buffer.
    void dispatchThickness(cl_mem pos, cl_mem faceIndices, cl_mem thicknessOut,
                           int numFaces);

    // Dispatches adhesion.cl — Park & Byun §3.2 cohesion spring forces.
    // Only processes particles flagged as surface-contact (vel.w bit 0).
    // Stub until Sprint 3 surface contact detection is wired up.
    void dispatchAdhesion(cl_mem pos, cl_mem vel,
                          cl_mem paramsUBO, int numParticles);

    // Blocks until all queued CL commands have completed (clFinish).
    // Must be called before any glDrawElements each frame.
    void finish();

    cl_context       context()          const { return m_ctx;        }
    cl_command_queue queue()            const { return m_queue;      }
    // Error counter buffer — incremented by integrate.cl on NaN reset.
    // Passed as the `vel` argument to dispatchIntegrate.
    cl_mem           errorCountBuffer() const { return m_errorCount; }

private:
    cl_context       m_ctx   = nullptr;
    cl_command_queue m_queue = nullptr;

    cl_kernel m_kIntegrate   = nullptr;
    cl_kernel m_kConstraints = nullptr;
    cl_kernel m_kThickness   = nullptr;
    cl_kernel m_kAdhesion    = nullptr;

    // Atomic NaN error counter — incremented by integrate.cl on particle reset.
    // Passed as the `vel` argument to dispatchIntegrate (velocities are inline
    // in the Particle struct, so `vel` is repurposed for the error counter).
    cl_mem m_errorCount = nullptr;

    std::string loadSource(const std::string& path);
};
