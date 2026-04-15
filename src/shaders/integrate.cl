/*
 * integrate.cl — Semi-implicit Verlet integration + sphere SDF collision + NaN guard
 *
 * Dispatched once per frame (before constraint solving).
 * One work item per particle (global_size = numParticles, local_size = 64).
 *
 * Per-particle steps:
 *   1. Skip pinned particles (pos.w == 0.0 → mass_inv = 0)
 *   2. Apply gravity (downward -Y): vel.y -= gravity * dt
 *   3. Apply grab spring if this particle is grabbed
 *   4. Damp velocity: vel *= damping
 *   5. Integrate position: pos += vel * dt
 *   6. Sphere SDF collision: project outside; zero inward normal velocity
 *   7. NaN / out-of-bounds guard: reset to last-known-good position
 *
 * SimParams layout (flat float array, matches C++ SimParams std140):
 *   [0]  dt              [1]  substeps (int bits)   [2]  stiffness   [3]  bend_stiffness
 *   [4]  damping         [5]  gravity               [6]  pad0        [7]  pad1
 *   [8]  sphere.x        [9]  sphere.y              [10] sphere.z    [11] sphere.w (radius)
 *   [12] adhesion_k      [13] adhesion_radius       [14] grab_particle (int bits) [15] pad2
 *   [16] grab_target.x   [17] grab_target.y         [18] grab_target.z [19] grab_target.w
 *
 * Particle layout:
 *   float4 pos — xyz = world position,  w = mass_inv (0.0 = pinned)
 *   float4 vel — xyz = velocity,        w = flags (bit0 = surface contact, bit1 = grabbed)
 */

typedef struct {
    float4 pos;
    float4 vel;
} Particle;

__kernel void integrate(
    __global const Particle* posIn,      /* read-only  — particle buffer A */
    __global       Particle* posOut,     /* write-only — particle buffer B */
    __constant     float*    params,     /* SimParams as flat float array  */
    __global       int*      errorCount, /* atomic NaN reset counter       */
    int numParticles
)
{
    int id = get_global_id(0);
    if (id >= numParticles) return;

    /* ── Read SimParams ───────────────────────────────────────────────────── */
    float dt         = params[0];
    /* params[1] = substeps (int) — not needed here */
    float damping    = params[4];
    float gravity    = params[5];

    float4 sphere    = (float4)(params[8], params[9], params[10], params[11]);
    int    grab_idx  = as_int(params[14]);
    float3 grab_tgt  = (float3)(params[16], params[17], params[18]);

    /* ── Read particle ────────────────────────────────────────────────────── */
    Particle p    = posIn[id];
    float mass_inv = p.pos.w;

    /* Pinned particle — copy unchanged */
    if (mass_inv == 0.0f) {
        posOut[id] = p;
        return;
    }

    float3 pos   = p.pos.xyz;
    float3 vel   = p.vel.xyz;
    uint   flags = as_uint(p.vel.w);

    /* ── 1. Gravity (−Y direction) ───────────────────────────────────────── */
    vel.y -= gravity * dt;

    /* ── 2. Grab spring ─────────────────────────────────────────────────── */
    if (id == grab_idx) {
        /* Strong spring toward grab target; stiffness chosen for responsiveness */
        float3 delta = grab_tgt - pos;
        vel += delta * 200.0f * dt;
        flags |= 2u;  /* set "grabbed" bit */
    } else {
        flags &= ~2u; /* clear "grabbed" bit */
    }

    /* ── 3. Damping ──────────────────────────────────────────────────────── */
    vel *= damping;

    /* ── 4. Integrate position ───────────────────────────────────────────── */
    pos += vel * dt;

    /* ── 5. Sphere SDF collision ─────────────────────────────────────────── */
    float3 toParticle = pos - sphere.xyz;
    float  dist       = length(toParticle);
    float  radius     = sphere.w;

    if (dist < radius && dist > 1e-5f) {
        float3 normal = toParticle / dist;
        /* Project particle just outside sphere surface (epsilon prevents re-entry) */
        pos = sphere.xyz + normal * (radius + 0.001f);
        /* Cancel inward normal velocity component */
        float normalVel = dot(vel, normal);
        if (normalVel < 0.0f)
            vel -= normalVel * normal;
        /* Tangential friction: damp sliding velocity on the sphere surface */
        float3 tangential = vel - dot(vel, normal) * normal;
        vel -= tangential * 0.55f;
        flags |= 1u;   /* set "surface contact" bit */
    } else if (dist >= radius) {
        flags &= ~1u;  /* clear "surface contact" bit */
    }

    /* ── 6. NaN / out-of-bounds guard ───────────────────────────────────── */
    /* Any NaN or position exploding past 100 m → reset to last-known-good  */
    int bad = (isnan(pos.x) | isnan(pos.y) | isnan(pos.z) |
               isnan(vel.x) | isnan(vel.y) | isnan(vel.z) |
               (int)(fabs(pos.x) > 100.0f) |
               (int)(fabs(pos.y) > 100.0f) |
               (int)(fabs(pos.z) > 100.0f));

    if (bad) {
        /* Freeze at last-known-good position (from posIn), zero velocity */
        posOut[id].pos = (float4)(posIn[id].pos.xyz, mass_inv);
        posOut[id].vel = (float4)(0.0f, 0.0f, 0.0f,
                                  as_float(flags & ~(1u | 2u)));
        atomic_inc(errorCount);
        return;
    }

    /* ── Write output ────────────────────────────────────────────────────── */
    posOut[id].pos = (float4)(pos, mass_inv);
    posOut[id].vel = (float4)(vel, as_float(flags));
}
