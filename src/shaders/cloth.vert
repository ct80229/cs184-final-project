#version 410 core

// cloth.vert — Cloth mesh vertex shader (OpenGL 4.1 core profile)
//
// TODO (Sprint 3): Read particle positions from particle SSBO via gl_VertexID;
//                  pass thickness_nm flat to fragment shader.
//                  Apply model/view/projection transforms.

void main()
{
    // Stub: position all vertices at origin until particle SSBO is wired up.
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
