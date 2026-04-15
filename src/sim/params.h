#pragma once

// SimParams — physics constants UBO (binding 3, std140 layout).
//
// IMPORTANT: This C++ struct must match the GLSL uniform block layout in all
// shaders and the OpenCL kernel parameter structs exactly. std140 rules:
//   float/int = 4 bytes, aligned 4
//   vec4      = 16 bytes, aligned 16
//   Each 16-byte "row" must be padded with explicit float fields.
//
// Layout (80 bytes total):
//   Row 1 [ 0..15]:  dt, substeps, stiffness, bend_stiffness
//   Row 2 [16..31]:  damping, gravity, pad0, pad1
//   Row 3 [32..47]:  sphere (vec4)
//   Row 4 [48..63]:  adhesion_k, adhesion_radius, grab_particle, pad2
//   Row 5 [64..79]:  grab_target (vec4)

#include <glm/glm.hpp>

struct alignas(16) SimParams {
    // Row 1
    float dt;               // fixed timestep: 1.0f / 60.0f
    int   substeps;         // constraint solver iterations per frame: 8–16
    float stiffness;        // structural spring stiffness: ~50.0
    float bend_stiffness;   // bending spring stiffness: ~20.0

    // Row 2
    float damping;          // per-substep velocity damping multiplier: 0.98
    float gravity;          // gravitational acceleration (m/s²): 9.8
    float pad0;             // std140 padding
    float pad1;             // std140 padding

    // Row 3
    glm::vec4 sphere;       // xyz = sphere center, w = sphere radius

    // Row 4
    float adhesion_k;       // adhesion stiffness: ~0.1 × stiffness
    float adhesion_radius;  // adhesion search radius: ~2× rest particle spacing
    int   grab_particle;    // grabbed particle index; -1 = none
    float pad2;             // std140 padding

    // Row 5
    glm::vec4 grab_target;  // xyz = world-space mouse drag target position
};

// Returns a SimParams populated with the starting values from CLAUDE.md §Physics Constants.
// All values are tunable at runtime via Dear ImGui sliders.
inline SimParams defaultSimParams()
{
    SimParams p{};
    p.dt             = 1.0f / 60.0f;
    p.substeps       = 12;
    p.stiffness      = 50.0f;
    p.bend_stiffness = 20.0f;
    p.damping        = 0.98f;
    p.gravity        = 9.8f;
    p.pad0           = 0.0f;
    p.pad1           = 0.0f;
    p.sphere         = glm::vec4(0.0f, -0.5f, 0.0f, 0.5f); // centered below cloth
    p.adhesion_k     = 0.1f * p.stiffness;                  // 5.0
    p.adhesion_radius= 0.04f;                               // ~2× spacing at 64×64
    p.grab_particle  = -1;
    p.pad2           = 0.0f;
    p.grab_target    = glm::vec4(0.0f);
    return p;
}
