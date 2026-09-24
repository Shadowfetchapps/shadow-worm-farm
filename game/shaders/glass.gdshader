// The front pane. Almost invisible: a faint sheet reflection of the room, light dust, and condensation — fine
// mist and a few larger drops, mostly in the humid headspace under the lid, heavier after the bin is misted,
// with the occasional drop that has run down leaving a clear trail.
shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_back, unshaded;

uniform float reflection_strength = 0.045;
uniform float dust_strength = 0.03;
uniform float humidity = 0.3;
uniform float surface_v = 0.2; ///< where the bedding surface is, in pane UV (0 top)
uniform float seed = 0.0;

float hash(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031 + seed * 0.011);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}
vec2 hash2(vec2 p) {
	return vec2(hash(p), hash(p + 17.3));
}
float vnoise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}

// Drops on a jittered grid: returns (coverage, highlight) for drops of about `size` cells of the grid.
vec2 drops(vec2 p, float density, float size) {
	vec2 i = floor(p), f = fract(p);
	float cover = 0.0, hi = 0.0;
	for (int y = -1; y <= 1; y++)
		for (int x = -1; x <= 1; x++) {
			vec2 g = vec2(float(x), float(y));
			vec2 id = i + g;
			if (hash(id * 1.7) > density) continue;
			vec2 c = g + hash2(id) * 0.8 + 0.1 - f;
			float r = size * (0.5 + 0.7 * hash(id * 3.1));
			vec2 q = c / r;
			float d = length(q * vec2(1.0, 0.92));
			float drop = smoothstep(1.0, 0.8, d);
			cover = max(cover, drop);
			// light from the upper left: a bright crescent on the far side, a dim rim
			hi = max(hi, drop * (smoothstep(0.35, 0.95, dot(normalize(-q + 1e-4), vec2(0.5, -0.8)) * d) * 0.9 + smoothstep(0.7, 1.0, d) * 0.4));
		}
	return vec2(cover, hi);
}

void fragment() {
	vec2 uv = UV;
	float band = smoothstep(0.55, 0.0, abs((uv.x * 0.8 + uv.y * 0.55) - 0.32)) * smoothstep(1.2, 0.2, uv.x + uv.y);
	float sheen = band * reflection_strength;
	float edge = 1.0 - smoothstep(0.0, 0.08, min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y)));
	float dust = step(0.99993, hash(floor(uv * vec2(3840.0, 2160.0) * 0.5))) * dust_strength * 3.0;
	float smudge = smoothstep(0.62, 0.9, vnoise(uv * vec2(6.0, 3.5) + 11.0)) * vnoise(uv * vec2(90.0, 14.0)) * 0.018;

	// Condensation: dense above the bedding, thinning below it.
	float zone = mix(1.0, 0.25, smoothstep(surface_v - 0.03, surface_v + 0.2, uv.y)) * smoothstep(0.0, 0.02, uv.y);
	float h = clamp(humidity, 0.0, 1.0);
	float fog = zone * h * (0.6 + 0.4 * vnoise(uv * vec2(14.0, 8.0))) * 0.07;
	vec2 aspect = vec2(1.78, 1.0);
	vec2 small = vec2(0.0);
	vec2 big = drops(uv * aspect * 38.0 + 3.0, 0.3 * zone * smoothstep(0.3, 0.9, h), 0.32);
	// A few runs: narrow clear trails below larger drops.
	float col_id = floor(uv.x * 140.0);
	float run = step(0.965, hash(vec2(col_id, 5.0))) * step(uv.y, surface_v + 0.12 * hash(vec2(col_id, 9.0))) * smoothstep(0.35, 0.5, h);
	float trail = run * smoothstep(0.35, 0.0, abs(fract(uv.x * 140.0) - 0.5));
	fog *= 1.0 - trail;
	float dropCover = max(small.x, big.x);
	float dropHi = max(small.y * 0.5, big.y);

	float a = sheen + edge * 0.012 + dust + smudge + fog + dropCover * 0.05 * zone + dropHi * 0.16;
	vec3 c = mix(vec3(0.9, 0.93, 0.97), vec3(1.0), clamp(dropHi * 2.0 + dust / max(a, 1e-4), 0.0, 1.0));
	ALBEDO = c;
	ALPHA = clamp(a, 0.0, 0.35);
}
