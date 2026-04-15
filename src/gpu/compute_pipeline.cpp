#include "gpu/compute_pipeline.h"

#include <cstdio>

ComputePipeline::ComputePipeline() = default;

ComputePipeline::~ComputePipeline()
{
    // TODO: implement — release kernels, program, queue, context
}

bool ComputePipeline::init(cl_device_id device)
{
    // TODO: implement
    (void)device;
    return false;
}

void ComputePipeline::dispatchIntegrate(cl_mem posIn, cl_mem posOut, cl_mem vel,
                                        cl_mem paramsUBO, int numParticles)
{
    // TODO: implement
    (void)posIn; (void)posOut; (void)vel; (void)paramsUBO; (void)numParticles;
}

void ComputePipeline::dispatchConstraints(cl_mem posIn, cl_mem posOut, cl_mem springs,
                                          cl_mem paramsUBO, int numSprings, bool reverseOrder)
{
    // TODO: implement
    (void)posIn; (void)posOut; (void)springs; (void)paramsUBO;
    (void)numSprings; (void)reverseOrder;
}

void ComputePipeline::dispatchThickness(cl_mem pos, cl_mem faceIndices,
                                        cl_mem thicknessOut, int numFaces)
{
    // TODO: implement
    (void)pos; (void)faceIndices; (void)thicknessOut; (void)numFaces;
}

void ComputePipeline::dispatchAdhesion(cl_mem pos, cl_mem vel,
                                       cl_mem paramsUBO, int numParticles)
{
    // TODO: implement
    (void)pos; (void)vel; (void)paramsUBO; (void)numParticles;
}

void ComputePipeline::finish()
{
    // TODO: implement — clFinish(m_queue)
}

std::string ComputePipeline::loadSource(const std::string& path)
{
    // TODO: implement — open file, read to string, return
    (void)path;
    return {};
}
