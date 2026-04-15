# TODO.md — Virtual Saran Wrap

**Final deadline:** 2026-05-04
**Milestone deadline:** 2026-04-20 (Gradescope: [Final Project] Milestone)
**Demo target:** 60 fps at 64×64, 2020 MacBook Pro (integrated GPU)

---

## Milestone Deliverables (Due 2026-04-20 @ 11:59 PM)

Submitted as a PDF to Gradescope with clickable links to all three items below.

- [ ] **Milestone webpage** — ~1 page: project name, accomplishments, preliminary results, updated work plan
- [ ] **1-minute video** — slides + narration or screen recording; all group members appear/speak; upload as unlisted YouTube or Google Drive (visible to all Berkeley users); link on webpage
- [ ] **2–3 slide deck** — project summary and current progress; link on webpage
- [ ] Submit PDF to Gradescope "[Final Project] Milestone" — use Safari Print → Save as PDF for correct sizing

*Staff feedback due back by 2026-04-24.*

---

## Sprint 1 (Week of 2026-04-14) — Foundation & Platform

### Platform Strategy: OpenCL + OpenGL 4.1 — [priority: high]
**Decision (2026-04-14):** Use OpenCL for compute passes + OpenGL 4.1 for rendering.
Buffer sharing via `cl_mem` / `CGLGetCurrentContext`. Supported on Intel + Apple Silicon (Rosetta).
- [x] Verify OpenCL availability on dev machine (`probeOpenCL()` in main.cpp) — 2026-04-15
- [x] Add OpenCL framework to CMakeLists (`-framework OpenCL`) — 2026-04-15
- [x] Replace SSBO compute dispatches with OpenCL kernels; keep OpenGL for VAO/draw pipeline — 2026-04-15
- [x] Establish CL/GL interop: `clCreateFromGLBuffer` for shared particle buffers — 2026-04-15
- [x] Document CL kernel file naming convention in CLAUDE.md (`src/shaders/*.cl`) — 2026-04-15

### CMake & Project Skeleton — [priority: high]
- [x] Write `CMakeLists.txt` — GLFW 3.4, GLM 1.0.1, ImGui v1.91.5 via FetchContent, `-framework OpenCL`
- [x] All source file stubs created under `src/` (gpu/, sim/, render/, shaders/)
- [x] Confirm `cmake -B build && cmake --build build` produces a runnable binary
- [x] GLFW window opens, OpenGL 4.1 core profile context, render loop running — completed 2026-04-15

### GPU Context Verification — [priority: high]
- [x] Print OpenGL version and renderer string at startup — completed 2026-04-15
- [x] Implement `CHECK_GL_ERROR()` macro in `src/gpu/gl_check.h` — completed 2026-04-15
- [x] OpenCL GPU probe at startup (`probeOpenCL()` in `main.cpp`) — completed 2026-04-15
- [x] GL ARB debug callback (`GL_ARB_debug_output`) for debug builds — completed 2026-04-15
- [ ] ImGui panels wired up (deferred to Sprint 4)

---

## Sprint 2 (2026-04-15 – 2026-04-19) — Cloth Mesh & Physics Core ✅ COMPLETE 2026-04-15

### Cloth Mesh & Particle Buffer — [priority: high]
- [x] `sim/cloth.cpp` — 64×64 particle grid, structural/shear/bend springs (23938 total) — 2026-04-15
- [x] Particle layout: `{ vec4 pos (w=mass_inv); vec4 vel (w=flags) }` = 32 bytes — 2026-04-15
- [x] `gpu/buffer_manager.cpp` — GL_ARRAY_BUFFER ping-pong (posA/B), springs, thickness — 2026-04-15
      NOTE: GL_SHADER_STORAGE_BUFFER is 4.3+ only; GL_ARRAY_BUFFER used for CL interop.
- [x] `SimParams` as a CL buffer via `clCreateBuffer` (not a GL UBO for CL compute) — 2026-04-15
- [x] Corner pinning REMOVED — all particles mass_inv=1.0f — 2026-04-15
      NOTE: pinned corners caused a tent shape (edges anchored, centre fell). Saran wrap
      has no anchor points; drape is achieved purely by gravity + sphere collision.

### Integration Compute Kernel — [priority: high]
- [x] `shaders/integrate.cl` — semi-implicit Verlet, damping, gravity — 2026-04-15
- [x] Sphere SDF collision inline — 2026-04-15
- [x] NaN guard + atomic error counter — 2026-04-15
- [x] `gpu/compute_pipeline.cpp` — CGL/CL sharegroup context, kernel load, dispatch — 2026-04-15
- [x] Verify visually: cloth falls and drapes on sphere — confirmed 2026-04-15

### Constraint Solver — [priority: high]
- [x] `shaders/constraints.cl` — serial Gauss-Seidel PBD, global_size=1 — 2026-04-15
- [x] Forward + reverse pass per substep (reverseOrder flag) — 2026-04-15
- [x] Skip pinned particles (`pos.w == 0.0`) — still in code but no particles are currently pinned — 2026-04-15
- [x] Dispatch loop wired in main.cpp render loop — 2026-04-15
- [x] Verify visually: cloth drapes correctly without oscillation — confirmed 2026-04-15 (no pinned corners; drape verified)
- [ ] Sprint 4: parallelise via checkerboard graph colouring for 60 fps at 64×64

---

## Sprint 3 — Rendering & Iridescence (minimal rendering COMPLETE 2026-04-15)

### Bowl Mesh ✅
- [x] `render/bowl_mesh.cpp` — procedural full sphere (theta 0→π), back-face culled during draw — 2026-04-15
      NOTE: corrected from upper hemisphere to full sphere after visual review.
- [x] `shaders/bowl.vert` + `shaders/bowl.frag` — created — 2026-04-15
- [x] Distinct visually (grey-blue Phong vs white cloth) — 2026-04-15

### Cloth Rendering Pipeline ✅
- [x] `render/cloth_mesh.cpp` — VAO+EBO, TBO for particle positions — 2026-04-15
- [x] `shaders/cloth.vert` — TBO fetch via texelFetch(uPosTBO, gl_VertexID*2) — 2026-04-15
- [x] `render_pipeline.cpp` — loadShaders, bind/unbind, all uniform setters — 2026-04-15
- [x] TBO rebind each frame (ClothMesh::rebindTBO) — handles ping-pong ID alternation — 2026-04-15
- [x] Wireframe via glPolygonMode(GL_LINE) + GL_TRIANGLES (W key) — 2026-04-15

### Thickness Compute Shader — [priority: high, Sprint 3 remaining]
- [ ] `shaders/thickness.cl` — per-face area ratio → `thickness_nm`; wire restAreas buffer
- [ ] `thickness_nm = 12000.0 / strain`, clamped 150–800 nm range
- [ ] **Verify T key heatmap shows values in visible interference range before connecting iridescence**

### Iridescence Fragment Shader — [priority: high, Sprint 3 remaining]
- [ ] `shaders/cloth.frag` — Wyman et al. (2013) CIE approximation (preferred over Zucconi — better violet/red rolloff; see docs/research/thin_film_iridescence.md)
- [ ] `wavelength_to_rgb()` via xFit/yFit/zFit Gaussians + XYZ→sRGB
- [ ] `thin_film_color()` — 20-sample spectrum integration, 380–780 nm
- [ ] View-dependent OPD: `2.0 * n * d * cos(theta_t)` (Snell's law for theta_t)
- [ ] Base material: near-transparent (alpha 0.75–0.92), blue-green tint
- [ ] Final mix: `mix(base, iridescence, 0.4 + 0.4*stretch_mix + 0.2*fresnel)`

### Adhesion Compute Shader — [priority: medium, Sprint 3 remaining]
- [ ] `shaders/adhesion.cl` — Park & Byun §3.2 cohesion springs (wire up dispatch)
- [ ] Only runs on contact-flagged particles
- [ ] Neighbor search within `adhesion_radius`

---

## Sprint 4 (2026-04-28 – 2026-05-04) — Interaction, Polish & Demo

### Mouse Interaction — [priority: high]
- [ ] `sim/interaction.cpp/.h` — ray unproject, Möller–Trumbore triangle hit
- [ ] CPU-side position mirror via `glGetBufferSubData` (once per grab start)
- [ ] Drag: re-unproject at depth plane, update `grab_target` via `glBufferSubData`
- [ ] Release: set `grab_particle = -1`
- [ ] Verify: drag stretches cloth, releases correctly

### Debug Modes & ImGui — [priority: medium]
- [x] `W` — wireframe overlay (glPolygonMode + GL_TRIANGLES) — 2026-04-15
- [ ] `T` — thickness heatmap (requires thickness.cl to be implemented first)
- [ ] `N` — surface normals as line segments
- [x] `R` — reset cloth to rest state — 2026-04-15
- [x] `P` — pause/resume — 2026-04-15
- [ ] ImGui sliders: stiffness, bend_stiffness, damping, substeps, n_dry/n_wet, adhesion_k

### Performance Profiling — [priority: medium]
- [ ] `glfwGetTime()` frame delta display in ImGui
- [ ] `GL_TIME_ELAPSED` queries per compute dispatch
- [ ] Hit 60 fps at 64×64 on integrated GPU — tune substeps if needed
- [ ] Test 128×128 — target ≥ 30 fps

### Final Demo Prep — [priority: high]
- [ ] Visual comparison against `assets/reference/` real saran wrap images
- [ ] Record demo video (screen capture, 60 fps)
- [ ] Writeup: physics constants table, GPU timing results

---

## Backlog

- [ ] Self-collision (descoped — see CLAUDE.md: proximity repulsion only)
- [ ] Spectral CIE rendering (descoped — Zucconi Gaussian is sufficient)
- [ ] Blender mesh import (descoped — procedural bowl only)

---

## Debt

- [ ] `bowl_mesh.cpp`: generates a full sphere (theta 0→π) then relies on back-face culling
      to hide the lower hemisphere. Could generate only the upper hemisphere (theta 0→π/2)
      to halve the vertex/index count. Low priority — geometry is tiny vs. particle buffers.
- [ ] `interaction.cpp`: all three methods are stubs with `(void)` suppression. Sprint 4 work.
- [ ] `dispatchThickness` / `dispatchAdhesion` in `compute_pipeline.cpp`: are no-ops.
      Wire up once `restAreas` buffer and surface-contact detection are implemented (Sprint 3).
- [ ] Cloth corners not pinned (`mass_inv = 1.0` for all). Decide before final demo whether
      to pin corners for a cleaner drape or keep free-fall for interactive grabbing.

---

## Completed

<!-- - [x] Task name — completed YYYY-MM-DD -->
