# Thin-Film Iridescence — Implementation Reference

## Summary

Plastic wrap iridescence arises from thin-film interference: light reflects from
both the top and bottom surfaces of the film, and the two reflected rays
interfere constructively or destructively depending on the optical path
difference (OPD). Because OPD is wavelength-dependent, different wavelengths
reinforce or cancel at different angles and thicknesses, producing rainbow color
shifts. The Zucconi (2017) Gaussian approximation gives a cheap, visually
accurate mapping from wavelength to RGB that is practical in a fragment shader.

---

## Physics Foundation

### Optical Path Difference

For a single dielectric film (refractive index `n`) of thickness `d` nm,
illuminated at angle `theta_i` (measured from the surface normal):

```
OPD = 2 * n * d * cos(theta_t)       [units: nm]
```

where `theta_t` is the refracted angle inside the film, found via Snell's law:

```
n_air * sin(theta_i) = n * sin(theta_t)
sin(theta_t) = sin(theta_i) / n
cos(theta_t) = sqrt(1 - sin²(theta_t))
```

For `n = 1.5` (dry saran wrap) viewed at normal incidence (`theta_i = 0`):
`cos(theta_t) = 1`, so `OPD = 2 * 1.5 * d = 3d`.

### Reflectance from Phase Difference

The phase shift accumulated by the bottom-surface reflection:

```
delta = (2*pi / lambda) * OPD        [radians]
```

There is also a half-wave phase shift at the air→film interface (higher-n
medium), but NOT at the film→air interface. Net effect: an additional `pi`
phase shift, which inverts the cosine term.

Single-layer reflectance (Fabry-Perot thin film, ignoring multiple-bounce
energy loss for a low-R film like plastic):

```
R0 = ((n - 1) / (n + 1))²           [normal-incidence Fresnel]

R(lambda) = 2 * R0 * (1 - cos(delta + pi))
           / (1 + R0² - 2 * R0 * cos(delta + pi))

         = 2 * R0 * (1 + cos(delta))
           / (1 + R0² + 2 * R0 * cos(delta))
```

For `n = 1.5`: `R0 = ((0.5)/(2.5))² = 0.04` (4% per surface, typical for glass/plastic).
The modulation depth is small but visible — saran wrap is mostly transparent with
a color tint, not a strong mirror.

For `n = 1.33` (wet): `R0 = ((0.33)/(2.33))² ≈ 0.02`.

---

## Thickness → Visible Iridescence

| Thickness (nm) | OPD at n=1.5 | First constructive λ | Visible color |
|---|---|---|---|
| 80 | 240 nm | UV (not visible) | transparent |
| 130 | 390 nm | violet | faint violet tint |
| 160 | 480 nm | blue-green | blue-green shimmer |
| 230 | 690 nm | red | red/orange shimmer |
| 330 | 990 nm | ~990 nm (IR) | 2nd-order violet |
| 400 | 1200 nm | 2nd-order blue | blue again |
| 500 | 1500 nm | 2nd-order green | green |

**Target range: 150–800 nm** covers the first and second interference orders,
producing the full rainbow progression from blue-green → red → violet → blue
as the wrap stretches and thins. Below 150 nm: nearly transparent. Above
800 nm: higher-order fringes wash out into white.

---

## Wavelength → RGB Mapping

### Wyman et al. (2013) CIE Approximation

More accurate than the Zucconi Gaussian for matching human color perception.
Implements CIE 1931 XYZ color matching functions as a sum of Gaussians, then
converts XYZ → linear sRGB.

```glsl
// Wyman, Sloan, Shirley (2013): "Simple Analytic Approximations to the
// CIE XYZ Color Matching Functions"
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

// XYZ → linear sRGB (D65 illuminant)
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
```

### Zucconi (2017) Fast Approximation

Less physically accurate but cheaper (no transcendentals beyond `exp`). Good
as a fallback if the Wyman approach is too expensive.

Zucconi represents the visible spectrum as a mixture of Gaussian bumps directly
in RGB space. One common implementation (from his "Improving the Rainbow"
article):

```glsl
// Zucconi (2017) — direct RGB bump approximation
// Piecewise linear wavelength → RGB, smooth blend
vec3 spectral_zucconi(float w) {
    // Normalized wavelength in [0,1] over 380–780 nm
    float x = clamp((w - 380.0) / 400.0, 0.0, 1.0);

    // Red peak at ~700nm, green at ~550nm, blue at ~440nm
    float r = smoothstep(0.55, 0.75, x) - smoothstep(0.85, 1.0, x)
            + smoothstep(0.0, 0.05, x) * 0.3;   // small violet-red overlap
    float g = smoothstep(0.25, 0.45, x) - smoothstep(0.65, 0.85, x);
    float b = smoothstep(0.0, 0.2, x) - smoothstep(0.35, 0.55, x);
    return clamp(vec3(r, g, b), 0.0, 1.0);
}
```

**Recommendation for this project:** use the Wyman CIE approximation. It
correctly handles the violet-to-red rolloff and produces more accurate
blue-green shimmers at 150–250 nm thickness.

---

## Fragment Shader Implementation

### Complete `thin_film_color` Function

```glsl
#define PI 3.14159265358979

// --- Wyman CIE helpers (paste from above) ---
// float xFit(float w) { ... }
// float yFit(float w) { ... }
// float zFit(float w) { ... }
// vec3 xyz_to_rgb(vec3 xyz) { ... }
// vec3 wavelength_to_rgb(float lambda) { ... }

vec3 thin_film_color(float thickness_nm, float n, float cos_theta_i) {
    // Clamp thickness to useful range
    thickness_nm = clamp(thickness_nm, 150.0, 1200.0);

    // Refracted angle inside film (Snell's law)
    float sin_theta_i = sqrt(max(0.0, 1.0 - cos_theta_i * cos_theta_i));
    float sin_theta_t = sin_theta_i / n;
    float cos_theta_t = sqrt(max(0.0, 1.0 - sin_theta_t * sin_theta_t));

    // Normal-incidence Fresnel reflectance at film interfaces
    float R0 = pow((n - 1.0) / (n + 1.0), 2.0);

    // Integrate over visible spectrum (20 samples, 380–780 nm)
    vec3 color = vec3(0.0);
    const int N_SAMPLES = 20;
    for (int i = 0; i < N_SAMPLES; i++) {
        float lambda = 380.0 + float(i) * (780.0 - 380.0) / float(N_SAMPLES - 1);

        // Optical path difference and phase
        float opd   = 2.0 * n * thickness_nm * cos_theta_t;
        float delta = 2.0 * PI * opd / lambda;

        // Thin-film reflectance (includes half-wave phase shift at top surface)
        float R = 2.0 * R0 * (1.0 + cos(delta))
                / (1.0 + R0*R0 + 2.0*R0*cos(delta));

        color += R * wavelength_to_rgb(lambda);
    }
    color /= float(N_SAMPLES);
    return color;
}
```

### Integrating into `cloth.frag`

```glsl
// Inputs from vertex shader
flat in float thickness_nm;   // from thickness.comp via face SSBO
in vec3 N;                    // surface normal (world space)
in vec3 V;                    // view direction (world space, toward camera)

uniform float n_film;         // 1.5 dry, 1.33 wet (ImGui slider)
uniform float stretch_mix;    // 0–1, extra iridescence intensity at high stretch

out vec4 frag_color;

void main() {
    vec3 Nn = normalize(N);
    vec3 Vn = normalize(V);
    float cos_theta = abs(dot(Nn, Vn));   // abs: show both sides of cloth

    // Iridescent color from thin-film model
    vec3 iridescence = thin_film_color(thickness_nm, n_film, cos_theta);

    // Base material: near-transparent, slight blue-green tint
    vec3 base = vec3(0.85, 0.95, 0.90) * 0.15;

    // Fresnel-weighted blend: more iridescence at grazing angles
    float fresnel = pow(1.0 - cos_theta, 2.0);
    float mix_factor = clamp(0.4 + 0.4 * stretch_mix + 0.2 * fresnel, 0.0, 1.0);

    vec3 final_color = mix(base, iridescence, mix_factor);

    // Alpha: mostly transparent; slightly more opaque where stretched
    float alpha = clamp(0.75 + 0.15 * stretch_mix, 0.75, 0.92);

    frag_color = vec4(final_color, alpha);
}
```

---

## Fresnel Reflectance Reference Values

| Film | n | R0 (normal incidence) |
|---|---|---|
| Dry saran wrap | 1.5 | 0.040 (4%) |
| Wet saran wrap | 1.33 | 0.020 (2%) |
| Water | 1.33 | 0.020 (2%) |
| Glass | 1.52 | 0.043 (4.3%) |

R0 is low — saran wrap is mostly transparent. The iridescence color is a
tint, not a strong metallic sheen. Crank `mix_factor` toward 1.0 when the
wrap is heavily stretched to make the effect visible.

---

## Thickness Pipeline Verification (T Debug Mode)

Before connecting to the iridescence shader, verify `thickness_nm` values
are in range with the heatmap mode:

```glsl
// In cloth.frag, when DEBUG_THICKNESS is defined:
vec3 thickness_heat(float t) {
    // Blue = thin (150nm), Red = thick (800nm+)
    float u = clamp((t - 150.0) / (800.0 - 150.0), 0.0, 1.0);
    return mix(vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), u);
}
```

Expected values at rest: 12000 nm (rest thickness). Visible iridescence range
triggers once the wrap has been stretched to 15–80× area (area_deformed /
rest_area = 15–80 → thickness_nm ≈ 150–800 nm). This is a large deformation —
the ImGui stiffness slider may need to allow values as low as 5.0 to see
visible stretching.

---

## Gotchas

### 1. `flat in float` for face thickness

Face thickness is a per-triangle value (not per-vertex). Use `flat` interpolation
to prevent the driver from blending it across the triangle face:

```glsl
// cloth.vert
flat out float thickness_nm;

// cloth.frag
flat in float thickness_nm;
```

### 2. Half-wave phase shift

At the air→film top interface, there IS a half-wave (pi) phase shift because
n_film > n_air. At the film→air bottom interface, there is NOT (n_air < n_film).
Net: one half-wave shift total, which flips the cosine:
`cos(delta + pi) = -cos(delta)`. Make sure your formula accounts for this — it
determines whether minimum thickness produces constructive or destructive
interference (the two formulas give visually opposite color maps at the same
thickness).

### 3. Clamping `cos_theta_t`

`1 - sin²(theta_t)` can go slightly negative due to float precision at grazing
angles. Always `max(0.0, ...)` before `sqrt`.

### 4. Gamma correction

The CIE XYZ → sRGB transform produces linear light values. If your framebuffer
is sRGB (which it should be for correct display), either write linear values and
let the hardware apply gamma, or manually apply `pow(color, 1.0/2.2)`. Do not
double-gamma-correct — check your `glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE)`
and `glEnable(GL_FRAMEBUFFER_SRGB)` settings.

### 5. Performance of 20-sample loop

20 `exp()` calls per fragment per sample × 20 samples = 60 `exp()` calls/fragment.
At 64×64 mesh this is acceptable. At higher resolutions, consider reducing to
10 samples (visually similar) or precomputing a 1D texture `thickness_nm → RGB`
and sampling it in the fragment shader.

---

## References

- Wyman, Sloan, Shirley (2013). "Simple Analytic Approximations to the CIE XYZ
  Color Matching Functions." *JCGT* 2(2).
- Zucconi, Alan (2017). "Improving the Rainbow." https://www.alanzucconi.com/2017/07/15/improving-the-rainbow-2/
- Born & Wolf, *Principles of Optics*, Ch. 7 (thin-film interference, Fabry-Perot).
- Hecht, *Optics* 5th ed., §9.4 (phase shifts at interfaces).
