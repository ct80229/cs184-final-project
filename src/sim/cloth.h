#pragma once

// Cloth — CPU-side N×N particle grid and spring topology.
//
// Holds rest state for GPU initialization, spring index/length data,
// and a CPU mirror of current positions used for mouse-interaction raycasting.
// All simulation runs on the GPU (OpenCL kernels); this class is CPU-only.

#include <glm/glm.hpp>
#include <vector>

class BufferManager; // defined in gpu/buffer_manager.h — forward-declare to avoid circular includes

class Cloth {
public:
    explicit Cloth(int gridSize = 64);
    ~Cloth();

    // TODO: Build rest-position grid; set corner particles pinned by writing
    //       mass_inv = 0.0 into pos.w of the four corner entries.
    void init();

    // TODO: Populate m_springs with structural (grid edges), shear (diagonals),
    //       and bend (skip-one neighbor) springs; compute rest lengths from grid spacing.
    void buildSprings();

    // TODO: Upload rest positions (Particle structs) to posBufferA and posBufferB,
    //       and spring data to the springs SSBO via BufferManager.
    void uploadToGPU(BufferManager& buffers);

    // TODO: Re-upload rest positions to posBufferA — called on 'R' key press.
    void resetToRest(BufferManager& buffers);

    // TODO: glGetBufferSubData from posBufferA into m_cpuPositions.
    //       Called once at the start of a mouse grab (acceptable 1-frame latency).
    void syncPositionsFromGPU(BufferManager& buffers);

    // Accessors
    int gridSize()    const { return m_gridSize; }
    int numParticles() const { return m_gridSize * m_gridSize; }
    int numSprings()   const { return static_cast<int>(m_springs.size()); }
    // Two triangles per quad cell
    int numFaces()     const { return 2 * (m_gridSize - 1) * (m_gridSize - 1); }

    // CPU mirror of current particle positions — valid after syncPositionsFromGPU().
    const std::vector<glm::vec4>& cpuPositions() const { return m_cpuPositions; }

private:
    int m_gridSize;

    // Rest state uploaded to GPU on init and on 'R' reset.
    // Each entry is the pos component of a Particle (w = mass_inv).
    std::vector<glm::vec4> m_restPositions;
    std::vector<glm::vec4> m_restVelocities; // all zero at init

    // Populated on demand by syncPositionsFromGPU() for raycasting.
    std::vector<glm::vec4> m_cpuPositions;

    // Spring topology — uploaded once to the springs SSBO.
    struct Spring {
        int   i;        // particle index A
        int   j;        // particle index B
        float restLen;  // rest length in world units
        float pad;      // std430 alignment pad (16-byte element)
    };
    std::vector<Spring> m_springs;
};
