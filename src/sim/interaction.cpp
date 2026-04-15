#include "sim/interaction.h"

Interaction::Interaction() = default;
Interaction::~Interaction() = default;

void Interaction::onMouseDown(double screenX, double screenY,
                              int viewportW,  int viewportH,
                              const glm::mat4& proj, const glm::mat4& view,
                              const std::vector<glm::vec4>& cpuPositions,
                              int numParticles, int gridSize,
                              GLuint paramsUBO, SimParams& params)
{
    // TODO: implement
    // 1. Convert screen coords to NDC: ndcX = 2*screenX/W - 1, ndcY = 1 - 2*screenY/H
    // 2. Unproject ray: rayDir = glm::inverse(proj * view) * vec4(ndcX, ndcY, -1, 1)
    // 3. Iterate cloth triangles (2 per quad, (N-1)×(N-1) quads), Möller–Trumbore test
    // 4. On hit: record grabbed particle index, set m_grabDepth, set m_grabbing=true
    // 5. Update params.grab_particle; glBufferSubData(GL_UNIFORM_BUFFER, paramsUBO)
    (void)screenX; (void)screenY; (void)viewportW; (void)viewportH;
    (void)proj; (void)view; (void)cpuPositions;
    (void)numParticles; (void)gridSize; (void)paramsUBO; (void)params;
}

void Interaction::onMouseMove(double screenX, double screenY,
                              int viewportW,  int viewportH,
                              const glm::mat4& proj, const glm::mat4& view,
                              GLuint paramsUBO, SimParams& params)
{
    // TODO: implement
    // 1. Unproject to a plane at m_grabDepth in world space
    // 2. Update params.grab_target with new world position
    // 3. glBufferSubData to update only grab_target field in paramsUBO
    (void)screenX; (void)screenY; (void)viewportW; (void)viewportH;
    (void)proj; (void)view; (void)paramsUBO; (void)params;
}

void Interaction::onMouseRelease(GLuint paramsUBO, SimParams& params)
{
    // TODO: implement
    // 1. params.grab_particle = -1
    // 2. glBufferSubData to update grab_particle field in paramsUBO
    // 3. m_grabbing = false
    (void)paramsUBO; (void)params;
}
