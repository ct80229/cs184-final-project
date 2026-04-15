#include "render/bowl_mesh.h"

BowlMesh::BowlMesh() = default;

BowlMesh::~BowlMesh()
{
    // TODO: implement — glDeleteProgram, glDeleteVertexArrays, glDeleteBuffers ×3
}

void BowlMesh::init(int rings, int sectors)
{
    // TODO: implement
    // 1. Generate hemisphere vertices for the lower half (y ≤ 0):
    //    for each (ring, sector): position = (sin(phi)*cos(theta), -cos(phi), sin(phi)*sin(theta))
    //    where phi in [0, π/2], theta in [0, 2π]
    // 2. Compute normals (same as normalized position for a sphere)
    // 3. Compute UVs: u = sector / sectors, v = ring / rings
    // 4. Generate index buffer (two triangles per quad, wound CCW)
    // 5. Upload interleaved vertex data and indices to GPU
    // 6. Load bowl Phong shader via RenderPipeline (Sprint 3 task)
    (void)rings; (void)sectors;
}

void BowlMesh::draw(const glm::mat4& model, const glm::mat4& view,
                    const glm::mat4& proj,  const glm::vec3& lightPos)
{
    // TODO: implement — bind shader, set MVP uniforms, glDrawElements
    (void)model; (void)view; (void)proj; (void)lightPos;
}
