#version 410 core

// cloth.frag — Cloth mesh fragment shader (OpenGL 4.1 core profile)
//
// Sprint 3: Wyman et al. (2013) CIE thin-film iridescence (uDebugThickness == 0)
//           or blue→red thickness heatmap (uDebugThickness == 1).
//
// Thickness source: texelFetch(uThicknessTBO, gl_PrimitiveID).r
//   gl_PrimitiveID == face index (available since OpenGL 3.2, no geometry shader needed).
//
// Iridescence model: thin-film Fabry-Perot reflectance integrated over 380–780 nm
// using Wyman/Sloan/Shirley (2013) CIE XYZ color matching function approximations.
// Reference: docs/research/thin_film_iridescence.md

#define PI 3.14159265358979

// ── Inputs from vertex shader ────────────────────────────────────────────────
in vec3 vNormal;   // world-space surface normal (approximate)
in vec3 vViewDir;  // world-space direction toward camera

// ── Uniforms ─────────────────────────────────────────────────────────────────
uniform samplerBuffer uThicknessTBO;   // per-face float (GL_R32F), texture unit 1
uniform int           uDebugThickness; // 1 = heatmap, 0 = iridescence
uniform float         uNFilm;          // film refractive index: 1.5 dry, 1.33 wet

out vec4 fragColor;

// ── Wyman, Sloan, Shirley (2013) CIE XYZ color matching functions ────────────
// "Simple Analytic Approximations to the CIE XYZ Color Matching Functions"
// Pasted verbatim from docs/research/thin_film_iridescence.md.
float xFit(float w) {
    float t1 = (w - 442.0) * ((w < 442.0) ? 0.0624 : 0.0374);
    float t2 = (w - 599.8) * ((w < 599.8) ? 0.0264 : 0.0323);
    float t3 = (w - 501.1) * ((w < 501.1) ? 0.0490 : 0.0382);
    return 0.362*exp(-0.5*t1*t1) + 1.056*exp(-0.5*t2*t2) - 0.065*exp(-0.5*t3*t3);
}
float yFit(float w) {
    float t1 = (w - 568.8) * ((w < 568.8) ? 0.0213 : 0.0247);
    float t2 = (w - 530.9) * ((w < 530.9) ? 0.0613 : 0.0322);
    return 0.821*exp(-0.5*t1*t1) + 0.286*exp(-0.5*t2*t2);
}
float zFit(float w) {
    float t1 = (w - 437.0) * ((w < 437.0) ? 0.0845 : 0.0278);
    float t2 = (w - 459.0) * ((w < 459.0) ? 0.0385 : 0.0725);
    return 1.217*exp(-0.5*t1*t1) + 0.681*exp(-0.5*t2*t2);
}

// CIE XYZ → linear sRGB (D65 illuminant)
vec3 xyz_to_rgb(vec3 xyz) {
    return vec3(
         3.2406*xyz.x - 1.5372*xyz.y - 0.4986*xyz.z,
        -0.9689*xyz.x + 1.8758*xyz.y + 0.0415*xyz.z,
         0.0557*xyz.x - 0.2040*xyz.y + 1.0570*xyz.z
    );
}

vec3 wavelength_to_rgb(float lambda) {
    vec3 xyz = vec3(xFit(lambda), yFit(lambda), zFit(lambda));
    return max(vec3(0.0), xyz_to_rgb(xyz));
}

// ── Thin-film interference color ─────────────────────────────────────────────
// Integrates single-layer Fabry-Perot reflectance over 380–780 nm (20 samples).
// Includes the half-wave phase shift at the air→film top interface:
//   R = 2*R0*(1 + cos(delta)) / (1 + R0² + 2*R0*cos(delta))
// which correctly produces constructive interference at OPD = 0 (normal incidence,
// zero thickness → transparent), consistent with Hecht Optics §9.4.
vec3 thin_film_color(float thickness_nm, float n, float cos_theta_i) {
    // Clamp to range where distinct iridescence orders are visible.
    // Above 1200 nm the higher-order fringes wash out; below 150 nm → transparent.
    thickness_nm = clamp(thickness_nm, 150.0, 1200.0);

    // Snell's law: refracted angle inside film
    float sin_theta_i = sqrt(max(0.0, 1.0 - cos_theta_i * cos_theta_i));
    float sin_theta_t = sin_theta_i / n;
    float cos_theta_t = sqrt(max(0.0, 1.0 - sin_theta_t * sin_theta_t));

    // Normal-incidence Fresnel reflectance (same at both interfaces for this film)
    float R0 = pow((n - 1.0) / (n + 1.0), 2.0);

    vec3 color = vec3(0.0);
    for (int k = 0; k < 20; k++) {
        float lambda = 380.0 + float(k) * (780.0 - 380.0) / 19.0;

        float opd   = 2.0 * n * thickness_nm * cos_theta_t;
        float delta = 2.0 * PI * opd / lambda;

        // Fabry-Perot reflectance with half-wave shift at top surface
        float R = 2.0 * R0 * (1.0 + cos(delta))
                / (1.0 + R0*R0 + 2.0*R0*cos(delta));

        color += R * wavelength_to_rgb(lambda);
    }
    return color / 20.0;
}

// ── Heatmap: blue (150 nm) → cyan → green → yellow → red (12000 nm) ──────────
// Range is 150–12000 nm (full simulation output) so variations are visible
// even when cloth never reaches the narrow iridescence window (150–800 nm).
vec3 thicknessHeatmap(float t_nm) {
    float u = clamp((t_nm - 150.0) / (12000.0 - 150.0), 0.0, 1.0);
    vec3 c;
    if      (u < 0.25) c = mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), u / 0.25);
    else if (u < 0.5)  c = mix(vec3(0.0, 1.0, 1.0), vec3(0.0, 1.0, 0.0), (u - 0.25) / 0.25);
    else if (u < 0.75) c = mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), (u - 0.5)  / 0.25);
    else               c = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (u - 0.75) / 0.25);
    return c;
}

void main()
{
    float thickness_nm = texelFetch(uThicknessTBO, gl_PrimitiveID).r;

    if (uDebugThickness == 1) {
        // T key: thickness heatmap — bypasses iridescence for pipeline verification.
        fragColor = vec4(thicknessHeatmap(thickness_nm), 1.0);
        return;
    }

    // ── Iridescence path ──────────────────────────────────────────────────────
    vec3 Nn = normalize(vNormal);
    vec3 Vn = normalize(vViewDir);

    // abs(): saran wrap is double-sided — iridescence same on both faces
    float cos_theta = abs(dot(Nn, Vn));

    // Physically R0 = 0.04 (4% reflectance) — correct for a HDR pipeline but
    // nearly invisible when clamped to [0,1] on a bright background.
    // 5× perceptual boost preserves color character while making the effect readable.
    vec3 iridescence = thin_film_color(thickness_nm, uNFilm, cos_theta) * 5.0;

    // Base material: near-transparent, slight blue-green tint (plastic look)
    vec3 base = vec3(0.85, 0.95, 0.90) * 0.15;

    // stretch_mix: 0 at rest (12000 nm), 1 at max stretch (150 nm)
    float stretch_mix = clamp((12000.0 - thickness_nm) / (12000.0 - 150.0), 0.0, 1.0);

    // Fresnel: stronger iridescence at grazing angles
    float fresnel     = pow(1.0 - cos_theta, 2.0);
    float mix_factor  = clamp(0.4 + 0.4 * stretch_mix + 0.2 * fresnel, 0.0, 1.0);

    vec3 final_color = mix(base, iridescence, mix_factor);

    // Alpha: mostly transparent; slightly more opaque where stretched
    float alpha = clamp(0.75 + 0.15 * stretch_mix, 0.75, 0.92);

    fragColor = vec4(final_color, alpha);
}
