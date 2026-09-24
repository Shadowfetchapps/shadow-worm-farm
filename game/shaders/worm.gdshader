// Red wigglers (MultiMesh of one tube mesh). Each instance bends its tube along its worm's body: the body
// points come from a float texture (x, y, depth into the bin, radius; one row per worm), interpolated with a
// Catmull-Rom spline so the body is a smooth curve. INSTANCE_CUSTOM: x growth (1 = adult, clitellum shows),
// y peristalsis phase (advances with distance crawled), z a per-worm random value, w activity (+2 while feeding).
shader_type spatial;
render_mode cull_back, specular_schlick_ggx;

uniform sampler2D body_data : filter_nearest, repeat_disable;
uniform int body_points = 24;
uniform float contact_z = 0.016;   ///< body axis z when pressed against the glass
uniform float depth_range = 0.2;   ///< how far back the body goes at depth 1 (it is hidden by the substrate long before)
uniform float glass_z = 0.034;     ///< the inside of the front pane: bodies flatten against it
uniform float segments = 95.0;

varying float v_u;
varying float v_depth;
varying float v_side;

vec4 point_at(int i, int row) {
	return texelFetch(body_data, ivec2(clamp(i, 0, body_points - 1), row), 0);
}
vec4 catmull(vec4 p0, vec4 p1, vec4 p2, vec4 p3, float t) {
	return 0.5 * ((2.0 * p1) + (-p0 + p2) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t * t + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t * t * t);
}
vec4 catmull_d(vec4 p0, vec4 p1, vec4 p2, vec4 p3, float t) {
	return 0.5 * ((-p0 + p2) + 2.0 * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t + 3.0 * (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t * t);
}

void vertex() {
	int row = INSTANCE_ID;
	float u = clamp(UV.x, 0.0, 1.0);
	float fi = u * float(body_points - 1);
	int i1 = min(int(floor(fi)), body_points - 2);
	float t = fi - float(i1);
	vec4 p0 = point_at(i1 - 1, row), p1 = point_at(i1, row), p2 = point_at(i1 + 1, row), p3 = point_at(i1 + 2, row);
	if (i1 == 0) p0 = 2.0 * p1 - p2;
	if (i1 + 2 >= body_points) p3 = 2.0 * p2 - p1;
	vec4 c = catmull(p0, p1, p2, p3, t);
	vec4 d = catmull_d(p0, p1, p2, p3, t);

	float grow = INSTANCE_CUSTOM.x;
	float phase = INSTANCE_CUSTOM.y;
	float rnd = INSTANCE_CUSTOM.z;
	float act = INSTANCE_CUSTOM.w;
	bool feeding = act >= 2.0;
	act = feeding ? act - 2.0 : act;

	float depth = clamp(c.z, 0.0, 1.0);
	vec3 axis = vec3(c.x, c.y, contact_z - depth * depth_range);
	vec3 tan3 = vec3(d.x, d.y, -d.z * depth_range);
	if (length(tan3) < 1e-6) tan3 = vec3(1.0, 0.0, 0.0);
	tan3 = normalize(tan3);
	vec3 side = normalize(cross(vec3(0.0, 0.0, 1.0), tan3) + vec3(1e-5, 0.0, 0.0));
	vec3 up = normalize(cross(tan3, side));

	// A feeding worm works its head over the food; a resting one breathes a little.
	float t_s = TIME + rnd * 50.0;
	float headw = smoothstep(0.18, 0.0, u);
	if (feeding) axis += side * sin(t_s * 2.3) * headw * 0.012 + tan3 * sin(t_s * 1.7) * headw * 0.006;

	// Peristalsis: waves of contraction run from head to tail as the worm crawls (and slowly at rest).
	float wave = sin(u * 22.0 - phase * 1.0 - t_s * (0.4 + 0.2 * act));
	float r = c.w * (1.0 + (0.05 + 0.07 * act) * wave);
	// Annuli: shallow grooves between segments.
	float seg = u * segments * mix(0.55, 1.0, grow);
	r *= 1.0 - 0.035 * pow(abs(cos(seg * 3.14159)), 12.0);
	r *= UV2.x; // end caps close to a point

	float ang = UV.y * 6.28318;
	vec3 n = cos(ang) * up + sin(ang) * side;
	vec3 pos = axis + n * r * vec3(1.0, 1.0, 0.85);
	// Pressed against the pane the body flattens where it touches the glass.
	pos.z = min(pos.z, glass_z - 0.0015);
	VERTEX = pos;
	NORMAL = normalize(n);
	TANGENT = tan3;
	BINORMAL = cross(NORMAL, TANGENT);
	v_u = u;
	v_depth = depth;
	v_side = dot(n, vec3(0.0, 0.0, 1.0));
	COLOR = vec4(grow, rnd, act, feeding ? 1.0 : 0.0);
}

float hash11(float p) {
	p = fract(p * 0.1031);
	p *= p + 33.33;
	p *= p + p;
	return fract(p);
}

void fragment() {
	float grow = COLOR.r;
	float rnd = COLOR.g;
	float u = v_u;
	float seg = u * segments * mix(0.55, 1.0, grow);
	float groove = pow(abs(cos(seg * 3.14159)), 10.0);
	// Eisenia fetida: deep maroon to red-purple segments, buff bands in the grooves ("tiger worm"), a paler
	// underside, a yellowish tail tip, the swollen orange-tan clitellum on adults. Young worms are pale pink.
	vec3 body = mix(vec3(0.19, 0.025, 0.035), vec3(0.3, 0.05, 0.05), 0.5 + 0.5 * sin(rnd * 40.0 + u * 3.0));
	body = mix(body, vec3(0.42, 0.22, 0.12), groove * 0.5 * smoothstep(0.08, 0.2, u));
	body = mix(body, vec3(0.62, 0.42, 0.2), smoothstep(0.86, 1.0, u) * 0.6);
	float cl = smoothstep(0.235, 0.255, u) * (1.0 - smoothstep(0.325, 0.345, u)) * step(0.999, grow);
	body = mix(body, vec3(0.5, 0.2, 0.13), cl * 0.75);
	body = mix(body, vec3(0.62, 0.36, 0.33), (1.0 - smoothstep(0.0, 0.8, grow)) * 0.8);
	// the underside (facing the back) is paler; the head end is darker and glossier
	body = mix(body, body * vec3(1.35, 1.2, 1.1), smoothstep(0.2, -0.8, v_side) * 0.4);
	body *= mix(0.8, 1.0, smoothstep(0.0, 0.1, u));
	// specks of substrate stuck to the skin
	float speck = step(0.985, hash11(floor(seg * 3.0) * 13.1 + floor(v_side * 6.0) * 7.7 + rnd * 91.0));
	body = mix(body, vec3(0.08, 0.06, 0.045), speck * 0.7);
	// worms further from the glass are seen through a film of moist substrate
	body *= 1.0 - 0.35 * smoothstep(0.02, 0.2, v_depth);

	ALBEDO = body;
	ROUGHNESS = mix(0.38, 0.5, groove);
	SPECULAR = 0.5;
	CLEARCOAT = 0.45;
	CLEARCOAT_ROUGHNESS = 0.18;
	BACKLIGHT = vec3(0.35, 0.06, 0.05) * 0.4;
}
