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

<!-- Append entries in this format:

### YYYY-MM-DD — [Short symptom title]
**Symptom:** what broke / what was observed
**Root cause:** why it happened
**Fix:** what was changed
**Prevention:** convention or check to add going forward

-->
