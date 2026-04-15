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

    // TODO: Load, compile, and link a vertex + fragment shader pair from disk.
    //       Prints compile/link errors to stderr. Returns false on failure.
    bool loadShaders(const std::string& vertPath, const std::string& fragPath);

    // TODO: glUseProgram(m_program)
    void bind();

    // TODO: glUseProgram(0)
    void unbind();

    // TODO: glUniform1f — set float uniform by name
    void setFloat(const std::string& name, float value);

    // TODO: glUniform1i — set int uniform by name
    void setInt(const std::string& name, int value);

    // TODO: glUniformMatrix4fv — set mat4 uniform by name
    void setMat4(const std::string& name, const glm::mat4& m);

    // TODO: glUniform3fv — set vec3 uniform by name
    void setVec3(const std::string& name, const glm::vec3& v);

    GLuint programId() const { return m_program; }

private:
    GLuint m_program = 0;

    // TODO: Read file at path into a string; return empty on error
    std::string readFile(const std::string& path);

    // TODO: Compile a single shader stage (GL_VERTEX_SHADER / GL_FRAGMENT_SHADER).
    //       Returns shader ID on success, 0 on error (prints log to stderr).
    GLuint compileShader(const std::string& src, GLenum type);
};
