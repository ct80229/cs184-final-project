#include "render/bowl_mesh.h"
#include "gpu/gl_check.h"

#include <glm/gtc/type_ptr.hpp>       // glm::value_ptr
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

// ── File-local shader helpers (same pattern as RenderPipeline) ────────────────
static std::string readShaderFile(const char* path)
{
    std::ifstream f(path);
    if (!f.is_open()) {
        fprintf(stderr, "[BowlMesh] Cannot open shader: %s\n", path);
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static GLuint compileStage(const std::string& src, GLenum type)
{
    GLuint shader = glCreateShader(type);
    const char* srcp = src.c_str();
    glShaderSource(shader, 1, &srcp, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        fprintf(stderr, "[BowlMesh] Shader compile error (%s):\n%s\n",
                type == GL_VERTEX_SHADER ? "vert" : "frag", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint linkProgram(GLuint vert, GLuint frag)
{
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096] = {};
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "[BowlMesh] Program link error:\n%s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// ─────────────────────────────────────────────────────────────────────────────

BowlMesh::BowlMesh() = default;

BowlMesh::~BowlMesh()
{
    if (m_shaderProgram) { glDeleteProgram(m_shaderProgram);      m_shaderProgram = 0; }
    if (m_vao)           { glDeleteVertexArrays(1, &m_vao);       m_vao = 0; }
    if (m_vbo)           { glDeleteBuffers(1, &m_vbo);            m_vbo = 0; }
    if (m_ebo)           { glDeleteBuffers(1, &m_ebo);            m_ebo = 0; }
}

void BowlMesh::init(int rings, int sectors)
{
    // ── Geometry: full sphere mesh for SimParams sphere ───────────────────────
    // Sphere: center (0, -0.5, 0), radius 0.5.  Cloth drapes over the TOP.
    // Full sphere: theta from 0 (north pole, y=0) to π (south pole, y=-1.0).
    // The lower hemisphere back-faces are culled by glEnable(GL_CULL_FACE) at the
    // draw site, so only the upper dome is visible.
    //   theta = r * (π / rings)            for ring  r in [0, rings]
    //   phi   = s * (2π / sectors)         for sector s in [0, sectors]
    //
    //   unit-sphere pos = (sin(theta)*cos(phi),  cos(theta),  sin(theta)*sin(phi))
    //   world pos = center + unit-sphere pos * radius
    //   normal    = unit-sphere pos   (outward from sphere center)
    //
    // Vertex layout (interleaved, 32 bytes):  vec3 pos | vec3 normal | vec2 uv

    const glm::vec3 center(0.0f, -0.5f, 0.0f);
    const float     radius = 0.5f;
    const float     M_PI_F = 3.14159265358979f;

    struct Vertex { float px, py, pz, nx, ny, nz, u, v; };
    std::vector<Vertex>  verts;
    std::vector<GLuint>  indices;

    // rings+1 latitude lines, sectors+1 longitude lines (last = first, closed loop)
    verts.reserve((rings + 1) * (sectors + 1));
    indices.reserve(rings * sectors * 6);

    for (int r = 0; r <= rings; ++r) {
        float theta = static_cast<float>(r) * M_PI_F / static_cast<float>(rings);
        float sinT = sinf(theta);
        float cosT = cosf(theta);

        for (int s = 0; s <= sectors; ++s) {
            float phi  = static_cast<float>(s) * (2.0f * M_PI_F) / static_cast<float>(sectors);
            float sinP = sinf(phi);
            float cosP = cosf(phi);

            // Unit-sphere direction (outward from sphere center)
            float nx = sinT * cosP;
            float ny = cosT;
            float nz = sinT * sinP;

            Vertex v;
            v.px = center.x + nx * radius;
            v.py = center.y + ny * radius;
            v.pz = center.z + nz * radius;
            v.nx = nx;
            v.ny = ny;
            v.nz = nz;
            v.u  = static_cast<float>(s) / static_cast<float>(sectors);
            v.v  = static_cast<float>(r) / static_cast<float>(rings);
            verts.push_back(v);
        }
    }

    // Quad indices — CCW when viewed from outside the sphere (+Y for upper hemi)
    int stride = sectors + 1;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < sectors; ++s) {
            GLuint tl = static_cast<GLuint>(r       * stride + s);
            GLuint tr = static_cast<GLuint>(r       * stride + s + 1);
            GLuint bl = static_cast<GLuint>((r + 1) * stride + s);
            GLuint br = static_cast<GLuint>((r + 1) * stride + s + 1);

            // Triangle 1: tl, bl, tr
            indices.push_back(tl); indices.push_back(bl); indices.push_back(tr);
            // Triangle 2: bl, br, tr
            indices.push_back(bl); indices.push_back(br); indices.push_back(tr);
        }
    }
    m_indexCount = static_cast<int>(indices.size());

    // ── Upload to GPU ─────────────────────────────────────────────────────────
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                 verts.data(), GL_STATIC_DRAW);

    // attrib 0: position (vec3, offset 0,  stride 32)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(0));
    // attrib 1: normal   (vec3, offset 12, stride 32)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(12));
    // attrib 2: uv       (vec2, offset 24, stride 32)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(24));

    glGenBuffers(1, &m_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(GLuint)),
                 indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
    CHECK_GL_ERROR();

    // ── Compile Phong shader ──────────────────────────────────────────────────
    std::string vertSrc = readShaderFile("src/shaders/bowl.vert");
    std::string fragSrc = readShaderFile("src/shaders/bowl.frag");
    if (vertSrc.empty() || fragSrc.empty()) {
        fprintf(stderr, "[BowlMesh] Failed to load bowl shaders\n");
        return;
    }
    GLuint vert = compileStage(vertSrc, GL_VERTEX_SHADER);
    GLuint frag = compileStage(fragSrc, GL_FRAGMENT_SHADER);
    if (!vert || !frag) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return;
    }
    m_shaderProgram = linkProgram(vert, frag);  // deletes vert/frag internally

    printf("[BowlMesh] init: %d verts, %d indices, rings=%d sectors=%d\n",
           (int)verts.size(), m_indexCount, rings, sectors);
}

void BowlMesh::draw(const glm::mat4& model, const glm::mat4& view,
                    const glm::mat4& proj,  const glm::vec3& lightPos)
{
    if (!m_shaderProgram || !m_vao) return;

    glUseProgram(m_shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "uModel"), 1, GL_FALSE,
                       glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "uView"),  1, GL_FALSE,
                       glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "uProj"),  1, GL_FALSE,
                       glm::value_ptr(proj));
    glUniform3fv(glGetUniformLocation(m_shaderProgram, "uLightPos"), 1,
                 glm::value_ptr(lightPos));

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    glUseProgram(0);
    CHECK_GL_ERROR();
}
