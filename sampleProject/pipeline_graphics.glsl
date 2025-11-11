#version 430

layout(location = 2) uniform float g_time;
layout(location = 3) uniform vec2 g_resolution;
layout(location = 9) uniform int g_pipelinePassIndex;

layout(binding = 0) uniform sampler2D u_input0;

layout(location = 0) out vec4 outColor0;

vec3 drawGradientPattern(vec2 uv){
	float stripe = step(0.5, fract(uv.y * 10.0 + g_time * 0.5));
	vec3 base = vec3(uv, 0.5 + 0.5 * sin(g_time));
	return mix(base, base.bgr, stripe);
}

void runDrawPass(){
	vec2 uv = gl_FragCoord.xy / g_resolution;
	outColor0 = vec4(drawGradientPattern(uv), 1.0);
}

void runBlitPass(){
	vec2 uv = gl_FragCoord.xy / g_resolution;
	outColor0 = texture(u_input0, uv);
}

void main(){
	runBlitPass();
}
