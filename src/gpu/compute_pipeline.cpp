// compute_pipeline.cpp — OpenCL context (CGL sharegroup), kernel compilation,
// and dispatch wrappers for integrate, constraints, thickness, adhesion.
//
// Platform: macOS, OpenGL 4.1 + OpenCL via CGL sharegroup interop.
// All compute work uses OpenCL kernels (.cl); no GLSL compute shaders.
//
// CL/GL interop: clCreateContext with CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE
// automatically shares all GL objects (SSBOs, VBOs) with the CL context —
// no explicit device list needed; the context adopts the GPU backing the GL context.

#include "gpu/compute_pipeline.h"
#include "gpu/gl_check.h"

#include <OpenGL/OpenGL.h>   // CGLGetCurrentContext, CGLShareGroupObj
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ── Internal helper: print CL build log ──────────────────────────────────────
static void printBuildLog(cl_program prog, cl_device_id dev, const char* label)
{
    size_t logLen = 0;
    clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logLen);
    if (logLen > 1) {
        std::vector<char> log(logLen);
        clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG,
                              logLen, log.data(), nullptr);
        fprintf(stderr, "[CL BUILD %s]\n%s\n", label, log.data());
    }
}

// ── Compile one .cl file and extract a named kernel ──────────────────────────
// Returns nullptr on any error; caller does NOT release the program
// (program is released here after kernel is extracted — kernel holds ref).
static cl_kernel buildKernel(cl_context ctx, cl_device_id dev,
                              const std::string& src, const char* kernelName)
{
    const char* srcp   = src.c_str();
    size_t      srcLen = src.size();

    cl_int err;
    cl_program prog = clCreateProgramWithSource(ctx, 1, &srcp, &srcLen, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[CL] clCreateProgramWithSource failed (%s): %d\n",
                kernelName, err);
        return nullptr;
    }

    // Build flags: strict aliasing off is safer for our type-punning patterns.
    const char* buildFlags = "";
    err = clBuildProgram(prog, 1, &dev, buildFlags, nullptr, nullptr);
    printBuildLog(prog, dev, kernelName);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[CL] clBuildProgram failed (%s): %d\n", kernelName, err);
        clReleaseProgram(prog);
        return nullptr;
    }

    cl_kernel k = clCreateKernel(prog, kernelName, &err);
    // The kernel holds an implicit reference to the program.
    // Releasing the program here is safe — it will be destroyed when the kernel is released.
    clReleaseProgram(prog);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[CL] clCreateKernel failed (%s): %d\n", kernelName, err);
        return nullptr;
    }
    return k;
}

// ─────────────────────────────────────────────────────────────────────────────

ComputePipeline::ComputePipeline() = default;

ComputePipeline::~ComputePipeline()
{
    if (m_kIntegrate)   clReleaseKernel(m_kIntegrate);
    if (m_kConstraints) clReleaseKernel(m_kConstraints);
    if (m_kThickness)   clReleaseKernel(m_kThickness);
    if (m_kAdhesion)    clReleaseKernel(m_kAdhesion);
    if (m_errorCount)   clReleaseMemObject(m_errorCount);
    if (m_queue)        clReleaseCommandQueue(m_queue);
    if (m_ctx)          clReleaseContext(m_ctx);
}

bool ComputePipeline::init(cl_device_id /*device*/)
{
    // ── 1. Create CL context sharing the current CGL OpenGL context ───────────
    // Using CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE means the CL context
    // inherits the GPU device(s) that back the active GL context.
    // The device list is therefore empty (0 / nullptr) — the sharegroup determines it.
    CGLContextObj    cglCtx     = CGLGetCurrentContext();
    CGLShareGroupObj shareGroup = CGLGetShareGroup(cglCtx);
    if (!shareGroup) {
        fprintf(stderr, "[CL] CGLGetShareGroup returned null — is a GL context active?\n");
        return false;
    }

    cl_context_properties props[] = {
        CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE,
        reinterpret_cast<cl_context_properties>(shareGroup),
        0
    };

    cl_int err;
    m_ctx = clCreateContext(props, 0, nullptr, nullptr, nullptr, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[CL] clCreateContext (CGL sharegroup) failed: %d\n", err);
        return false;
    }

    // ── 2. Get the device from the context, create command queue ─────────────
    cl_device_id dev = nullptr;
    clGetContextInfo(m_ctx, CL_CONTEXT_DEVICES, sizeof(dev), &dev, nullptr);
    if (!dev) {
        fprintf(stderr, "[CL] No device found in CGL-shared context\n");
        return false;
    }

    {
        char name[256] = {};
        clGetDeviceInfo(dev, CL_DEVICE_NAME, sizeof(name), name, nullptr);
        printf("[CL] Using device: %s\n", name);
    }

    m_queue = clCreateCommandQueue(m_ctx, dev, 0, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[CL] clCreateCommandQueue failed: %d\n", err);
        return false;
    }

    // ── 3. Load and compile all four kernels from disk ────────────────────────
    // Paths are relative to project root (run as ./build/cloth_sim from project root).
    auto load = [&](const char* path) -> std::string {
        return loadSource(path);
    };

    std::string srcIntegrate   = load("src/shaders/integrate.cl");
    std::string srcConstraints = load("src/shaders/constraints.cl");
    std::string srcThickness   = load("src/shaders/thickness.cl");
    std::string srcAdhesion    = load("src/shaders/adhesion.cl");

    if (srcIntegrate.empty() || srcConstraints.empty() ||
        srcThickness.empty()  || srcAdhesion.empty()) {
        fprintf(stderr, "[CL] Failed to load one or more .cl source files\n");
        fprintf(stderr, "     Run the binary from the project root (./build/cloth_sim)\n");
        return false;
    }

    m_kIntegrate   = buildKernel(m_ctx, dev, srcIntegrate,   "integrate");
    m_kConstraints = buildKernel(m_ctx, dev, srcConstraints, "constraints");
    m_kThickness   = buildKernel(m_ctx, dev, srcThickness,   "thickness");
    m_kAdhesion    = buildKernel(m_ctx, dev, srcAdhesion,    "adhesion");

    if (!m_kIntegrate || !m_kConstraints || !m_kThickness || !m_kAdhesion) {
        fprintf(stderr, "[CL] Kernel compilation failed\n");
        return false;
    }

    // ── 4. Create the atomic error counter buffer ─────────────────────────────
    cl_int zero = 0;
    m_errorCount = clCreateBuffer(m_ctx,
                                  CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                  sizeof(cl_int), &zero, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[CL] clCreateBuffer errorCount failed: %d\n", err);
        return false;
    }

    printf("[CL] ComputePipeline ready: integrate, constraints, thickness, adhesion\n");
    return true;
}

// ── dispatchIntegrate ─────────────────────────────────────────────────────────
// Maps to integrate.cl kernel args:
//   arg 0: posIn      (posIn  — CL/GL shared buffer)
//   arg 1: posOut     (posOut — CL/GL shared buffer)
//   arg 2: params     (paramsUBO — CL buffer of SimParams as raw floats)
//   arg 3: errorCount (vel param repurposed; passed as error counter buffer)
//   arg 4: numParticles (cl_int)
//
// Global work size = numParticles (one work item per particle).
void ComputePipeline::dispatchIntegrate(cl_mem posIn, cl_mem posOut, cl_mem vel,
                                        cl_mem paramsUBO, int numParticles)
{
    cl_int err;
    cl_int np = static_cast<cl_int>(numParticles);

    // vel is the error counter buffer (velocities are inline in the Particle struct)
    CHECK_CL_ERROR(clSetKernelArg(m_kIntegrate, 0, sizeof(cl_mem), &posIn));
    CHECK_CL_ERROR(clSetKernelArg(m_kIntegrate, 1, sizeof(cl_mem), &posOut));
    CHECK_CL_ERROR(clSetKernelArg(m_kIntegrate, 2, sizeof(cl_mem), &paramsUBO));
    CHECK_CL_ERROR(clSetKernelArg(m_kIntegrate, 3, sizeof(cl_mem), &vel));
    CHECK_CL_ERROR(clSetKernelArg(m_kIntegrate, 4, sizeof(cl_int), &np));

    size_t global = static_cast<size_t>(numParticles);
    size_t local  = 64;  // safe workgroup size for integrated GPU
    // Round global up to multiple of local
    if (global % local != 0) global = ((global / local) + 1) * local;

    err = clEnqueueNDRangeKernel(m_queue, m_kIntegrate, 1,
                                 nullptr, &global, &local,
                                 0, nullptr, nullptr);
    CHECK_CL_ERROR(err);
}

// ── dispatchConstraints ───────────────────────────────────────────────────────
// Maps to constraints.cl kernel args:
//   arg 0: posIn      (read-only — original positions this pass)
//   arg 1: posOut     (read-write — constraint-corrected positions)
//   arg 2: springs    (spring index buffer)
//   arg 3: params     (SimParams)
//   arg 4: numSprings (cl_int)
//   arg 5: reverseOrder (cl_int)
//
// Strategy: copy posIn → posOut first (serial Gauss-Seidel init),
//           then dispatch with global_size = 1 (serial, avoids write races).
//           The kernel reads/writes posOut exclusively.
void ComputePipeline::dispatchConstraints(cl_mem posIn, cl_mem posOut,
                                          cl_mem springs,
                                          cl_mem paramsUBO, int numSprings,
                                          bool reverseOrder)
{
    cl_int err;

    // Copy posIn → posOut to initialise Gauss-Seidel starting state.
    // Use clGetMemObjectInfo to get the byte size without needing numParticles here.
    size_t bufSize = 0;
    clGetMemObjectInfo(posIn, CL_MEM_SIZE, sizeof(size_t), &bufSize, nullptr);

    cl_event copyDone;
    err = clEnqueueCopyBuffer(m_queue, posIn, posOut, 0, 0, bufSize,
                              0, nullptr, &copyDone);
    CHECK_CL_ERROR(err);

    cl_int ns  = static_cast<cl_int>(numSprings);
    cl_int rev = reverseOrder ? 1 : 0;
    // Particle count derived from buffer size (each Particle = 32 bytes)
    cl_int np  = static_cast<cl_int>(bufSize / 32);

    CHECK_CL_ERROR(clSetKernelArg(m_kConstraints, 0, sizeof(cl_mem), &posIn));
    CHECK_CL_ERROR(clSetKernelArg(m_kConstraints, 1, sizeof(cl_mem), &posOut));
    CHECK_CL_ERROR(clSetKernelArg(m_kConstraints, 2, sizeof(cl_mem), &springs));
    CHECK_CL_ERROR(clSetKernelArg(m_kConstraints, 3, sizeof(cl_mem), &paramsUBO));
    CHECK_CL_ERROR(clSetKernelArg(m_kConstraints, 4, sizeof(cl_int), &ns));
    CHECK_CL_ERROR(clSetKernelArg(m_kConstraints, 5, sizeof(cl_int), &rev));
    CHECK_CL_ERROR(clSetKernelArg(m_kConstraints, 6, sizeof(cl_int), &np));

    // Serial execution: global_size = local_size = 1.
    // All springs are iterated in a loop inside work item 0.
    size_t global = 1;
    size_t local  = 1;

    err = clEnqueueNDRangeKernel(m_queue, m_kConstraints, 1,
                                 nullptr, &global, &local,
                                 1, &copyDone, nullptr);
    CHECK_CL_ERROR(err);
    clReleaseEvent(copyDone);
}

// ── dispatchThickness ─────────────────────────────────────────────────────────
// Kernel args (see thickness.cl):
//   0: particles   (pos CL/GL shared buffer — must be acquired before calling)
//   1: faceIndices (flat int CL-only buffer, 3 ints per face)
//   2: restAreas   (float CL-only buffer, one per face)
//   3: thicknessOut(CL/GL shared thickness buffer — must be acquired)
//   4: numFaces    (cl_int)
//
// One work item per face; global size padded to multiple of workgroup size 64.
void ComputePipeline::dispatchThickness(cl_mem pos, cl_mem faceIndices,
                                        cl_mem restAreas, cl_mem thicknessOut,
                                        int numFaces)
{
    if (!m_kThickness || numFaces <= 0) return;

    cl_int nf = static_cast<cl_int>(numFaces);
    CHECK_CL_ERROR(clSetKernelArg(m_kThickness, 0, sizeof(cl_mem), &pos));
    CHECK_CL_ERROR(clSetKernelArg(m_kThickness, 1, sizeof(cl_mem), &faceIndices));
    CHECK_CL_ERROR(clSetKernelArg(m_kThickness, 2, sizeof(cl_mem), &restAreas));
    CHECK_CL_ERROR(clSetKernelArg(m_kThickness, 3, sizeof(cl_mem), &thicknessOut));
    CHECK_CL_ERROR(clSetKernelArg(m_kThickness, 4, sizeof(cl_int), &nf));

    size_t global = static_cast<size_t>(numFaces);
    size_t local  = 64;
    // Pad global to next multiple of local so work items cover all faces.
    if (global % local != 0) global = ((global / local) + 1) * local;

    cl_int err = clEnqueueNDRangeKernel(m_queue, m_kThickness, 1,
                                         nullptr, &global, &local,
                                         0, nullptr, nullptr);
    CHECK_CL_ERROR(err);
}

// ── dispatchAdhesion ──────────────────────────────────────────────────────────
// Kernel args (see adhesion.cl):
//   0: particles  (CL/GL shared pos buffer, Particle*: pos+vel, read-write)
//   1: params     (CL SimParams flat float array, __constant)
//   2: numParticles (cl_int)
//
// One work item per particle; global size padded to multiple of 64.
// Contact-flag filter (bit 0 of vel.w) runs inside the kernel — work items for
// non-contact particles return immediately, keeping dispatch cost low.
void ComputePipeline::dispatchAdhesion(cl_mem particles, cl_mem paramsUBO,
                                       int numParticles)
{
    if (!m_kAdhesion || numParticles <= 0) return;

    cl_int np = static_cast<cl_int>(numParticles);
    CHECK_CL_ERROR(clSetKernelArg(m_kAdhesion, 0, sizeof(cl_mem), &particles));
    CHECK_CL_ERROR(clSetKernelArg(m_kAdhesion, 1, sizeof(cl_mem), &paramsUBO));
    CHECK_CL_ERROR(clSetKernelArg(m_kAdhesion, 2, sizeof(cl_int), &np));

    size_t global = static_cast<size_t>(numParticles);
    size_t local  = 64;
    if (global % local != 0) global = ((global / local) + 1) * local;

    cl_int err = clEnqueueNDRangeKernel(m_queue, m_kAdhesion, 1,
                                         nullptr, &global, &local,
                                         0, nullptr, nullptr);
    CHECK_CL_ERROR(err);
}

void ComputePipeline::finish()
{
    CHECK_CL_ERROR(clFinish(m_queue));
}

std::string ComputePipeline::loadSource(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) {
        fprintf(stderr, "[CL] Cannot open shader: %s\n", path.c_str());
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
