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

### 2026-04-15 — Thickness pipeline: int3 alignment, faceIndices strategy, gl_PrimitiveID TBO

**Symptom / Decision log for Sprint 3 thickness pipeline.**

**int3 alignment:**
OpenCL `int3` has `sizeof(int3) == 16` (same as `int4`, due to alignment). If the kernel
declares `__global const int3* faceIndices`, each pointer advance moves 16 bytes, so the
host buffer must store 4 ints per face (3 data + 1 pad). To avoid this silent gotcha,
changed the kernel to `__global const int* faceIndices` and index as
`faceIndices[gid*3+0/1/2]`. This uses 12 bytes per face with no padding and is unambiguous.
**Prevention:** Never use `int3`/`float3` as buffer element types in OpenCL kernel args.
Use a flat scalar array or a 4-component type (`int4`, `float4`) instead.

**faceIndices approach — CL-only buffer in Cloth:**
The EBO in `ClothMesh` holds the same triangle indices but is bound inside the VAO and
not accessible to OpenCL. Options: (a) move EBO allocation to `BufferManager` as a
CL/GL shared buffer, (b) compute face indices in `Cloth` and upload as a CL-only buffer.
Chose (b) because: the face topology is static (computed once from rest positions) and
never needs GL rendering access; a CL-only `clCreateBuffer` is simpler than adding
another CL/GL shared buffer to the acquire/release list; and it keeps `ClothMesh` self-
contained as a pure rendering object. The key invariant is that `Cloth::buildFaceData()`
replicates the exact `r,c` loop from `ClothMesh::init()` so `gl_PrimitiveID == kernel gid`.

**thickness_nm in fragment shader — gl_PrimitiveID + samplerBuffer TBO:**
Two options: (a) `flat in float` passed from cloth.vert using the provoking-vertex face ID,
(b) `gl_PrimitiveID` in the fragment shader + a `samplerBuffer` TBO (GL_R32F) wrapping the
thickness GL buffer. Chose (b) because: the same vertex can belong to multiple triangles
with different thicknesses, making the flat-in approach fragile and error-prone; `gl_PrimitiveID`
is a fragment-shader built-in since OpenGL 3.2 (no geometry shader needed); and the thickness
buffer GL ID never ping-pongs, so the TBO needs to be set up only once. The `GL_R32F` format
(1-component 32-bit float) is a valid TBO internal format since OpenGL 3.1.
**Prevention:** For any per-face (not per-vertex) data that the fragment shader needs, prefer
`gl_PrimitiveID` + a TBO over flat-in varyings. The provoking vertex rule creates subtle
ordering dependencies; gl_PrimitiveID is unambiguous.

---

### 2026-04-21 — Thickness heatmap shows uniform solid red: physics range, not pipeline bug
**Symptom:** Pressing T shows solid uniform red across the entire cloth surface with zero
spatial gradation. Cloth drapes correctly over the sphere (physics is working).
**Root cause:** The heatmap maps 150–800 nm to blue→red. For any face to appear non-red,
its area must strain ≥ 15× (12000 nm / 800 nm) from rest. With `stiffness=100` (PBD
scale=1.0, 32 passes/frame), the cloth is near-inextensible — the dome contact region
reaches only ~1.09× area strain. All faces output ~12000 nm → `u=clamp(18.23,0,1)=1.0`
→ identical solid red. The kernel, TBO binding, rest areas, and faceIndices are all correct.
A uniform-red result with an unbound unit-1 sampler would display blue (returns 0.0 → u=0),
not red — ruling out a TBO bug.
**Fix:** The pipeline is correct. To verify end-to-end, reduce `stiffness` to 5–10 via
the ImGui slider; the dome region will turn blue-green while the skirt remains red.
Restore to stiffness=100 for production physics. One-time CPU readback diagnostic was
added (gated on `g_debugThickness`); prints min/max/first-5 thickness values to stdout.
Remove the readback block from `main.cpp` after confirming values.
**Prevention:** The heatmap range 150–800 nm is the *iridescence* range, not the full
deformation range. Rest thickness (12000 nm) is 15–80× above it. Always test the thickness
pipeline at low stiffness (≤10) before connecting to the iridescence shader. Document the
required strain threshold (15×) so future engineers know what physics settings will produce
visible color variation.

---

### 2026-04-21 — Heatmap solid red even at stiffness=5: two compounding bugs
**Symptom:** After lowering stiffness to 5 (cloth visibly stretches into lightbulb shape),
the thickness heatmap remained uniform solid red with zero variation.
**Root cause:** Two bugs compound each other:
1. `thickness.cl` strain clamp upper bound was `10.0f`, making the minimum possible
   thickness output `12000 / 10 = 1200nm`. The heatmap range was `[150, 800]nm`.
   Since 1200 > 800, `u = clamp((t_nm-150)/650) ≥ 1.0` for every face → solid red.
   The kernel was correct, but it physically could not produce values the heatmap could show.
2. `cloth.frag` heatmap range `[150, 800]nm` was designed for the iridescence visible
   spectrum but has zero overlap with the simulation output range `[1200, 12000]nm`.
   Even with the clamp fixed, a cloth at stiffness 5–100 produces values in `[1200, 12000]nm`
   and the narrow `[150, 800]nm` window still shows all red.
**Fix:**
- `thickness.cl`: Changed strain clamp from `(0.1, 10.0)` to `(0.1, 80.0)`. Upper bound
  80 = 12000nm / 150nm, allowing the full iridescence strain range.
- `cloth.frag`: Changed heatmap range from `[150, 800]nm` to `[150, 12000]nm`. This covers
  the full simulation output (rest=12000nm=red, maximum stretch=150nm=blue) and shows
  visible deformation gradients at any stiffness value.
**Prevention:** When designing a debug heatmap, verify the display range covers the actual
output range of the data being visualized. The iridescence wavelength range (150–800nm)
is NOT the same as the thickness simulation range (150–12000nm). A strain clamp of 10
silently sets a floor on thickness that can exceed the heatmap ceiling. Always sanity-check:
`minimum_displayable_value < max_kernel_output` and `maximum_displayable_value > min_kernel_output`.

### 2026-04-21 — Iridescence shader, vertex normals, adhesion

**Vertex normal approximation from TBO neighbors:**
Normals are computed in cloth.vert by sampling two TBO neighbors per vertex (right/down or
left/up at boundaries) and taking a cross product. Integer modulo and integer division on
`gl_VertexID` work cleanly in GLSL 4.1 with `uniform int uGridSize`. Boundary clamping
uses one-sided differences (forward at interior/bottom/left, backward at top/right edge).
The result is approximate but sufficient for the `abs(dot(N,V))` Fresnel angle; no geometry
shader is needed. All TBO indices stay in bounds for a 64×64 grid (max texel 8191 < 8192).

**thin_film_color: clamp range [150, 1200] nm, not [150, 12000]:**
The research doc clamps to 1200 nm (not 12000). Above 1200 nm the higher interference orders
wash out; clamping to 1200 gives a 2nd-order blue-green tint at rest which is correct for
the "at rest" transparent-with-subtle-tint look. The heatmap uses the wider 12000 nm range
so deformation is visible even before reaching the iridescence window.

**Alpha blending for cloth:**
`fragColor.a` is 0.75–0.92 (from the stretch_mix formula). Without `glEnable(GL_BLEND)` the
alpha channel is silently discarded and the cloth appears solid white. Bowl must be drawn
first (depth-tested) so blending correctly composites the semi-transparent cloth over it.
Added `glEnable(GL_BLEND) + glBlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)` around cloth draw,
`glDisable(GL_BLEND)` after to prevent contaminating ImGui rendering.

**Adhesion kernel write-safety:**
Each work item i reads `.pos.xyz` from neighbours (never modified by this kernel) and writes
only to `particles[i].vel.xyz`. No race: no two work items share an output index. The contact-
flag check (`vel.w bit 0`) is set by integrate.cl before the adhesion dispatch, and the flags
are never written by adhesion. Correct per the CL in-order queue sequencing guarantee.

### 2026-04-21 — Mouse interaction: Möller-Trumbore, drag plane, callback ordering

**GLFW mouse callback ordering vs. ImGui:**
Registering `glfwSetMouseButtonCallback` / `glfwSetCursorPosCallback` AFTER
`ImGui_ImplGlfw_InitForOpenGL(install_callbacks=true)` causes ImGui to overwrite our
callbacks rather than chain to them — clicks on the cloth are silently swallowed by ImGui.
Fix: register all custom GLFW callbacks BEFORE ImGui init. ImGui's `install_callbacks=true`
saves the prior callback pointers and calls them from its own handler.

**Drag plane convention (constant NDC z):**
After a particle is grabbed, the drag target must follow the cursor at constant depth
(otherwise the particle jumps toward the camera on the first move). Fix: store the grabbed
particle's NDC z at grab time: `m_grabDepth = (proj*view*vec4(worldPos,1)).z / clipPos.w`.
On every cursor-move event, unproject the cursor to that same NDC z plane. This gives the
illusion that the particle is glued to the cursor at its original depth as the camera stays fixed.

**syncPositionsFromGPU call site:**
`glGetBufferSubData` must be called while GL owns the buffer (before `acquireForCL`).
The correct site is inside `mouseButtonCallback`, which fires from `glfwPollEvents()`.
Since `glfwPollEvents()` runs before `acquireForCL` in the render loop, the call is always safe.
Do NOT call `syncPositionsFromGPU` inside the compute dispatch block (after `acquireForCL`).

**SimParams mutated directly, not via glBufferSubData:**
`interaction.onMouseMove` and `onMouseRelease` only mutate the host-side `SimParams&`.
The existing `clEnqueueWriteBuffer(clParams, ...)` at the top of the per-frame compute block
uploads the modified struct to the GPU every frame — no extra `glBufferSubData` call needed.
(The paramsUBO GL buffer is unused by shaders and does not need updating at all.)

---

### 2026-04-21 — Mouse drag: cloth doesn't follow cursor + spurious grab release
**Symptom:** Clicking cloth shows "GRAB: ACTIVE" but dragging doesn't move the cloth.
Grab also released unexpectedly when cursor moved into the ImGui overlay or off the mesh.
**Root cause:** Three separate bugs:
1. *Spring force vs. constraints* — The grab spring in `integrate.cl` applies
   `vel += delta * 200 * dt ≈ delta * 3.33/frame`. The constraint solver runs 32 passes
   at scale=1.0, fully restoring all spring lengths after each integration step — the tiny
   position change from the spring is overridden every frame, so the cloth never visibly moves.
2. *Drag blocked by ImGui overlay* — `cursorPosCallback` had `if (WantCaptureMouse) return`
   at the top. The ImGui overlay (340×225px, top-left) has `WantCaptureMouse=true` whenever
   the cursor overlaps it, so any drag into that region froze the grab target mid-flight.
3. *Release swallowed when over ImGui* — `mouseButtonCallback` had `WantCaptureMouse` guard
   before the RELEASE path. Releasing the button while cursor was over ImGui skipped
   `onMouseRelease`, leaving `grab_particle` permanently set in `simParams`.
**Fix:**
- `constraints.cl`: After the spring loop, before sphere projection, add a PBD position
  constraint that re-pins the grabbed particle at `grab_target` each pass. Neighbouring
  particles converge to a constraint-satisfied state anchored at the cursor position.
- `main.cpp` `cursorPosCallback`: Remove `WantCaptureMouse` guard — active drags must
  continue tracking the cursor even when it passes over the ImGui overlay.
- `main.cpp` `mouseButtonCallback`: Move RELEASE handling BEFORE the `WantCaptureMouse`
  guard so the grab always ends cleanly regardless of where the button is released.
**Prevention:** PBD grab = position constraint, not force. A spring force fighting the
constraint solver is the wrong primitive — the solver always wins at scale=1.0. The correct
pattern: after each constraint pass, force-override the grabbed particle's position to the
cursor's 3D intersection. `WantCaptureMouse` guards belong only on PRESS events, not on
RELEASE or cursor-move events during an active drag.

<!-- Append entries in this format:

### YYYY-MM-DD — [Short symptom title]
**Symptom:** what broke / what was observed
**Root cause:** why it happened
**Fix:** what was changed
**Prevention:** convention or check to add going forward

-->
