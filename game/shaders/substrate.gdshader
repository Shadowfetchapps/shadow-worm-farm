// The worm bin's contents pressed against the front glass. The simulation grid arrives as a small RGBA8 texture
// (material, castings, moisture, density); everything finer than a cell — torn cardboard and newspaper strips,
// coir fibres, crumbly compost, casting pellets, wet sheen, the burrows worms leave along the glass — is generated
// here. One cell is about 2 mm.
shader_type spatial;
render_mode cull_back, depth_draw_opaque, specular_schlick_ggx;

uniform sampler2D terrain_nearest : filter_nearest, repeat_disable;
uniform sampler2D terrain_linear : filter_linear, repeat_disable;
uniform vec2 grid_size = vec2(320.0, 180.0);
uniform vec3 back_top : source_color = vec3(0.16, 0.15, 0.14);
uniform vec3 back_bottom : source_color = vec3(0.2, 0.18, 0.155);
uniform float seed = 0.0;

// ---- hashing / noise ------------------------------------------------------------------------
float hash12(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031 + seed * 0.013);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}
vec2 hash22(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973) + seed * 0.017);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.xx + p3.yz) * p3.zy);
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
		p = p * 2.03 + 17.1;
		a *= 0.5;
	}
	return s;
}
// Cellular pieces: distance to the nearest centre, gap to the second nearest, the piece's id hash and its offset.
vec4 cells(vec2 p, out vec2 rel) {
	vec2 i = floor(p), f = fract(p);
	float d1 = 8.0, d2 = 8.0;
	vec2 id = vec2(0.0);
	rel = vec2(0.0);
	for (int y = -1; y <= 1; y++)
		for (int x = -1; x <= 1; x++) {
			vec2 g = vec2(float(x), float(y));
			vec2 o = hash22(i + g) * 0.8 + 0.1;
			vec2 r = g + o - f;
			float d = length(r);
			if (d < d1) {
				d2 = d1;
				d1 = d;
				id = i + g;
				rel = r;
			} else if (d < d2) {
				d2 = d;
			}
		}
	return vec4(d1, d2 - d1, hash12(id * 1.37 + 3.1), hash12(id * 2.71 + 8.3));
}
mat2 rot(float a) {
	float c = cos(a), s = sin(a);
	return mat2(vec2(c, s), vec2(-s, c));
}

// Bicubic (B-spline) filtering from four bilinear taps: the grid never shows as steps or diamonds.
vec4 cubic_w(float v) {
	vec4 n = vec4(1.0, 2.0, 3.0, 4.0) - v;
	vec4 s = n * n * n;
	float x = s.x, y = s.y - 4.0 * s.x, z = s.z - 4.0 * s.y + 6.0 * s.x;
	return vec4(x, y, z, 6.0 - x - y - z) * (1.0 / 6.0);
}
vec4 bicubic(sampler2D tex, vec2 uv, vec2 size) {
	vec2 t = uv * size - 0.5;
	vec2 f = fract(t);
	t -= f;
	vec4 xc = cubic_w(f.x), yc = cubic_w(f.y);
	vec4 c = t.xxyy + vec2(-0.5, 1.5).xyxy;
	vec4 s = vec4(xc.xz + xc.yw, yc.xz + yc.yw);
	vec4 o = c + vec4(xc.yw, yc.yw) / s;
	o /= size.xxyy;
	float sx = s.x / (s.x + s.y), sy = s.z / (s.z + s.w);
	return mix(mix(texture(tex, o.yw), texture(tex, o.xw), sx), mix(texture(tex, o.yz), texture(tex, o.xz), sx), sy);
}

// Shredded bedding: torn strips of brown cardboard and grey newsprint lying at every angle, with coir fibres
// between them. Returns colour; `h` gets a small relief height.
vec3 bedding(vec2 p, float pw, out float h) {
	// Coir between the strips: dark and fibrous, the fibres lying in drifting directions.
	float fa = vnoise(p * 0.15) * 6.283;
	vec2 fp = rot(fa) * p;
	float fib = vnoise(fp * vec2(0.8, 7.0)) * 0.6 + vnoise(fp * vec2(1.6, 15.0) + 3.0) * 0.4;
	vec3 col = mix(vec3(0.1, 0.065, 0.04), vec3(0.24, 0.16, 0.09), fib);
	col *= 0.75 + 0.3 * vnoise(p * 0.35);
	h = 0.15;
	// Three layers of strips. Each Voronoi site carries one torn strip at its own angle; a pixel looks at the
	// strips of the neighbouring sites too, so strips run across cell borders at full length.
	for (int layer = 0; layer < 3; layer++) {
		float scale = layer == 0 ? 0.1 : (layer == 1 ? 0.13 : 0.17);
		vec2 q = p * scale + float(layer) * 13.7;
		vec2 qi = floor(q), qf = fract(q);
		float best = -1.0;
		vec3 bestCol = vec3(0.0);
		float bestH = 0.0;
		for (int y = -1; y <= 1; y++)
			for (int x = -1; x <= 1; x++) {
				vec2 g = vec2(float(x), float(y));
				vec2 id = qi + g;
				vec2 rel = g + hash22(id) * 0.6 + 0.2 - qf; // centres kept off the cell edges: every strip is found
				float hz = hash12(id * 1.37 + 3.1), hw = hash12(id * 2.71 + 8.3);
				if (hz < 0.3) continue; // gaps where the coir and compost show
				vec2 lr = rot(hz * 6.283) * rel;
				float len = 1.0 + 1.1 * hw, wid = 0.11 + 0.08 * fract(hw * 7.3);
				lr.y += 0.035 * sin(lr.x * 3.0 + hz * 20.0); // strips lie a little crumpled
				float edge = (vnoise(lr * vec2(14.0, 40.0) + hz * 40.0) - 0.5) * 0.07 + (vnoise(lr * vec2(40.0, 90.0)) - 0.5) * 0.03; // torn edges
				float aa = pw * scale * 1.2;
				float inside = step(abs(lr.x) + edge * 2.0, len * 0.5) * smoothstep(wid + aa, wid - aa, abs(lr.y) + edge);
				if (inside < 0.01 || hz < best) continue;
				best = hz;
				vec3 sc;
				if (fract(hz * 5.1) < 0.68) {
					// soaked cardboard: brown, blotchy where it has taken up water
					sc = mix(vec3(0.25, 0.165, 0.09), vec3(0.34, 0.235, 0.135), hw);
					sc *= 0.8 + 0.3 * vnoise(lr * vec2(6.0, 14.0) + hz * 13.0);
				} else {
					// damp newsprint: grey, softened, faint lines of print broken into words
					sc = mix(vec3(0.27, 0.255, 0.23), vec3(0.34, 0.32, 0.29), hw);
					float line = step(0.55, fract(lr.y * 34.0 + hz * 7.0)) * step(0.45, vnoise(vec2(lr.x * 50.0, floor(lr.y * 34.0) * 3.1 + hz * 9.0)));
					sc = mix(sc, vec3(0.17, 0.17, 0.18), line * 0.22);
				}
				sc *= 0.88 + 0.2 * vnoise(lr * 25.0 + hz * 9.0);
				sc *= 0.85 + 0.15 * smoothstep(wid, wid * 0.5, abs(lr.y)); // damp, darker torn edges
				bestCol = mix(col * 0.7, sc, inside);
				bestH = 0.55 + 0.12 * float(layer);
			}
		if (best >= 0.0) {
			col = bestCol;
			h = bestH;
		}
	}
	// Crumbs of compost and castings worked in among the strips.
	vec2 rel;
	vec4 cr = cells(p * 0.7 + 21.0 + (vec2(vnoise(p * 1.7), vnoise(p * 1.7 + 9.0)) - 0.5) * 0.5, rel);
	float crumb = step(cr.z, 0.16) * smoothstep(0.5, 0.25, cr.x + (vnoise(p * 4.0) - 0.5) * 0.3) * (0.4 + 0.6 * cr.w);
	col = mix(col, mix(vec3(0.07, 0.05, 0.035), vec3(0.15, 0.1, 0.065), cr.w), crumb);
	h = mix(h, 0.8, crumb);
	return col;
}

// A cellular piece lit as a rounded lump: brighter on the side facing the light (upper left), darker below,
// with fine grain inside so no piece is a flat tile.
float lump_shade(vec4 c, vec2 rel, vec2 p) {
	float edge = smoothstep(0.02, 0.35, c.y);
	float facing = dot(normalize(rel + 1e-4), normalize(vec2(1.0, 1.0)));
	return (0.82 + 0.3 * facing * smoothstep(0.05, 0.45, c.x)) * mix(0.55, 1.0, edge) * (0.88 + 0.24 * vnoise(p * 3.7));
}

// Finished compost: dark crumbs of every size with bits of leaf and the odd eggshell fleck.
vec3 compost(vec2 p, out float h) {
	vec2 rel;
	// Irregular crumbs: a warped domain, and coarse and fine crumbs in patches.
	vec2 pw = p + (vec2(vnoise(p * 0.6), vnoise(p * 0.6 + 5.0)) - 0.5) * 1.4;
	float fine = smoothstep(0.35, 0.65, vnoise(p * 0.12 + 40.0));
	vec4 c = cells(pw * 0.85, rel);
	vec3 col = mix(vec3(0.08, 0.055, 0.035), vec3(0.18, 0.12, 0.075), c.z * 0.7 + vnoise(p * 0.25) * 0.3);
	col *= lump_shade(c, rel, p);
	vec2 relf;
	vec4 cf = cells(pw * 1.9 + 11.0, relf);
	vec3 colf = mix(vec3(0.07, 0.05, 0.032), vec3(0.16, 0.11, 0.07), cf.z * 0.7 + vnoise(p * 0.25) * 0.3) * lump_shade(cf, relf, p * 1.7);
	col = mix(col, colf, fine);
	vec2 rel2;
	vec4 f = cells(p * 2.1 + 7.0, rel2);
	col *= 0.85 + 0.2 * lump_shade(f, rel2, p * 1.9);
	col = mix(col, vec3(0.05, 0.035, 0.025), smoothstep(0.08, 0.0, f.y) * 0.4);
	float leaf = step(0.97, c.w) * step(0.5, vnoise(p * 2.0 + 3.0));
	col = mix(col, vec3(0.2, 0.125, 0.06) * lump_shade(c, rel, p), leaf * 0.6);
	float shell = step(0.998, f.z);
	col = mix(col, vec3(0.8, 0.76, 0.68), shell * 0.7);
	h = 0.35 + 0.4 * smoothstep(0.0, 0.45, c.x) * (1.0 - smoothstep(0.35, 0.7, c.x));
	return col;
}

// Garden soil underneath: finer, more even loam with sand grains.
vec3 soil(vec2 p, out float h) {
	vec2 rel;
	vec4 g = cells(p * 1.8, rel);
	vec3 col = mix(vec3(0.11, 0.075, 0.05), vec3(0.2, 0.14, 0.09), g.z * 0.6 + fbm(p * 0.3) * 0.4);
	col *= lump_shade(g, rel, p);
	vec2 rel2;
	vec4 lump = cells(p * 0.45 + 5.0, rel2);
	col *= 0.85 + 0.25 * lump.z;
	col *= 1.0 - 0.35 * smoothstep(0.08, 0.0, lump.y);
	col = mix(col, vec3(0.42, 0.38, 0.32), step(0.985, g.z) * 0.4);
	col *= 1.0 - 0.3 * smoothstep(0.08, 0.0, g.y);
	h = 0.3 + 0.3 * smoothstep(0.0, 0.3, g.x);
	return col;
}

// Worm castings: near-black, round and granular, a little glossy.
vec3 castings(vec2 p, out float h) {
	vec2 rel;
	vec4 c = cells(p * 1.3 + 3.0, rel);
	float pellet = smoothstep(0.42, 0.28, c.x);
	vec3 col = mix(vec3(0.045, 0.032, 0.024), vec3(0.1, 0.07, 0.05), c.z);
	col *= (0.55 + 0.45 * pellet) * lump_shade(c, rel, p);
	h = 0.25 + 0.45 * pellet * (1.0 - c.x * 1.5);
	return col;
}

vec3 material_look(int m, vec2 p, float pw, out float h) {
	if (m == 1) return bedding(p, pw, h);
	if (m == 2) return compost(p, h);
	return soil(p, h);
}

void fragment() {
	vec2 cellp = UV * grid_size;
	// Screen-space derivatives are taken here, in uniform control flow: inside the material branches and loops
	// neighbouring pixels diverge at cell borders and derivatives would be garbage (it shows as a faint grid).
	float pw = length(fwidth(cellp));
	vec4 lin = bicubic(terrain_linear, UV, grid_size);
	// Material: the four nearest cells vote with bilinear weights plus a little noise per material, so the
	// boundaries between bedding, compost and soil are irregular and never follow the grid.
	vec2 warp = (vec2(fbm(cellp * 0.12 + 1.7), fbm(cellp * 0.12 + 9.2)) - 0.5) * 3.0;
	vec2 wp = cellp + warp - 0.5;
	vec2 b = floor(wp), f = fract(wp);
	vec4 votes = vec4(0.0); // air, bedding, compost, soil
	for (int k = 0; k < 4; k++) {
		vec2 o = vec2(float(k & 1), float(k >> 1));
		ivec2 ci = ivec2(clamp(b + o, vec2(0.0), grid_size - 1.0));
		int mk = int(texelFetch(terrain_nearest, ci, 0).r * 255.0 + 0.5) / 64;
		float w = (o.x > 0.5 ? f.x : 1.0 - f.x) * (o.y > 0.5 ? f.y : 1.0 - f.y);
		votes[mk] += w;
	}
	votes.yzw += (vec3(vnoise(cellp * 0.5), vnoise(cellp * 0.5 + 31.0), vnoise(cellp * 0.5 + 57.0)) - 0.5) * 0.5;
	int m = 1;
	if (votes.z > votes.y && votes.z >= votes.w) m = 2;
	else if (votes.w > votes.y && votes.w > votes.z) m = 3;
	float present = smoothstep(0.12, 0.4, lin.a + (vnoise(cellp * 1.3) - 0.5) * 0.16); // air above the surface

	float h;
	vec3 col = material_look(m, cellp, pw, h);
	// Castings: dark granules mixed in, patchy at small scale.
	float cast = clamp(lin.g * 1.4 + (vnoise(cellp * 0.6) - 0.5) * 0.3, 0.0, 1.0);
	float ch;
	vec3 cc = castings(cellp, ch);
	float castMix = smoothstep(0.25, 0.85, cast);
	col = mix(col, cc, castMix);
	h = mix(h, ch, castMix);

	// Moisture: wetter is darker and glossier.
	float wet = clamp(lin.b, 0.0, 1.0);
	col *= 1.0 - 0.42 * smoothstep(0.35, 0.95, wet);

	// Burrows: where a worm has pushed along the glass the bedding is parted — a darker channel with a moist,
	// slimy lining and crumbly walls.
	float dens = lin.a;
	float open = smoothstep(0.8, 0.55, dens + (vnoise(cellp * 1.4) - 0.5) * 0.08) * present;
	vec3 tunnel = col * 0.45 + vec3(0.012, 0.008, 0.005);
	col = mix(col, tunnel, open * 0.75);
	h = mix(h, -0.2, open);

	// Headspace: the dim back of the bin above the bedding, with a little dampness on the rear wall.
	vec3 back = mix(back_top, back_bottom, smoothstep(0.0, 0.12, UV.y));
	back *= 0.92 + 0.08 * vnoise(cellp * vec2(0.03, 0.1));
	col = mix(back, col, present);
	h *= present;

	vec2 dh = vec2(dFdx(h), dFdy(h));
	vec3 n = normalize(vec3(-dh.x * 2.2, dh.y * 2.2, 1.0));
	NORMAL_MAP = n * 0.5 + 0.5;
	NORMAL_MAP_DEPTH = 1.0;

	ALBEDO = col;
	float gloss = max(smoothstep(0.5, 1.0, wet) * 0.6, open * 0.5);
	ROUGHNESS = mix(0.92, 0.38, gloss) * mix(1.0, 0.8, castMix);
	SPECULAR = 0.4;
	AO = mix(1.0, 0.55, open);
	AO_LIGHT_AFFECT = 0.4;
}
