#pragma once
// A glass-sided worm bin. Red wigglers crawl through bedding and compost, find food scraps by the scent of their
// decay, eat them and leave castings, press against the glass now and then (that is when you see them), and lay
// cocoons that hatch into pale young worms. Every worm decides for itself from what it senses where it is; the
// keeper's only actions are adding food and misting the bin.

#include "wormfarm/bytes.hpp"
#include "wormfarm/config.hpp"
#include "wormfarm/rng.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace wormfarm {

enum class Material : uint8_t { Air = 0, Bedding = 1, Compost = 2, Soil = 3 };

/// The substrate between the panes, as seen against the front glass.
class World {
public:
	void generate(const SimConfig &cfg, uint64_t seed);
	int width() const { return m_w; }
	int height() const { return m_h; }
	bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < m_w && y < m_h; }
	int index(int x, int y) const { return y * m_w + x; }
	Material material(int x, int y) const { return inBounds(x, y) ? Material(m_material[size_t(index(x, y))]) : Material::Air; }
	bool isSubstrate(int x, int y) const { return inBounds(x, y) && m_material[size_t(index(x, y))] != 0; }
	int surface(int x) const { return m_surface[size_t(std::max(0, std::min(m_w - 1, x)))]; }
	float moisture(int x, int y) const { return inBounds(x, y) ? float(m_moisture[size_t(index(x, y))]) / 255.0f : 0.0f; }
	float density(int x, int y) const { return inBounds(x, y) ? float(m_density[size_t(index(x, y))]) / 255.0f : 1.0f; }

	/// A worm pressed against the glass pushes the substrate aside: a visible burrow that slowly fills in.
	void burrow(float x, float y, float radius);
	void addCastings(float x, float y, float amount);
	void addMoisture(float x, float y, float radius, float amount);
	/// Burrows settle, fresh castings mix in, the top dries, water seeps down (call about once a second).
	void settle(float dt, const SimConfig &cfg);
	void mist(float amount);

	const std::vector<uint8_t> &materials() const { return m_material; }
	const std::vector<uint8_t> &castings() const { return m_castings; }
	const std::vector<uint8_t> &moistures() const { return m_moisture; }
	const std::vector<uint8_t> &densities() const { return m_density; }
	uint64_t version() const { return m_version; }
	double totalCastings() const;
	uint64_t checksum() const;
	void save(ByteWriter &w) const;
	bool load(ByteReader &r);

private:
	int m_w = 0, m_h = 0;
	std::vector<uint8_t> m_material, m_castings, m_moisture, m_density;
	std::vector<uint8_t> m_castBase; ///< castings as generated: fresh castings fade back to this as the bin mixes
	std::vector<int> m_surface;
	float m_settleAccum = 0, m_dryAccum = 0, m_seepAccum = 0, m_mixAccum = 0;
	uint64_t m_version = 0;
};

enum class FoodType : uint8_t { BananaPeel, AppleCore, Leaf, CarrotPeel, CoffeeGrounds, Eggshell, Count };
const char *foodName(FoodType t);

struct Food {
	uint32_t id = 0;
	FoodType type = FoodType::Leaf;
	uint8_t pad_[3] = {}; ///< explicit, so the state hash never reads indeterminate padding
	float x = 0, y = 0, radius = 6, angle = 0;
	float mass = 1, mass0 = 1; ///< mass units (1 ≈ a handful)
	float decay = 0;           ///< 0 fresh … 1 fully rotten
	uint32_t seed = 0;
	int eaters = 0;            ///< worms feeding on it this tick (derived)
};

struct Cocoon {
	uint32_t id = 0;
	float x = 0, y = 0, age = 0, hatchAt = 0;
	uint32_t seed = 0;
};

enum class WormState : uint8_t { Wander, SeekFood, Feed, Rest, Count };
const char *wormStateName(WormState s);

constexpr int kBodyPoints = 24;

struct Worm {
	uint32_t id = 0;
	float length = 30;        ///< cells
	float adultLength = 38;
	float grow = 1;           ///< 0 hatchling … 1 adult (the clitellum shows from 1)
	float px[kBodyPoints] = {}, py[kBodyPoints] = {}, pz[kBodyPoints] = {};
	float ox[kBodyPoints] = {}, oy[kBodyPoints] = {}, oz[kBodyPoints] = {}; ///< previous tick (interpolation)
	float heading = 0, desired = 0, speed = 0;
	float zTarget = 0.6f;     ///< 0 pressed against the glass … 1 deep in the bin
	float zTimer = 0;
	float hunger = 0.5f, digest = 0; ///< digest: castings still to pass
	WormState state = WormState::Wander;
	uint8_t pad_[3] = {};
	float stateTime = 0, castAccum = 0, restTimer = 0; ///< castAccum: cells crawled since the last casting
	int32_t food = -1;        ///< id of the food it is heading for / eating
	float phase = 0;          ///< peristalsis wave, advances with distance crawled
	float speedMul = 1, persistence = 1, glassAffinity = 1;
	uint32_t noiseSeed = 0;
	float homeDepth = 0.5f;   ///< where this worm likes to rest and wander, 0 just under the surface … 1 the bottom
	Pcg32 rng;
};
// Checkpoints and the state hash use the raw bytes of these structs: no implicit padding allowed.
static_assert(sizeof(Food) == 11 * 4 && sizeof(Cocoon) == 6 * 4 && sizeof(Worm) == 680);

enum class EventType : uint8_t { FoodDropped, Mist, Hatch, Squelch, Rustle };
struct Event {
	EventType type;
	float x, y, strength;
};

struct Stats {
	double seconds = 0;
	int worms = 0, adults = 0, juveniles = 0, cocoons = 0, foods = 0, feeding = 0, atGlass = 0;
	std::array<int, size_t(WormState::Count)> byState{};
	double eaten = 0, castings = 0, foodMass = 0;
	int feedings = 0, mists = 0, hatched = 0;
};

class Simulation {
public:
	bool init(const SimConfig &cfg, uint64_t seed);
	void step();
	void stepN(int n);

	uint64_t seed() const { return m_seed; }
	uint64_t tick() const { return m_tick; }
	double seconds() const { return double(m_tick) / double(m_cfg.ticksPerSecond); }
	const SimConfig &config() const { return m_cfg; }
	const World &world() const { return m_world; }
	const std::vector<Worm> &worms() const { return m_worms; }
	const std::vector<Food> &foods() const { return m_foods; }
	const std::vector<Cocoon> &cocoons() const { return m_cocoons; }
	float scent(float x, float y) const;
	float humidity() const { return m_humidity; } ///< 0..1, condensation on the glass
	Stats stats() const;

	/// Keeper actions (also scheduled on their own): add food scraps now, mist the bin now.
	void feedNow();
	void mistNow();

	/// Events since `from` (a running sequence number); returns the new sequence number.
	uint64_t readEvents(uint64_t from, std::vector<Event> &out, size_t max) const;
	uint64_t eventsWritten() const { return m_eventsWritten; }

	uint64_t stateHash() const;
	std::vector<uint8_t> saveCheckpoint() const;
	bool loadCheckpoint(const std::vector<uint8_t> &data, std::string *error);
	static constexpr uint32_t kCheckpointVersion = 1;
	size_t memoryFootprint() const;

private:
	void spawnWorm(float x, float y, bool adult);
	void placeFood(FoodType type, float x, float decay);
	void updateWorm(Worm &w, float dt);
	void decide(Worm &w);
	void moveWorm(Worm &w, float dt);
	void updateFoods(float dt);
	void updateScent(float dt);
	void updateCocoons(float dt);
	void rebuildHash();
	int neighbours(float x, float y, float r, uint32_t self) const;
	void emit(EventType t, float x, float y, float s);
	Food *findFood(int32_t id);

	SimConfig m_cfg;
	uint64_t m_seed = 0, m_tick = 0;
	World m_world;
	std::vector<Worm> m_worms;
	std::vector<Food> m_foods;
	std::vector<Cocoon> m_cocoons;
	std::vector<float> m_scent, m_scentTmp; ///< coarse field (2 cells), food odour through the substrate
	int m_sw = 0, m_sh = 0;
	Pcg32 m_rng;
	uint32_t m_nextId = 1;
	double m_nextFeeding = 0, m_nextMist = 0, m_lastSettle = 0, m_lastScent = 0;
	float m_humidity = 0.3f;
	double m_eaten = 0;
	int m_feedings = 0, m_mists = 0, m_hatched = 0;
	// events: fixed ring
	std::vector<Event> m_events;
	uint64_t m_eventsWritten = 0;
	// spatial hash (8-cell buckets) of worm heads
	static constexpr int kBucket = 8;
	int m_bw = 0, m_bh = 0;
	std::vector<int> m_bucketStart, m_bucketItems, m_bucketFill;
};

} // namespace wormfarm
