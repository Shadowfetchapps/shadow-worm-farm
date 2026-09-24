class_name FarmView
extends Node3D
## The presentation: a shallow 3D view of the worm bin behind its front pane. It only reads the simulation
## (substrate texture, worm bodies, food, cocoons, humidity) and never changes it.

const CELL := 0.03            ## world units per grid cell (about 2 mm)
const GLASS_Z := 0.035
const FRAME_W := 0.34
const FRAME_MARGIN_CELLS := 2.0
const TUBE_RINGS := 72
const TUBE_SIDES := 12
const MAX_FOODS := 32
const MAX_COCOONS := 128

var sim: WormFarmSim
var grid := Vector2i(320, 180)
var camera: Camera3D
var substrate_material: ShaderMaterial
var glass_material: ShaderMaterial
var terrain_image: Image
var terrain_texture: ImageTexture
var body_image: Image
var body_texture: ImageTexture
var body_rows := 0
var worms_mm: MultiMesh
var food_mm: MultiMesh
var cocoon_mm: MultiMesh
var key_light: DirectionalLight3D

var _terrain_timer := 0.0
var _slow_timer := 0.0
var _humidity_shown := 0.3


func setup(p_sim: WormFarmSim) -> void:
	sim = p_sim
	grid = sim.get_grid_size()
	for c in get_children():
		c.queue_free()
	_build_environment()
	_build_substrate()
	_build_frame_and_glass()
	_build_worms()
	_build_food_and_cocoons()
	_update_terrain(true)
	_update_slow()
	_humidity_shown = sim.get_humidity()
	_fit_camera()
	if not get_viewport().size_changed.is_connected(_fit_camera):
		get_viewport().size_changed.connect(_fit_camera)


func _build_environment() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.03, 0.028, 0.026)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.66, 0.63, 0.6)
	env.ambient_light_energy = 0.36
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.tonemap_exposure = 0.95
	env.tonemap_white = 6.0
	env.adjustment_enabled = true
	env.adjustment_contrast = 1.04
	env.adjustment_saturation = 1.03
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)

	key_light = DirectionalLight3D.new()
	key_light.light_color = Color(1.0, 0.95, 0.87)
	key_light.light_energy = 0.62
	key_light.shadow_enabled = true
	key_light.shadow_blur = 1.4
	key_light.shadow_bias = 0.02
	key_light.shadow_normal_bias = 0.4
	key_light.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	key_light.directional_shadow_max_distance = 40.0
	add_child(key_light)
	key_light.look_at_from_position(Vector3(-3.0, 4.0, 6.0), Vector3.ZERO, Vector3.UP)

	# A lamp above and in front of the bin, off to the left: the gentle falloff across the glass of a photo.
	var lamp := SpotLight3D.new()
	lamp.light_color = Color(1.0, 0.93, 0.82)
	lamp.light_energy = 7.0
	lamp.spot_range = 40.0
	lamp.spot_angle = 55.0
	lamp.spot_attenuation = 1.2
	lamp.spot_angle_attenuation = 1.6
	lamp.shadow_enabled = false
	add_child(lamp)
	lamp.look_at_from_position(Vector3(-2.8, 3.6, 7.5), Vector3(0.6, -0.4, 0.0), Vector3.UP)

	var fill := DirectionalLight3D.new()
	fill.light_color = Color(0.9, 0.92, 1.0)
	fill.light_energy = 0.22
	add_child(fill)
	fill.look_at_from_position(Vector3(5.0, -1.0, 6.0), Vector3.ZERO, Vector3.UP)

	camera = Camera3D.new()
	camera.fov = 16.0
	camera.near = 0.5
	camera.far = 200.0
	add_child(camera)
	camera.current = true


func _fit_camera() -> void:
	if camera == null:
		return
	fit_camera_to(camera, get_viewport().get_visible_rect().size)


## Places `cam` so the bin fills a view of `vp` pixels (the window, or the live-stream picture).
func fit_camera_to(cam: Camera3D, vp: Vector2) -> void:
	if vp.y <= 0:
		return
	var aspect := vp.x / vp.y
	var inner := Vector2(grid.x - 2.0 * FRAME_MARGIN_CELLS, grid.y - 2.0 * FRAME_MARGIN_CELLS) * CELL
	var outer := inner + Vector2(2.0 * FRAME_W, 2.0 * FRAME_W)
	# Fill the screen with the bin: the frame may be cropped a little at the edges, never the contents.
	var half_h: float = max(inner.y * 0.5 + FRAME_W * 0.35, (inner.x * 0.5 + FRAME_W * 0.35) / aspect)
	half_h = min(half_h, max(outer.y * 0.5, outer.x * 0.5 / aspect))
	var dist := half_h / tan(deg_to_rad(cam.fov * 0.5))
	cam.position = Vector3(0.0, 0.0, dist + GLASS_Z)
	cam.look_at(Vector3(0.0, 0.0, 0.0), Vector3.UP)


func _build_substrate() -> void:
	terrain_image = Image.create(grid.x, grid.y, false, Image.FORMAT_RGBA8)
	terrain_texture = ImageTexture.create_from_image(terrain_image)
	substrate_material = ShaderMaterial.new()
	substrate_material.shader = load("res://shaders/substrate.gdshader")
	substrate_material.set_shader_parameter("terrain_nearest", terrain_texture)
	substrate_material.set_shader_parameter("terrain_linear", terrain_texture)
	substrate_material.set_shader_parameter("grid_size", Vector2(grid))
	substrate_material.set_shader_parameter("seed", float(sim.get_seed() % 997))
	var quad := QuadMesh.new()
	quad.size = Vector2(grid.x * CELL, grid.y * CELL)
	var mi := MeshInstance3D.new()
	mi.mesh = quad
	mi.material_override = substrate_material
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mi)


func _build_frame_and_glass() -> void:
	var inner := Vector2(grid.x - 2.0 * FRAME_MARGIN_CELLS, grid.y - 2.0 * FRAME_MARGIN_CELLS) * CELL
	var depth := 0.16
	var z := GLASS_Z - depth * 0.5 + 0.03
	var bars := [
		[Vector3(0, inner.y * 0.5 + FRAME_W * 0.5, z), Vector3(inner.x + 2.0 * FRAME_W, FRAME_W, depth), false],
		[Vector3(0, -inner.y * 0.5 - FRAME_W * 0.5, z), Vector3(inner.x + 2.0 * FRAME_W, FRAME_W, depth), false],
		[Vector3(-inner.x * 0.5 - FRAME_W * 0.5, 0, z), Vector3(FRAME_W, inner.y, depth), true],
		[Vector3(inner.x * 0.5 + FRAME_W * 0.5, 0, z), Vector3(FRAME_W, inner.y, depth), true],
	]
	var wood := load("res://shaders/wood.gdshader")
	for b in bars:
		var bm := BoxMesh.new()
		bm.size = b[1]
		var mat := ShaderMaterial.new()
		mat.shader = wood
		mat.set_shader_parameter("vertical", b[2])
		mat.set_shader_parameter("seed", float(bars.find(b)) * 3.7)
		var mi := MeshInstance3D.new()
		mi.mesh = bm
		mi.material_override = mat
		mi.position = b[0]
		add_child(mi)
	# A dark bead where the pane sits in its groove.
	var bead := StandardMaterial3D.new()
	bead.albedo_color = Color(0.1, 0.075, 0.05)
	bead.roughness = 0.6
	for b in [[Vector3(0, inner.y * 0.5 + 0.006, GLASS_Z + 0.004), Vector3(inner.x, 0.012, 0.01)],
			[Vector3(0, -inner.y * 0.5 - 0.006, GLASS_Z + 0.004), Vector3(inner.x, 0.012, 0.01)],
			[Vector3(-inner.x * 0.5 - 0.006, 0, GLASS_Z + 0.004), Vector3(0.012, inner.y, 0.01)],
			[Vector3(inner.x * 0.5 + 0.006, 0, GLASS_Z + 0.004), Vector3(0.012, inner.y, 0.01)]]:
		var lm := BoxMesh.new()
		lm.size = b[1]
		var li := MeshInstance3D.new()
		li.mesh = lm
		li.material_override = bead
		li.position = b[0]
		li.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(li)

	var glass := QuadMesh.new()
	glass.size = inner
	glass_material = ShaderMaterial.new()
	glass_material.shader = load("res://shaders/glass.gdshader")
	glass_material.set_shader_parameter("seed", float(sim.get_seed() % 991))
	var gi := MeshInstance3D.new()
	gi.mesh = glass
	gi.material_override = glass_material
	gi.position = Vector3(0, 0, GLASS_Z)
	gi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(gi)


## One tube, head (u = 0) to tail (u = 1); the worm shader bends it along each body. UV2.x closes the ends.
func _build_tube() -> ArrayMesh:
	var verts := PackedVector3Array()
	var normals := PackedVector3Array()
	var uvs := PackedVector2Array()
	var uv2s := PackedVector2Array()
	var idx := PackedInt32Array()
	var rings := TUBE_RINGS + 3
	for r in rings:
		var u: float
		var cap := 1.0
		if r == 0:
			u = 0.0
			cap = 0.0
		elif r == rings - 1:
			u = 1.0
			cap = 0.0
		else:
			u = float(r - 1) / float(rings - 3)
			if r == 1 or r == rings - 2:
				cap = 0.75
		for s in TUBE_SIDES + 1:
			verts.append(Vector3(u, 0, 0))
			normals.append(Vector3(0, 0, 1))
			uvs.append(Vector2(u, float(s) / float(TUBE_SIDES)))
			uv2s.append(Vector2(cap, 0))
	var row := TUBE_SIDES + 1
	for r in rings - 1:
		for s in TUBE_SIDES:
			var a := r * row + s
			var b := a + row
			idx.append_array([a, a + 1, b, a + 1, b + 1, b])
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_TEX_UV2] = uv2s
	arrays[Mesh.ARRAY_INDEX] = idx
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mesh.custom_aabb = AABB(Vector3(-10, -10, -1), Vector3(20, 20, 2))
	return mesh


func _build_worms() -> void:
	body_rows = sim.get_max_worms()
	body_image = Image.create_empty(sim.get_body_points(), body_rows, false, Image.FORMAT_RGBAF)
	body_texture = ImageTexture.create_from_image(body_image)
	var mat := ShaderMaterial.new()
	mat.shader = load("res://shaders/worm.gdshader")
	mat.set_shader_parameter("body_data", body_texture)
	mat.set_shader_parameter("body_points", sim.get_body_points())
	mat.set_shader_parameter("glass_z", GLASS_Z - 0.001)
	worms_mm = MultiMesh.new()
	worms_mm.transform_format = MultiMesh.TRANSFORM_3D
	worms_mm.use_custom_data = true
	worms_mm.mesh = _build_tube()
	worms_mm.instance_count = body_rows
	worms_mm.visible_instance_count = 0
	var wi := MultiMeshInstance3D.new()
	wi.multimesh = worms_mm
	wi.material_override = mat
	wi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	wi.custom_aabb = AABB(Vector3(-10, -10, -1), Vector3(20, 20, 2))
	add_child(wi)


func _build_food_and_cocoons() -> void:
	var fmat := ShaderMaterial.new()
	fmat.shader = load("res://shaders/food.gdshader")
	food_mm = MultiMesh.new()
	food_mm.transform_format = MultiMesh.TRANSFORM_3D
	food_mm.use_custom_data = true
	var q := QuadMesh.new()
	q.size = Vector2(2, 2)
	food_mm.mesh = q
	food_mm.instance_count = MAX_FOODS
	food_mm.visible_instance_count = 0
	var fi := MultiMeshInstance3D.new()
	fi.multimesh = food_mm
	fi.material_override = fmat
	fi.custom_aabb = AABB(Vector3(-10, -10, -1), Vector3(20, 20, 2))
	add_child(fi)

	var cmat := StandardMaterial3D.new()
	cmat.vertex_color_use_as_albedo = true
	cmat.roughness = 0.35
	cmat.clearcoat_enabled = true
	cmat.clearcoat = 0.6
	cocoon_mm = MultiMesh.new()
	cocoon_mm.transform_format = MultiMesh.TRANSFORM_3D
	cocoon_mm.use_colors = true
	var sphere := SphereMesh.new()
	sphere.radius = 0.5
	sphere.height = 1.0
	sphere.radial_segments = 16
	sphere.rings = 8
	cocoon_mm.mesh = sphere
	cocoon_mm.instance_count = MAX_COCOONS
	cocoon_mm.visible_instance_count = 0
	var ci := MultiMeshInstance3D.new()
	ci.multimesh = cocoon_mm
	ci.material_override = cmat
	ci.custom_aabb = AABB(Vector3(-10, -10, -1), Vector3(20, 20, 2))
	add_child(ci)


## Called every rendered frame with the interpolation factor.
func refresh(delta: float, alpha: float) -> void:
	if sim == null:
		return
	_terrain_timer -= delta
	if _terrain_timer <= 0.0:
		_terrain_timer = 0.2
		_update_terrain(false)
	_slow_timer -= delta
	if _slow_timer <= 0.0:
		_slow_timer = 0.5
		_update_slow()
	var n := mini(sim.get_worm_count(), body_rows)
	var data := sim.build_body_data(alpha, CELL, body_rows)
	body_image.set_data(sim.get_body_points(), body_rows, false, Image.FORMAT_RGBAF, data.to_byte_array())
	body_texture.update(body_image)
	worms_mm.buffer = sim.build_worm_instances(body_rows)
	worms_mm.visible_instance_count = n
	# Condensation follows the bin's humidity slowly (drops form and clear over minutes, not frames).
	_humidity_shown = lerpf(_humidity_shown, sim.get_humidity(), clampf(delta * 0.05, 0.0, 1.0))
	glass_material.set_shader_parameter("humidity", _humidity_shown)


func _update_terrain(force: bool) -> void:
	var bytes := sim.take_terrain_update(force)
	if bytes.is_empty():
		return
	terrain_image.set_data(grid.x, grid.y, false, Image.FORMAT_RGBA8, bytes)
	terrain_texture.update(terrain_image)
	# Where the bedding surface sits (for the glass condensation band).
	var inner_h := grid.y - 2.0 * FRAME_MARGIN_CELLS
	var top := 0
	var x := grid.x / 2
	while top < grid.y and bytes[(top * grid.x + x) * 4] == 0:
		top += 1
	glass_material.set_shader_parameter("surface_v", clampf((top - FRAME_MARGIN_CELLS) / inner_h, 0.0, 1.0))


func _grid_to_world(p: Vector2, z: float) -> Vector3:
	return Vector3((p.x - grid.x * 0.5) * CELL, (grid.y * 0.5 - p.y) * CELL, z)


func _update_slow() -> void:
	var foods := sim.get_foods()
	var fb := PackedFloat32Array()
	fb.resize(MAX_FOODS * 16)
	var n := 0
	var k := 0
	while k + 8 < foods.size() and n < MAX_FOODS:
		var w := _grid_to_world(Vector2(foods[k], foods[k + 1]), 0.006 + 0.0004 * n)
		var s := foods[k + 2] * CELL
		var a := foods[k + 3]
		var o := n * 16
		fb[o + 0] = cos(a) * s
		fb[o + 1] = -sin(a) * s
		fb[o + 2] = 0.0
		fb[o + 3] = w.x
		fb[o + 4] = sin(a) * s
		fb[o + 5] = cos(a) * s
		fb[o + 6] = 0.0
		fb[o + 7] = w.y
		fb[o + 8] = 0.0
		fb[o + 9] = 0.0
		fb[o + 10] = s
		fb[o + 11] = w.z
		fb[o + 12] = foods[k + 4]
		fb[o + 13] = foods[k + 5]
		fb[o + 14] = foods[k + 6]
		fb[o + 15] = foods[k + 7]
		n += 1
		k += 9
	food_mm.buffer = fb
	food_mm.visible_instance_count = n

	var coc := sim.get_cocoons()
	var cb := PackedFloat32Array()
	cb.resize(MAX_COCOONS * 16)
	var m := 0
	k = 0
	while k + 3 < coc.size() and m < MAX_COCOONS:
		var r: float = coc[k + 3]
		# About half of the cocoons are laid where they can be seen against the glass.
		if r < 0.55:
			var w := _grid_to_world(Vector2(coc[k], coc[k + 1]), 0.012)
			var s := CELL * (1.0 + 0.3 * r)
			var a := r * 40.0
			var o := m * 16
			cb[o + 0] = cos(a) * s * 1.45
			cb[o + 1] = -sin(a) * s
			cb[o + 2] = 0.0
			cb[o + 3] = w.x
			cb[o + 4] = sin(a) * s * 1.45
			cb[o + 5] = cos(a) * s
			cb[o + 6] = 0.0
			cb[o + 7] = w.y
			cb[o + 8] = 0.0
			cb[o + 9] = 0.0
			cb[o + 10] = s
			cb[o + 11] = w.z
			# fresh cocoons are pale yellow-green, darkening to reddish brown as they near hatching
			var age: float = coc[k + 2]
			var col := Color(0.78, 0.72, 0.36).lerp(Color(0.48, 0.24, 0.12), age)
			cb[o + 12] = col.r
			cb[o + 13] = col.g
			cb[o + 14] = col.b
			cb[o + 15] = 1.0
			m += 1
		k += 4
	cocoon_mm.buffer = cb
	cocoon_mm.visible_instance_count = m
