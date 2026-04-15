/*
 * integrate.cl — Semi-implicit Verlet integration + sphere SDF collision + NaN guard
 *
 * Dispatched once per frame, before constraint solving.
 * Reads particle data from posIn (binding 0), writes to posOut (binding 1).
 * Sphere SDF parameters come from SimParams (paramsUBO).
 *
 * Per-particle work:
 *   vel += gravity * dt
 *   vel *= damping
 *   pos += vel * dt
 *   project pos outside sphere if penetrating; zero normal velocity component
 *   NaN guard: if any pos component is NaN or |pos| > 100, reset to rest and
 *              increment atomic error counter
 *
 * Particle layout (matches C++ struct Cloth::Particle / sim/params.h):
 *   float4 pos  — xyz = world position, w = mass_inv (0.0 = pinned)
 *   float4 vel  — xyz = velocity,       w = flags (bit0 = surface contact, bit1 = grabbed)
 */

typedef struct {
    float4 pos;
    float4 vel;
} Particle;

__kernel void integrate(
    __global const Particle* posIn,      /* read-only particle buffer A  */
    __global       Particle* posOut,     /* write-only particle buffer B */
    __constant     float*    params,     /* SimParams (flattened floats) */
    __global       int*      errorCount, /* atomic NaN error counter     */
    int numParticles
)
{
    /* TODO: implement */
}
