/*
 * adhesion.cl — Park & Byun §3.2 cohesion spring forces
 *
 * Dispatched once per frame after thickness computation.
 * Only processes particles flagged as surface-contact (vel.w bit 0 set).
 * Limits O(n²) neighbor search to the contact subset — not the full mesh.
 *
 * Per-contact-particle force:
 *   for each neighbor j within adhesion_radius:
 *     dist = length(pos[j] - pos[i])
 *     if dist < adhesion_radius:
 *       force = adhesion_k * (adhesion_radius - dist) * normalize(pos[j] - pos[i])
 *       vel[i] += force * dt / mass   (mass = 1.0 / pos.w when pos.w != 0)
 *
 * adhesion_k and adhesion_radius are read from SimParams.
 */

typedef struct {
    float4 pos;
    float4 vel;
} Particle;

__kernel void adhesion(
    __global       Particle* particles,  /* read-write (updates vel.xyz only) */
    __constant     float*    params,     /* SimParams as flat float array      */
    int numParticles
)
{
    int i = get_global_id(0);
    if (i >= numParticles) return;

    /* Skip pinned particles (mass_inv == 0 means pinned) */
    if (particles[i].pos.w == 0.0f) return;

    /* Only apply cohesion to contact-flagged particles (vel.w bit 0) */
    if (!(as_uint(particles[i].vel.w) & 1u)) return;

    /* Read SimParams (flat float array, same layout as C++ SimParams std140):
     *   params[0]  = dt
     *   params[12] = adhesion_k      (byte offset 48)
     *   params[13] = adhesion_radius (byte offset 52)
     */
    float dt             = params[0];
    float adhesion_k     = params[12];
    float adhesion_radius = params[13];
    float mass_inv       = particles[i].pos.w;

    float3 pi   = particles[i].pos.xyz;
    float3 vel_i = particles[i].vel.xyz;

    /* Park & Byun §3.2 cohesion: attract contact neighbors within adhesion_radius.
     * Only work item i writes to particles[i].vel.xyz; all other reads are from
     * .pos.xyz (written by integrate.cl, not modified by this kernel).
     * No write races: each work item is responsible for exactly one output index. */
    for (int j = 0; j < numParticles; j++) {
        if (j == i) continue;
        /* Only attract to other contact particles to limit search cost */
        if (!(as_uint(particles[j].vel.w) & 1u)) continue;

        float3 diff = particles[j].pos.xyz - pi;
        float  dist = length(diff);

        if (dist > 1.0e-5f && dist < adhesion_radius) {
            /* Cohesion force: proportional to gap (adhesion_radius - dist) */
            float3 force = adhesion_k * (adhesion_radius - dist) * (diff / dist);
            vel_i += force * dt * mass_inv;
        }
    }

    particles[i].vel.xyz = vel_i;
}
