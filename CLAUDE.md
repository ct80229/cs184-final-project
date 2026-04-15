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
- **GPU API:** OpenGL 4.3 — compute shaders (SSBOs), vertex/fragment pipeline
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
│   │   ├── compute_pipeline.cpp/.h   # Compute shader dispatch wrappers
│   │   ├── render_pipeline.cpp/.h    # Vertex/fragment pipeline setup
│   │   └── buffer_manager.cpp/.h     # SSBO allocation, ping-pong helpers
│   ├── sim/
│   │   ├── cloth.cpp/.h        # Particle/spring mesh, rest state init
│   │   ├── params.h            # Physics constants (stiffness, damping, gravity)
│   │   └── interaction.cpp/.h  # Mouse raycasting, grab constraint injection
│   ├── shaders/
│   │   ├── integrate.comp      # Verlet integration + sphere SDF collision
│   │   ├── constraints.comp    # PBD spring constraint solver
│   │   ├── thickness.comp      # Per-face area ratio → thickness_nm
│   │   ├── adhesion.comp       # Park & Byun cohesion spring forces
│   │   ├── cloth.vert          # Cloth mesh vertex shader
│   │   └── cloth.frag          # Thin-film interference fragment shader
│   └── render/
│       ├── cloth_mesh.cpp/.h   # VAO/VBO setup, draw call
│       └── bowl_mesh.cpp/.h    # Procedural hemisphere mesh + Phong shader
├── external/
│   ├── glfw/                   # GLFW submodule or find_package
│   ├── glm/                    # GLM header-only
│   └── imgui/                  # Dear ImGui source
├── assets/
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

- **OpenGL version:** 4.3 core profile (required for compute shaders)
- **Compute shaders:** use SSBOs (`layout(std430, binding = N)`) — not UBOs
- **Workgroup size:** `layout(local_size_x = 64)` — safe for Intel integrated GPU
- **Binding slots:**
  - binding 0 = particle positions A (ping-pong read)
  - binding 1 = particle positions B (ping-pong write)
  - binding 2 = velocities
  - binding 3 = sim params UBO
  - binding 4 = springs index buffer
  - binding 5 = face thickness output
- **Sync between dispatches:** always call `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)`
  between consecutive compute dispatches that share an SSBO
- **Shader loading:** load `.comp`/`.vert`/`.frag` files at runtime from the
  `src/shaders/` directory — do NOT hardcode shader source as C++ string literals
- **Error checking:** wrap every `glDraw*` and `glDispatchCompute` call with
  a `CHECK_GL_ERROR()` macro in debug builds

---

## Particle Buffer Layout (GLSL struct, std430)

```glsl
// Must match the C++ struct in sim/cloth.h exactly
struct Particle {
    vec4 pos;   // xyz = world position, w = mass_inv (0.0 = pinned)
    vec4 vel;   // xyz = velocity, w = flags (bit0=surface contact, bit1=grabbed)
};
```

Use `vec4` to avoid std430 alignment surprises — `vec3` in an SSBO has 16-byte
alignment but only 12-byte size; padding bugs are silent and hard to diagnose.

---

## Key Uniforms (`SimParams` UBO)

```glsl
layout(std140, binding = 3) uniform SimParams {
    float dt;               // fixed 1/60s
    int   substeps;         // 8–16
    float stiffness;        // ~50.0 for saran wrap
    float bend_stiffness;   // ~20.0
    float damping;          // 0.98 per substep
    float gravity;          // 9.8
    float pad0; float pad1; // std140 requires 16-byte row alignment
    vec4  sphere;           // xyz = center, w = radius
    float adhesion_k;       // ~0.1 * stiffness
    float adhesion_radius;  // ~2x rest particle spacing
    int   grab_particle;    // index of grabbed particle (-1 = none)
    float pad2;
    vec4  grab_target;      // xyz = world-space drag target
};
```

---

## GPU Pipeline (per frame)

```
glDispatchCompute → integrate.comp
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)
glDispatchCompute → constraints.comp   (repeated substeps times)
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)
glDispatchCompute → thickness.comp
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)
glDispatchCompute → adhesion.comp
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)
    glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT)
glDrawElements   → cloth.vert + cloth.frag
```

---

## Physics Implementation

### Cloth Mesh

- Grid of N×N particles; default N=64
- Springs: structural (grid edges), shear (diagonals), bend (skip-one neighbor)
- Rest lengths stored in a separate SSBO alongside a spring index buffer
- Corner particles pinned by setting `pos.w = 0.0` (mass_inv) in the particle buffer

### Integration (`integrate.comp`)

- Semi-implicit Verlet: `vel += gravity * dt`, `vel *= damping`, `pos += vel * dt`
- Sphere SDF collision inline: project particle outside sphere surface, zero normal velocity
- NaN guard: `if (any(isnan(pos.xyz)) || any(greaterThan(abs(pos.xyz), vec3(100.0))))` →
  reset to rest position, increment an atomic error counter in a single-element SSBO

### Constraint Solving (`constraints.comp`)

- Position-Based Dynamics (PBD), not force-based
- Per substep: iterate all springs, apply correction `Δp = (|d| - rest) / |d| * stiffness * 0.5`
- Two dispatch passes per substep (forward then reverse spring index order) to reduce directional bias
- Fixed particles: skip correction when `pos.w == 0.0` (mass_inv field)
- Use Jacobi-buffered writes (ping-pong between position SSBOs A and B) — do NOT
  do in-place Gauss-Seidel; workgroup execution order is undefined in OpenGL compute

### Thickness Mapping (`thickness.comp`)

- Per face: `area_deformed = 0.5 * length(cross(ab, ac))`
- `strain = clamp(area_deformed / rest_area, 0.1, 10.0)`
- `thickness_nm = 12000.0 / strain`  (12µm rest thickness, conservation of volume)
- Written to face_thickness SSBO; read as a flat-interpolated per-face value in
  the fragment shader (`flat in float thickness_nm` in vert/frag pair)

### Adhesion (`adhesion.comp`) — Park & Byun §3.2

- Only runs on particles where `floatBitsToUint(vel.w) & 1u != 0u` (surface contact flag)
- For each contact particle, finds neighbors within `adhesion_radius`
- Force: `k_a * (r_c - dist) * normalize(p_j - p_i)` when `dist < r_c`
- Limits O(n²) search to contact subset — not full mesh

---

## Rendering

### Iridescence Shader (`cloth.frag`)

Implements Zucconi (2017) thin-film interference model in GLSL:

```glsl
// Gaussian wavelength → RGB (Zucconi §6)
vec3 wavelength_to_rgb(float lambda);

// OPD = 2.0 * n * d * cos(theta_t)
// Integrate over visible spectrum 380–780nm, 20 samples
vec3 thin_film_color(float thickness_nm, float n, float cos_theta);

// View-dependent phase shift: modulate by dot(N, V)
// Base material: near-transparent (alpha 0.85), slight blue-green tint
// Final mix: mix(base, iridescence, 0.6 + 0.4 * stretch)
```

Refractive index: `n = 1.5` (dry), `n = 1.33` (wet — controlled by ImGui slider).
Minimum thickness floor: `150.0` nm to prevent invisible regions at rest.

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
| `stiffness`       | 50.0       | Near-inextensible                  |
| `bend_stiffness`  | 20.0       | Floppy but not limp                |
| `damping`         | 0.98       | Per substep; ImGui: 0.95–0.999     |
| `substeps`        | 12         | Trade quality vs. perf at 128×128  |
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
- `R` — reset cloth to rest state (re-upload rest positions to SSBO)
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
- **`glMemoryBarrier` is mandatory** between compute passes sharing an SSBO.
  Missing it causes one-frame-stale data, silent wrong results, and intermittent
  shimmer that is extremely hard to diagnose.

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