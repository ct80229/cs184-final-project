# Virtual Saran Wrap — CS184 Final Project

## Project Overview

Interactive real-time simulation of plastic wrap (saran wrap) that combines
GPU-accelerated cloth physics with thin-film iridescence rendering. Users
click and drag the material to stretch it; stretched regions produce
iridescent rainbow color shifts driven by real thin-film interference optics.
The wrap drapes over a rigid bowl object and adheres to surfaces.

**Course:** CS184 — Computer Graphics, UC Berkeley
**Deadline:** 4 weeks from project start
**Demo target:** 60 fps at 64×64 mesh on 2020 MacBook Pro (integrated GPU)

---

## Tech Stack

- **Language:** C++17
- **GPU API:** OpenGL 4.1 (vertex/fragment pipeline) + OpenCL (all GPU compute)
- **Windowing:** GLFW 3.x (window, input, OpenGL context)
- **Math:** GLM (OpenGL Mathematics library)
- **UI:** Dear ImGui (parameter sliders, debug overlays)
- **Build:** CMake 3.20+

---

## Repository Structure

```
/
├── CLAUDE.md                   # This file
├── CMakeLists.txt              # Top-level build definition
├── LEARNINGS.md                # Persistent bug/fix log (read before debugging)
├── TODO.md                     # Weekly task tracker
├── src/
│   ├── main.cpp                # Entry point: GLFW init, render loop, input
│   ├── gpu/
│   │   ├── compute_pipeline.cpp/.h   # OpenCL kernel dispatch wrappers
│   │   ├── render_pipeline.cpp/.h    # Vertex/fragment pipeline setup
│   │   └── buffer_manager.cpp/.h     # GL_ARRAY_BUFFER allocation for CL interop, ping-pong helpers
│   ├── sim/
│   │   ├── cloth.cpp/.h        # Particle/spring mesh, rest state init
│   │   ├── params.h            # Physics constants (stiffness, damping, gravity)
│   │   └── interaction.cpp/.h  # Mouse raycasting, grab constraint injection
│   ├── shaders/
│   │   ├── integrate.cl        # OpenCL: Verlet integration + sphere SDF collision
│   │   ├── constraints.cl      # OpenCL: PBD spring constraint solver
│   │   ├── thickness.cl        # OpenCL: Per-face area ratio → thickness_nm
│   │   ├── adhesion.cl         # OpenCL: Park & Byun cohesion spring forces
│   │   ├── cloth.vert          # Cloth mesh vertex shader
│   │   └── cloth.frag          # Thin-film interference fragment shader
│   └── render/
│       ├── cloth_mesh.cpp/.h   # VAO/VBO setup, draw call
│       └── bowl_mesh.cpp/.h    # Procedural hemisphere mesh + Phong shader
├── assets/  # (no external/ dir — GLFW, GLM, ImGui fetched via CMake FetchContent)
│   └── reference/              # Real saran wrap images for visual comparison
└── docs/
    ├── proposal.pdf
    ├── outline.docx
    └── research/               # Researcher agent output (markdown)
```

---

## Build System

```bash
# Configure (run once, or after CMakeLists changes)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build -j$(nproc)

# Run
./build/cloth_sim
```

Use `Debug` during development (enables OpenGL debug callbacks).
Switch to `Release` for performance measurement and the final demo.

CMake minimum required version: **3.20**. Requires a compiler with C++17 support
(Apple Clang 14+ on macOS, or GCC 11+).

---

## OpenGL / GLSL Conventions

- **OpenGL version:** 4.1 core profile (macOS maximum — no compute shaders, no SSBOs)
- **All GPU compute:** OpenCL kernels (`.cl`) — never `glDispatchCompute` or `GL_SHADER_STORAGE_BUFFER`
- **CL workgroup size:** 64 — safe for Intel UHD 630, multiple of preferred SIMD-8/16 width
- **CL kernel argument slots** — velocities are stored inline in the Particle struct,
  so there is no separate velocity buffer. Actual per-kernel layouts:
  - `integrate.cl`:   arg 0=posIn, arg 1=posOut, arg 2=params, arg 3=errorCount, arg 4=numParticles
  - `constraints.cl`: arg 0=posIn, arg 1=posOut, arg 2=springs, arg 3=params, arg 4=numSprings, arg 5=reverseOrder, arg 6=numParticles
  - `thickness.cl`:   arg 0=pos, arg 1=faceIndices, arg 2=restAreas, arg 3=thicknessOut, arg 4=numFaces (Sprint 3)
  - `adhesion.cl`:    arg 0=particles (pos+vel combined), arg 1=params, arg 2=numParticles (Sprint 3)
- **Sync between CL and GL:** `clEnqueueAcquireGLObjects` before dispatch,
  `clEnqueueReleaseGLObjects` + `clFinish` before any `glDraw*` call.
  Do NOT use `glMemoryBarrier` for CL/GL sync — it has no effect on CL queues.
- **Vertex shader particle access:** particle buffers are `GL_ARRAY_BUFFER` objects
  wrapped via TBO (`GL_TEXTURE_BUFFER`, OpenGL 3.1+). Bind as `GL_TEXTURE_BUFFER`,
  sample in the vertex shader with `texelFetch(posBuffer, gl_VertexID * 2)` for pos
  and `texelFetch(posBuffer, gl_VertexID * 2 + 1)` for vel. Format: `GL_RGBA32F`.
  Do NOT use `GL_SHADER_STORAGE_BUFFER` (requires 4.3).
- **Shader loading:** load `.vert`/`.frag` files at runtime from `src/shaders/`;
  load `.cl` source the same way for OpenCL programs. Never hardcode source strings.
- **Error checking:** `CHECK_GL_ERROR()` after every `glDraw*`; `CHECK_CL_ERROR(err)`
  after every CL call. Both macros are defined in `src/gpu/gl_check.h`.

---

## Particle Buffer Layout

Each particle is 32 bytes (two `float4` / `vec4`):

```c
// C++ / OpenCL (.cl) layout — must match sim/cloth.h exactly
// float4 pos: xyz = world position, w = mass_inv (0.0 = pinned)
// float4 vel: xyz = velocity,       w = flags (bit0=surface contact, bit1=grabbed)
```

```glsl
// GLSL vertex shader access via TBO (texelFetch):
// particle i occupies texels [i*2] and [i*2+1]
vec4 pos = texelFetch(uPosTBO, gl_VertexID * 2);
vec4 vel = texelFetch(uPosTBO, gl_VertexID * 2 + 1);
```

Use `float4`/`vec4` (not `float3`/`vec3`) — avoids alignment padding bugs that
are silent and hard to diagnose.

---

## Key Uniforms (`SimParams`)

`SimParams` is a **plain CL buffer** (`clCreateBuffer` + `clEnqueueWriteBuffer`),
not a GL UBO — it is never read by the vertex/fragment shaders directly.

```c
// C struct — upload via clEnqueueWriteBuffer every frame before dispatch
typedef struct {
    float dt;               // fixed 1/60s
    int   substeps;         // 8–16
    float stiffness;        // ~50.0 for saran wrap
    float bend_stiffness;   // ~20.0
    float damping;          // 0.98 per substep
    float gravity;          // 9.8
    float pad0; float pad1; // keep 16-byte alignment
    float sphere[4];        // xyz = center, w = radius
    float adhesion_k;       // ~0.1 * stiffness
    float adhesion_radius;  // ~2x rest particle spacing
    int   grab_particle;    // index of grabbed particle (-1 = none)
    float pad2;
    float grab_target[4];   // xyz = world-space drag target
} SimParams;
```

MVP, view, and lighting uniforms for the vertex/fragment shaders are set via
standard `glUniform*` calls — they do not go through `SimParams`.

---

## GPU Pipeline (per frame)

```
clEnqueueAcquireGLObjects(shared CL/GL buffers)

clEnqueueNDRangeKernel → integrate.cl
clEnqueueNDRangeKernel → constraints.cl  (forward pass)
clEnqueueNDRangeKernel → constraints.cl  (reverse pass)  × substeps
clEnqueueNDRangeKernel → thickness.cl
clEnqueueNDRangeKernel → adhesion.cl

clEnqueueReleaseGLObjects(shared CL/GL buffers)
clFinish(queue)          ← MUST drain before any GL call

glDrawElements   → cloth.vert + cloth.frag
glDrawElements   → bowl.vert  + bowl.frag
```

`clFinish` is mandatory — `clFlush` is not sufficient. Missing it causes GL to
draw with stale particle positions from the previous frame.

---

## Physics Implementation

### Cloth Mesh

- Grid of N×N particles; default N=64
- Springs: structural (grid edges), shear (diagonals), bend (skip-one neighbor)
- Rest lengths stored alongside the spring index buffer (GL_ARRAY_BUFFER, not SSBO)
- All particles are currently free (`pos.w = mass_inv = 1.0`); cloth falls freely over the bowl.
  Re-pinning corners is a one-line change in `cloth.cpp:init()`.

### Integration (`integrate.cl`)

- Semi-implicit Verlet: `vel += gravity * dt`, `vel *= damping`, `pos += vel * dt`
- Sphere SDF collision inline: project particle outside sphere surface, zero normal velocity
- NaN guard: reset to rest position + `atomic_inc` error counter on out-of-bounds pos
- Skip pinned particles: `if (pos.w == 0.0f) return;`

### Constraint Solving (`constraints.cl`)

- Position-Based Dynamics (PBD), not force-based
- Sprint 2 implementation: serial Gauss-Seidel (`global_size=1`) — deterministic,
  correct. One work item iterates all springs. Dispatched twice per substep
  (forward pass then reverse pass via `reverseOrder` flag) to reduce bias.
- Sprint 4 goal: parallelise via checkerboard graph colouring (independent colour
  sets dispatched separately). Do NOT implement in-place parallel writes without
  graph colouring — write races produce exploding cloth.
- Fixed particles: skip correction when `pos.w == 0.0f`

### Thickness Mapping (`thickness.cl`)

- Per face: `area_deformed = 0.5 * length(cross(ab, ac))`
- `strain = clamp(area_deformed / rest_area, 0.1f, 10.0f)`
- `thickness_nm = 12000.0f / strain`  (12µm rest thickness, conservation of volume)
- Written to face_thickness CL/GL buffer; read as a flat-interpolated per-face value
  in the fragment shader (`flat in float thickness_nm` in vert/frag pair)

### Adhesion (`adhesion.cl`) — Park & Byun §3.2

- Only runs on particles where `floatBitsToUint(vel.w) & 1u != 0u` (surface contact flag)
- For each contact particle, finds neighbors within `adhesion_radius`
- Force: `k_a * (r_c - dist) * normalize(p_j - p_i)` when `dist < r_c`
- Limits O(n²) search to contact subset — not full mesh

---

## Rendering

### Iridescence Shader (`cloth.frag`)

Implements thin-film interference using the **Wyman et al. (2013) CIE approximation**
(preferred over Zucconi — better violet/red rolloff). See `docs/research/thin_film_iridescence.md`
for the complete GLSL implementation.

```glsl
// OPD = 2.0 * n * d * cos(theta_t)  (Snell's law for theta_t)
// Integrate over visible spectrum 380–780nm, 20 samples
vec3 thin_film_color(float thickness_nm, float n, float cos_theta_i);

// View-dependent: cos_theta_i = abs(dot(N, V))
// Base material: near-transparent (alpha 0.75–0.92), slight blue-green tint
// Final mix: mix(base, iridescence, 0.4 + 0.4*stretch_mix + 0.2*fresnel)
```

Refractive index: `n = 1.5` (dry), `n = 1.33` (wet — `n_film` uniform, ImGui slider).
Thickness is clamped `150–1200 nm` in the shader. Visible iridescence appears at
150–800 nm (requires 15–80× area stretch — lower stiffness to see it during demo).
`flat in float thickness_nm` — face value, not interpolated across triangle.

### Bowl Mesh (`bowl_mesh.cpp`)

- Procedural UV hemisphere generated in C++, no Blender import
- Simple Phong shading — distinct enough to read as a physical object
- Collision body remains an analytic sphere SDF in the compute shader

---

## Interaction

### Mouse Drag (`interaction.cpp`)

1. On mouse button down: unproject ray via `glm::inverse(proj * view)`,
   Möller–Trumbore triangle intersection against a CPU-side mirror of current
   positions (maintain a `std::vector<glm::vec4>` updated via `glGetBufferSubData`
   once per interaction start — acceptable 1-frame latency)
2. On mouse move: re-unproject to a depth plane at the hit particle's original depth,
   update `SimParams.grab_target` via `glBufferSubData` — no further GPU readback
3. On mouse release: set `SimParams.grab_particle = -1`

---

## Performance Targets

| Mesh resolution | Target FPS |
|-----------------|------------|
| 32×32           | 60 fps     |
| 64×64           | 60 fps     |
| 128×128         | ≥ 30 fps   |

Measure with `glfwGetTime()` frame delta. Use `GL_TIME_ELAPSED` queries
(`glGenQueries` / `glBeginQuery` / `glEndQuery`) to profile individual
compute dispatches. Report results in the writeup.

---

## Physics Constants (starting values — tune via Dear ImGui)

| Parameter         | Value      | Notes                              |
|-------------------|------------|------------------------------------|
| `stiffness`       | 100.0      | scale=1.0 in constraints.cl (was 50 → rubber-like stretch) |
| `bend_stiffness`  | 20.0       | Floppy but not limp                |
| `damping`         | 0.98       | Per substep; ImGui: 0.95–0.999     |
| `substeps`        | 16         | 32 passes/frame (was 12 → insufficient convergence) |
| `adhesion_k`      | 0.1×stiff  | Fraction of structural stiffness   |
| `adhesion_radius` | 2× spacing | ~0.04 world units at 64×64         |
| `rest_thickness`  | 12000 nm   | 12µm, real saran wrap range        |
| `n_dry`           | 1.5        | Refractive index, dry film         |
| `n_wet`           | 1.33       | Refractive index, wet film         |

---

## Debug Modes

Toggle via keyboard shortcuts in `main.cpp` (GLFW key callbacks):

- `W` — wireframe overlay (`glPolygonMode(GL_FRONT_AND_BACK, GL_LINE)`)
- `T` — thickness heatmap (bypasses iridescence, visualizes raw `thickness_nm`)
- `N` — show surface normals as line segments
- `R` — reset cloth to rest state (re-upload rest positions to GL buffer)
- `P` — pause/resume simulation

The thickness heatmap (`T`) must be verified before the iridescence shader is
connected — confirms the strain→thickness pipeline produces values in the
150–800nm visible interference range.

---

## Known Constraints & Decisions

- **No full self-collision.** Replaced with proximity repulsion force in the compute
  shader. Prevents worst folding artifacts without 5–7 day BVH implementation risk.
- **No Blender mesh import.** Bowl is an analytic sphere SDF for collision and a
  procedural UV hemisphere for rendering. Visually equivalent at demo distance.
- **No spectral rendering.** Zucconi Gaussian approximation (20 wavelength samples)
  delivers ~90% of the visual result. Full CIE spectral integration is descoped.
- **`clFinish` before `glDraw*` is mandatory.** CL/GL sync is `clEnqueueReleaseGLObjects`
  + `clFinish` — not `glMemoryBarrier` (which has no effect on CL queues). Missing
  `clFinish` causes GL to draw with stale particle data, producing one-frame-lagged
  or corrupted positions that are hard to diagnose.

---

## macOS Platform Strategy — OpenCL + OpenGL 4.1

**Decision (2026-04-14):** OpenCL for compute passes + OpenGL 4.1 for rendering.

macOS caps OpenGL at 4.1 (no compute shaders). All GPU compute (integration,
constraints, thickness, adhesion) runs as **OpenCL kernels** (`.cl` files).
Rendering (vertex/fragment pipeline, VAO, draw calls) uses **OpenGL 4.1**.

### Key conventions

- Compute kernel sources live in `src/shaders/` alongside GLSL shaders, with `.cl` extension
  (e.g., `integrate.cl`, `constraints.cl`, `thickness.cl`, `adhesion.cl`)
- CL/GL interop: particle position and velocity buffers are created with
  `clCreateFromGLBuffer` so the OpenGL VAO and the OpenCL kernels share the
  same GPU memory — no CPU round-trips per frame
- Acquire/release pattern: call `clEnqueueAcquireGLObjects` before each CL dispatch
  and `clEnqueueReleaseGLObjects` after, then `clFinish` before any GL draw call
- The `SimParams` block is a CL buffer (not a GL UBO) — upload via `clEnqueueWriteBuffer`
- Workgroup size: 64 (matches original CLAUDE.md spec, safe for Intel integrated GPU)
- `CHECK_GL_ERROR()` macro for GL calls; `CHECK_CL_ERROR()` macro for CL calls (debug builds)

### CMake setup

```cmake
find_package(OpenCL REQUIRED)   # resolves to -framework OpenCL on macOS
target_link_libraries(cloth_sim OpenCL::OpenCL)
```

### Startup verification

Print at launch:
```
OpenGL:  4.1 Metal - Apple Intel UHD Graphics ...
OpenCL:  Apple (platform), Intel(R) UHD Graphics 630 (device)
```
Abort with a clear error if no CL GPU device is found.

---

## Self-Improvement Protocol

After fixing any bug or resolving a GPU/shader error:
1. Append a dated entry to `LEARNINGS.md` with: symptom, root cause, fix, prevention
2. If a new convention was established, update this file
3. Before debugging any issue, read `LEARNINGS.md` first — the fix may already exist