/*
 * constraints.cl — PBD spring constraint solver (serial Gauss-Seidel)
 *
 * Dispatched 2 × substeps times per frame.
 * Each dispatch: one forward pass OR one reverse pass through all springs.
 * Alternating forward/reverse reduces the directional bias inherent in
 * sequential Gauss-Seidel.
 *
 * Execution model (Sprint 2): global_size = local_size = 1.
 * A single work item iterates all springs sequentially.
 * This avoids write-race conditions on shared particles without requiring
 * graph coloring. Sprint 4 can replace this with a parallel graph-coloring
 * approach once correctness is verified.
 *
 * Before this kernel is dispatched, the caller (ComputePipeline::dispatchConstraints)
 * copies posIn → posOut via clEnqueueCopyBuffer so that posOut starts as the
 * same state as posIn.  The kernel then reads and writes posOut in-place
 * (Gauss-Seidel style: each updated position is visible to subsequent springs).
 *
 * PBD spring correction (equal masses):
 *   d        = posOut[j] - posOut[i]
 *   dist     = length(d)
 *   stretch  = dist - restLen
 *   correction = (d / dist) * stretch * 0.5
 *   posOut[i] += correction  (skipped if mass_inv == 0.0 — pinned)
 *   posOut[j] -= correction  (skipped if mass_inv == 0.0 — pinned)
 *
 * SimParams offset [2] = stiffness — used as a scale factor:
 *   scale = clamp(stiffness / 100.0, 0.0, 1.0)
 *   stiffness=50  → scale=0.50 (soft; converges with 12 substeps)
 *   stiffness=100 → scale=1.00 (fully resolves constraint each step)
 *
 * Spring buffer element (matches BufferManager::allocateSpringBuffer):
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
    __global const Particle* posIn,    /* not used directly — copy done in host */
    __global       Particle* posOut,   /* read + write: Gauss-Seidel in-place   */
    __global const Spring*   springs,
    __constant     float*    params,   /* SimParams flat float array            */
    int   numSprings,
    int   reverseOrder,                /* 1 = iterate from numSprings-1 down to 0 */
    int   numParticles                 /* needed for post-spring sphere projection */
)
{
    /* Only work item 0 does the serial loop; others return immediately */
    int gid = get_global_id(0);
    if (gid != 0) return;

    /* stiffness → correction scale in [0, 1] */
    float stiffness = params[2];
    float scale = clamp(stiffness / 100.0f, 0.0f, 1.0f);

    for (int k = 0; k < numSprings; ++k) {
        int idx = reverseOrder ? (numSprings - 1 - k) : k;

        int   iA      = springs[idx].indexA;
        int   iB      = springs[idx].indexB;
        float restLen = springs[idx].restLen;

        float3 pi = posOut[iA].pos.xyz;
        float3 pj = posOut[iB].pos.xyz;
        float  wi = posOut[iA].pos.w;   /* mass_inv; 0.0 = pinned */
        float  wj = posOut[iB].pos.w;

        float3 d    = pj - pi;
        float  dist = length(d);
        if (dist < 1e-6f) continue;    /* degenerate spring — skip */

        float  stretch    = dist - restLen;
        float3 dir        = d / dist;
        float  totalW     = wi + wj;
        if (totalW < 1e-6f) continue;  /* both pinned — skip */

        /* Correction vector (full PBD equal-mass formula, scaled by stiffness) */
        float3 correction = dir * stretch * scale;

        if (wi > 0.0f)
            posOut[iA].pos.xyz += correction * (wi / totalW);
        if (wj > 0.0f)
            posOut[iB].pos.xyz -= correction * (wj / totalW);
    }

    /* ── Post-spring sphere collision projection ─────────────────────────────
     * Spring corrections can drag particles back inside the sphere.
     * Re-project all non-pinned particles outside the sphere after each pass.
     * This is the standard PBD approach: solve all constraints (including
     * collision) together in the same loop.  Since global_size=1 this is cheap.
     */
    float3 sphereCenter = (float3)(params[8], params[9], params[10]);
    float  radius       = params[11];

    for (int i = 0; i < numParticles; ++i) {
        float mass_inv = posOut[i].pos.w;
        if (mass_inv == 0.0f) continue;   /* pinned — skip */

        float3 p   = posOut[i].pos.xyz;
        float3 toP = p - sphereCenter;
        float  d   = length(toP);

        if (d < radius && d > 1e-5f) {
            float3 normal  = toP / d;
            /* Push particle just outside sphere surface (epsilon prevents re-entry) */
            posOut[i].pos.xyz = sphereCenter + normal * (radius + 0.001f);
            /* Cancel inward normal velocity so the particle doesn't tunnel next step */
            float3 vel       = posOut[i].vel.xyz;
            float  normalVel = dot(vel, normal);
            if (normalVel < 0.0f)
                posOut[i].vel.xyz = vel - normalVel * normal;
        }
    }
}
