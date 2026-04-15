#pragma once

// ClothMesh — VAO + index buffer for indexed triangle rendering of the cloth grid.
//
// Particle positions are read directly from the particle SSBO via a vertex attrib
// binding, so no CPU→GPU position upload is needed each frame. The index buffer
// is static (grid topology never changes).

#include <OpenGL/gl3.h>

class ClothMesh {
public:
    ClothMesh();
    ~ClothMesh();

    // TODO: Generate triangle index buffer for an N×N grid (2 triangles per quad cell).
    //       Configure VAO with a vertex attrib binding that reads from posBuffer
    //       (stride = sizeof(Particle) = 32 bytes, offset = 0 for pos.xyz).
    //       thicknessBuffer is bound for the flat thickness_nm per-face attribute.
    void init(int gridSize, GLuint posBuffer, GLuint thicknessBuffer);

    // TODO: glBindVertexArray(m_vao); glDrawElements(primitiveMode, m_indexCount, ...)
    void draw(GLenum primitiveMode = GL_TRIANGLES);

    // Re-point the TBO at a new GL buffer object.  Must be called each frame
    // after the ping-pong swap so the vertex shader reads current particle data.
    // (The GL buffer ID behind posBufferA() alternates every frame.)
    void rebindTBO(GLuint posBuffer);

    int indexCount() const { return m_indexCount; }

private:
    GLuint m_vao        = 0;
    GLuint m_ebo        = 0;   // element (index) buffer object
    GLuint m_tbo        = 0;   // texture buffer object — particle positions (GL_RGBA32F)
    int    m_indexCount = 0;
};
