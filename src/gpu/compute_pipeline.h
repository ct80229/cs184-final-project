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

    // TODO: Create CL context sharing the current CGL OpenGL context,
    //       create command queue, then load and compile all .cl kernels
    //       from src/shaders/ (integrate, constraints, thickness, adhesion).
    //       Returns false and prints diagnostics on any CL error.
    bool init(cl_device_id device);

    // TODO: Dispatch integrate.cl — semi-implicit Verlet integration,
    //       sphere SDF collision, NaN guard with atomic error counter.
    //       Reads posIn, writes posOut; call clEnqueueAcquireGLObjects first.
    void dispatchIntegrate(cl_mem posIn, cl_mem posOut, cl_mem vel,
                           cl_mem paramsUBO, int numParticles);

    // TODO: Dispatch constraints.cl — PBD spring corrections (Jacobi ping-pong).
    //       Call twice per substep: once forward (reverseOrder=false),
    //       once reverse (reverseOrder=true) to reduce directional bias.
    //       Skip particles where pos.w == 0.0 (pinned corner, mass_inv=0).
    void dispatchConstraints(cl_mem posIn, cl_mem posOut, cl_mem springs,
                             cl_mem paramsUBO, int numSprings, bool reverseOrder);

    // TODO: Dispatch thickness.cl — per-face area ratio → thickness_nm.
    //       thickness_nm = 12000.0 / clamp(area_deformed / rest_area, 0.1, 10.0)
    void dispatchThickness(cl_mem pos, cl_mem faceIndices, cl_mem thicknessOut,
                           int numFaces);

    // TODO: Dispatch adhesion.cl — Park & Byun §3.2 cohesion spring forces.
    //       Only processes particles flagged as surface-contact (vel.w bit 0).
    void dispatchAdhesion(cl_mem pos, cl_mem vel,
                          cl_mem paramsUBO, int numParticles);

    // TODO: clFinish — block until all queued CL commands have completed.
    //       Call before glDrawElements each frame.
    void finish();

    cl_context       context() const { return m_ctx; }
    cl_command_queue queue()   const { return m_queue; }

private:
    cl_context       m_ctx     = nullptr;
    cl_command_queue m_queue   = nullptr;
    cl_program       m_program = nullptr;

    cl_kernel m_kIntegrate   = nullptr;
    cl_kernel m_kConstraints = nullptr;
    cl_kernel m_kThickness   = nullptr;
    cl_kernel m_kAdhesion    = nullptr;

    // TODO: Load a .cl source file from disk and return its contents as a string.
    std::string loadSource(const std::string& path);
};
