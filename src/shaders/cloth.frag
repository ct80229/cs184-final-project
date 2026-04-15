#version 410 core

// cloth.frag — Cloth mesh fragment shader (OpenGL 4.1 core profile)
//
// TODO (Sprint 3): Implement Zucconi (2017) thin-film interference model:
//   - wavelength_to_rgb(lambda): Gaussian wavelength → RGB
//   - thin_film_color(thickness_nm, n, cos_theta): 20-sample spectrum integration
//   - OPD = 2.0 * n * d * cos(theta_t)
//   - View-dependent phase shift via dot(N, V)
//   - Base: near-transparent (alpha 0.85), slight blue-green tint
//   - Final: mix(base, iridescence, 0.6 + 0.4 * stretch)
//   - n = 1.5 dry / 1.33 wet (controlled by ImGui slider)

out vec4 fragColor;

void main()
{
    // Stub: output solid white until iridescence shader is implemented.
    fragColor = vec4(1.0);
}
