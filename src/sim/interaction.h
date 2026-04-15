#pragma once

// Interaction — mouse raycasting and grab-constraint injection.
//
// On grab start: CPU reads current particle positions from GPU once
// (1-frame latency is acceptable). Möller–Trumbore ray-triangle intersection
// finds the nearest hit particle.
//
// On drag: only grab_target is updated each move event via glBufferSubData —
// no further GPU readback during the drag.
//
// On release: grab_particle is reset to -1.

#include <glm/glm.hpp>
#include <vector>
#include <OpenGL/gl3.h>
#include "sim/params.h"

class Interaction {
public:
    Interaction();
    ~Interaction();

    // TODO: Unproject a ray from (screenX, screenY), test against cloth triangles
    //       using Möller–Trumbore, find the nearest hit particle index.
    //       Sets SimParams::grab_particle and uploads the updated UBO.
    //       cpuPositions: CPU mirror populated by Cloth::syncPositionsFromGPU().
    void onMouseDown(double screenX, double screenY,
                     int viewportW,  int viewportH,
                     const glm::mat4& proj, const glm::mat4& view,
                     const std::vector<glm::vec4>& cpuPositions,
                     int numParticles, int gridSize,
                     GLuint paramsUBO, SimParams& params);

    // TODO: Re-unproject mouse position to a depth plane at m_grabDepth.
    //       Update SimParams::grab_target and upload via glBufferSubData.
    void onMouseMove(double screenX, double screenY,
                     int viewportW,  int viewportH,
                     const glm::mat4& proj, const glm::mat4& view,
                     GLuint paramsUBO, SimParams& params);

    // TODO: Set grab_particle = -1; upload updated params to paramsUBO.
    void onMouseRelease(GLuint paramsUBO, SimParams& params);

    bool isGrabbing() const { return m_grabbing; }

private:
    bool      m_grabbing  = false;
    float     m_grabDepth = 0.0f;   // NDC depth of grabbed particle (used for drag plane)
    glm::vec3 m_grabWorld{};        // world-space position at grab start
};
