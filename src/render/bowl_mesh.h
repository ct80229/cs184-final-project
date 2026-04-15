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

    // Generates a UV sphere mesh with positions, normals, UVs and uploads to GPU.
    // rings = horizontal latitude subdivisions, sectors = longitudinal subdivisions.
    // Back-face culling (enabled at draw site) hides the lower hemisphere.
    void init(int rings = 20, int sectors = 20);

    // Binds the Phong shader, sets model/view/proj/light uniforms, draws with GL_TRIANGLES.
    // Shader loaded from disk: src/shaders/bowl.vert + bowl.frag.
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
