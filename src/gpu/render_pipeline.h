#pragma once

// RenderPipeline — wraps an OpenGL vertex + fragment shader program (OpenGL 4.1 core).
//
// Loads .vert and .frag files from disk at runtime.
// Never hardcodes shader source strings — see CLAUDE.md shader loading convention.

#include <OpenGL/gl3.h>
#include <glm/glm.hpp>
#include <string>

class RenderPipeline {
public:
    RenderPipeline();
    ~RenderPipeline();

    // Load, compile, and link a vertex + fragment shader pair from disk.
    // Prints compile/link errors to stderr. Returns false on failure.
    bool loadShaders(const std::string& vertPath, const std::string& fragPath);

    void bind();
    void unbind();

    void setFloat(const std::string& name, float value);
    void setInt(const std::string& name, int value);
    void setMat4(const std::string& name, const glm::mat4& m);
    void setVec3(const std::string& name, const glm::vec3& v);

    GLuint programId() const { return m_program; }

private:
    GLuint m_program = 0;

    std::string readFile(const std::string& path);
    GLuint compileShader(const std::string& src, GLenum type);
};
