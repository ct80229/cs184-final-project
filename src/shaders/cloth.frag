#version 410 core

// cloth.frag — Cloth mesh fragment shader (OpenGL 4.1 core profile)
//
// TODO (Sprint 3): Implement Wyman et al. (2013) CIE thin-film interference model
//   (preferred over Zucconi — better violet/red rolloff):
//   - wavelength_to_rgb(lambda): xFit/yFit/zFit Gaussians + XYZ→sRGB
//   - thin_film_color(thickness_nm, n, cos_theta_i): 20-sample spectrum integration (380–780 nm)
//   - OPD = 2.0 * n * d * cos(theta_t)  (Snell's law for theta_t)
//   - View-dependent: cos_theta_i = abs(dot(N, V))
//   - Base: near-transparent (alpha 0.75–0.92), slight blue-green tint
//   - Final: mix(base, iridescence, 0.4 + 0.4*stretch_mix + 0.2*fresnel)
//   - n = 1.5 dry / 1.33 wet (controlled by ImGui slider)

out vec4 fragColor;

void main()
{
    // Stub: output solid white until iridescence shader is implemented.
    fragColor = vec4(1.0);
}
