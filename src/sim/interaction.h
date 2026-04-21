#pragma once

// Interaction — mouse raycasting and grab-constraint injection.
//
// On grab start: CPU reads current particle positions from GPU once
// (1-frame latency is acceptable). Möller–Trumbore ray-triangle intersection
// finds the nearest hit particle.
//
// On drag: grab_target is updated in the SimParams struct each cursor-move
// event. The render loop's clEnqueueWriteBuffer picks it up next frame —
// no additional glBufferSubData calls are needed.
//
// On release: grab_particle is reset to -1.
//
// IMPORTANT: All three methods mutate SimParams& directly.
// Do NOT call glBufferSubData on paramsUBO — no shader reads it.
// The CL render loop uploads simParams every frame via clEnqueueWriteBuffer.

#include <glm/glm.hpp>
#include <vector>
#include "sim/params.h"

class Interaction {
public:
    Interaction();
    ~Interaction();

    // Unproject a ray from (screenX, screenY) in window coordinates (screen points),
    // test against cloth triangles using Möller–Trumbore, set grab_particle to the
    // vertex of the nearest hit triangle closest to the hit position.
    // cpuPositions: CPU mirror populated by Cloth::syncPositionsFromGPU() immediately
    // before this call.
    void onMouseDown(double screenX,  double screenY,
                     int windowW,     int windowH,
                     const glm::mat4& proj, const glm::mat4& view,
                     const std::vector<glm::vec4>& cpuPositions,
                     int numParticles, int gridSize,
                     SimParams& params);

    // Re-unproject cursor to a constant-depth plane (depth = grabbed particle's NDC z).
    // Writes the world-space intersection to params.grab_target.
    void onMouseMove(double screenX,  double screenY,
                     int windowW,     int windowH,
                     const glm::mat4& proj, const glm::mat4& view,
                     SimParams& params);

    // Set grab_particle = -1 and clear grab state.
    void onMouseRelease(SimParams& params);

    bool isGrabbing() const { return m_grabbing; }

private:
    bool      m_grabbing  = false;
    float     m_grabDepth = 0.0f;   // NDC z of grabbed particle (used for drag plane)
    glm::vec3 m_grabWorld{};        // world-space position at grab start
};
