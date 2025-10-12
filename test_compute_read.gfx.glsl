#version 430

/*
	ComputeRT の内容を可視化するフラグメントシェーダ。
	graphics_compute_shader.glsl で書き込まれたテクスチャを
	binding = 4 ～ 7 から読み取って表示する。
*/

layout(binding = 4) uniform sampler2D computeRT0;
layout(binding = 5) uniform sampler2D computeRT1;
layout(binding = 6) uniform sampler2D computeRT2;
layout(binding = 7) uniform sampler2D computeRT3;
layout(location = 0) uniform int waveOutPosition;

#if defined(EXPORT_EXECUTABLE)
	vec2 resolution = vec2(SCREEN_XRESO, SCREEN_YRESO);
	#define NUM_SAMPLES_PER_SEC 48000.0
	float time = waveOutPosition / NUM_SAMPLES_PER_SEC;
#else
	layout(location = 2) uniform float time;
	layout(location = 3) uniform vec2 resolution;
#endif

layout(location = 0) out vec4 outColor0;
layout(location = 1) out vec4 outColor1;
layout(location = 2) out vec4 outColor2;
layout(location = 3) out vec4 outColor3;

vec3 visualize(vec4 sampleColor, vec2 uv){
	/* 簡易可視化：RGB は元色、アルファをグリッドで強調 */
	float grid = step(0.995, fract(uv.x * 20.0)) + step(0.995, fract(uv.y * 20.0));
	return mix(sampleColor.rgb, vec3(1.0, 0.2, 0.1), grid > 0.0 ? 0.3 : 0.0);
}

void main(){
	vec2 uv = gl_FragCoord.xy / resolution;

	vec4 rt0 = texture(computeRT0, uv);
	vec4 rt1 = texture(computeRT1, uv);
	vec4 rt2 = texture(computeRT2, uv);
	vec4 rt3 = texture(computeRT3, uv);

	/* デフォルトは RT0 を表示 */
	vec3 displayColor = visualize(rt0, uv);
	if (uv.x > 0.5) displayColor = visualize(rt1, uv);
	if (uv.y > 0.5) displayColor = visualize(rt2, uv);
	if (uv.x > 0.5 && uv.y > 0.5) displayColor = visualize(rt3, uv);

	float alpha = clamp(rt0.a + rt1.a + rt2.a + rt3.a, 0.0, 1.0);
	vec4 outputColor = vec4(displayColor, alpha);

	outColor0 = outputColor;
	outColor1 = outputColor;
	outColor2 = outputColor;
	outColor3 = outputColor;
}
