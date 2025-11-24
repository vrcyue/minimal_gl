#version 430

/*
	YouTube Shorts 向けの 9:16 サンプル。
	レンダーターゲットを固定解像度 720x1280 (9:16) にし、
	present 時は中央にピラーボックス表示される。
*/

layout(location = 2) uniform float time;
layout(location = 3) uniform vec2 resolution;

out vec4 outColor;

void main(){
	vec2 uv = gl_FragCoord.xy / resolution;

	/* 円が縦長で歪まないようにアスペクト補正した座標 */
	vec2 aspect = vec2(resolution.x / resolution.y, 1.0);
	vec2 p = (uv - 0.5) * aspect;

	float dist = length(p);
	float ring = smoothstep(0.35, 0.34, dist);
	float gradient = uv.y;

	vec3 base = mix(vec3(0.08, 0.12, 0.2), vec3(0.95, 0.55, 0.22), gradient);
	base += 0.05 * sin(time + vec3(uv.x * 10.0, uv.y * 7.0, (uv.x + uv.y) * 6.0));
	vec3 color = mix(base, vec3(1.0), ring * 0.35);

	outColor = vec4(color, 1.0);
}
