#version 430

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(location = 0) uniform int g_waveOutPos;
layout(location = 2) uniform float g_time;
layout(location = 3) uniform vec2 g_resolution;
layout(location = 9) uniform int g_pipelinePassIndex;

layout(binding = 4) uniform sampler2D u_sceneTexture;
layout(binding = 5) uniform sampler2D u_historyTexture;

layout(binding = 4, rgba16f) writeonly uniform image2D u_feedbackImage;

vec3 swirlSample(vec2 uv, float time){
	vec2 offset = (uv - 0.5) * 0.02;
	offset += 0.01 * vec2(
		sin(time + uv.y * 12.0),
		cos(time * 0.8 + uv.x * 10.0)
	);
	return texture(u_sceneTexture, uv + offset).rgb;
}

void main(){
	ivec2 imageSize = imageSize(u_feedbackImage);
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (pixel.x >= imageSize.x || pixel.y >= imageSize.y) {
		return;
	}

	vec2 uv = (vec2(pixel) + 0.5) / vec2(imageSize);
	float time = g_time;

	vec3 scene = texture(u_sceneTexture, uv).rgb;
	vec3 swirl = swirlSample(uv, time);
	vec3 history = texture(u_historyTexture, uv).rgb;

	float ripple = sin((uv.x + uv.y) * 12.0 + time * 1.5) * 0.5 + 0.5;
	vec3 glow = vec3(ripple) * vec3(0.4, 0.6, 1.0);

	vec3 reactive = mix(scene, swirl, 0.5);
	float historyMix = mix(0.88, 0.82, clamp(float(g_pipelinePassIndex), 0.0, 1.0));
	historyMix = 0.98;
	vec3 accumulated = mix(reactive + glow * 0.2, history, historyMix);

	imageStore(u_feedbackImage, pixel, vec4(accumulated, 1.0));
}
