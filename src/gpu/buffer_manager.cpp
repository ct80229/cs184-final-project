#include "gpu/buffer_manager.h"

BufferManager::BufferManager() = default;

BufferManager::~BufferManager()
{
    // TODO: implement — release CL buffers, delete GL buffers
}

void BufferManager::allocateParticleBuffers(int numParticles)
{
    // TODO: implement — glGenBuffers, glBindBuffer(GL_SHADER_STORAGE_BUFFER),
    //       glBufferData(size = numParticles * 32), glBindBufferBase(0) and (1)
    (void)numParticles;
}

void BufferManager::allocateParamsUBO()
{
    // TODO: implement — glGenBuffers, GL_UNIFORM_BUFFER, binding 3
}

void BufferManager::allocateSpringBuffer(int numSprings)
{
    // TODO: implement — GL_SHADER_STORAGE_BUFFER, binding 4
    //       element size: 16 bytes (int, int, float, float_pad)
    (void)numSprings;
}

void BufferManager::allocateThicknessBuffer(int numFaces)
{
    // TODO: implement — GL_SHADER_STORAGE_BUFFER, binding 5
    //       element size: 4 bytes (float thickness_nm)
    (void)numFaces;
}

void BufferManager::createCLBuffers(cl_context ctx)
{
    // TODO: implement — clCreateFromGLBuffer for posA, posB, springs, thickness
    (void)ctx;
}

void BufferManager::acquireForCL(cl_command_queue queue)
{
    // TODO: implement — clEnqueueAcquireGLObjects on all four CL buffers
    (void)queue;
}

void BufferManager::releaseFromCL(cl_command_queue queue)
{
    // TODO: implement — clEnqueueReleaseGLObjects on all four CL buffers
    (void)queue;
}

void BufferManager::swapPingPong()
{
    // TODO: implement — std::swap(m_posA, m_posB); std::swap(m_clPosA, m_clPosB)
}
