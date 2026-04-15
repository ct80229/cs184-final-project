#version 410 core

// bowl.vert — Procedural hemisphere vertex shader (OpenGL 4.1 core profile)
//
// Interleaved vertex layout (32 bytes):
//   location 0: vec3 position  (offset  0)
//   location 1: vec3 normal    (offset 12)
//   location 2: vec2 uv        (offset 24)

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out vec3 vWorldPos;
out vec3 vNormal;

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos     = worldPos.xyz;

    // Normal matrix: transpose(inverse(model)) — correct for non-uniform scaling.
    // For uniform scaling (scale = identity here) this simplifies to mat3(uModel).
    vNormal = normalize(mat3(transpose(inverse(uModel))) * aNormal);

    gl_Position = uProj * uView * worldPos;
}
