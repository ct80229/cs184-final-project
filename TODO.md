# TODO.md — Virtual Saran Wrap

**Deadline:** 2026-05-12 (4 weeks from 2026-04-14)
**Demo target:** 60 fps at 64×64, 2020 MacBook Pro (integrated GPU)

---

## Current Sprint (Week of 2026-04-14) — Foundation & Platform

### Platform Strategy: OpenCL + OpenGL 4.1 — [priority: high]
**Decision (2026-04-14):** Use OpenCL for compute passes + OpenGL 4.1 for rendering.
Buffer sharing via `cl_mem` / `CGLGetCurrentContext`. Supported on Intel + Apple Silicon (Rosetta).
- [ ] Verify OpenCL availability on dev machine (`clinfo` or `CL_DEVICE_TYPE_GPU` query at startup)
- [ ] Add OpenCL framework to CMakeLists (`find_package(OpenCL)` on macOS = `-framework OpenCL`)
- [ ] Replace SSBO compute dispatches with OpenCL kernels; keep OpenGL for VAO/draw pipeline
- [ ] Establish CL/GL interop: `clCreateFromGLBuffer` for shared particle buffers
- [ ] Document CL kernel file naming convention in CLAUDE.md (e.g., `src/shaders/*.cl`)

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

## Sprint 2 (Week of 2026-04-21) — Cloth Mesh & Physics Core

### Cloth Mesh & Particle Buffer — [priority: high]
- [ ] `sim/cloth.h/.cpp` — N×N particle grid, structural/shear/bend springs
- [ ] SSBO layout matching `Particle { vec4 pos; vec4 vel; }` (std430)
- [ ] `gpu/buffer_manager` — allocate ping-pong SSBOs (binding 0/1), velocity SSBO (2), springs (4), thickness (5)
- [ ] `SimParams` UBO (binding 3) — upload initial constants
- [ ] Corner particles pinned (`pos.w = 0.0`)

### Integration Compute Shader — [priority: high]
- [ ] `shaders/integrate.comp` — semi-implicit Verlet, damping, gravity
- [ ] Sphere SDF collision inline
- [ ] NaN guard + atomic error counter SSBO
- [ ] `gpu/compute_pipeline` — shader load, dispatch wrapper
- [ ] Verify: cloth falls under gravity and rests on sphere (wireframe)

### Constraint Solver — [priority: high]
- [ ] `shaders/constraints.comp` — PBD spring corrections (Jacobi/ping-pong)
- [ ] Forward + reverse pass per substep to reduce directional bias
- [ ] Skip pinned particles (`pos.w == 0.0`)
- [ ] Verify: cloth hangs from pinned corners without wild oscillation
- [ ] `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)` between every dispatch

---

## Sprint 3 (Week of 2026-04-28) — Rendering & Iridescence

### Bowl Mesh — [priority: high]
- [ ] `render/bowl_mesh.cpp/.h` — procedural UV hemisphere, Phong shader
- [ ] Distinct enough visually to read as a physical bowl
- [ ] Confirm cloth drapes visually over bowl (sphere SDF already handles collision)

### Cloth Rendering Pipeline — [priority: high]
- [ ] `render/cloth_mesh.cpp/.h` — VAO/VBO setup, indexed draw
- [ ] `shaders/cloth.vert` — read particle positions from SSBO, pass `thickness_nm` flat
- [ ] Basic Phong/diffuse pass first, confirm mesh renders correctly

### Thickness Compute Shader — [priority: high]
- [ ] `shaders/thickness.comp` — per-face area ratio → `thickness_nm`
- [ ] `thickness_nm = 12000.0 / strain`, clamped 150–800 nm range
- [ ] **Verify thickness heatmap (`T` key) shows values in visible interference range before connecting iridescence**

### Iridescence Fragment Shader — [priority: high]
- [ ] `shaders/cloth.frag` — Zucconi (2017) thin-film interference
- [ ] `wavelength_to_rgb()` Gaussian approximation
- [ ] `thin_film_color()` — 20-sample spectrum integration
- [ ] View-dependent OPD: `2.0 * n * d * cos(theta_t)`
- [ ] Base material: near-transparent (alpha 0.85), blue-green tint
- [ ] Final mix: `mix(base, iridescence, 0.6 + 0.4 * stretch)`

### Adhesion Compute Shader — [priority: medium]
- [ ] `shaders/adhesion.comp` — Park & Byun §3.2 cohesion springs
- [ ] Only runs on contact-flagged particles
- [ ] Neighbor search within `adhesion_radius`

---

## Sprint 4 (Week of 2026-05-05) — Interaction, Polish & Demo

### Mouse Interaction — [priority: high]
- [ ] `sim/interaction.cpp/.h` — ray unproject, Möller–Trumbore triangle hit
- [ ] CPU-side position mirror via `glGetBufferSubData` (once per grab start)
- [ ] Drag: re-unproject at depth plane, update `grab_target` via `glBufferSubData`
- [ ] Release: set `grab_particle = -1`
- [ ] Verify: drag stretches cloth, releases correctly

### Debug Modes & ImGui — [priority: medium]
- [ ] `W` — wireframe overlay
- [ ] `T` — thickness heatmap
- [ ] `N` — surface normals as line segments
- [ ] `R` — reset cloth to rest state (re-upload rest positions)
- [ ] `P` — pause/resume
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

## Completed

<!-- - [x] Task name — completed YYYY-MM-DD -->
