#version 430

layout(local_size_x = 8, local_size_y = 8) in;

// Previous frame results (binding = 0-3)
layout(binding = 0, rgba8) uniform readonly image2D prevRT0;
layout(binding = 1, rgba8) uniform readonly image2D prevRT1;
layout(binding = 2, rgba8) uniform readonly image2D prevRT2;
layout(binding = 3, rgba8) uniform readonly image2D prevRT3;

// Targets for this frame (binding = 4-7)
layout(binding = 4, rgba8) uniform writeonly image2D currRT0;
layout(binding = 5, rgba8) uniform writeonly image2D currRT1;
layout(binding = 6, rgba8) uniform writeonly image2D currRT2;
layout(binding = 7, rgba8) uniform writeonly image2D currRT3;

layout(location = 2) uniform float time;
layout(location = 3) uniform vec2 resolution;

uint hash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= int(resolution.x) || pixel.y >= int(resolution.y)) {
        return;
    }

    vec2 uv = (vec2(pixel) + 0.5) / resolution;

    // RT0: simple accumulation with decay
    vec4 prev0 = imageLoad(prevRT0, pixel);
    vec4 color0 = mix(prev0, vec4(uv, 0.5 + 0.5 * sin(time), 1.0), 0.05);
    imageStore(currRT0, pixel, color0);

    // RT1: random particle trail
    uint seed = hash(uint(pixel.x) + uint(pixel.y) * 4096u + uint(time * 60.0));
    ivec2 particlePos = ivec2(int(hash(seed) % uint(resolution.x)), int(hash(seed * 3u) % uint(resolution.y)));
    float blink = step(0.97, fract(sin(float(seed))));
    if (blink > 0.0) {
        vec3 particleColor = vec3(0.6 + 0.4 * sin(time + vec3(0.0, 2.0, 4.0)));
        imageStore(currRT1, particlePos, vec4(particleColor, 1.0));
    } else {
        imageStore(currRT1, pixel, imageLoad(prevRT1, pixel) * 0.96);
    }

    // RT2: ripple pattern evolving over time
    vec2 centered = uv - 0.5;
    float ripple = sin(length(centered) * 32.0 - time * 4.0) * 0.5 + 0.5;
    vec4 prev2 = imageLoad(prevRT2, pixel);
    imageStore(currRT2, pixel, mix(prev2, vec4(vec3(ripple), 1.0), 0.1));

    // RT3: store auxiliary data for debugging/visualisation
    vec4 data = vec4(uv, fract(time), 1.0);
    imageStore(currRT3, pixel, data);
}
