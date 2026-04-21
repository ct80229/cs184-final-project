#include "sim/interaction.h"

#include <glm/gtc/matrix_inverse.hpp>  // glm::inverse
#include <cfloat>   // FLT_MAX
#include <cstdio>

Interaction::Interaction() = default;
Interaction::~Interaction() = default;

// ── Möller–Trumbore ray–triangle intersection ─────────────────────────────────
// Returns true if the ray (orig + t*dir) hits triangle (v0,v1,v2) with t > 0.
// On hit, writes t (distance), u and v (barycentric coords for v1 and v2).
// Barycentric coord for v0 = 1 - u - v.
static bool mollerTrumbore(const glm::vec3& orig, const glm::vec3& dir,
                            const glm::vec3& v0,   const glm::vec3& v1,
                            const glm::vec3& v2,
                            float& t, float& u, float& v_out)
{
    const float EPS = 1e-7f;
    glm::vec3 e1 = v1 - v0;
    glm::vec3 e2 = v2 - v0;
    glm::vec3 h  = glm::cross(dir, e2);
    float a = glm::dot(e1, h);
    if (a > -EPS && a < EPS) return false;   // ray parallel to triangle

    float f = 1.0f / a;
    glm::vec3 s = orig - v0;
    u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;

    glm::vec3 q = glm::cross(s, e1);
    v_out = f * glm::dot(dir, q);
    if (v_out < 0.0f || u + v_out > 1.0f) return false;

    t = f * glm::dot(e2, q);
    return t > EPS;   // positive t: hit in front of ray origin
}

// ── Unproject: screen point → world-space ray ─────────────────────────────────
// screenX/Y are GLFW window coords (screen points), windowW/H are from glfwGetWindowSize.
// Returns {rayOrigin, rayDir} (rayDir is normalized).
static void unprojectRay(double screenX, double screenY,
                         int windowW,    int windowH,
                         const glm::mat4& proj, const glm::mat4& view,
                         glm::vec3& rayOrigin, glm::vec3& rayDir)
{
    float ndcX =  2.0f * (float)screenX / (float)windowW - 1.0f;
    float ndcY =  1.0f - 2.0f * (float)screenY / (float)windowH;

    glm::mat4 invVP = glm::inverse(proj * view);

    // Near and far points in NDC (z = -1 and z = 1)
    glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farH  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);

    glm::vec3 nearW = glm::vec3(nearH) / nearH.w;
    glm::vec3 farW  = glm::vec3(farH)  / farH.w;

    rayOrigin = nearW;
    rayDir    = glm::normalize(farW - nearW);
}

// ─────────────────────────────────────────────────────────────────────────────

void Interaction::onMouseDown(double screenX,  double screenY,
                               int windowW,     int windowH,
                               const glm::mat4& proj, const glm::mat4& view,
                               const std::vector<glm::vec4>& cpuPositions,
                               int numParticles, int gridSize,
                               SimParams& params)
{
    if ((int)cpuPositions.size() < numParticles) {
        printf("[Interaction] cpuPositions not synced yet — skipping grab\n");
        return;
    }

    glm::vec3 rayOrigin, rayDir;
    unprojectRay(screenX, screenY, windowW, windowH, proj, view, rayOrigin, rayDir);

    int   N      = gridSize;
    float bestT  = FLT_MAX;
    int   bestParticle = -1;

    // Iterate cloth triangles in the same r,c order as ClothMesh::init() and
    // thickness.cl: tri0=(TL,BL,TR), tri1=(BL,BR,TR).
    for (int r = 0; r < N - 1; ++r) {
        for (int c = 0; c < N - 1; ++c) {
            int tl = r       * N + c;
            int bl = (r + 1) * N + c;
            int tr = r       * N + (c + 1);
            int br = (r + 1) * N + (c + 1);

            int tris[2][3] = { {tl, bl, tr}, {bl, br, tr} };

            for (auto& tri : tris) {
                glm::vec3 v0 = glm::vec3(cpuPositions[tri[0]]);
                glm::vec3 v1 = glm::vec3(cpuPositions[tri[1]]);
                glm::vec3 v2 = glm::vec3(cpuPositions[tri[2]]);

                float t, u, v;
                if (mollerTrumbore(rayOrigin, rayDir, v0, v1, v2, t, u, v)) {
                    if (t < bestT) {
                        bestT = t;
                        // Barycentric weights: w0 = 1-u-v (v0), w1 = u (v1), w2 = v (v2)
                        float w0 = 1.0f - u - v;
                        float w1 = u;
                        float w2 = v;
                        if      (w0 >= w1 && w0 >= w2) bestParticle = tri[0];
                        else if (w1 >= w2)              bestParticle = tri[1];
                        else                            bestParticle = tri[2];
                    }
                }
            }
        }
    }

    if (bestParticle < 0) return;   // no hit

    m_grabWorld = glm::vec3(cpuPositions[bestParticle]);

    // Project the grabbed particle into clip space to get its NDC z.
    // onMouseMove unprojcts to the plane at this same NDC z — gives constant-depth
    // tracking (particle follows mouse at its original depth as camera stays fixed).
    glm::vec4 clipPos = (proj * view) * glm::vec4(m_grabWorld, 1.0f);
    m_grabDepth = clipPos.z / clipPos.w;

    m_grabbing           = true;
    params.grab_particle = bestParticle;
    params.grab_target   = glm::vec4(m_grabWorld, 0.0f);

    printf("[Interaction] Grabbed particle %d at (%.2f, %.2f, %.2f) t=%.3f\n",
           bestParticle,
           m_grabWorld.x, m_grabWorld.y, m_grabWorld.z, bestT);
}

void Interaction::onMouseMove(double screenX,  double screenY,
                               int windowW,     int windowH,
                               const glm::mat4& proj, const glm::mat4& view,
                               SimParams& params)
{
    if (!m_grabbing) return;

    float ndcX =  2.0f * (float)screenX / (float)windowW - 1.0f;
    float ndcY =  1.0f - 2.0f * (float)screenY / (float)windowH;

    // Unproject to the same NDC depth as the grabbed particle so the grab point
    // stays at constant depth (no z-fighting or particle jumping).
    glm::mat4 invVP = glm::inverse(proj * view);
    glm::vec4 worldH = invVP * glm::vec4(ndcX, ndcY, m_grabDepth, 1.0f);
    glm::vec3 worldPos = glm::vec3(worldH) / worldH.w;

    params.grab_target = glm::vec4(worldPos, 0.0f);
}

void Interaction::onMouseRelease(SimParams& params)
{
    params.grab_particle = -1;
    m_grabbing           = false;
}
