#include "wormfarm/sim.hpp"

#include <cmath>
#include <cstring>

namespace wormfarm {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;
constexpr float kHungerPerMass = 90.0f;  ///< a worm's fill is about 1/90 of a handful of scraps
constexpr float kDigestPerMass = 1000.0f; ///< castings passed per handful eaten

float wrapAngle(float a)
{
	while (a > kPi)
		a -= kTwoPi;
	while (a < -kPi)
		a += kTwoPi;
	return a;
}

uint8_t clampByte(float v) { return uint8_t(std::min(255.0f, std::max(0.0f, v + 0.5f))); }

uint64_t fnv64(const void *data, size_t n, uint64_t h = 1469598103934665603ULL)
{
	const uint8_t *p = static_cast<const uint8_t *>(data);
	for (size_t i = 0; i < n; ++i) {
		h ^= p[i];
		h *= 1099511628211ULL;
	}
	return h;
}

template<typename T> uint64_t fnvVec(const std::vector<T> &v, uint64_t h)
{
	return v.empty() ? h : fnv64(v.data(), v.size() * sizeof(T), h);
}

struct FoodTraits {
	float radiusMin, radiusMax, mass, decayRate;
};

const FoodTraits &traits(FoodType t)
{
	static const FoodTraits table[] = {
		{20.0f, 26.0f, 1.2f, 1.2f}, // banana peel (radius: half its length, cells)
		{12.0f, 15.0f, 1.0f, 1.0f}, // apple core
		{15.0f, 20.0f, 0.7f, 1.4f}, // lettuce leaf
		{13.0f, 17.0f, 0.8f, 0.9f}, // carrot peelings
		{12.0f, 16.0f, 0.9f, 0.8f}, // coffee grounds
		{8.0f, 11.0f, 0.3f, 0.15f}, // eggshell
	};
	return table[size_t(t)];
}

} // namespace

const char *foodName(FoodType t)
{
	switch (t) {
	case FoodType::BananaPeel: return "banana peel";
	case FoodType::AppleCore: return "apple core";
	case FoodType::Leaf: return "lettuce leaf";
	case FoodType::CarrotPeel: return "carrot peelings";
	case FoodType::CoffeeGrounds: return "coffee grounds";
	case FoodType::Eggshell: return "eggshell";
	default: return "?";
	}
}

const char *wormStateName(WormState s)
{
	switch (s) {
	case WormState::Wander: return "WANDER";
	case WormState::SeekFood: return "SEEK_FOOD";
	case WormState::Feed: return "FEED";
	case WormState::Rest: return "REST";
	default: return "?";
	}
}

// ---- World ------------------------------------------------------------------------------------

void World::generate(const SimConfig &cfg, uint64_t seed)
{
	m_w = cfg.width;
	m_h = cfg.height;
	const size_t n = size_t(m_w) * size_t(m_h);
	m_material.assign(n, 0);
	m_castings.assign(n, 0);
	m_moisture.assign(n, 0);
	m_density.assign(n, 255);
	m_surface.assign(size_t(m_w), 0);
	m_settleAccum = m_dryAccum = m_seepAccum = m_mixAccum = 0;
	Pcg32 rng(seed, 11);
	const uint32_t s0 = rng.next(), s1 = rng.next(), s2 = rng.next(), s3 = rng.next(), s4 = rng.next();
	const float head = cfg.headspaceFraction * float(m_h);
	double avg = 0;
	for (int x = 0; x < m_w; ++x) {
		const float fx = float(x);
		const float y = head + (fbm(fx / 60.0f, 0.5f, s0, 3) - 0.5f) * 2.0f * cfg.surfaceRoughness +
		                (fbm(fx / 9.0f, 3.5f, s1, 2) - 0.5f) * 1.4f;
		m_surface[size_t(x)] = std::max(4, std::min(m_h - 20, int(std::lround(y))));
		avg += m_surface[size_t(x)];
	}
	avg /= m_w;
	const float beddingDepth = float(m_h - avg) * cfg.beddingFraction;
	const float compostDepth = float(m_h - avg) * 0.72f;
	for (int y = 0; y < m_h; ++y)
		for (int x = 0; x < m_w; ++x) {
			const size_t i = size_t(index(x, y));
			const int surf = m_surface[size_t(x)];
			if (y < surf)
				continue;
			const float fx = float(x), fy = float(y);
			const float depth = float(y - surf);
			const float warp = (fbm(fx / 45.0f, fy / 30.0f, s2, 3) - 0.5f) * 26.0f + (fbm(fx / 9.0f, fy / 7.0f, s3 ^ 0x19u, 2) - 0.5f) * 6.0f;
			const float d = depth + warp;
			Material m;
			if (d < beddingDepth)
				m = fbm(fx / 18.0f, fy / 10.0f, s3, 3) > 0.68f ? Material::Compost : Material::Bedding;
			else if (d < compostDepth)
				m = fbm(fx / 26.0f, fy / 9.0f, s4, 3) > 0.72f ? Material::Soil : Material::Compost;
			else
				m = fbm(fx / 30.0f, fy / 12.0f, s4 ^ 0x5Au, 3) > 0.45f ? Material::Soil : Material::Compost;
			m_material[i] = uint8_t(m);
			float moist = 150.0f + depth * 0.5f + (fbm(fx / 35.0f, fy / 20.0f, s1 ^ 0x33u, 3) - 0.5f) * 60.0f;
			if (depth < 4)
				moist *= 0.8f;
			m_moisture[i] = clampByte(moist);
			const float patch = fbm(fx / 22.0f, fy / 14.0f, s0 ^ 0x77u, 3);
			float cast = 0;
			if (m == Material::Compost)
				cast = 40.0f + patch * 60.0f;
			else if (m == Material::Soil)
				cast = 20.0f + patch * 20.0f;
			else
				cast = patch * 14.0f;
			m_castings[i] = clampByte(cast);
			m_density[i] = m == Material::Bedding ? 225 : 255;
		}
	m_castBase = m_castings;
	++m_version;
}

void World::burrow(float x, float y, float radius)
{
	const int r = int(std::ceil(radius));
	bool changed = false;
	for (int dy = -r; dy <= r; ++dy)
		for (int dx = -r; dx <= r; ++dx) {
			const int cx = int(x) + dx, cy = int(y) + dy;
			if (!isSubstrate(cx, cy))
				continue;
			const float d = std::sqrt(float(dx * dx + dy * dy));
			if (d > radius)
				continue;
			const uint8_t target = clampByte(70.0f + 90.0f * d / std::max(0.5f, radius));
			uint8_t &v = m_density[size_t(index(cx, cy))];
			if (v > target) {
				v = target;
				changed = true;
			}
		}
	if (changed)
		++m_version;
}

void World::addCastings(float x, float y, float amount)
{
	const int cx = int(x), cy = int(y);
	if (!isSubstrate(cx, cy))
		return;
	uint8_t &v = m_castings[size_t(index(cx, cy))];
	const uint8_t nv = clampByte(float(v) + amount * 255.0f);
	if (nv != v) {
		v = nv;
		++m_version;
	}
}

void World::addMoisture(float x, float y, float radius, float amount)
{
	const int r = int(std::ceil(radius));
	for (int dy = -r; dy <= r; ++dy)
		for (int dx = -r; dx <= r; ++dx) {
			const int cx = int(x) + dx, cy = int(y) + dy;
			if (!isSubstrate(cx, cy) || float(dx * dx + dy * dy) > radius * radius)
				continue;
			uint8_t &v = m_moisture[size_t(index(cx, cy))];
			v = clampByte(float(v) + amount * 255.0f);
		}
	++m_version;
}

void World::settle(float dt, const SimConfig &cfg)
{
	// Burrows fill in one density step at a time, over about burrowRefillMinutes from open to packed.
	const float step = cfg.burrowRefillMinutes * 60.0f / 185.0f;
	m_settleAccum += dt;
	bool changed = false;
	while (m_settleAccum >= step) {
		m_settleAccum -= step;
		for (size_t i = 0; i < m_density.size(); ++i)
			if (m_material[i] != 0 && m_density[i] < 255) {
				const uint8_t packed = m_material[i] == uint8_t(Material::Bedding) ? 225 : 255;
				if (m_density[i] < packed) {
					++m_density[i];
					changed = true;
				}
			}
	}
	// The top of the bedding dries; water seeps slowly downward.
	m_dryAccum += dt * cfg.evaporationPerHour * 255.0f / 3600.0f;
	while (m_dryAccum >= 1.0f) {
		m_dryAccum -= 1.0f;
		for (int x = 0; x < m_w; ++x)
			for (int k = 0; k < 4; ++k) {
				const int y = m_surface[size_t(x)] + k;
				if (y < m_h && m_moisture[size_t(index(x, y))] > 60)
					--m_moisture[size_t(index(x, y))];
			}
		changed = true;
	}
	// Fresh castings are worked into the substrate: about a level every 10 minutes back towards what was there.
	m_mixAccum += dt;
	if (m_mixAccum >= 600.0f) {
		m_mixAccum -= 600.0f;
		for (size_t i = 0; i < m_castings.size(); ++i)
			if (m_castings[i] > m_castBase[i])
				--m_castings[i];
		changed = true;
	}
	m_seepAccum += dt;
	if (m_seepAccum >= 20.0f) {
		m_seepAccum = 0;
		for (int y = m_h - 2; y >= 0; --y)
			for (int x = 0; x < m_w; ++x) {
				const size_t a = size_t(index(x, y)), b = size_t(index(x, y + 1));
				if (m_material[a] && m_material[b] && m_moisture[a] > m_moisture[b] + 12) {
					--m_moisture[a];
					++m_moisture[b];
				}
			}
		changed = true;
	}
	if (changed)
		++m_version;
}

void World::mist(float amount)
{
	for (int x = 0; x < m_w; ++x)
		for (int k = 0; k < 8; ++k) {
			const int y = m_surface[size_t(x)] + k;
			if (y >= m_h)
				break;
			uint8_t &v = m_moisture[size_t(index(x, y))];
			v = clampByte(float(v) + amount * 255.0f * (1.0f - float(k) / 8.0f));
		}
	++m_version;
}

double World::totalCastings() const
{
	double s = 0;
	for (size_t i = 0; i < m_castings.size(); ++i)
		if (m_material[i])
			s += m_castings[i] / 255.0;
	return s;
}

uint64_t World::checksum() const
{
	uint64_t h = fnvVec(m_material, 1469598103934665603ULL);
	h = fnvVec(m_castings, h);
	h = fnvVec(m_moisture, h);
	h = fnvVec(m_density, h);
	h = fnvVec(m_castBase, h);
	h = fnvVec(m_surface, h);
	h = fnv64(&m_mixAccum, sizeof m_mixAccum, h);
	h = fnv64(&m_settleAccum, sizeof m_settleAccum, h);
	h = fnv64(&m_dryAccum, sizeof m_dryAccum, h);
	return fnv64(&m_seepAccum, sizeof m_seepAccum, h);
}

void World::save(ByteWriter &w) const
{
	w.pod<int32_t>(m_w);
	w.pod<int32_t>(m_h);
	w.vec(m_material);
	w.vec(m_castings);
	w.vec(m_moisture);
	w.vec(m_density);
	w.vec(m_castBase);
	w.vec(m_surface);
	w.pod(m_settleAccum);
	w.pod(m_dryAccum);
	w.pod(m_seepAccum);
	w.pod(m_mixAccum);
}

bool World::load(ByteReader &r)
{
	m_w = r.pod<int32_t>();
	m_h = r.pod<int32_t>();
	r.vec(m_material);
	r.vec(m_castings);
	r.vec(m_moisture);
	r.vec(m_density);
	r.vec(m_castBase);
	r.vec(m_surface);
	m_settleAccum = r.pod<float>();
	m_dryAccum = r.pod<float>();
	m_seepAccum = r.pod<float>();
	m_mixAccum = r.pod<float>();
	const size_t n = size_t(m_w) * size_t(m_h);
	++m_version;
	return r.ok && m_w > 0 && m_h > 0 && m_material.size() == n && m_castings.size() == n && m_moisture.size() == n &&
	       m_density.size() == n && m_castBase.size() == n && m_surface.size() == size_t(m_w);
}

// ---- Simulation -------------------------------------------------------------------------------

bool Simulation::init(const SimConfig &cfg, uint64_t seed)
{
	m_cfg = cfg;
	m_seed = seed;
	m_tick = 0;
	m_rng = Pcg32(deriveSeed(seed, Stream::Behavior), 5);
	m_world.generate(cfg, deriveSeed(seed, Stream::Terrain));
	m_sw = (cfg.width + 1) / 2;
	m_sh = (cfg.height + 1) / 2;
	m_scent.assign(size_t(m_sw * m_sh), 0.0f);
	m_scentTmp.assign(m_scent.size(), 0.0f);
	m_events.assign(size_t(std::max(64, cfg.eventCapacity)), Event{});
	m_eventsWritten = 0;
	m_bw = (cfg.width + kBucket - 1) / kBucket;
	m_bh = (cfg.height + kBucket - 1) / kBucket;
	m_worms.clear();
	m_foods.clear();
	m_cocoons.clear();
	m_nextId = 1;
	m_eaten = 0;
	m_feedings = m_mists = m_hatched = 0;
	m_humidity = 0.3f;
	m_lastSettle = m_lastScent = 0;
	m_nextFeeding = cfg.feedingIntervalHours * 3600.0 * 0.5;
	m_nextMist = cfg.mistIntervalHours * 3600.0;

	const int n = cfg.minWorms + int(m_rng.below(uint32_t(std::max(1, cfg.maxWorms - cfg.minWorms + 1))));
	for (int k = 0; k < n; ++k) {
		for (int tries = 0; tries < 50; ++tries) {
			const float x = m_rng.range(10.0f, float(cfg.width - 10));
			const float y = m_rng.range(float(m_world.surface(int(x)) + 8), float(cfg.height - 6));
			if (m_world.isSubstrate(int(x), int(y))) {
				spawnWorm(x, y, m_rng.chance(0.85f));
				break;
			}
		}
	}
	// Scraps from the last feeding before the worms moved in, already starting to rot.
	for (int k = 0; k < 3; ++k)
		placeFood(FoodType(m_rng.below(uint32_t(FoodType::Eggshell))), m_rng.range(0.15f, 0.85f) * float(cfg.width), m_rng.range(0.15f, 0.5f));
	for (int k = 0; k < 20; ++k)
		updateScent(1.0f);
	m_eventsWritten = 0;
	return true;
}

void Simulation::spawnWorm(float x, float y, bool adult)
{
	Worm w;
	w.id = m_nextId++;
	w.rng = Pcg32(deriveSeed(m_seed, Stream::Behavior, w.id), w.id);
	w.adultLength = w.rng.range(m_cfg.adultLengthMin, m_cfg.adultLengthMax);
	// At the start some worms are half grown; later every non-adult is a fresh hatchling.
	w.grow = adult ? 1.0f : (m_tick == 0 ? w.rng.range(0.2f, 0.9f) : 0.0f);
	w.length = m_cfg.hatchlingLength + (w.adultLength - m_cfg.hatchlingLength) * smoothstep01(w.grow);
	w.heading = w.rng.range(-kPi, kPi);
	w.desired = w.heading;
	w.speedMul = w.rng.range(0.8f, 1.2f);
	w.persistence = w.rng.range(0.8f, 1.2f);
	w.glassAffinity = w.rng.range(0.6f, 1.4f);
	w.noiseSeed = w.rng.next();
	w.homeDepth = std::pow(w.rng.uniform(), 0.8f);
	w.hunger = w.rng.range(0.2f, 0.7f);
	w.restTimer = w.rng.range(300.0f, 2400.0f);
	w.zTarget = w.rng.chance(m_cfg.glassTime * w.glassAffinity) ? w.rng.range(0.0f, 0.12f) : w.rng.range(0.45f, 1.0f);
	w.zTimer = w.rng.range(30.0f, 240.0f);
	// Lay the body out behind the head along a gently curving path that stays in the substrate.
	const float seg = w.length / float(kBodyPoints - 1);
	float bx = x, by = y, dir = w.heading + kPi, bend = w.rng.range(-0.25f, 0.25f);
	for (int i = 0; i < kBodyPoints; ++i) {
		if (i > 0) {
			bend += w.rng.range(-0.12f, 0.12f);
			bend = std::max(-0.35f, std::min(0.35f, bend));
			dir += bend;
			float nx = bx + std::cos(dir) * seg, ny = by + std::sin(dir) * seg;
			if (!m_world.isSubstrate(int(nx), int(ny)) || ny < float(m_world.surface(int(nx))) + 1.0f) {
				dir += kPi * 0.5f; // turn away from the air or the panes
				nx = bx + std::cos(dir) * seg;
				ny = by + std::sin(dir) * seg;
			}
			bx = nx;
			by = ny;
		}
		const float px = std::min(float(m_cfg.width) - 1.5f, std::max(1.5f, bx));
		const float py = std::min(float(m_cfg.height) - 1.5f, std::max(float(m_world.surface(int(px))) + 1.0f, by));
		w.px[i] = w.ox[i] = px;
		w.py[i] = w.oy[i] = py;
		w.pz[i] = w.oz[i] = w.zTarget;
	}
	m_worms.push_back(w);
}

void Simulation::placeFood(FoodType type, float x, float decay)
{
	Food f;
	f.id = m_nextId++;
	f.type = type;
	const FoodTraits &t = traits(type);
	f.radius = m_rng.range(t.radiusMin, t.radiusMax);
	f.x = std::min(float(m_cfg.width) - f.radius - 2.0f, std::max(f.radius + 2.0f, x));
	// Laid on the bedding and pressed in a little: the top edge shows at the surface.
	f.y = float(m_world.surface(int(f.x))) + f.radius * 0.45f + m_rng.range(0.0f, 4.0f);
	f.angle = m_rng.range(-kPi, kPi);
	f.mass0 = t.mass;
	f.mass = t.mass * (1.0f - decay * 0.3f);
	f.decay = decay;
	f.seed = m_rng.next();
	m_foods.push_back(f);
	m_world.addMoisture(f.x, f.y, f.radius, 0.08f);
	emit(EventType::FoodDropped, f.x, f.y, f.mass);
}

void Simulation::feedNow()
{
	const int n = std::max(1, m_cfg.foodPerFeeding + int(m_rng.below(3)) - 1);
	for (int k = 0; k < n; ++k) {
		float x = 0;
		for (int tries = 0; tries < 12; ++tries) {
			x = m_rng.range(0.1f, 0.9f) * float(m_cfg.width);
			bool clear = true;
			for (const Food &f : m_foods)
				if (std::fabs(f.x - x) < f.radius + 24.0f)
					clear = false;
			if (clear)
				break;
		}
		const FoodType t = m_rng.chance(0.12f) ? FoodType::Eggshell : FoodType(m_rng.below(uint32_t(FoodType::Eggshell)));
		placeFood(t, x, 0.0f);
	}
	++m_feedings;
}

void Simulation::mistNow()
{
	m_world.mist(0.12f);
	m_humidity = std::min(1.0f, m_humidity + 0.45f);
	emit(EventType::Mist, float(m_cfg.width) * 0.5f, 0.0f, 1.0f);
	++m_mists;
}

void Simulation::emit(EventType t, float x, float y, float s)
{
	m_events[size_t(m_eventsWritten % m_events.size())] = Event{t, x, y, s};
	++m_eventsWritten;
}

uint64_t Simulation::readEvents(uint64_t from, std::vector<Event> &out, size_t max) const
{
	const uint64_t cap = m_events.size();
	if (m_eventsWritten > cap && from < m_eventsWritten - cap)
		from = m_eventsWritten - cap;
	for (; from < m_eventsWritten && out.size() < max; ++from)
		out.push_back(m_events[size_t(from % cap)]);
	return from;
}

void Simulation::stepN(int n)
{
	for (int i = 0; i < n; ++i)
		step();
}

void Simulation::step()
{
	const float dt = 1.0f / float(m_cfg.ticksPerSecond);
	const double now = seconds();
	for (Worm &w : m_worms) {
		std::memcpy(w.ox, w.px, sizeof w.px);
		std::memcpy(w.oy, w.py, sizeof w.py);
		std::memcpy(w.oz, w.pz, sizeof w.pz);
	}
	rebuildHash();
	for (Food &f : m_foods)
		f.eaters = 0;
	for (size_t i = 0; i < m_worms.size(); ++i)
		updateWorm(m_worms[i], dt);
	updateFoods(dt);
	updateCocoons(dt);
	if (now - m_lastScent >= 1.0) {
		updateScent(float(now - m_lastScent));
		m_lastScent = now;
	}
	if (now - m_lastSettle >= 1.0) {
		const float sdt = float(now - m_lastSettle);
		m_world.settle(sdt, m_cfg);
		m_humidity += (0.28f - m_humidity) * std::min(1.0f, sdt / 1800.0f);
		m_lastSettle = now;
	}
	if (now >= m_nextFeeding) {
		feedNow();
		m_nextFeeding += m_cfg.feedingIntervalHours * 3600.0;
	}
	if (now >= m_nextMist) {
		double top = 0;
		for (int x = 0; x < m_cfg.width; x += 4)
			top += m_world.moisture(x, m_world.surface(x) + 1);
		if (top / double((m_cfg.width + 3) / 4) < 0.62)
			mistNow();
		m_nextMist += m_cfg.mistIntervalHours * 3600.0;
	}
	++m_tick;
}

void Simulation::rebuildHash()
{
	const size_t nb = size_t(m_bw * m_bh);
	m_bucketStart.assign(nb + 1, 0);
	auto bucketOf = [this](const Worm &w) {
		const int bx = std::min(m_bw - 1, std::max(0, int(w.px[0]) / kBucket));
		const int by = std::min(m_bh - 1, std::max(0, int(w.py[0]) / kBucket));
		return by * m_bw + bx;
	};
	for (const Worm &w : m_worms)
		++m_bucketStart[size_t(bucketOf(w)) + 1];
	for (size_t i = 1; i <= nb; ++i)
		m_bucketStart[i] += m_bucketStart[i - 1];
	m_bucketItems.resize(m_worms.size());
	m_bucketFill.assign(m_bucketStart.begin(), m_bucketStart.end() - 1);
	for (size_t i = 0; i < m_worms.size(); ++i)
		m_bucketItems[size_t(m_bucketFill[size_t(bucketOf(m_worms[i]))]++)] = int(i);
}

int Simulation::neighbours(float x, float y, float r, uint32_t self) const
{
	int count = 0;
	const int bx0 = std::max(0, int(x - r) / kBucket), bx1 = std::min(m_bw - 1, int(x + r) / kBucket);
	const int by0 = std::max(0, int(y - r) / kBucket), by1 = std::min(m_bh - 1, int(y + r) / kBucket);
	for (int by = by0; by <= by1; ++by)
		for (int bx = bx0; bx <= bx1; ++bx) {
			const int b = by * m_bw + bx;
			for (int k = m_bucketStart[size_t(b)]; k < m_bucketStart[size_t(b + 1)]; ++k) {
				const Worm &o = m_worms[size_t(m_bucketItems[size_t(k)])];
				if (o.id == self)
					continue;
				const float dx = o.px[0] - x, dy = o.py[0] - y;
				if (dx * dx + dy * dy <= r * r)
					++count;
			}
		}
	return count;
}

Food *Simulation::findFood(int32_t id)
{
	for (Food &f : m_foods)
		if (int32_t(f.id) == id)
			return &f;
	return nullptr;
}

float Simulation::scent(float x, float y) const
{
	const int cx = std::min(m_sw - 1, std::max(0, int(x) / 2)), cy = std::min(m_sh - 1, std::max(0, int(y) / 2));
	return m_scent[size_t(cy * m_sw + cx)];
}

void Simulation::updateWorm(Worm &w, float dt)
{
	w.stateTime += dt;
	w.hunger = std::min(1.0f, w.hunger + dt / (m_cfg.hungerHours * 3600.0f));
	if (w.grow < 1.0f) {
		w.grow = std::min(1.0f, w.grow + dt / (m_cfg.growHours * 3600.0f) * (w.hunger < 0.6f ? 1.2f : 0.6f));
		w.length = m_cfg.hatchlingLength + (w.adultLength - m_cfg.hatchlingLength) * smoothstep01(w.grow);
	}
	// Towards or away from the glass: most of the time a worm is somewhere inside the bin.
	w.zTimer -= dt;
	if (w.zTimer <= 0.0f) {
		w.zTimer = w.rng.range(60.0f, 300.0f);
		const float glass = std::min(0.95f, m_cfg.glassTime * w.glassAffinity * (w.state == WormState::Feed ? 1.6f : 1.0f));
		w.zTarget = w.rng.chance(glass) ? w.rng.range(0.0f, 0.12f) : w.rng.range(0.45f, 1.0f);
	}

	Food *food = w.food >= 0 ? findFood(w.food) : nullptr;
	switch (w.state) {
	case WormState::Feed: {
		if (!food) {
			w.state = WormState::Wander;
			w.stateTime = 0;
			w.food = -1;
			break;
		}
		const float d = std::hypot(food->x - w.px[0], food->y - w.py[0]);
		if (d > food->radius + 4.0f) {
			w.state = WormState::SeekFood;
			w.stateTime = 0;
			break;
		}
		const float size = w.length / 40.0f;
		const float amt = std::min(food->mass, m_cfg.wormEatRate * dt * (0.4f + food->decay) * size);
		food->mass -= amt;
		++food->eaters;
		m_eaten += amt;
		w.hunger = std::max(0.0f, w.hunger - amt * kHungerPerMass / size);
		w.digest += amt * kDigestPerMass;
		if (w.rng.chance(0.03f * dt))
			emit(EventType::Squelch, w.px[0], w.py[0], 0.5f);
		if (w.hunger < 0.05f || w.stateTime > 3600.0f) {
			w.state = WormState::Wander;
			w.stateTime = 0;
			w.food = -1;
			w.restTimer = std::min(w.restTimer, w.rng.range(30.0f, 240.0f));
		}
		break;
	}
	case WormState::SeekFood:
		if (!food || food->decay < 0.12f) {
			w.state = WormState::Wander;
			w.stateTime = 0;
			w.food = -1;
		} else if (std::hypot(food->x - w.px[0], food->y - w.py[0]) < food->radius + 1.0f) {
			w.state = WormState::Feed;
			w.stateTime = 0;
		} else if (w.stateTime > 900.0f) {
			w.state = WormState::Wander; // lost the trail: wander for a while
			w.stateTime = 0;
			w.food = -1;
		}
		break;
	case WormState::Rest:
		w.restTimer -= dt;
		if (w.restTimer <= 0.0f) {
			w.state = WormState::Wander;
			w.stateTime = 0;
			w.restTimer = w.rng.range(900.0f, 3600.0f);
		}
		break;
	case WormState::Wander:
	default:
		w.restTimer -= dt;
		if (w.restTimer <= 0.0f) {
			w.state = WormState::Rest;
			w.stateTime = 0;
			w.restTimer = w.rng.range(300.0f, 1500.0f);
		} else if (w.hunger > 0.45f && (m_tick + w.id) % uint64_t(m_cfg.ticksPerSecond * 5) == 0) {
			// Hungry: head for the nearest rotting scrap it can smell.
			float best = 1e9f;
			int32_t id = -1;
			for (const Food &f : m_foods) {
				if (f.decay < 0.15f)
					continue;
				const float d = std::hypot(f.x - w.px[0], f.y - w.py[0]);
				if (d < 30.0f + 110.0f * f.decay && d < best) {
					best = d;
					id = int32_t(f.id);
				}
			}
			if (id >= 0) {
				w.food = id;
				w.state = WormState::SeekFood;
				w.stateTime = 0;
			}
		}
		break;
	}

	if ((m_tick + w.id) % uint64_t(std::max(1, m_cfg.decisionIntervalTicks)) == 0)
		decide(w);
	moveWorm(w, dt);

	// Cocoons: well-fed adults now and then, fewer as the bin fills up (checked once a minute per worm).
	const uint64_t minute = uint64_t(m_cfg.ticksPerSecond) * 60;
	if (w.grow >= 1.0f && w.hunger < 0.7f && (m_tick + w.id) % minute == 0) {
		const float crowd = 1.0f - float(m_worms.size() + m_cocoons.size() * 2) / float(std::max(1, m_cfg.populationCap));
		if (crowd > 0.0f && w.rng.chance(m_cfg.cocoonsPerAdultPerDay / 1440.0f * crowd * crowd)) {
			Cocoon c;
			c.id = m_nextId++;
			c.x = w.px[7];
			c.y = w.py[7];
			c.hatchAt = m_cfg.hatchHours * 3600.0f * w.rng.range(0.8f, 1.2f);
			c.seed = w.rng.next();
			if (m_world.isSubstrate(int(c.x), int(c.y)))
				m_cocoons.push_back(c);
		}
	}
	if (w.py[0] - float(m_world.surface(int(w.px[0]))) < 5.0f && w.speed > 0.5f && w.pz[0] < 0.3f && w.rng.chance(0.4f * dt))
		emit(EventType::Rustle, w.px[0], w.py[0], std::min(1.0f, w.speed / m_cfg.crawlSpeed));
}

void Simulation::decide(Worm &w)
{
	const float hx = w.px[0], hy = w.py[0];
	Food *food = w.food >= 0 ? findFood(w.food) : nullptr;
	if (w.state == WormState::Feed && food) {
		// Working over the scrap: head for a new spot on it now and then, so feeding worms curl about it.
		const float a = w.rng.range(-kPi, kPi), r = food->radius * std::sqrt(w.rng.uniform()) * 0.8f;
		const float tx = food->x + std::cos(a) * r, ty = std::max(food->y + std::sin(a) * r * 0.6f, float(m_world.surface(int(hx))) + 2.0f);
		w.desired = std::atan2(ty - hy, tx - hx) + w.rng.range(-0.6f, 0.6f);
		return;
	}
	constexpr int kCand = 11;
	float scores[kCand], heads[kCand];
	bool ok[kCand];
	const float variation = (noise1(float(seconds()) * 0.08f + float(w.id) * 3.1f, w.noiseSeed) - 0.5f) * 1.4f;
	const float scentHere = scent(hx, hy);
	int feasible = 0;
	for (int k = 0; k < kCand; ++k) {
		const float h = w.heading + float(k - kCand / 2) * 0.3f;
		heads[k] = h;
		const float ux = std::cos(h), uy = std::sin(h);
		const float px = hx + ux * 3.0f, py = hy + uy * 3.0f;
		const int ipx = int(px), ipy = int(py);
		ok[k] = m_world.isSubstrate(ipx, ipy) && py >= float(m_world.surface(ipx)) + 1.0f && px > 2 && px < m_cfg.width - 2 &&
		        py < m_cfg.height - 2;
		if (!ok[k]) {
			scores[k] = -1e9f;
			continue;
		}
		++feasible;
		float s = std::cos(h - w.heading) * 0.8f * w.persistence;
		if (w.state == WormState::SeekFood && food) {
			const float fx = food->x - hx, fy = food->y - hy, fl = std::max(0.01f, std::hypot(fx, fy));
			s += 0.8f * (ux * fx + uy * fy) / fl;
		} else if (w.hunger > 0.4f) {
			s += std::max(-1.0f, std::min(1.0f, (scent(px, py) - scentHere) * 8.0f));
		}
		s -= std::fabs(m_world.moisture(ipx, ipy) - 0.7f) * 1.2f;
		if (w.state == WormState::Wander || w.state == WormState::Rest) {
			// Away from food each worm drifts back to the depth it likes, so the whole bin stays occupied.
			const float surf = float(m_world.surface(ipx));
			const float home = surf + 6.0f + w.homeDepth * (float(m_cfg.height) - surf - 12.0f);
			s -= std::min(1.0f, std::fabs(py - home) / 40.0f) * 0.5f - std::min(1.0f, std::fabs(hy - home) / 40.0f) * 0.5f;
		}
		const float below = py - float(m_world.surface(ipx));
		if (below < 6.0f && !(food && w.state == WormState::SeekFood))
			s -= (6.0f - below) * 0.15f; // light and dry air above: stay under the surface
		s += (1.0f - m_world.density(ipx, ipy)) * 0.3f;
		const int n = neighbours(px, py, 5.0f, w.id);
		s += w.state == WormState::Rest ? std::min(n, 3) * 0.25f : -float(n) * 0.15f;
		s += 0.35f * std::cos(h - (w.heading + variation));
		scores[k] = s;
	}
	if (feasible == 0) {
		w.desired = w.heading + kPi * w.rng.range(0.6f, 1.0f) * (w.rng.chance(0.5f) ? 1.0f : -1.0f);
		return;
	}
	float mx = -1e9f;
	for (float s : scores)
		mx = std::max(mx, s);
	float sum = 0, weights[kCand];
	for (int k = 0; k < kCand; ++k) {
		weights[k] = ok[k] ? std::exp((scores[k] - mx) / 0.3f) : 0.0f;
		sum += weights[k];
	}
	float r = w.rng.uniform() * sum;
	int pick = kCand / 2;
	for (int k = 0; k < kCand; ++k) {
		if (!ok[k])
			continue;
		pick = k;
		if (r < weights[k])
			break;
		r -= weights[k];
	}
	w.desired = heads[pick];
}

void Simulation::moveWorm(Worm &w, float dt)
{
	const float turn = wrapAngle(w.desired - w.heading);
	w.heading = wrapAngle(w.heading + std::max(-m_cfg.turnRate * dt, std::min(m_cfg.turnRate * dt, turn)));
	// Worms meander as they go: a slow drift of the heading (smooth noise, per worm).
	const float meander = (noise1(float(seconds()) * 0.12f + float(w.id) * 7.3f, w.noiseSeed ^ 0x3Cu) - 0.5f) * 2.0f;
	w.heading = wrapAngle(w.heading + meander * m_cfg.turnRate * 0.6f * dt * std::min(1.0f, w.speed / (0.3f * m_cfg.crawlSpeed)));
	const int hx = int(w.px[0]), hy = int(w.py[0]);
	float stateFactor = 0.7f;
	switch (w.state) {
	case WormState::Rest: stateFactor = 0.06f; break;
	case WormState::Feed: stateFactor = 0.3f; break;
	case WormState::SeekFood: stateFactor = 1.0f; break;
	default: break;
	}
	float matFactor = 1.0f;
	switch (m_world.material(hx, hy)) {
	case Material::Compost: matFactor = 0.8f; break;
	case Material::Soil: matFactor = 0.62f; break;
	default: break;
	}
	const float target = m_cfg.crawlSpeed * w.speedMul * (0.45f + 0.55f * w.grow) * stateFactor * matFactor *
	                     (0.7f + 0.3f * (1.0f - m_world.density(hx, hy)));
	w.speed += std::max(-3.0f * dt, std::min(3.0f * dt, target - w.speed));
	const float step = w.speed * dt;
	if (step > 1e-5f) {
		const float nx = w.px[0] + std::cos(w.heading) * step, ny = w.py[0] + std::sin(w.heading) * step;
		const bool free = m_world.isSubstrate(int(nx), int(ny)) && ny >= float(m_world.surface(int(nx))) + 0.8f && nx > 1.5f &&
		                  nx < float(m_cfg.width) - 1.5f && ny < float(m_cfg.height) - 1.5f;
		if (!free) {
			w.desired = w.heading + kPi * w.rng.range(0.5f, 1.0f) * (w.rng.chance(0.5f) ? 1.0f : -1.0f);
			w.speed *= 0.5f;
		} else {
			w.px[0] = nx;
			w.py[0] = ny;
			w.phase = std::fmod(w.phase + step / std::max(1.0f, w.length * 0.22f) * kTwoPi, kTwoPi * 256.0f);
		}
	}
	// The body follows the head like a rope: each point keeps its spacing behind the one before it.
	const float seg = w.length / float(kBodyPoints - 1);
	float moved = 0;
	for (int i = 1; i < kBodyPoints; ++i) {
		const float dx = w.px[i] - w.px[i - 1], dy = w.py[i] - w.py[i - 1];
		const float d = std::sqrt(dx * dx + dy * dy);
		if (d > 1e-4f) {
			const float nx = w.px[i - 1] + dx / d * seg, ny = w.py[i - 1] + dy / d * seg;
			moved += std::fabs(nx - w.px[i]) + std::fabs(ny - w.py[i]);
			w.px[i] = std::min(float(m_cfg.width) - 0.5f, std::max(0.5f, nx));
			w.py[i] = std::min(float(m_cfg.height) - 0.5f, std::max(float(m_world.surface(int(w.px[i]))) + 0.5f, ny));
		}
	}
	// Depth: the head leads, the body follows as it crawls.
	const float zRate = (w.speed > 0.1f ? 0.05f : 0.015f) * dt;
	w.pz[0] += std::max(-zRate, std::min(zRate, w.zTarget - w.pz[0]));
	const float follow = std::min(1.0f, (step + moved / float(kBodyPoints)) / std::max(0.2f, seg) * 1.5f);
	for (int i = 1; i < kBodyPoints; ++i)
		w.pz[i] += (w.pz[i - 1] - w.pz[i]) * follow;
	// Pressed against the glass, the worm leaves a visible burrow; while digesting it leaves castings behind.
	if (w.pz[0] < 0.1f && step > 1e-4f)
		m_world.burrow(w.px[0], w.py[0], m_cfg.bodyRadius * (0.55f + 0.45f * w.grow));
	if (step > 1e-4f) {
		// Worms eat bedding as they go too; what they have eaten leaves the tail as a casting every few cells.
		if (m_world.material(hx, hy) == Material::Bedding)
			w.digest += step * 0.004f;
		w.castAccum += step;
		if (w.digest >= 1.0f && w.castAccum >= 5.0f) {
			w.digest -= 1.0f;
			w.castAccum = 0.0f;
			const int t = kBodyPoints - 1;
			m_world.addCastings(w.px[t], w.py[t], m_cfg.castingsPerCell * (1.0f - 0.5f * w.pz[t]));
		}
	}
}

void Simulation::updateFoods(float dt)
{
	for (Food &f : m_foods) {
		const FoodTraits &t = traits(f.type);
		const float moist = m_world.moisture(int(f.x), int(f.y));
		f.decay = std::min(1.0f, f.decay + dt / (m_cfg.decayHours * 3600.0f) * t.decayRate * (0.6f + 0.8f * moist));
		f.mass -= f.mass0 * 0.000012f * f.decay * dt; // microbes and moulds take their share too
	}
	for (size_t i = 0; i < m_foods.size();) {
		Food &f = m_foods[i];
		if (f.mass <= f.mass0 * 0.03f) {
			// All gone: a dark, crumbly patch of fresh castings where it lay.
			for (int k = 0; k < 24; ++k) {
				const float a = m_rng.range(-kPi, kPi), r = m_rng.range(0.0f, f.radius);
				m_world.addCastings(f.x + std::cos(a) * r, f.y + std::sin(a) * r, 0.12f);
			}
			m_foods.erase(m_foods.begin() + std::ptrdiff_t(i));
			continue;
		}
		++i;
	}
}

void Simulation::updateScent(float dt)
{
	const float decay = std::exp2(-dt / std::max(1.0f, m_cfg.scentHalfLife));
	for (const Food &f : m_foods) {
		const float amount = f.mass * std::pow(f.decay, 0.8f) * dt * 0.08f;
		const int r = std::max(1, int(f.radius / 4.0f));
		const int cx = int(f.x) / 2, cy = int(f.y) / 2;
		for (int dy = -r; dy <= r; ++dy)
			for (int dx = -r; dx <= r; ++dx) {
				const int x = cx + dx, y = cy + dy;
				if (x >= 0 && y >= 0 && x < m_sw && y < m_sh)
					m_scent[size_t(y * m_sw + x)] += amount / float((2 * r + 1) * (2 * r + 1));
			}
	}
	const float a = std::min(0.2f, m_cfg.scentDiffusion * std::min(dt, 1.0f));
	for (int y = 0; y < m_sh; ++y)
		for (int x = 0; x < m_sw; ++x) {
			const size_t i = size_t(y * m_sw + x);
			if (!m_world.isSubstrate(x * 2, y * 2 + 1)) {
				m_scentTmp[i] = 0.0f;
				continue;
			}
			const float c = m_scent[i];
			float flux = 0;
			if (x > 0)
				flux += m_scent[i - 1] - c;
			if (x + 1 < m_sw)
				flux += m_scent[i + 1] - c;
			if (y > 0)
				flux += m_scent[i - size_t(m_sw)] - c;
			if (y + 1 < m_sh)
				flux += m_scent[i + size_t(m_sw)] - c;
			const float v = (c + a * flux) * decay;
			m_scentTmp[i] = v < 1e-5f ? 0.0f : std::min(v, 10.0f);
		}
	m_scent.swap(m_scentTmp);
}

void Simulation::updateCocoons(float dt)
{
	for (size_t i = 0; i < m_cocoons.size();) {
		Cocoon &c = m_cocoons[i];
		c.age += dt;
		if (c.age >= c.hatchAt) {
			Pcg32 r(c.seed, 3);
			const int n = 1 + int(r.below(2));
			const float x = c.x, y = c.y;
			m_cocoons.erase(m_cocoons.begin() + std::ptrdiff_t(i));
			for (int k = 0; k < n; ++k)
				spawnWorm(x, y, false);
			m_hatched += n;
			emit(EventType::Hatch, x, y, float(n));
			continue;
		}
		++i;
	}
}

Stats Simulation::stats() const
{
	Stats s;
	s.seconds = seconds();
	s.worms = int(m_worms.size());
	for (const Worm &w : m_worms) {
		++s.byState[size_t(w.state)];
		(w.grow >= 1.0f ? s.adults : s.juveniles)++;
		if (w.pz[0] < 0.2f)
			++s.atGlass;
	}
	s.feeding = s.byState[size_t(WormState::Feed)];
	s.cocoons = int(m_cocoons.size());
	s.foods = int(m_foods.size());
	for (const Food &f : m_foods)
		s.foodMass += f.mass;
	s.eaten = m_eaten;
	s.castings = m_world.totalCastings();
	s.feedings = m_feedings;
	s.mists = m_mists;
	s.hatched = m_hatched;
	return s;
}

uint64_t Simulation::stateHash() const
{
	uint64_t h = fnv64(&m_tick, sizeof m_tick);
	h = fnv64(&m_seed, sizeof m_seed, h);
	h = fnv64(&m_rng, sizeof m_rng, h);
	h = fnvVec(m_worms, h);
	h = fnvVec(m_foods, h);
	h = fnvVec(m_cocoons, h);
	h = fnvVec(m_scent, h);
	const uint64_t wc = m_world.checksum();
	h = fnv64(&wc, sizeof wc, h);
	h = fnv64(&m_nextId, sizeof m_nextId, h);
	h = fnv64(&m_nextFeeding, sizeof m_nextFeeding, h);
	h = fnv64(&m_nextMist, sizeof m_nextMist, h);
	h = fnv64(&m_humidity, sizeof m_humidity, h);
	return fnv64(&m_eaten, sizeof m_eaten, h);
}

size_t Simulation::memoryFootprint() const
{
	return m_worms.capacity() * sizeof(Worm) + m_foods.capacity() * sizeof(Food) + m_cocoons.capacity() * sizeof(Cocoon) +
	       (m_scent.capacity() + m_scentTmp.capacity()) * sizeof(float) + m_events.capacity() * sizeof(Event) +
	       m_world.materials().capacity() * 4;
}

std::vector<uint8_t> Simulation::saveCheckpoint() const
{
	ByteWriter w;
	w.data.insert(w.data.end(), {'S', 'W', 'F', 'C', 'K', 'P', 'T', '1'});
	w.pod<uint32_t>(kCheckpointVersion);
	w.pod<uint32_t>(uint32_t(sizeof(Worm)));
	w.str(m_cfg.dump());
	w.pod(m_cfg.hash());
	w.pod(m_seed);
	w.pod(m_tick);
	m_world.save(w);
	// Foods carry a derived per-tick field (eaters); it is saved as is and recomputed every tick.
	w.vec(m_worms);
	w.vec(m_foods);
	w.vec(m_cocoons);
	w.vec(m_scent);
	w.pod(m_rng);
	w.pod(m_nextId);
	w.pod(m_nextFeeding);
	w.pod(m_nextMist);
	w.pod(m_lastSettle);
	w.pod(m_lastScent);
	w.pod(m_humidity);
	w.pod(m_eaten);
	w.pod(m_feedings);
	w.pod(m_mists);
	w.pod(m_hatched);
	w.pod<uint64_t>(stateHash());
	w.pod<uint64_t>(fnv64(w.data.data(), w.data.size()));
	return w.data;
}

bool Simulation::loadCheckpoint(const std::vector<uint8_t> &data, std::string *error)
{
	auto fail = [&](const char *why) {
		if (error)
			*error = why;
		return false;
	};
	if (data.size() < 24 || std::memcmp(data.data(), "SWFCKPT1", 8) != 0)
		return fail("not a Shadow Worm Farm checkpoint");
	uint64_t stored = 0;
	std::memcpy(&stored, data.data() + data.size() - 8, 8);
	if (stored != fnv64(data.data(), data.size() - 8))
		return fail("checkpoint is damaged (checksum mismatch)");
	ByteReader r(data.data() + 8, data.size() - 16);
	if (r.pod<uint32_t>() != kCheckpointVersion)
		return fail("checkpoint version is not supported by this build");
	if (r.pod<uint32_t>() != sizeof(Worm))
		return fail("checkpoint was written by an incompatible build");
	const std::string cfgText = r.str();
	const uint64_t cfgHash = r.pod<uint64_t>();
	SimConfig cfg;
	for (size_t pos = 0; pos < cfgText.size();) {
		const size_t nl = cfgText.find('\n', pos);
		const std::string line = cfgText.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		pos = nl == std::string::npos ? cfgText.size() : nl + 1;
		const auto eq = line.find(" = ");
		if (eq != std::string::npos)
			cfg.set(line.substr(0, eq), line.substr(eq + 3));
	}
	if (cfg.hash() != cfgHash)
		return fail("checkpoint configuration is corrupt");
	Simulation s;
	s.m_cfg = cfg;
	s.m_seed = r.pod<uint64_t>();
	s.m_tick = r.pod<uint64_t>();
	if (!s.m_world.load(r))
		return fail("checkpoint substrate is corrupt");
	r.vec(s.m_worms, 100000);
	r.vec(s.m_foods, 10000);
	r.vec(s.m_cocoons, 100000);
	r.vec(s.m_scent);
	s.m_rng = r.pod<Pcg32>();
	s.m_nextId = r.pod<uint32_t>();
	s.m_nextFeeding = r.pod<double>();
	s.m_nextMist = r.pod<double>();
	s.m_lastSettle = r.pod<double>();
	s.m_lastScent = r.pod<double>();
	s.m_humidity = r.pod<float>();
	s.m_eaten = r.pod<double>();
	s.m_feedings = r.pod<int>();
	s.m_mists = r.pod<int>();
	s.m_hatched = r.pod<int>();
	const uint64_t hash = r.pod<uint64_t>();
	if (!r.ok)
		return fail("checkpoint is truncated");
	s.m_sw = (cfg.width + 1) / 2;
	s.m_sh = (cfg.height + 1) / 2;
	if (s.m_scent.size() != size_t(s.m_sw * s.m_sh))
		return fail("checkpoint scent data is corrupt");
	s.m_scentTmp.assign(s.m_scent.size(), 0.0f);
	s.m_events.assign(size_t(std::max(64, cfg.eventCapacity)), Event{});
	s.m_bw = (cfg.width + kBucket - 1) / kBucket;
	s.m_bh = (cfg.height + kBucket - 1) / kBucket;
	if (s.stateHash() != hash)
		return fail("checkpoint failed its integrity check");
	*this = std::move(s);
	return true;
}

} // namespace wormfarm
