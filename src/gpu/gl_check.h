#pragma once

// Must be included AFTER <OpenGL/gl3.h> and <OpenCL/opencl.h>.
// Both macros are no-ops in Release builds (NDEBUG defined).

#include <OpenGL/gl3.h>
#include <OpenCL/opencl.h>
#include <cstdio>

#ifndef NDEBUG

// CHECK_GL_ERROR() — call after any glDraw* or glDispatchCompute equivalent.
// Prints the offending error code, file, and line to stderr.
#define CHECK_GL_ERROR()                                                        \
    do {                                                                        \
        GLenum _gl_err = glGetError();                                          \
        if (_gl_err != GL_NO_ERROR)                                             \
            fprintf(stderr, "[GL] error 0x%04x  %s:%d\n",                      \
                    (unsigned)_gl_err, __FILE__, __LINE__);                     \
    } while (0)

// CHECK_CL_ERROR(err) — wrap the cl_int return value of any OpenCL API call.
// Example: CHECK_CL_ERROR(clEnqueueNDRangeKernel(...));
#define CHECK_CL_ERROR(err)                                                     \
    do {                                                                        \
        if ((cl_int)(err) != CL_SUCCESS)                                        \
            fprintf(stderr, "[CL] error %d  %s:%d\n",                          \
                    (int)(err), __FILE__, __LINE__);                            \
    } while (0)

#else

#define CHECK_GL_ERROR()        do {} while (0)
#define CHECK_CL_ERROR(err)     do { (void)(err); } while (0)

#endif // NDEBUG
