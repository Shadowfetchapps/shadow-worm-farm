#include "worm_farm_sim.h"

#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/random.h>
#include <unistd.h>

using namespace godot;
using namespace wormfarm;

namespace {

uint64_t osRandomSeed()
{
	uint64_t v = 0;
	if (getrandom(&v, sizeof v, 0) != sizeof v) {
		if (std::FILE *f = std::fopen("/dev/urandom", "rb")) {
			if (std::fread(&v, sizeof v, 1, f) != 1)
				v = 0;
			std::fclose(f);
		}
	}
	// Keep seeds below 2^63 so they survive Godot's signed 64-bit integers unchanged.
	v &= 0x7FFFFFFFFFFFFFFFull;
	return v ? v : 0x1E3779B97F4A7C15ull;
}

std::string toStd(const String &s)
{
	const CharString c = s.utf8();
	return std::string(c.get_data(), size_t(c.length()));
}

/// Body radius along the worm (u: 0 head … 1 tail): a pointed head, full girth behind it, the swollen
/// clitellum on adults, a slightly flattened, tapering tail.
float bodyProfile(float u, float grow)
{
	float r;
	if (u < 0.08f)
		r = 0.35f + 0.65f * std::sqrt(u / 0.08f);
	else if (u < 0.75f)
		r = 1.0f;
	else
		r = 1.0f - 0.55f * std::pow((u - 0.75f) / 0.25f, 1.4f);
	if (grow >= 1.0f) {
		const float c = (u - 0.29f) / 0.045f;
		r += 0.14f * std::exp(-c * c);
	}
	return r;
}

} // namespace

bool WormFarmSim::start_new(int64_t seed, const String &config_path)
{
	SimConfig cfg;
	if (!config_path.is_empty()) {
		std::string err;
		if (!cfg.loadFile(toStd(config_path), &err)) {
			m_error = String::utf8(err.c_str());
			return false;
		}
	}
	auto sim = std::make_unique<Simulation>();
	const uint64_t s = seed == 0 ? osRandomSeed() : uint64_t(seed);
	if (!sim->init(cfg, s)) {
		m_error = "could not set up the bin";
		return false;
	}
	m_sim = std::move(sim);
	m_accum = 0;
	m_droppedTicks = 0;
	m_eventCursor = m_sim->eventsWritten();
	m_terrainVersion = ~uint64_t(0);
	return true;
}

bool WormFarmSim::load_checkpoint(const String &path)
{
	std::FILE *f = std::fopen(toStd(path).c_str(), "rb");
	if (!f) {
		m_error = "checkpoint not found";
		return false;
	}
	std::vector<uint8_t> data;
	uint8_t buf[1 << 16];
	for (size_t got; (got = std::fread(buf, 1, sizeof buf, f)) > 0;)
		data.insert(data.end(), buf, buf + got);
	std::fclose(f);
	auto sim = std::make_unique<Simulation>();
	std::string err;
	if (!sim->loadCheckpoint(data, &err)) {
		m_error = String::utf8(err.c_str());
		return false;
	}
	m_sim = std::move(sim);
	m_accum = 0;
	m_eventCursor = m_sim->eventsWritten();
	m_terrainVersion = ~uint64_t(0);
	return true;
}

bool WormFarmSim::save_checkpoint(const String &path) const
{
	if (!m_sim)
		return false;
	const std::vector<uint8_t> data = m_sim->saveCheckpoint();
	const std::string final = toStd(path);
	const std::string tmp = final + ".tmp";
	const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0)
		return false;
	size_t off = 0;
	while (off < data.size()) {
		const ssize_t w = ::write(fd, data.data() + off, data.size() - off);
		if (w <= 0) {
			::close(fd);
			::unlink(tmp.c_str());
			return false;
		}
		off += size_t(w);
	}
	const bool synced = ::fsync(fd) == 0;
	::close(fd);
	if (!synced || std::rename(tmp.c_str(), final.c_str()) != 0) {
		::unlink(tmp.c_str());
		return false;
	}
	const size_t slash = final.find_last_of('/');
	if (slash != std::string::npos) {
		const int dfd = ::open(final.substr(0, slash).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (dfd >= 0) {
			::fsync(dfd);
			::close(dfd);
		}
	}
	return true;
}

int WormFarmSim::advance(double real_dt, int max_ticks)
{
	if (!m_sim)
		return 0;
	const double tps = m_sim->config().ticksPerSecond;
	// Gaps longer than a couple of seconds (system suspend, a stalled display) are not replayed at once;
	// the skipped time is counted instead.
	if (real_dt > 2.5)
		m_droppedTicks += int64_t((real_dt - 2.5) * tps);
	m_accum += std::clamp(real_dt, 0.0, 2.5);
	int n = int(std::floor(m_accum * tps));
	if (n > max_ticks) {
		m_droppedTicks += n - max_ticks;
		n = max_ticks;
		m_accum = 0.0;
	} else {
		m_accum -= double(n) / tps;
	}
	for (int i = 0; i < n; ++i)
		m_sim->step();
	return n;
}

int WormFarmSim::step_ticks(int n)
{
	if (!m_sim)
		return 0;
	for (int i = 0; i < n; ++i)
		m_sim->step();
	return n;
}

double WormFarmSim::get_alpha() const
{
	return m_sim ? std::clamp(m_accum * m_sim->config().ticksPerSecond, 0.0, 1.0) : 0.0;
}

double WormFarmSim::get_sim_seconds() const { return m_sim ? m_sim->seconds() : 0.0; }
int64_t WormFarmSim::get_tick() const { return m_sim ? int64_t(m_sim->tick()) : 0; }
int64_t WormFarmSim::get_seed() const { return m_sim ? int64_t(m_sim->seed()) : 0; }
int WormFarmSim::get_ticks_per_second() const { return m_sim ? m_sim->config().ticksPerSecond : 30; }
int WormFarmSim::get_max_worms() const { return m_sim ? std::max(m_sim->config().populationCap, m_sim->config().maxWorms) + 16 : 0; }
int WormFarmSim::get_worm_count() const { return m_sim ? int(m_sim->worms().size()) : 0; }
double WormFarmSim::get_humidity() const { return m_sim ? m_sim->humidity() : 0.0; }

Vector2i WormFarmSim::get_grid_size() const
{
	return m_sim ? Vector2i(m_sim->world().width(), m_sim->world().height()) : Vector2i();
}

void WormFarmSim::feed_now()
{
	if (m_sim)
		m_sim->feedNow();
}

void WormFarmSim::mist_now()
{
	if (m_sim)
		m_sim->mistNow();
}

PackedByteArray WormFarmSim::take_terrain_update(bool force)
{
	PackedByteArray out;
	if (!m_sim)
		return out;
	const World &w = m_sim->world();
	if (!force && w.version() == m_terrainVersion)
		return out;
	m_terrainVersion = w.version();
	const size_t n = w.materials().size();
	out.resize(int64_t(n * 4));
	uint8_t *o = out.ptrw();
	const auto &mat = w.materials();
	const auto &cast = w.castings();
	const auto &moist = w.moistures();
	const auto &dens = w.densities();
	for (size_t i = 0; i < n; ++i) {
		o[i * 4 + 0] = uint8_t(mat[i] * 64);
		o[i * 4 + 1] = cast[i];
		o[i * 4 + 2] = moist[i];
		o[i * 4 + 3] = mat[i] ? dens[i] : 0;
	}
	return out;
}

PackedFloat32Array WormFarmSim::build_body_data(double alpha, double cell_size, int rows)
{
	PackedFloat32Array out;
	if (!m_sim)
		return out;
	const auto &worms = m_sim->worms();
	const SimConfig &cfg = m_sim->config();
	const float W = float(m_sim->world().width()), H = float(m_sim->world().height());
	const float cs = float(cell_size), al = float(std::clamp(alpha, 0.0, 1.0));
	out.resize(int64_t(rows) * kBodyPoints * 4);
	float *o = out.ptrw();
	std::memset(o, 0, size_t(out.size()) * sizeof(float));
	const int count = std::min(rows, int(worms.size()));
	for (int k = 0; k < count; ++k) {
		const Worm &wm = worms[size_t(k)];
		const float girth = cfg.bodyRadius * (0.45f + 0.55f * wm.grow) * cs;
		float *row = o + size_t(k) * kBodyPoints * 4;
		for (int i = 0; i < kBodyPoints; ++i) {
			const float x = wm.ox[i] + (wm.px[i] - wm.ox[i]) * al;
			const float y = wm.oy[i] + (wm.py[i] - wm.oy[i]) * al;
			const float z = wm.oz[i] + (wm.pz[i] - wm.oz[i]) * al;
			row[i * 4 + 0] = (x - W * 0.5f) * cs;
			row[i * 4 + 1] = (H * 0.5f - y) * cs;
			row[i * 4 + 2] = z;
			row[i * 4 + 3] = girth * bodyProfile(float(i) / float(kBodyPoints - 1), wm.grow);
		}
	}
	return out;
}

PackedFloat32Array WormFarmSim::build_worm_instances(int rows)
{
	PackedFloat32Array out;
	if (!m_sim)
		return out;
	const auto &worms = m_sim->worms();
	const SimConfig &cfg = m_sim->config();
	out.resize(int64_t(rows) * 16);
	float *o = out.ptrw();
	const float t[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
	for (int k = 0; k < rows; ++k) {
		std::memcpy(o + size_t(k) * 16, t, sizeof t);
		std::memset(o + size_t(k) * 16 + 12, 0, 4 * sizeof(float));
	}
	const int count = std::min(rows, int(worms.size()));
	for (int k = 0; k < count; ++k) {
		const Worm &wm = worms[size_t(k)];
		o[12] = wm.grow;
		o[13] = std::fmod(wm.phase, 6.28318530718f * 64.0f);
		o[14] = float((wm.id * 2654435761u) >> 8) / float(1u << 24);
		o[15] = std::clamp(wm.speed / cfg.crawlSpeed, 0.0f, 1.5f) + (wm.state == WormState::Feed ? 2.0f : 0.0f);
		o += 16;
	}
	return out;
}

PackedFloat32Array WormFarmSim::get_foods() const
{
	PackedFloat32Array out;
	if (!m_sim)
		return out;
	for (const Food &f : m_sim->foods()) {
		const float v[9] = {f.x, f.y, f.radius, f.angle, float(int(f.type)), f.decay, f.mass / std::max(1e-6f, f.mass0),
		                    float(f.seed >> 8) / float(1u << 24), float(f.eaters)};
		for (float x : v)
			out.push_back(x);
	}
	return out;
}

PackedFloat32Array WormFarmSim::get_cocoons() const
{
	PackedFloat32Array out;
	if (!m_sim)
		return out;
	for (const Cocoon &c : m_sim->cocoons()) {
		out.push_back(c.x);
		out.push_back(c.y);
		out.push_back(std::clamp(c.age / std::max(1.0f, c.hatchAt), 0.0f, 1.0f));
		out.push_back(float(c.seed >> 8) / float(1u << 24));
	}
	return out;
}

PackedFloat32Array WormFarmSim::poll_events(int max_events)
{
	PackedFloat32Array out;
	if (!m_sim)
		return out;
	m_eventScratch.clear();
	m_eventCursor = m_sim->readEvents(m_eventCursor, m_eventScratch, size_t(std::max(0, max_events)));
	out.resize(int64_t(m_eventScratch.size() * 4));
	float *o = out.ptrw();
	for (const Event &e : m_eventScratch) {
		o[0] = float(int(e.type));
		o[1] = e.x;
		o[2] = e.y;
		o[3] = e.strength;
		o += 4;
	}
	return out;
}

Dictionary WormFarmSim::get_stats() const
{
	Dictionary d;
	if (!m_sim)
		return d;
	const Stats s = m_sim->stats();
	d["tick"] = int64_t(m_sim->tick());
	d["seconds"] = s.seconds;
	d["worms"] = s.worms;
	d["adults"] = s.adults;
	d["juveniles"] = s.juveniles;
	d["at_glass"] = s.atGlass;
	d["feeding"] = s.feeding;
	d["cocoons"] = s.cocoons;
	d["foods"] = s.foods;
	d["food_mass"] = s.foodMass;
	d["eaten"] = s.eaten;
	d["castings"] = s.castings;
	d["feedings"] = s.feedings;
	d["mists"] = s.mists;
	d["hatched"] = s.hatched;
	d["humidity"] = m_sim->humidity();
	d["dropped_ticks"] = m_droppedTicks;
	Dictionary states;
	for (int k = 0; k < int(WormState::Count); ++k)
		states[String(wormStateName(WormState(k)))] = s.byState[size_t(k)];
	d["states"] = states;
	return d;
}

String WormFarmSim::get_config_dump() const
{
	return m_sim ? String::utf8(m_sim->config().dump().c_str()) : String();
}

String WormFarmSim::get_state_hash() const
{
	if (!m_sim)
		return String();
	char buf[24];
	std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)m_sim->stateHash());
	return String(buf);
}

int64_t WormFarmSim::get_memory_bytes() const { return m_sim ? int64_t(m_sim->memoryFootprint()) : 0; }

void WormFarmSim::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("start_new", "seed", "config_path"), &WormFarmSim::start_new);
	ClassDB::bind_method(D_METHOD("load_checkpoint", "path"), &WormFarmSim::load_checkpoint);
	ClassDB::bind_method(D_METHOD("save_checkpoint", "path"), &WormFarmSim::save_checkpoint);
	ClassDB::bind_method(D_METHOD("get_last_error"), &WormFarmSim::get_last_error);
	ClassDB::bind_method(D_METHOD("is_running"), &WormFarmSim::is_running);
	ClassDB::bind_method(D_METHOD("advance", "real_dt", "max_ticks"), &WormFarmSim::advance);
	ClassDB::bind_method(D_METHOD("step_ticks", "n"), &WormFarmSim::step_ticks);
	ClassDB::bind_method(D_METHOD("get_alpha"), &WormFarmSim::get_alpha);
	ClassDB::bind_method(D_METHOD("get_sim_seconds"), &WormFarmSim::get_sim_seconds);
	ClassDB::bind_method(D_METHOD("get_tick"), &WormFarmSim::get_tick);
	ClassDB::bind_method(D_METHOD("get_seed"), &WormFarmSim::get_seed);
	ClassDB::bind_method(D_METHOD("get_dropped_ticks"), &WormFarmSim::get_dropped_ticks);
	ClassDB::bind_method(D_METHOD("get_grid_size"), &WormFarmSim::get_grid_size);
	ClassDB::bind_method(D_METHOD("get_ticks_per_second"), &WormFarmSim::get_ticks_per_second);
	ClassDB::bind_method(D_METHOD("get_body_points"), &WormFarmSim::get_body_points);
	ClassDB::bind_method(D_METHOD("get_max_worms"), &WormFarmSim::get_max_worms);
	ClassDB::bind_method(D_METHOD("feed_now"), &WormFarmSim::feed_now);
	ClassDB::bind_method(D_METHOD("mist_now"), &WormFarmSim::mist_now);
	ClassDB::bind_method(D_METHOD("take_terrain_update", "force"), &WormFarmSim::take_terrain_update);
	ClassDB::bind_method(D_METHOD("build_body_data", "alpha", "cell_size", "rows"), &WormFarmSim::build_body_data);
	ClassDB::bind_method(D_METHOD("build_worm_instances", "rows"), &WormFarmSim::build_worm_instances);
	ClassDB::bind_method(D_METHOD("get_worm_count"), &WormFarmSim::get_worm_count);
	ClassDB::bind_method(D_METHOD("get_foods"), &WormFarmSim::get_foods);
	ClassDB::bind_method(D_METHOD("get_cocoons"), &WormFarmSim::get_cocoons);
	ClassDB::bind_method(D_METHOD("get_humidity"), &WormFarmSim::get_humidity);
	ClassDB::bind_method(D_METHOD("poll_events", "max_events"), &WormFarmSim::poll_events);
	ClassDB::bind_method(D_METHOD("get_stats"), &WormFarmSim::get_stats);
	ClassDB::bind_method(D_METHOD("get_config_dump"), &WormFarmSim::get_config_dump);
	ClassDB::bind_method(D_METHOD("get_state_hash"), &WormFarmSim::get_state_hash);
	ClassDB::bind_method(D_METHOD("get_memory_bytes"), &WormFarmSim::get_memory_bytes);
}
