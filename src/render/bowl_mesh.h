#pragma once

// BowlMesh — procedural UV hemisphere for the bowl collision object.
//
// Geometry is generated entirely in C++ (no Blender import).
// Rendered with simple Phong shading — distinct enough to read as a physical bowl.
// The actual collision geometry is an analytic sphere SDF in the compute kernels
// (SimParams::sphere); this mesh is purely for rendering.
//
// Bowl shaders are loaded from disk at runtime (see RenderPipeline).

#include <OpenGL/gl3.h>
#include <glm/glm.hpp>

class BowlMesh {
public:
    BowlMesh();
    ~BowlMesh();

    // TODO: Generate UV hemisphere (lower half of sphere) with positions, normals, UVs.
    //       rings = horizontal latitude subdivisions, sectors = longitudinal subdivisions.
    //       Upload to GPU via glGenBuffers / glBufferData.
    void init(int rings = 20, int sectors = 20);

    // TODO: Bind shader program, set MVP + light uniforms, draw with Phong shading.
    //       Shader loaded from disk (src/shaders/bowl.vert + bowl.frag — added in Sprint 3).
    void draw(const glm::mat4& model,
              const glm::mat4& view,
              const glm::mat4& proj,
              const glm::vec3& lightPos);

private:
    GLuint m_vao           = 0;
    GLuint m_vbo           = 0;   // interleaved: position (vec3) + normal (vec3) + uv (vec2)
    GLuint m_ebo           = 0;
    int    m_indexCount    = 0;
    GLuint m_shaderProgram = 0;
};
