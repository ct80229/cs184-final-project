#include "sim/cloth.h"
#include "gpu/buffer_manager.h"

Cloth::Cloth(int gridSize)
    : m_gridSize(gridSize)
{
    // TODO: implement — reserve vector capacity based on gridSize
}

Cloth::~Cloth() = default;

void Cloth::init()
{
    // TODO: implement
    // - Build m_restPositions: N×N grid evenly spaced in [-1,1]×[-1,1] at y=0
    // - Set pos.w = 1.0 (mass_inv) for free particles
    // - Set pos.w = 0.0 (mass_inv) for the four corner particles (pinned)
    // - Fill m_restVelocities with vec4(0)
}

void Cloth::buildSprings()
{
    // TODO: implement
    // - Structural springs: horizontal and vertical grid edges
    // - Shear springs: both diagonals of each quad cell
    // - Bend springs: skip-one neighbors (2 cells apart, horizontal + vertical)
    // - Compute rest length for each spring from particle positions in m_restPositions
}

void Cloth::uploadToGPU(BufferManager& buffers)
{
    // TODO: implement
    // - glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers.posBufferA())
    // - glBufferSubData to upload interleaved {pos, vel} Particle structs
    // - Mirror same data to posBufferB (both start at rest)
    // - Upload spring array to buffers.springBuffer()
    (void)buffers;
}

void Cloth::resetToRest(BufferManager& buffers)
{
    // TODO: implement — re-upload m_restPositions to posBufferA (same as uploadToGPU)
    (void)buffers;
}

void Cloth::syncPositionsFromGPU(BufferManager& buffers)
{
    // TODO: implement
    // - Resize m_cpuPositions to numParticles()
    // - glGetBufferSubData from buffers.posBufferA() into m_cpuPositions
    //   (read only the pos.xyz fields; stride = sizeof(Particle) = 32 bytes)
    (void)buffers;
}
