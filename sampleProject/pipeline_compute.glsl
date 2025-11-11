#version 430

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(location = 2) uniform float g_time;
layout(location = 3) uniform vec2 g_resolution;
layout(location = 9) uniform int g_pipelinePassIndex;

layout(binding = 0, rgba16f) uniform image2D u_result;
layout(binding = 4) uniform sampler2D u_prevResult;

const uint TILE_SIZE = 16u;
const uint TILE_CAPACITY = TILE_SIZE * TILE_SIZE;

layout(std430, binding = 0) buffer TileParticleCounts {
    uint tileCounts[];
};

layout(std430, binding = 1) buffer TileParticleOffsets {
    uint tileOffsets[];
};

layout(std430, binding = 2) buffer TileParticleWriteHeads {
    uint tileWriteHeads[];
};

layout(std430, binding = 3) buffer TileParticleStats {
    // [0] totalCount, [1] overflowTiles, [2] totalTiles, [3] reserved
    uint tileStats[];
};

const float PI = 3.14159265358979323846;

float computeWave(vec2 uv) {
	return sin(g_time * 5.0 + uv.x * 12.0) * cos(g_time * 2.0 + uv.y * 9.0);
}

vec3 highlightColor(float hash) {
	return vec3(hash, 1.0 - hash, 0.5 + 0.5 * sin(g_time));
}

float vignetteMask(vec2 uv) {
	float distanceFromCenter = length(uv - 0.5);
	float ring = smoothstep(0.0, 0.35, distanceFromCenter) - smoothstep(0.35, 0.5, distanceFromCenter);
	float pulse = 0.2 * sin(2.0 * PI * distanceFromCenter);
	return clamp(ring + pulse, 0.0, 1.0);
}

float randomHash(vec3 v) {
	return fract(sin(dot(v, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
}

void clearParticleBuffers(uvec2 framebufferSize){
	uvec2 tileGrid = (framebufferSize + TILE_SIZE - 1u) / TILE_SIZE;
	uint totalTiles = tileGrid.x * tileGrid.y;
	for (uint i = 0u; i < totalTiles; ++i) {
		tileCounts[i] = 0u;
		tileOffsets[i] = i * TILE_CAPACITY;
		tileWriteHeads[i] = 0u;
	}
	tileStats[0] = 0u;
	tileStats[1] = 0u;
	tileStats[2] = totalTiles;
}

void main() {
	uvec2 globalID = uvec2(gl_GlobalInvocationID.xy);
	if (g_pipelinePassIndex == 0) {
		if (globalID == uvec2(0)) {
			uvec2 framebufferSize = uvec2(max(g_resolution, vec2(1.0)));
			clearParticleBuffers(framebufferSize);
		}
		return;
	}

	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 texSize = imageSize(u_result);
	if (pixel.x >= texSize.x || pixel.y >= texSize.y) {
		return;
	}

	vec2 uv = (vec2(pixel) + 0.5) / vec2(texSize);
	vec4 historyColor = texelFetch(u_prevResult, pixel, 0);

	uvec2 tileGrid = (uvec2(texSize) + TILE_SIZE - 1u) / TILE_SIZE;
	uint tileIndex = (uint(pixel.y) / TILE_SIZE) * tileGrid.x + (uint(pixel.x) / TILE_SIZE);

	uint writePos = atomicAdd(tileWriteHeads[tileIndex], 1u);
	uint countBefore = atomicAdd(tileCounts[tileIndex], 1u);
	atomicAdd(tileStats[0], 1u);
	if (writePos >= TILE_CAPACITY) {
		atomicAdd(tileStats[1], 1u);
	}

	float wave = computeWave(uv);
	float hash = randomHash(vec3(uv, g_time));
	float mask = max(step(0.65, 0.5 + 0.5 * wave), vignetteMask(uv));

	float tileHighlight = float(countBefore % TILE_CAPACITY) / float(TILE_CAPACITY);
	vec4 result = mix(historyColor, vec4(highlightColor(hash), 1.0), mask);
	result.rgb = mix(result.rgb, vec3(tileHighlight, 1.0 - tileHighlight, wave * 0.5 + 0.5), 0.35);
	imageStore(u_result, pixel, result);
}
