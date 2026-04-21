#include "sim/cloth.h"
#include "gpu/buffer_manager.h"

#include <OpenGL/gl3.h>
#include <cmath>
#include <cassert>
#include <cstdio>

// Local struct matching Particle in GLSL/CL: vec4 pos + vec4 vel (32 bytes, std430).
// Must match the CL typedef exactly (float4 pos, float4 vel).
struct ParticleUpload {
    glm::vec4 pos;  // xyz = position, w = mass_inv (0 = pinned)
    glm::vec4 vel;  // xyz = velocity, w = flags
};

Cloth::Cloth(int gridSize)
    : m_gridSize(gridSize)
{
    int n = gridSize * gridSize;
    m_restPositions.reserve(n);
    m_restVelocities.reserve(n);
    m_cpuPositions.reserve(n);
}

Cloth::~Cloth() = default;

void Cloth::init()
{
    int N = m_gridSize;
    m_restPositions.clear();
    m_restVelocities.clear();

    // Place cloth in the XZ plane at y = 0.7 — above the sphere top (y = 0.0).
    // Spans [-0.6, 0.6] in both X and Z to match sphere radius 0.5.
    // Grid spacing = 1.2 / (N-1).
    const float halfExtent = 0.6f;
    float spacing = (2.0f * halfExtent) / static_cast<float>(N - 1);

    for (int row = 0; row < N; ++row) {
        for (int col = 0; col < N; ++col) {
            float x = -halfExtent + col * spacing;
            float z = -halfExtent + row * spacing;
            float y = 0.7f;

            float mass_inv = 1.0f;  // no pinned corners — cloth falls freely

            m_restPositions.push_back(glm::vec4(x, y, z, mass_inv));
            m_restVelocities.push_back(glm::vec4(0.0f));
        }
    }

    assert((int)m_restPositions.size() == numParticles());
}

void Cloth::buildSprings()
{
    int N = m_gridSize;
    m_springs.clear();

    auto idx = [&](int r, int c) { return r * N + c; };

    auto addSpring = [&](int i, int j) {
        glm::vec3 pi = glm::vec3(m_restPositions[i]);
        glm::vec3 pj = glm::vec3(m_restPositions[j]);
        float restLen = glm::length(pj - pi);
        m_springs.push_back({ i, j, restLen, 0.0f });
    };

    // ── Structural: horizontal (col → col+1) and vertical (row → row+1) ────────
    for (int r = 0; r < N; ++r)
        for (int c = 0; c < N - 1; ++c)
            addSpring(idx(r, c), idx(r, c + 1));

    for (int r = 0; r < N - 1; ++r)
        for (int c = 0; c < N; ++c)
            addSpring(idx(r, c), idx(r + 1, c));

    // ── Shear: both diagonals of each quad cell ──────────────────────────────
    for (int r = 0; r < N - 1; ++r) {
        for (int c = 0; c < N - 1; ++c) {
            addSpring(idx(r, c),     idx(r + 1, c + 1));  // top-left → bottom-right
            addSpring(idx(r, c + 1), idx(r + 1, c));      // top-right → bottom-left
        }
    }

    // ── Bend: skip-one neighbors (horizontal and vertical) ──────────────────
    for (int r = 0; r < N; ++r)
        for (int c = 0; c < N - 2; ++c)
            addSpring(idx(r, c), idx(r, c + 2));

    for (int r = 0; r < N - 2; ++r)
        for (int c = 0; c < N; ++c)
            addSpring(idx(r, c), idx(r + 2, c));

    printf("[Cloth] %d particles, %d springs (grid %dx%d)\n",
           numParticles(), numSprings(), N, N);
}

// Build a vector of interleaved Particle structs from rest state.
static std::vector<ParticleUpload> buildParticleUpload(
    const std::vector<glm::vec4>& pos,
    const std::vector<glm::vec4>& vel)
{
    std::vector<ParticleUpload> out(pos.size());
    for (size_t i = 0; i < pos.size(); ++i) {
        out[i].pos = pos[i];
        out[i].vel = vel[i];
    }
    return out;
}

void Cloth::uploadToGPU(BufferManager& buffers)
{
    auto particles = buildParticleUpload(m_restPositions, m_restVelocities);
    size_t byteSize = particles.size() * sizeof(ParticleUpload);

    // Upload to posA
    glBindBuffer(GL_ARRAY_BUFFER, buffers.posBufferA());
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(byteSize), particles.data());

    // Mirror to posB so both buffers start at rest (ping-pong requires both valid)
    glBindBuffer(GL_ARRAY_BUFFER, buffers.posBufferB());
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(byteSize), particles.data());

    // Upload springs
    struct SpringUpload { int i, j; float restLen, pad; };
    std::vector<SpringUpload> springData(m_springs.size());
    for (size_t k = 0; k < m_springs.size(); ++k) {
        springData[k] = { m_springs[k].i, m_springs[k].j,
                          m_springs[k].restLen, 0.0f };
    }
    glBindBuffer(GL_ARRAY_BUFFER, buffers.springBuffer());
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(springData.size() * sizeof(SpringUpload)),
                    springData.data());

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Cloth::resetToRest(BufferManager& buffers)
{
    auto particles = buildParticleUpload(m_restPositions, m_restVelocities);
    size_t byteSize = particles.size() * sizeof(ParticleUpload);

    glBindBuffer(GL_ARRAY_BUFFER, buffers.posBufferA());
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(byteSize), particles.data());

    glBindBuffer(GL_ARRAY_BUFFER, buffers.posBufferB());
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(byteSize), particles.data());

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Cloth::syncPositionsFromGPU(BufferManager& buffers)
{
    // Read interleaved Particle structs from posA; extract only pos (first vec4).
    int N = numParticles();
    std::vector<ParticleUpload> tmp(N);

    glBindBuffer(GL_ARRAY_BUFFER, buffers.posBufferA());
    glGetBufferSubData(GL_ARRAY_BUFFER, 0,
                       static_cast<GLsizeiptr>(N * sizeof(ParticleUpload)),
                       tmp.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_cpuPositions.resize(N);
    for (int i = 0; i < N; ++i)
        m_cpuPositions[i] = tmp[i].pos;
}

void Cloth::buildFaceData()
{
    int N = m_gridSize;
    int numF = numFaces();

    m_faceIndices.clear();
    m_faceIndices.reserve(numF * 3);
    m_restAreas.clear();
    m_restAreas.reserve(numF);

    // Replicate the exact loop from ClothMesh::init() so that:
    //   face index f in thickness buffer == gl_PrimitiveID f in fragment shader.
    // Triangle winding (CCW from above):
    //   tri 0: tl, bl, tr
    //   tri 1: bl, br, tr
    for (int r = 0; r < N - 1; ++r) {
        for (int c = 0; c < N - 1; ++c) {
            int tl = r * N + c;
            int bl = (r + 1) * N + c;
            int tr = r * N + (c + 1);
            int br = (r + 1) * N + (c + 1);

            // tri 0
            m_faceIndices.push_back(tl);
            m_faceIndices.push_back(bl);
            m_faceIndices.push_back(tr);

            // tri 1
            m_faceIndices.push_back(bl);
            m_faceIndices.push_back(br);
            m_faceIndices.push_back(tr);

            // Compute rest areas from m_restPositions
            auto restArea = [&](int i0, int i1, int i2) -> float {
                glm::vec3 p0 = glm::vec3(m_restPositions[i0]);
                glm::vec3 p1 = glm::vec3(m_restPositions[i1]);
                glm::vec3 p2 = glm::vec3(m_restPositions[i2]);
                return 0.5f * glm::length(glm::cross(p1 - p0, p2 - p0));
            };

            m_restAreas.push_back(restArea(tl, bl, tr));
            m_restAreas.push_back(restArea(bl, br, tr));
        }
    }

    assert(static_cast<int>(m_restAreas.size())    == numF);
    assert(static_cast<int>(m_faceIndices.size())  == numF * 3);
    printf("[Cloth] buildFaceData: %d faces\n", numF);
}

void Cloth::uploadFaceDataToCL(cl_context ctx,
                                cl_mem& outFaceIndices,
                                cl_mem& outRestAreas)
{
    cl_int err;

    // Face indices: flat int array (no int3 alignment issues), 3 ints per face.
    // CL_MEM_COPY_HOST_PTR copies on creation — safe to let m_faceIndices live on stack.
    size_t indicesBytes = m_faceIndices.size() * sizeof(int);
    outFaceIndices = clCreateBuffer(ctx,
                                    CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                    indicesBytes, m_faceIndices.data(), &err);
    if (err != CL_SUCCESS)
        fprintf(stderr, "[CL] clCreateBuffer faceIndices failed: %d\n", err);

    // Rest areas: one float per face.
    size_t areasBytes = m_restAreas.size() * sizeof(float);
    outRestAreas = clCreateBuffer(ctx,
                                  CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                  areasBytes, m_restAreas.data(), &err);
    if (err != CL_SUCCESS)
        fprintf(stderr, "[CL] clCreateBuffer restAreas failed: %d\n", err);

    printf("[Cloth] Uploaded face data to CL: %d faces (%zu + %zu bytes)\n",
           numFaces(), indicesBytes, areasBytes);
}
