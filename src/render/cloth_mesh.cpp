#include "render/cloth_mesh.h"
#include "gpu/gl_check.h"

#include <vector>
#include <cstdio>

ClothMesh::ClothMesh() = default;

ClothMesh::~ClothMesh()
{
    if (m_thicknessTBO) { glDeleteTextures(1, &m_thicknessTBO); m_thicknessTBO = 0; }
    if (m_tbo)          { glDeleteTextures(1, &m_tbo);          m_tbo          = 0; }
    if (m_ebo)          { glDeleteBuffers(1, &m_ebo);           m_ebo          = 0; }
    if (m_vao)          { glDeleteVertexArrays(1, &m_vao);      m_vao          = 0; }
}

void ClothMesh::init(int gridSize, GLuint posBuffer, GLuint thicknessBuffer)
{
    int N = gridSize;

    // ── 1. Generate triangle index buffer ────────────────────────────────────
    // (N-1)×(N-1) quads, 2 triangles each, 3 indices per triangle = 6 per quad.
    // Flat particle index for (row r, col c) = r*N + c.
    // CCW winding viewed from above (+Y):
    //   tri 0: (r,c), (r+1,c), (r,c+1)   — top-left, bottom-left, top-right
    //   tri 1: (r+1,c), (r+1,c+1), (r,c+1) — bottom-left, bottom-right, top-right
    std::vector<GLuint> indices;
    indices.reserve(6 * (N - 1) * (N - 1));

    for (int r = 0; r < N - 1; ++r) {
        for (int c = 0; c < N - 1; ++c) {
            GLuint tl = static_cast<GLuint>(r       * N + c);
            GLuint bl = static_cast<GLuint>((r + 1) * N + c);
            GLuint tr = static_cast<GLuint>(r       * N + (c + 1));
            GLuint br = static_cast<GLuint>((r + 1) * N + (c + 1));

            indices.push_back(tl);  indices.push_back(bl);  indices.push_back(tr);
            indices.push_back(bl);  indices.push_back(br);  indices.push_back(tr);
        }
    }
    m_indexCount = static_cast<int>(indices.size());

    // ── 2. VAO (holds EBO; no vertex attrib pointers — positions come from TBO) ─
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(GLuint)),
                 indices.data(), GL_STATIC_DRAW);

    // No glVertexAttribPointer — cloth.vert reads positions via texelFetch(uPosTBO, …).
    // The VAO just carries the EBO binding.
    glBindVertexArray(0);
    CHECK_GL_ERROR();

    // ── 3. Texture Buffer Object for particle positions ───────────────────────
    // The particle buffer (posBuffer) is a GL_ARRAY_BUFFER of 32-byte Particle structs
    // {vec4 pos, vec4 vel}. As a GL_RGBA32F TBO it exposes a flat array of float4 texels:
    //   texel i*2   = particle[i].pos (xyz = position, w = mass_inv)
    //   texel i*2+1 = particle[i].vel (xyz = velocity, w = flags)
    glGenTextures(1, &m_tbo);
    glBindTexture(GL_TEXTURE_BUFFER, m_tbo);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, posBuffer);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    CHECK_GL_ERROR();

    // ── 4. Texture Buffer Object for per-face thickness ───────────────────────
    // thicknessBuffer is a GL_ARRAY_BUFFER of numFaces floats written by the
    // OpenCL thickness kernel each frame.  GL_R32F exposes it as a 1-component
    // float texel array; gl_PrimitiveID in the fragment shader indexes directly
    // into it without any per-vertex attribute machinery.
    // The thickness buffer ID never changes (no ping-pong), so this TBO is set up
    // once and never rebound.
    if (thicknessBuffer) {
        glGenTextures(1, &m_thicknessTBO);
        glBindTexture(GL_TEXTURE_BUFFER, m_thicknessTBO);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, thicknessBuffer);
        glBindTexture(GL_TEXTURE_BUFFER, 0);
        CHECK_GL_ERROR();
    }

    printf("[ClothMesh] init: %d quads, %d indices, TBO wrapping buffer %u\n",
           (N - 1) * (N - 1), m_indexCount, posBuffer);
}

void ClothMesh::rebindTBO(GLuint posBuffer)
{
    // Re-associate the TBO with the current particle buffer ID.
    // Must be called each frame because ping-pong swapping alternates which
    // GL buffer object is "posA" — the TBO must always point to the current one.
    glBindTexture(GL_TEXTURE_BUFFER, m_tbo);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, posBuffer);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
}

void ClothMesh::draw(GLenum primitiveMode)
{
    // Unit 0: particle positions TBO — must be rebound each frame (ping-pong).
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, m_tbo);

    // Unit 1: per-face thickness TBO — static binding (thickness buffer never ping-pongs).
    if (m_thicknessTBO) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_BUFFER, m_thicknessTBO);
    }

    glBindVertexArray(m_vao);
    glDrawElements(primitiveMode, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    CHECK_GL_ERROR();
}
