// Kitchen scraps against the glass (MultiMesh of quads). INSTANCE_CUSTOM: x type (0 banana peel, 1 apple core,
// 2 lettuce leaf, 3 carrot peelings, 4 coffee grounds, 5 eggshell), y decay (0 fresh … 1 rotten), z the share
// still left, w a per-scrap random value. The shape is drawn here, eaten away from the edges as worms feed,
// browning, going soft and glossy, with white mould in the later stages.
shader_type spatial;
render_mode cull_disabled, specular_schlick_ggx, depth_draw_opaque;

varying vec4 v_data;

float hash12(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}
float vnoise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) {
	float s = 0.0, a = 0.5;
	for (int i = 0; i < 4; i++) {
		s += a * vnoise(p);
		p = p * 2.07 + 5.3;
		a *= 0.5;
	}
	return s;
}

void vertex() {
	v_data = INSTANCE_CUSTOM;
}

// Signed shape (negative inside) in the quad's -1..1 space, and the base colour of the fresh scrap.
float shape(int type, vec2 p, float rnd, out vec3 col, out float h) {
	float n = (fbm(p * 3.0 + rnd * 20.0) - 0.5) * 0.16;
	h = 0.5;
	if (type == 0) { // banana peel: a curved, tapering strip, pale inside edge
		float bend = 0.55;
		vec2 q = vec2(p.x, p.y + bend * (p.x * p.x) - 0.25);
		float w = 0.28 * (1.0 - 0.6 * abs(p.x));
		float d = max(abs(q.y) - w, abs(p.x) - 0.92) + n;
		col = mix(vec3(0.78, 0.62, 0.12), vec3(0.85, 0.72, 0.2), vnoise(p * 4.0));
		col = mix(col, vec3(0.93, 0.88, 0.7), smoothstep(-0.05, 0.0, q.y - w * 0.4) * 0.6);
		col = mix(col, vec3(0.3, 0.2, 0.08), step(0.82, vnoise(p * 9.0 + rnd * 31.0)) * 0.8);
		h = 0.4 + 0.3 * (1.0 - abs(q.y) / max(w, 0.01));
		return d;
	}
	if (type == 1) { // apple core: rounded ends, bitten-in sides, flesh browning, red skin at the ends, dark pips
		vec2 q = p * vec2(1.0, 0.95);
		float waist = 0.22 + 0.34 * smoothstep(0.1, 0.8, abs(q.y)) + 0.04 * sin(q.y * 14.0 + rnd * 9.0);
		float ends = length(vec2(q.x * 0.9, abs(q.y) - 0.55)) - 0.42;
		float d = max(max(abs(q.x) - waist, abs(q.y) - 0.9), min(ends, abs(q.y) - 0.55)) + n;
		d = max(abs(q.x) - waist, max(abs(q.y) - 0.92, 0.0)) + n;
		d = max(d, length(q * vec2(0.8, 0.95)) - 0.95);
		col = mix(vec3(0.82, 0.72, 0.5), vec3(0.88, 0.8, 0.58), vnoise(p * 5.0));
		col = mix(col, vec3(0.62, 0.42, 0.22), smoothstep(-0.12, 0.0, abs(q.x) - waist) * 0.7); // bitten faces brown first
		float skin = smoothstep(0.62, 0.8, abs(q.y));
		col = mix(col, mix(vec3(0.55, 0.07, 0.06), vec3(0.7, 0.2, 0.08), vnoise(p * 8.0)), skin * 0.9);
		float pip = step(length(vec2(abs(q.x) - 0.07, q.y * 0.55 - 0.02)), 0.06);
		col = mix(col, vec3(0.18, 0.09, 0.04), pip);
		h = 0.75 - 0.35 * abs(q.x) / max(waist, 0.01);
		return d;
	}
	if (type == 2) { // lettuce leaf: broad wavy-edged leaf with a pale midrib and veins
		float rr = 0.8 + 0.12 * sin(atan(p.y, p.x) * 9.0 + rnd * 6.0);
		float d = length(p * vec2(0.9, 1.25)) - rr + n;
		col = mix(vec3(0.3, 0.52, 0.16), vec3(0.5, 0.7, 0.26), vnoise(p * 3.0));
		float rib = smoothstep(0.06, 0.0, abs(p.y + 0.1 * sin(p.x * 2.0)));
		float veins = smoothstep(0.05, 0.0, abs(fract((p.x + abs(p.y) * 0.9) * 3.0) - 0.5) * 0.3);
		col = mix(col, vec3(0.75, 0.85, 0.55), max(rib * 0.8, veins * 0.3));
		h = 0.35;
		return d;
	}
	if (type == 3) { // carrot peelings: long, thin ribbons lying in gentle curls
		float d = 1e3;
		float along = 0.0;
		for (int k = 0; k < 3; k++) {
			float fk = float(k);
			float ang = (hash12(vec2(fk, rnd * 17.0)) - 0.5) * 1.2;
			vec2 q = p - vec2(0.0, -0.45 + fk * 0.42);
			q = mat2(vec2(cos(ang), sin(ang)), vec2(-sin(ang), cos(ang))) * q;
			q.y += (0.25 + 0.2 * hash12(vec2(fk, 3.0))) * (q.x * q.x - 0.3) * (hash12(vec2(fk, 9.0)) < 0.5 ? 1.0 : -1.0);
			float dk = max(abs(q.y) - 0.07 - 0.03 * (1.0 - abs(q.x)), abs(q.x) - 0.9);
			if (dk < d) {
				d = dk;
				along = q.x;
			}
		}
		d += n * 0.25;
		col = mix(vec3(0.78, 0.3, 0.05), vec3(0.9, 0.45, 0.1), vnoise(vec2(along * 3.0, rnd * 9.0)));
		col *= 0.9 + 0.1 * vnoise(vec2(along * 40.0, rnd)); // fine rings across the peel
		h = 0.5;
		return d;
	}
	if (type == 4) { // coffee grounds: a loose, irregular heap of dark granules
		float d = length(p * vec2(0.85, 1.3)) - 0.8 + (fbm(p * 2.0 + rnd * 11.0) - 0.5) * 0.5;
		float g = vnoise(p * 28.0 + rnd * 5.0);
		col = mix(vec3(0.07, 0.045, 0.03), vec3(0.2, 0.13, 0.08), g);
		h = 0.3 + 0.4 * g * (1.0 - length(p));
		return d;
	}
	// eggshell: a few curved, jagged shards, brown or white outside, a thin pale edge
	float d = 1e3;
	float rim = 0.0;
	bool brown = hash12(vec2(rnd, 1.0)) < 0.5;
	for (int k = 0; k < 4; k++) {
		float fk = float(k);
		vec2 c = vec2(hash12(vec2(fk, rnd * 13.0)), hash12(vec2(fk + 7.0, rnd * 5.0))) * 1.0 - 0.5;
		vec2 q = p - c;
		float dk = length(q) - 0.3;
		for (int e = 0; e < 5; e++) { // cut by random edges: a jagged polygon
			float a = hash12(vec2(fk * 5.0 + float(e), rnd * 3.0)) * 6.283;
			vec2 nrm = vec2(cos(a), sin(a));
			dk = max(dk, dot(q, nrm) - (0.12 + 0.14 * hash12(vec2(float(e), fk + rnd))));
		}
		if (dk < d) d = dk;
	}
	d += n * 0.15;
	col = brown ? vec3(0.66, 0.46, 0.3) : vec3(0.84, 0.81, 0.74);
	col *= 0.9 + 0.1 * vnoise(p * 12.0);
	col = mix(col, vec3(0.93, 0.91, 0.86), smoothstep(-0.035, -0.01, d) * 0.8); // the broken edge shows the shell's thickness
	h = 0.5 + 0.3 * smoothstep(0.0, -0.1, d);
	return d;
}

void fragment() {
	int type = int(v_data.x + 0.5);
	float decay = clamp(v_data.y, 0.0, 1.0);
	float left = clamp(v_data.z, 0.0, 1.0);
	float rnd = v_data.w;
	vec2 p = UV * 2.0 - 1.0;
	vec3 col;
	float h;
	float d = shape(type, p, rnd, col, h);
	// Eaten away from the edges (and in holes, where worms have fed into it) as the scrap shrinks.
	float bite = (1.0 - left) * 0.9;
	float holes = fbm(p * 4.5 + rnd * 17.0);
	d += bite * 0.55 + smoothstep(0.75 - bite * 0.6, 0.9, holes) * 0.6 * step(0.05, bite);
	if (d > 0.0) discard;

	// Decay: browning, then dark and soft; white mould blooms mid-way and fades.
	vec3 rotten = vec3(0.13, 0.085, 0.045);
	if (type == 2) rotten = vec3(0.2, 0.2, 0.08);
	if (type == 5) rotten = col * 0.85;
	col = mix(col, col * vec3(0.72, 0.55, 0.38), smoothstep(0.05, 0.4, decay));
	col = mix(col, rotten, smoothstep(0.3, 0.95, decay) * (type == 5 ? 0.3 : 1.0));
	float mould = smoothstep(0.35, 0.55, decay) * (1.0 - smoothstep(0.85, 1.0, decay)) * step(0.62, fbm(p * 6.0 + rnd * 3.0));
	col = mix(col, vec3(0.86, 0.87, 0.82), mould * 0.75 * (type == 5 || type == 4 ? 0.2 : 1.0));
	// edges darken where the scrap meets the bedding
	col *= 0.75 + 0.25 * smoothstep(0.0, -0.12, d);

	float hh = h * smoothstep(0.0, -0.15, d);
	vec2 dh = vec2(dFdx(hh), dFdy(hh));
	NORMAL_MAP = normalize(vec3(-dh.x * 3.0, dh.y * 3.0, 1.0)) * 0.5 + 0.5;
	ALBEDO = col;
	ROUGHNESS = mix(0.55, 0.22, smoothstep(0.3, 0.9, decay)) * (type == 4 ? 1.4 : 1.0);
	SPECULAR = 0.5;
	CLEARCOAT = smoothstep(0.4, 0.9, decay) * 0.5;
}
