/*
 * constraints.cl — PBD spring constraint solver (Jacobi / ping-pong)
 *
 * Dispatched 2 × substeps times per frame (forward pass + reverse pass per substep).
 * Reads from posIn, writes corrected positions to posOut.
 * Uses Jacobi-style double-buffering — do NOT write in-place (Gauss-Seidel);
 * workgroup execution order is undefined in OpenCL, making in-place updates racy.
 *
 * Per-spring correction (PBD):
 *   d = pos[j] - pos[i]
 *   |d| = length(d)
 *   Δp = (|d| - restLen) / |d| * stiffness * 0.5
 *   pos[i] += Δp * normalize(d)   (skip if mass_inv == 0.0 — pinned)
 *   pos[j] -= Δp * normalize(d)   (skip if mass_inv == 0.0 — pinned)
 *
 * Spring buffer element layout (matches BufferManager::allocateSpringBuffer):
 *   int   indexA
 *   int   indexB
 *   float restLen
 *   float pad
 */

typedef struct {
    float4 pos;
    float4 vel;
} Particle;

typedef struct {
    int   indexA;
    int   indexB;
    float restLen;
    float pad;
} Spring;

__kernel void constraints(
    __global const Particle* posIn,    /* read-only  */
    __global       Particle* posOut,   /* write-only */
    __global const Spring*   springs,
    __constant     float*    params,   /* SimParams  */
    int   numSprings,
    int   reverseOrder                 /* 1 = iterate springs in reverse index order */
)
{
    /* TODO: implement */
}
