#include "gpu/render_pipeline.h"
#include "gpu/gl_check.h"

#include <glm/gtc/type_ptr.hpp>  // glm::value_ptr

#include <fstream>
#include <sstream>
#include <cstdio>

RenderPipeline::RenderPipeline() = default;

RenderPipeline::~RenderPipeline()
{
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
}

// ── Private helpers ───────────────────────────────────────────────────────────

std::string RenderPipeline::readFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) {
        fprintf(stderr, "[GL] Cannot open shader file: %s\n", path.c_str());
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

GLuint RenderPipeline::compileShader(const std::string& src, GLenum type)
{
    GLuint shader = glCreateShader(type);
    const char* srcp = src.c_str();
    glShaderSource(shader, 1, &srcp, nullptr);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[4096] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        fprintf(stderr, "[GL] Shader compile error (%s):\n%s\n",
                type == GL_VERTEX_SHADER ? "vert" : "frag", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// ── Public API ────────────────────────────────────────────────────────────────

bool RenderPipeline::loadShaders(const std::string& vertPath,
                                 const std::string& fragPath)
{
    std::string vertSrc = readFile(vertPath);
    std::string fragSrc = readFile(fragPath);
    if (vertSrc.empty() || fragSrc.empty()) return false;

    GLuint vert = compileShader(vertSrc, GL_VERTEX_SHADER);
    GLuint frag = compileShader(fragSrc, GL_FRAGMENT_SHADER);
    if (!vert || !frag) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return false;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vert);
    glAttachShader(m_program, frag);
    glLinkProgram(m_program);

    // Shaders are linked into the program — safe to delete now
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint status = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &status);
    if (!status) {
        char log[4096] = {};
        glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        fprintf(stderr, "[GL] Program link error:\n%s\n", log);
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }

    CHECK_GL_ERROR();
    return true;
}

void RenderPipeline::bind()   { glUseProgram(m_program); }
void RenderPipeline::unbind() { glUseProgram(0); }

void RenderPipeline::setFloat(const std::string& name, float value)
{
    glUniform1f(glGetUniformLocation(m_program, name.c_str()), value);
}

void RenderPipeline::setInt(const std::string& name, int value)
{
    glUniform1i(glGetUniformLocation(m_program, name.c_str()), value);
}

void RenderPipeline::setMat4(const std::string& name, const glm::mat4& m)
{
    glUniformMatrix4fv(glGetUniformLocation(m_program, name.c_str()),
                       1, GL_FALSE, glm::value_ptr(m));
}

void RenderPipeline::setVec3(const std::string& name, const glm::vec3& v)
{
    glUniform3fv(glGetUniformLocation(m_program, name.c_str()),
                 1, glm::value_ptr(v));
}
