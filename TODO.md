# TODO.md — Virtual Saran Wrap

**Final deadline:** 2026-05-04
**Demo target:** 60 fps at 64×64, 2020 MacBook Pro (integrated GPU)

---

## Current Sprint (Week of 2026-04-20) — Interaction, Polish & Demo

### Mouse Interaction — [priority: high]
- [x] `sim/interaction.cpp` — ray unproject via `glm::inverse(proj * view)` — 2026-04-21
- [x] Möller–Trumbore triangle hit against CPU-side position mirror — 2026-04-21
- [x] CPU mirror: `glGetBufferSubData` into `std::vector<glm::vec4>` once per grab start — 2026-04-21
- [x] Mouse move: re-unproject to depth plane, write `grab_target` via `clEnqueueWriteBuffer` — 2026-04-21
- [x] Mouse release: `grab_particle = -1` — 2026-04-21
- [ ] Verify: drag stretches cloth and produces visible iridescence shift — [priority: high]

### ImGui Sliders — [priority: medium]
- [x] Stiffness slider (range 5–200, default 100) — drives stretch/iridescence visibility — 2026-04-21
- [x] Damping slider (0.95–0.999, default 0.98) — 2026-04-21
- [x] Substeps slider (4–32, default 16) — 2026-04-21
- [x] `n_film` slider (1.33–1.6, default 1.5) — wet/dry refractive index — 2026-04-21
- [ ] Adhesion_k slider (0.0–20.0) — [priority: low]

### Debug Modes — [priority: medium]
- [x] `W` — wireframe overlay — 2026-04-15
- [x] `T` — thickness heatmap (blue=150 nm, red=12000 nm) — 2026-04-15
- [x] `R` — reset cloth to rest — 2026-04-15
- [x] `P` — pause/resume — 2026-04-15
- [ ] `N` — surface normals as line segments — [priority: low]

### Performance Profiling — [priority: medium]
- [ ] `glfwGetTime()` frame delta → FPS counter in ImGui — [priority: medium]
- [ ] `GL_TIME_ELAPSED` query around CL dispatch block (one query wrapping the full compute pass) — [priority: medium]
- [ ] Verify 60 fps at 64×64 on integrated GPU — tune substeps if needed — [priority: high]
- [ ] Test 128×128 — target ≥ 30 fps — [priority: medium]

### Final Demo Prep — [priority: high]
- [ ] Visual comparison: render side-by-side with `assets/reference/` real saran wrap images — [priority: high]
- [ ] Record demo video (screen capture, 60 fps, 2–3 minutes) — [priority: high]
- [ ] Writeup: physics constants table, GPU timing results, iridescence model reference — [priority: high]

---

## Next Sprint — none; all remaining work is in the current sprint above

---

## Backlog (descoped — do not implement)

- Self-collision — replaced with proximity repulsion; BVH not worth 5–7 days
- Spectral CIE rendering — Wyman Gaussian (20 samples) is sufficient
- Blender mesh import — procedural bowl only
- Parallelise constraints via checkerboard graph colouring — serial Gauss-Seidel is
  correct and fast enough for 64×64 at current substeps; defer to post-deadline

---

## Debt (known issues, non-blocking)

- `bowl_mesh.cpp` generates a full sphere (theta 0→π) and relies on back-face culling.
  Generating only the upper hemisphere (theta 0→π/2) would halve vertex count — but
  geometry is negligible vs. particle buffers. Low priority.
- `bowl_mesh.cpp` duplicates `readShaderFile`/`compileStage`/`linkProgram` helpers
  that live in `RenderPipeline`. Fine while bowl shader is static.
- `buffer_manager.cpp::allocateParamsUBO()` allocates a GL_UNIFORM_BUFFER at binding 3
  that no shader reads — SimParams goes through CL. Remove if it causes confusion.

---

## Completed

### Milestone
- [x] Milestone webpage — 2026-04-20
- [x] 1-minute milestone video — 2026-04-20
- [x] 2–3 slide deck — 2026-04-20
- [x] Gradescope submission — 2026-04-20

### Sprint 1 — Foundation & Platform (complete 2026-04-15)
- [x] GLFW 4.1 core profile window, render loop, ImGui init
- [x] OpenCL GPU probe (`probeOpenCL()`), `-framework OpenCL` in CMakeLists
- [x] CL/GL interop established (`clCreateFromGLBuffer`)
- [x] `CHECK_GL_ERROR()` + `GL_ARB_debug_output` debug callback
- [x] `.cl` kernel file convention documented in CLAUDE.md

### Sprint 2 — Cloth Mesh & Physics Core (complete 2026-04-15)
- [x] 64×64 particle grid, 23938 springs (structural/shear/bend)
- [x] `integrate.cl` — Verlet, gravity, damping, sphere SDF collision, NaN guard
- [x] `constraints.cl` — serial Gauss-Seidel PBD, forward+reverse pass per substep
- [x] `BufferManager` — GL_ARRAY_BUFFER ping-pong, CL interop, `swapPingPong`
- [x] `ComputePipeline` — CGL/CL sharegroup context, kernel load, all dispatches
- [x] Physics corrections: corner pinning removed, cloth resized to [-0.6,0.6],
      tangential friction 0.55, sphere projection epsilon 0.001, stiffness→100, substeps→16

### Sprint 3 — Rendering & Iridescence (complete 2026-04-21)
- [x] `bowl_mesh.cpp` — procedural full sphere, grey-blue Phong, back-face culled
- [x] `cloth_mesh.cpp` — VAO+EBO, position TBO (GL_RGBA32F), thickness TBO (GL_R32F)
- [x] `cloth.vert` — `texelFetch(uPosTBO, gl_VertexID*2)`, TBO-neighbor normals, viewDir
- [x] `render_pipeline.cpp` — shader load, bind/unbind, all uniform setters
- [x] `thickness.cl` — per-face area ratio → thickness_nm (strain clamp 0.1–80)
- [x] `cloth.frag` — Wyman CIE (2013) thin-film iridescence, 20-sample spectrum,
      half-wave phase shift, Fresnel blend, T-key heatmap mode (`uDebugThickness`)
- [x] `adhesion.cl` — Park & Byun §3.2 cohesion springs, contact-flagged particles only
- [x] Alpha blending enabled; camera at (0, 1.5, 2.5) → (0, -0.3, 0)
- [x] W / T / P / R keys all working
