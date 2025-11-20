#version 430

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(location = 2) uniform float g_time;
layout(location = 3) uniform vec2 g_resolution;
layout(location = 9) uniform int g_pipelinePassIndex;

layout(binding = 0, rgba16f) uniform image2D u_result;
layout(binding = 4) uniform sampler2D u_prevResult;

const float PI = 3.14159265358979323846;

float hash(vec2 p){
	return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

vec4 precompute(vec2 uv){
	float stars = step(0.995, hash(uv * 128.0)) * 1.0;
	float radial = pow(1.0 - clamp(length(uv - 0.5) * 1.8, 0.0, 1.0), 2.0);
	return vec4(vec3(stars + radial), 1.0);
}

vec4 animate(vec2 uv, vec4 history){
	float ripple = sin(uv.x * 12.0 + g_time * 1.7) * cos(uv.y * 9.0 + g_time * 1.3);
	vec3 tint = vec3(0.4 + 0.6 * sin(g_time + uv.xyx * 5.0));
	vec3 mixed = mix(history.rgb, tint, 0.25 + 0.25 * ripple);
	return vec4(mixed, 1.0);
}

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 texSize = imageSize(u_result);
	if (pixel.x >= texSize.x || pixel.y >= texSize.y) {
		return;
	}
	vec2 uv = (vec2(pixel) + 0.5) / vec2(texSize);

	if (g_pipelinePassIndex == 0) {
		/* 起動時の 1 回だけ実行される初期化パス */
		imageStore(u_result, pixel, precompute(uv));
		return;
	}

	vec4 historyColor = texelFetch(u_prevResult, pixel, 0);
	imageStore(u_result, pixel, animate(uv, historyColor));
}
