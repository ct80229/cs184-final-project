#include "render/cloth_mesh.h"

ClothMesh::ClothMesh() = default;

ClothMesh::~ClothMesh()
{
    // TODO: implement — glDeleteVertexArrays, glDeleteBuffers
}

void ClothMesh::init(int gridSize, GLuint posBuffer, GLuint thicknessBuffer)
{
    // TODO: implement
    // 1. Generate triangle index pairs for all (N-1)×(N-1) quad cells
    //    Each quad → 2 triangles (upper-left and lower-right), wound CCW
    // 2. glGenBuffers(m_ebo); upload index data with GL_STATIC_DRAW
    // 3. glGenVertexArrays(m_vao); bind VAO
    // 4. Attach posBuffer via glBindVertexBuffer (or glVertexAttribPointer with SSBO trick)
    //    — stride 32 bytes, attrib 0 = vec4 pos at offset 0
    // 5. Attach thicknessBuffer for flat per-face thickness_nm (attrib 1)
    // 6. Bind m_ebo inside the VAO
    (void)gridSize; (void)posBuffer; (void)thicknessBuffer;
}

void ClothMesh::draw(GLenum primitiveMode)
{
    // TODO: implement — glBindVertexArray(m_vao); glDrawElements(...)
    (void)primitiveMode;
}
