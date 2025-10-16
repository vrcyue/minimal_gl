#version 430

layout(location = 0) uniform int g_waveOutPos;
layout(location = 2) uniform float g_time;
layout(location = 3) uniform vec2 g_resolution;
layout(location = 9) uniform int g_pipelinePassIndex;

layout(binding = 0) uniform sampler2D u_input0;
layout(binding = 1) uniform sampler2D u_input1;

layout(location = 0) out vec4 outColor0;

const float PI = 3.14159265359;

vec3 palette(float t){
	vec3 a = vec3(0.52, 0.36, 0.68);
	vec3 b = vec3(0.23, 0.29, 0.33);
	vec3 c = vec3(0.08, 0.23, 0.45);
	vec3 d = vec3(0.92, 0.57, 0.27);
	return a + b * cos(2.0 * PI * (c * t + d));
}

vec3 renderScene(vec2 uv, float time){
	vec2 centered = (uv - 0.5) * 2.0;
	float radial = length(centered);
	float angle = atan(centered.y, centered.x);

	float rings = smoothstep(1.2, 0.0, abs(sin(radial * 10.0 - time * 1.2)));
	float spokes = smoothstep(1.0, 0.0, abs(sin(angle * 6.0 + time * 0.8)));
	float envelope = smoothstep(0.95, 0.2, 1.0 - radial);

	vec3 base = palette(uv.x + uv.y + time * 0.05);
	vec3 highlight = palette(radial + time * 0.3);
	vec3 color = mix(base, highlight, rings * 0.6 + spokes * 0.4);

	color += 0.15 * vec3(envelope);
	color *= mix(0.7, 1.0, envelope);

	return color;
}

void main(){
	vec2 uv = gl_FragCoord.xy / g_resolution;
	float time = g_time;
	vec3 color = vec3(0.0);

	if (g_pipelinePassIndex == 0) {
		/* Scene/gbuffer pass: generate the base pattern */
		color = renderScene(uv, time*10.0);
		outColor0 = vec4(color, 1.0);
		return;
	}

	if (g_pipelinePassIndex == 2) {
		/* Composite pass: blend scene with feedback texture */
		vec3 feedback = texture(u_input0, uv).rgb;
		vec3 scene = texture(u_input1, uv).rgb;

		float vignette = smoothstep(1.0, 0.2, length((uv - 0.5) * 1.6));
		float pulse = 0.5 + 0.5 * sin(time * 0.7);

		vec3 mixed = mix(scene, feedback, 0.65);
		mixed += 0.15 * vignette * vec3(0.9, 0.6, 1.0);
		mixed += 0.08 * pulse * vec3(0.5, 0.8, 1.2);

		outColor0 = vec4(mixed, 1.0);
		return;
	}

	/* Fallback for unexpected pass indices */
	outColor0 = vec4(renderScene(uv, time), 1.0);
}
