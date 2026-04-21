#version 410 core

// cloth.vert — Cloth mesh vertex shader (OpenGL 4.1 core profile)
//
// Particle positions are read from a Texture Buffer Object (TBO) via gl_VertexID.
// SSBOs require OpenGL 4.3 (unavailable on macOS); TBOs are available since 3.1.
//
// TBO layout (GL_RGBA32F, matches Particle struct in sim/cloth.h):
//   texel i*2   = particle[i].pos   (xyz = world pos, w = mass_inv)
//   texel i*2+1 = particle[i].vel   (xyz = velocity,  w = flags)
//
// Normal approximation: cross product of horizontal and vertical TBO-neighbor tangents.
// Uses one-sided finite differences at grid boundaries (clamped index, not mirrored).
// The result is approximate but sufficient for Fresnel/iridescence angle computation.

uniform mat4          uMVP;       // projection * view * model (model = identity for cloth)
uniform samplerBuffer uPosTBO;    // particle buffer as flat float4 texels
uniform int           uGridSize;  // N (cloth is N×N particles)
uniform vec3          uCameraPos; // camera world-space eye position

out vec3 vNormal;   // world-space surface normal (approximate, from TBO neighbors)
out vec3 vViewDir;  // world-space direction toward camera (not normalized)

void main()
{
    int i = gl_VertexID;

    // Read this particle's world position
    vec3 pos = texelFetch(uPosTBO, i * 2).xyz;

    // ── Horizontal tangent (along column axis, +X in rest state) ─────────────
    // Right edge: use backward difference; interior and left: use forward difference.
    vec3 tangH;
    if (i % uGridSize < uGridSize - 1) {
        tangH = texelFetch(uPosTBO, (i + 1) * 2).xyz - pos;
    } else {
        tangH = pos - texelFetch(uPosTBO, (i - 1) * 2).xyz;
    }

    // ── Vertical tangent (along row axis, +Z in rest state) ──────────────────
    // Top edge: use backward difference; interior and bottom: use forward difference.
    vec3 tangV;
    if (i / uGridSize < uGridSize - 1) {
        tangV = texelFetch(uPosTBO, (i + uGridSize) * 2).xyz - pos;
    } else {
        tangV = pos - texelFetch(uPosTBO, (i - uGridSize) * 2).xyz;
    }

    // Cross product gives an approximate surface normal.
    // abs(dot(N, V)) in the fragment shader handles both sides correctly.
    vNormal = normalize(cross(tangH, tangV));

    // View direction: world-space vector from particle toward camera.
    // Normalized in the fragment shader (cheaper than per-vertex normalize).
    vViewDir = uCameraPos - pos;

    gl_Position = uMVP * vec4(pos, 1.0);
}
