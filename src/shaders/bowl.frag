#version 410 core

// bowl.frag — Phong shading for the bowl hemisphere.
//
// Ambient + diffuse only (no specular needed at milestone).
// Grey-blue colour, distinct from the white cloth.

in vec3 vWorldPos;
in vec3 vNormal;

uniform vec3 uLightPos;   // world-space light position

out vec4 fragColor;

void main()
{
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightPos - vWorldPos);

    // Soft grey-blue bowl colour
    vec3 baseColor = vec3(0.50, 0.52, 0.60);
    float ambient  = 0.30;
    float diffuse  = max(dot(N, L), 0.0) * 0.70;

    fragColor = vec4(baseColor * (ambient + diffuse), 1.0);
}
