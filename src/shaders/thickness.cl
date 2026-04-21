/*
 * thickness.cl — Per-face area ratio → thickness_nm
 *
 * Dispatched once per frame after constraint solving, before adhesion.
 * Reads current particle positions; writes one float (thickness_nm) per face.
 *
 * Per-face computation:
 *   area_deformed = 0.5 * length(cross(ab, ac))
 *   strain        = clamp(area_deformed / rest_area, 0.1, 10.0)
 *   thickness_nm  = 12000.0 / strain   (12 µm rest thickness, volume conservation)
 *
 * Face buffer layout (faceIndices): flat int array, 3 ints per face.
 * Flat int* is used (not int3) to avoid the OpenCL int3 = 16-byte alignment issue.
 * Face f uses faceIndices[f*3+0], faceIndices[f*3+1], faceIndices[f*3+2].
 *
 * Index order matches ClothMesh::init() EBO exactly (same r,c loop) so that
 * gl_PrimitiveID in the fragment shader indexes the same face as get_global_id(0).
 */

typedef struct {
    float4 pos;  /* xyz = world position, w = mass_inv */
    float4 vel;  /* xyz = velocity,       w = flags    */
} Particle;

__kernel void thickness(
    __global const Particle* particles,   /* current particle state          */
    __global const int*      faceIndices, /* flat int array, 3 ints per face */
    __global const float*    restAreas,   /* precomputed per-face rest areas */
    __global       float*    thicknessOut,/* output: thickness_nm per face   */
    int numFaces
)
{
    int gid = get_global_id(0);
    if (gid >= numFaces) return;

    /* Read the three vertex indices for this face */
    int i0 = faceIndices[gid * 3 + 0];
    int i1 = faceIndices[gid * 3 + 1];
    int i2 = faceIndices[gid * 3 + 2];

    /* Read deformed positions (xyz only; w = mass_inv is irrelevant here) */
    float3 p0 = particles[i0].pos.xyz;
    float3 p1 = particles[i1].pos.xyz;
    float3 p2 = particles[i2].pos.xyz;

    float3 ab = p1 - p0;
    float3 ac = p2 - p0;

    float area_deformed = 0.5f * length(cross(ab, ac));

    float rest_area = restAreas[gid];

    /* Guard against degenerate rest faces (should not occur on a regular grid) */
    if (rest_area < 1.0e-8f) {
        thicknessOut[gid] = 12000.0f;
        return;
    }

    /* strain = area ratio; clamped to [0.1, 80] so thickness spans [150, 120000] nm.
     * Upper bound 80 = 12000nm / 150nm (max iridescence strain).
     * Previous bound of 10 made min thickness 1200nm, above the 800nm heatmap ceiling —
     * every face clamped to red regardless of deformation. */
    float strain = clamp(area_deformed / rest_area, 0.1f, 80.0f);

    /* Volume conservation: thickness * area = const → thickness = 12000 / strain */
    thicknessOut[gid] = 12000.0f / strain;
}
