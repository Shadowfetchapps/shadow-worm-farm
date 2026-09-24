#pragma once
// Godot-facing wrapper around the deterministic worm-bin core. The presentation never changes the simulation
// except through the keeper's two actions (feed now, mist now): it advances it on a fixed tick and reads the
// substrate, worm bodies, food and events back.

#include "wormfarm/sim.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <memory>

namespace godot {

class WormFarmSim : public RefCounted {
	GDCLASS(WormFarmSim, RefCounted)

public:
	/// Starts a new bin. seed 0 draws a fresh seed from the operating system.
	bool start_new(int64_t seed, const String &config_path);
	/// Loads a checkpoint written by save_checkpoint (config comes from the file).
	bool load_checkpoint(const String &path);
	/// Atomically writes a checkpoint (temporary file, fsync, rename).
	bool save_checkpoint(const String &path) const;
	String get_last_error() const { return m_error; }
	bool is_running() const { return m_sim != nullptr; }

	/// Advances by real elapsed time on the fixed tick; at most max_ticks per call (the rest is dropped and
	/// counted, so a stall never turns into a catch-up burst). Returns the ticks run.
	int advance(double real_dt, int max_ticks);
	/// Runs exactly n ticks (accelerated/headless use).
	int step_ticks(int n);
	double get_alpha() const;
	double get_sim_seconds() const;
	int64_t get_tick() const;
	int64_t get_seed() const;
	int64_t get_dropped_ticks() const { return m_droppedTicks; }
	Vector2i get_grid_size() const;
	int get_ticks_per_second() const;
	int get_body_points() const { return wormfarm::kBodyPoints; }
	/// Upper bound on the number of worms (size the body texture once).
	int get_max_worms() const;

	/// Keeper actions.
	void feed_now();
	void mist_now();

	/// Substrate as RGBA8 (width x height): R material*64, G castings, B moisture, A density (255 packed,
	/// lower where a burrow runs along the glass). Returns an empty array when nothing changed since the last call.
	PackedByteArray take_terrain_update(bool force);

	/// Worm bodies at alpha, as an RGBA32F image of body_points x rows: per point x, y (3D units, centred),
	/// depth into the bin (0 at the glass … 1 at the back) and the local body radius (3D units). Rows beyond the
	/// worm count are zero. Call get_worm_count() for the number of rows in use.
	PackedFloat32Array build_body_data(double alpha, double cell_size, int rows);
	/// Per-worm MultiMesh buffer for `rows` instances (identity transform + custom: grow, peristalsis phase, seed,
	/// activity; +2 while feeding). Rows beyond the worm count are zero.
	PackedFloat32Array build_worm_instances(int rows);
	int get_worm_count() const;

	/// Food scraps: [x, y, radius, angle, type, decay, mass fraction, seed, eaters] per scrap (grid units).
	PackedFloat32Array get_foods() const;
	/// Cocoons: [x, y, age fraction, seed] per cocoon.
	PackedFloat32Array get_cocoons() const;
	double get_humidity() const;

	/// Events since the last poll: [type, x, y, strength] per event (grid coordinates).
	PackedFloat32Array poll_events(int max_events);

	Dictionary get_stats() const;
	String get_config_dump() const;
	String get_state_hash() const;
	int64_t get_memory_bytes() const;

protected:
	static void _bind_methods();

private:
	std::unique_ptr<wormfarm::Simulation> m_sim;
	double m_accum = 0;
	int64_t m_droppedTicks = 0;
	uint64_t m_eventCursor = 0;
	uint64_t m_terrainVersion = ~uint64_t(0);
	std::vector<wormfarm::Event> m_eventScratch;
	String m_error;
};

} // namespace godot
