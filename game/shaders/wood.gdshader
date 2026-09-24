// The bin's frame: oiled oak, grain running along each rail.
shader_type spatial;
render_mode cull_back, specular_schlick_ggx;

uniform bool vertical = false;
uniform float seed = 0.0;

float hash12(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031 + seed * 0.013);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}
float vnoise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}

varying vec3 v_world;

void vertex() {
	v_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec2 p = vertical ? v_world.yx : v_world.xy;
	p += v_world.z * vec2(0.0, 1.0);
	float warp = vnoise(vec2(p.x * 0.8, p.y * 6.0)) * 1.6;
	float rings = fract((p.y * 38.0 + warp * 2.5 + vnoise(p * vec2(0.5, 20.0)) * 0.6));
	float grain = smoothstep(0.0, 0.25, rings) * smoothstep(1.0, 0.55, rings);
	float pores = step(0.72, vnoise(vec2(p.x * 90.0, p.y * 900.0))) * 0.12;
	vec3 light = vec3(0.47, 0.32, 0.18), dark = vec3(0.3, 0.19, 0.1);
	vec3 col = mix(dark, light, grain * 0.7 + 0.3 * vnoise(p * vec2(2.0, 30.0)));
	col *= 1.0 - pores;
	ALBEDO = col * 0.9;
	ROUGHNESS = 0.55 - 0.1 * grain;
	SPECULAR = 0.35;
}
