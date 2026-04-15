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
    __global       Particle* particles,  /* read-write (updates vel in place) */
    __constant     float*    params,     /* SimParams                          */
    int numParticles
)
{
    /* TODO: implement */
}
