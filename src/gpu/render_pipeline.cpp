#include "gpu/render_pipeline.h"

RenderPipeline::RenderPipeline() = default;

RenderPipeline::~RenderPipeline()
{
    // TODO: implement — glDeleteProgram(m_program) if m_program != 0
}

bool RenderPipeline::loadShaders(const std::string& vertPath, const std::string& fragPath)
{
    // TODO: implement
    (void)vertPath; (void)fragPath;
    return false;
}

void RenderPipeline::bind()
{
    // TODO: implement — glUseProgram(m_program)
}

void RenderPipeline::unbind()
{
    // TODO: implement — glUseProgram(0)
}

void RenderPipeline::setFloat(const std::string& name, float value)
{
    // TODO: implement — glUniform1f(glGetUniformLocation(m_program, name.c_str()), value)
    (void)name; (void)value;
}

void RenderPipeline::setInt(const std::string& name, int value)
{
    // TODO: implement
    (void)name; (void)value;
}

void RenderPipeline::setMat4(const std::string& name, const glm::mat4& m)
{
    // TODO: implement
    (void)name; (void)m;
}

void RenderPipeline::setVec3(const std::string& name, const glm::vec3& v)
{
    // TODO: implement
    (void)name; (void)v;
}

std::string RenderPipeline::readFile(const std::string& path)
{
    // TODO: implement — open file, read entire contents to string
    (void)path;
    return {};
}

GLuint RenderPipeline::compileShader(const std::string& src, GLenum type)
{
    // TODO: implement — glCreateShader, glShaderSource, glCompileShader, check log
    (void)src; (void)type;
    return 0;
}
