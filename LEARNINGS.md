# LEARNINGS.md — Virtual Saran Wrap

Persistent log of bugs, root causes, fixes, and conventions.
Read this file before debugging any issue.

---

### 2026-04-14 — Platform decision: OpenCL + OpenGL 4.1 (not OpenGL 4.3)
**Symptom/Decision:** macOS caps OpenGL at 4.1; compute shaders (4.3) are unavailable.
**Root cause:** Apple deprecated OpenGL in macOS 10.14 and never shipped 4.3.
**Fix:** All compute passes (`integrate`, `constraints`, `thickness`, `adhesion`) are
OpenCL kernels (`.cl`). CL/GL interop via `clCreateFromGLBuffer` keeps buffers on the
GPU — no CPU readback per frame. Acquire/release pattern required around every dispatch.
**Prevention:** Never add `#version 430` or `layout(local_size_x=...)` GLSL syntax.
Keep `.cl` kernels in `src/shaders/`. Enforce `CHECK_CL_ERROR()` on all CL calls.

---

### 2026-04-15 — TBO must be re-bound every frame due to ping-pong GL ID swap
**Symptom:** (Preemptive design note — would cause every-other-frame cloth flicker)
**Root cause:** `BufferManager::swapPingPong()` physically swaps the GL object IDs stored in
`m_posA`/`m_posB`. After 25 swaps per frame (1 integrate + 12×2 constraint), the GL ID behind
`posBufferA()` alternates between the two physical GL buffers on even vs. odd frames. A TBO
created from the original `posBufferA` ID would point to the WRONG buffer every other frame.
**Fix:** `ClothMesh::rebindTBO(posBuffer)` calls `glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F,
posBuffer)` with the current `buffers.posBufferA()` ID, called once per frame immediately
after `compute.finish()`. `glTexBuffer` re-associates the existing TBO object cheaply.
**Prevention:** Any time a GL buffer ID is used to create a TBO (or any GL object that caches
a buffer reference), rebind it whenever the underlying buffer ownership changes.

---

### 2026-04-15 — Vertex shader particle access: TBO not SSBO on OpenGL 4.1
**Symptom:** (Preemptive — SSBO layout qualifier `layout(std430, binding=0)` would not compile)
**Root cause:** SSBOs in GLSL require OpenGL 4.3 core. The vertex shader cannot access the
particle SSBO via `layout(std430, binding=0) buffer Particles { ... }` on macOS 4.1.
**Fix:** Use a Texture Buffer Object (TBO). In C++: `glGenTextures → glBindTexture(GL_TEXTURE_BUFFER)
→ glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, posBufferA)`. In GLSL (cloth.vert):
`uniform samplerBuffer uPosTBO;` then `texelFetch(uPosTBO, gl_VertexID * 2).xyz` for position.
TBO format `GL_RGBA32F` exposes the Particle buffer as a flat `float4` array — particle i
occupies texels `i*2` (pos) and `i*2+1` (vel). TBOs are available since OpenGL 3.1.
**Prevention:** Never use SSBO layout qualifiers in .vert/.frag files. Only use them in .cl files
(OpenCL kernels). Vertex shader data from CL/GL shared buffers must go through a TBO.

---

### 2026-04-15 — glPolygonMode(GL_LINE) + GL_TRIANGLES = correct wireframe
**Symptom:** (Design note — using GL_LINES with triangle indices gives garbled wireframe)
**Root cause:** `GL_LINES` primitive interprets the index buffer as consecutive pairs
`(i0,i1), (i2,i3), ...`. A triangle index buffer (`A,B,C, D,E,F, ...`) becomes lines
`(A,B), (C,D), (E,F)` — only one edge of each triangle, and crossing quad boundaries.
**Fix:** Always draw with `GL_TRIANGLES`. The W key sets `glPolygonMode(GL_FRONT_AND_BACK, GL_LINE)`
which rasterises ALL three edges of every triangle — a proper wireframe grid.
**Prevention:** Wireframe via `glPolygonMode(GL_LINE)` + `GL_TRIANGLES` is the correct OpenGL
pattern. `GL_LINES` is only for explicitly line-topology geometry.

---

### 2026-04-15 — GL_SHADER_STORAGE_BUFFER unavailable on macOS OpenGL 4.1
**Symptom:** 17 compile errors — `use of undeclared identifier 'GL_SHADER_STORAGE_BUFFER'`
in buffer_manager.cpp and cloth.cpp.
**Root cause:** `GL_SHADER_STORAGE_BUFFER` is defined in OpenGL 4.3 (introduced alongside
compute shaders and SSBOs). macOS OpenGL caps at 4.1; `<OpenGL/gl3.h>` does not define it.
**Fix:** Use `GL_ARRAY_BUFFER` as the bind target when creating and uploading data to raw GPU
buffers. `clCreateFromGLBuffer` wraps a GL buffer object by ID, not by binding target — the
last bind target does not matter to the CL interop layer.
**Prevention:** Never use `GL_SHADER_STORAGE_BUFFER` in any C++/GL code; it is a 4.3 enum.
All SSBO-style data (particles, springs, thickness) are allocated as `GL_ARRAY_BUFFER` objects
and accessed exclusively by OpenCL kernels via CL/GL interop. Sprint 3 vertex shader access
will use TBOs (texture buffer objects, available in OpenGL 3.1+).

---

### 2026-04-15 — glBindBufferBase(GL_SHADER_STORAGE_BUFFER, …) also requires 4.3
**Symptom:** Binding-base calls in swapPingPong removed after GL_ARRAY_BUFFER migration.
**Root cause:** `glBindBufferBase` supports `GL_SHADER_STORAGE_BUFFER` only in 4.3+.
**Fix:** Removed the rebind calls from swapPingPong for Sprint 2. Sprint 3 will add TBO-based
vertex shader access; at that point we will bind the particle buffer as a `GL_TEXTURE_BUFFER`
and sample it via `texelFetch` in cloth.vert.
**Prevention:** Before calling `glBindBufferBase`, confirm the enum is valid for OpenGL 4.1
(`GL_UNIFORM_BUFFER` and `GL_TRANSFORM_FEEDBACK_BUFFER` are the only legal targets on 4.1).

---

### 2026-04-15 — CGL/CL sharegroup context device list must be empty
**Symptom:** (Preemptive — documented during init design)
**Root cause:** When using `CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE`, the CL context
adopts all GPU devices that back the current GL context. Passing a non-empty device list
causes `CL_INVALID_VALUE`.
**Fix:** Pass `numDevices=0` and `devices=nullptr` to `clCreateContext`. Query the device
afterward via `clGetContextInfo(ctx, CL_CONTEXT_DEVICES, …)`.
**Prevention:** CGL sharegroup contexts are device-list-free by design. See
compute_pipeline.cpp init() for the correct pattern.

---

### 2026-04-15 — Serial constraints kernel (global_size=1) for Sprint 2
**Symptom:** (Design note — not a bug)
**Root cause:** Parallel PBD constraints require graph coloring to avoid write races
(multiple work items writing to the same particle's posOut). Implementing graph coloring
takes ~1 day; Sprint 2 goal is physics correctness verification, not performance.
**Fix:** `constraints.cl` dispatched with `global_size=local_size=1`. Work item 0 iterates
all springs serially — deterministic, correct Gauss-Seidel. Convergence is good; 64×64
cloth may run slower than 60fps but will be physically correct.
**Prevention:** Sprint 4 parallel constraint pass: sort springs into independent colour sets
(checkerboard pattern for a regular grid cloth), dispatch each colour group independently.
Serial code in constraints.cl makes correctness easy to verify before optimising.

---

### 2026-04-15 — Pinned corners produce tent shape, not a drape
**Symptom:** Cloth formed a tent with the sphere as a centre pole. The four corners
stayed at rest height while the centre fell onto the sphere, pulling diagonally upward
along the diagonals — a pyramid shape, not a drape.
**Root cause:** Four corners were frozen (`pos.w = 0.0`). With edges anchored and the
centre free to fall, the spring network pulled the cloth into a cone rather than
letting it conform to the sphere surface.
**Fix:** Removed the `isCorner` block in cloth.cpp; all particles set to `mass_inv = 1.0f`.
**Prevention:** Saran wrap has no fixed anchor points — never pin corners unless
explicitly modelling a tablecloth or flag. Drape is achieved purely by gravity +
sphere collision, not by boundary constraints.

---

### 2026-04-15 — Cloth must be sized to match the sphere, not the viewport
**Symptom:** Only the central ~25% of the cloth touched the sphere; outer particles
fell freely and hung from spring tension alone, giving a bell-tent silhouette.
**Root cause:** Cloth spanned [-1,1]×[-1,1] = 2×2 world units; sphere equator is
1.0 unit wide. Grid spacing was also too large, making edge springs unusually
compliant and allowing the outer cloth to sag below the equator.
**Fix:** Resized cloth to [-0.6,0.6]×[-0.6,0.6] (1.2×1.2 units) and raised start
height to y=0.7 so the full extent drapes over the sphere dome.
**Prevention:** Cloth half-extent should be ~1.2–1.5× sphere radius so edges drape
over the sides but are not so wide that they miss the collision body entirely.
Start height should be above the sphere top (sphere top = sphere_center.y + radius).

---

### 2026-04-15 — PBD cloth slides through sphere without friction + projection epsilon
**Symptom:** After the initial drape, cloth slid off the sphere and fell through it.
**Root cause:** (a) Tangential friction coefficient of 0.25 was too low to prevent
sliding at the equator where gravity acts almost tangentially to the sphere surface.
(b) Particles projected exactly to `radius` drifted back inside on the next integration
step due to floating-point precision, causing repeated re-entry.
**Fix:** Raised tangential friction to 0.55 in `integrate.cl`. Added epsilon 0.001f
to both sphere projection sites (`integrate.cl` and `constraints.cl`) so particles
land at `radius + 0.001f`, safely outside the surface.
**Prevention:** Always project to `radius + epsilon` (1–5‰ of radius is sufficient).
Friction ~0.5 is a good baseline for cloth-on-solid contact; expose via ImGui for tuning.

---

### 2026-04-15 — PBD stiffness=50 too soft — rubber-like stretch under gravity
**Symptom:** Cloth stretched far below the sphere equator in a teardrop/lightbulb
shape rather than conforming to the dome. Springs visibly over-extended.
**Root cause:** `constraints.cl` computes `scale = clamp(stiffness / 100.0f, 0.0f, 1.0f)`.
At stiffness=50, scale=0.5 — only half the position error is corrected per pass.
With 12 substeps × 2 passes = 24 passes/frame, convergence is poor for large
deformations, leaving significant residual stretch each frame.
**Fix:** Raised `stiffness` to 100 (scale=1.0, full correction per pass) and
`substeps` to 16 (32 passes/frame) in `defaultSimParams()` in `params.h`.
**Prevention:** For near-inextensible materials like saran wrap, stiffness should be
100 (scale=1.0). Lower values suit rubber or soft cloth. The substeps/stiffness
tradeoff is tunable via ImGui (Sprint 4); document the scale formula so future
engineers understand what the parameter actually controls.

---

<!-- Append entries in this format:

### YYYY-MM-DD — [Short symptom title]
**Symptom:** what broke / what was observed
**Root cause:** why it happened
**Fix:** what was changed
**Prevention:** convention or check to add going forward

-->
