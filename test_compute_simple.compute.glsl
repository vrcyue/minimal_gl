#version 430

/*
	ComputeRT の動作チェック用ミニマルサンプル。
	- 前フレームバッファ(prevRT0-3)と書き込み先(currRT0-3)の両方を使用
	- 単純なパターン生成と減衰で書き込みが行われることを確認しやすい
*/

layout(local_size_x = 8, local_size_y = 8) in;

/* 前フレームで書かれたデータ（読み取り専用） */
layout(binding = 0, rgba8) uniform readonly image2D prevRT0;
layout(binding = 1, rgba8) uniform readonly image2D prevRT1;
layout(binding = 2, rgba8) uniform readonly image2D prevRT2;
layout(binding = 3, rgba8) uniform readonly image2D prevRT3;

/* 今フレームの書き込み先 */
layout(binding = 4, rgba8) uniform writeonly image2D currRT0;
layout(binding = 5, rgba8) uniform writeonly image2D currRT1;
layout(binding = 6, rgba8) uniform writeonly image2D currRT2;
layout(binding = 7, rgba8) uniform writeonly image2D currRT3;

layout(location = 2) uniform float time;
layout(location = 3) uniform vec2 resolution;

void main(){
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (pixel.x >= int(resolution.x) || pixel.y >= int(resolution.y)) {
		return;
	}

	vec2 uv = (vec2(pixel) + 0.5) / resolution;

	/* RT0: time に合わせて色相が変わるグラデーション */
	float hue = fract(time * 0.1) + uv.x;
	vec3 color0 = vec3(
		0.5 + 0.5 * sin(6.28318 * (hue + vec3(0.0, 0.33, 0.66)))
	);
	imageStore(currRT0, pixel, vec4(color0, 1.0));

	/* RT1: 前フレーム値をフェードさせつつ、ランダム座標へ散布書き込み */
	vec4 prev1 = imageLoad(prevRT1, pixel) * 0.96;
	imageStore(currRT1, pixel, prev1);

	uint seed = uint(pixel.x) * 73856093u ^ uint(pixel.y) * 19349663u ^ uint(time * 60.0);
	ivec2 randomPixel = ivec2(
		int((seed & 0xFFFFu) % uint(resolution.x)),
		int(((seed >> 16) & 0xFFFFu) % uint(resolution.y))
	);
	vec4 targetPrev = imageLoad(prevRT1, randomPixel);
	vec4 stamped = mix(targetPrev, vec4(0.2, 0.8, 1.0, 1.0), 0.25);
	imageStore(currRT1, randomPixel, stamped);

	/* RT2: 縞模様で書き込み位置の走査を確認 */
	float stripes = step(0.5, fract((uv.x + uv.y + time * 0.25) * 10.0));
	imageStore(currRT2, pixel, vec4(vec3(stripes), 1.0));

	/* RT3: デバッグ用に UV と time を格納 */
	imageStore(currRT3, pixel, vec4(uv, fract(time), 1.0));
}
