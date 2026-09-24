// Native tests of the simulation core (the same code the visible application runs through the GDExtension).
// Accelerated runs process ordinary fixed ticks faster than real time; the timestep never changes.
//
//   wormfarm_tests                 run everything (pacing uses 2 seeds × 24 simulated hours)
//   wormfarm_tests <name> ...      run selected tests
//   WORMFARM_PACING_SEEDS=4        more seeds for the pacing test

#include "wormfarm/sim.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace wormfarm;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                           \
		if (!(cond)) {                                                                                         \
			std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                  \
			++g_failures;                                                                                  \
			return;                                                                                        \
		}                                                                                                      \
	} while (0)

#define NOTE(...)                                                                                                      \
	do {                                                                                                           \
		std::printf("  ▸ ");                                                                                   \
		std::printf(__VA_ARGS__);                                                                              \
		std::printf("\n");                                                                                     \
	} while (0)

constexpr int kTps = 30;
int ticksFor(double seconds) { return int(seconds * kTps); }

SimConfig defaultConfig()
{
	SimConfig c;
	c.ticksPerSecond = kTps;
	return c;
}

// ---- tests -------------------------------------------------------------------------------------

void freshSeeds()
{
	std::set<uint64_t> worlds;
	std::set<size_t> counts;
	for (uint64_t seed : {1ULL, 2ULL, 0xDEADBEEFULL, 123456789ULL, 987654321987ULL}) {
		Simulation s;
		s.init(defaultConfig(), seed);
		worlds.insert(s.world().checksum());
		counts.insert(s.worms().size());
		CHECK(s.worms().size() >= 90 && s.worms().size() <= 150);
		CHECK(!s.foods().empty());
	}
	CHECK(worlds.size() == 5);
	CHECK(counts.size() >= 3);
	NOTE("5 seeds -> 5 distinct bins, %zu distinct population sizes", counts.size());
}

void determinism()
{
	Simulation a, b, c;
	a.init(defaultConfig(), 42);
	b.init(defaultConfig(), 42);
	c.init(defaultConfig(), 43);
	a.stepN(ticksFor(900));
	b.stepN(ticksFor(900));
	c.stepN(ticksFor(900));
	CHECK(a.stateHash() == b.stateHash());
	CHECK(c.stateHash() != a.stateHash());
	NOTE("same seed, 15 simulated minutes: identical state hash %016llx; different seed differs", (unsigned long long)a.stateHash());
}

void frameIndependence()
{
	// The visible app steps a variable number of fixed ticks per rendered frame (1 or 2 at 60 Hz, several after
	// a hitch). The outcome must not depend on how ticks are batched.
	Simulation a, b, c;
	a.init(defaultConfig(), 7);
	b.init(defaultConfig(), 7);
	c.init(defaultConfig(), 7);
	const int total = ticksFor(300);
	for (int i = 0; i < total; ++i)
		a.step();
	for (int done = 0; done < total;) {
		const int n = std::min(total - done, 1 + (done / 7) % 5);
		b.stepN(n);
		done += n;
	}
	for (int done = 0; done < total;) {
		const int n = std::min(total - done, 60);
		c.stepN(n);
		done += n;
	}
	CHECK(a.stateHash() == b.stateHash());
	CHECK(a.stateHash() == c.stateHash());
	NOTE("1-tick, varied and 60-tick batches give the same state after 5 simulated minutes");
}

void wormsStayInTheBin()
{
	// Every tick for 20 simulated minutes: heads are in the substrate under the surface, every body point is inside
	// the panes, body segments keep their spacing, depth stays within the bin.
	Simulation s;
	s.init(defaultConfig(), 99);
	const World &w = s.world();
	float worstGap = 0;
	for (int t = 0; t < ticksFor(1200); ++t) {
		s.step();
		for (const Worm &wm : s.worms()) {
			CHECK(w.isSubstrate(int(wm.px[0]), int(wm.py[0])));
			CHECK(wm.py[0] >= float(w.surface(int(wm.px[0]))));
			const float seg = wm.length / float(kBodyPoints - 1);
			for (int i = 0; i < kBodyPoints; ++i) {
				CHECK(wm.px[i] >= 0 && wm.py[i] >= 0 && wm.px[i] < float(w.width()) && wm.py[i] < float(w.height()));
				CHECK(wm.pz[i] >= -0.01f && wm.pz[i] <= 1.01f);
				if (i > 0)
					worstGap = std::max(worstGap, std::hypot(wm.px[i] - wm.px[i - 1], wm.py[i] - wm.py[i - 1]) / seg);
			}
		}
	}
	NOTE("20 simulated minutes: every head in the substrate, every body inside the panes; worst segment stretch ×%.2f", worstGap);
	CHECK(worstGap < 1.6f);
}

void movementAndVisibility()
{
	// Worms crawl (they are not frozen), none stays motionless for hours unless resting, and a good share is at
	// the glass at any time.
	Simulation s;
	s.init(defaultConfig(), 5);
	std::vector<float> startX, startY;
	for (const Worm &wm : s.worms()) {
		startX.push_back(wm.px[0]);
		startY.push_back(wm.py[0]);
	}
	double glassShare = 0;
	int samples = 0;
	for (int m = 0; m < 60; ++m) {
		s.stepN(ticksFor(60));
		glassShare += double(s.stats().atGlass) / double(s.worms().size());
		++samples;
	}
	int moved = 0;
	for (size_t i = 0; i < startX.size(); ++i)
		if (std::hypot(s.worms()[i].px[0] - startX[i], s.worms()[i].py[0] - startY[i]) > 10.0f)
			++moved;
	glassShare /= samples;
	NOTE("after 1 simulated hour %d of %zu worms have moved more than 10 cells; %.0f%% at the glass on average", moved,
	     startX.size(), glassShare * 100);
	CHECK(moved > int(startX.size() * 7 / 10));
	CHECK(glassShare > 0.25 && glassShare < 0.65);
}

void feedingAndCastings()
{
	Simulation s;
	s.init(defaultConfig(), 11);
	const double cast0 = s.world().totalCastings();
	s.feedNow();
	const size_t foods = s.foods().size();
	int maxFeeding = 0;
	for (int m = 0; m < 6 * 60; ++m) {
		s.stepN(ticksFor(60));
		maxFeeding = std::max(maxFeeding, s.stats().feeding);
	}
	const Stats st = s.stats();
	NOTE("feed now added %zu scraps; over 6 simulated hours up to %d worms fed at once, %.2f handfuls eaten, castings +%.0f",
	     foods - 3, maxFeeding, st.eaten, st.castings - cast0);
	CHECK(foods > 3);
	CHECK(maxFeeding >= 8);
	CHECK(st.eaten > 0.5);
	CHECK(st.castings > cast0 + 50);
	// Mist now wets the top of the bedding.
	const World &w = s.world();
	double before = 0, after = 0;
	for (int x = 0; x < w.width(); ++x)
		before += w.moisture(x, w.surface(x) + 1);
	s.mistNow();
	for (int x = 0; x < w.width(); ++x)
		after += w.moisture(x, w.surface(x) + 1);
	CHECK(after > before + 0.05 * w.width());
	CHECK(s.humidity() > 0.5f);
}

void lifeCycle()
{
	// Cocoons are laid, hatch into small worms, and the small worms grow.
	SimConfig cfg = defaultConfig();
	cfg.cocoonsPerAdultPerDay = 12.0f; // speed things up
	cfg.hatchHours = 1.0f;
	cfg.growHours = 2.0f;
	Simulation s;
	s.init(cfg, 21);
	const size_t n0 = s.worms().size();
	s.stepN(ticksFor(3 * 3600));
	const Stats st = s.stats();
	NOTE("3 simulated hours (accelerated life cycle): %d hatched, %d cocoons waiting, %zu -> %d worms", st.hatched, st.cocoons, n0, st.worms);
	CHECK(st.hatched > 0);
	CHECK(size_t(st.worms) > n0);
	CHECK(st.worms <= cfg.populationCap);
	bool grew = false;
	for (const Worm &w : s.worms())
		if (w.id > n0 + 3 && w.grow > 0.3f && w.length > cfg.hatchlingLength + 2)
			grew = true;
	CHECK(grew);
}

void populationIsBounded()
{
	SimConfig cfg = defaultConfig();
	cfg.cocoonsPerAdultPerDay = 40.0f;
	cfg.hatchHours = 0.5f;
	Simulation s;
	s.init(cfg, 31);
	for (int h = 0; h < 8; ++h) {
		s.stepN(ticksFor(3600));
		CHECK(int(s.worms().size()) <= cfg.populationCap);
	}
	NOTE("with 50× the laying rate, 8 simulated hours end at %zu worms (cap %d)", s.worms().size(), cfg.populationCap);
}

void persistence()
{
	Simulation a;
	a.init(defaultConfig(), 2024);
	a.stepN(ticksFor(3600));
	const std::vector<uint8_t> ck = a.saveCheckpoint();
	a.stepN(ticksFor(1800));
	Simulation b;
	b.init(defaultConfig(), 1); // a different bin, replaced by the checkpoint
	std::string err;
	CHECK(b.loadCheckpoint(ck, &err));
	CHECK(b.tick() == uint64_t(ticksFor(3600)));
	b.stepN(ticksFor(1800));
	CHECK(a.stateHash() == b.stateHash());
	std::vector<uint8_t> bad = ck;
	bad[bad.size() / 2] ^= 0x5A;
	Simulation c;
	c.init(defaultConfig(), 3);
	CHECK(!c.loadCheckpoint(bad, &err));
	const std::string corrupt = err;
	std::vector<uint8_t> cut(ck.begin(), ck.begin() + std::ptrdiff_t(ck.size() / 3));
	CHECK(!c.loadCheckpoint(cut, &err));
	CHECK(!c.loadCheckpoint({}, &err));
	NOTE("checkpoint %.2f MB; restored run matches the original after 30 more minutes; corrupted file rejected (%s)",
	     double(ck.size()) / 1e6, corrupt.c_str());
}

void configRoundTrip()
{
	SimConfig a;
	a.wormEatRate = 0.0000123f;
	a.minWorms = 77;
	SimConfig b;
	const std::string text = a.dump();
	for (size_t pos = 0; pos < text.size();) {
		const size_t nl = text.find('\n', pos);
		const std::string line = text.substr(pos, nl - pos);
		pos = nl == std::string::npos ? text.size() : nl + 1;
		const auto eq = line.find(" = ");
		if (eq != std::string::npos)
			CHECK(b.set(line.substr(0, eq), line.substr(eq + 3)));
	}
	CHECK(a.hash() == b.hash());
	CHECK(b.minWorms == 77 && b.wormEatRate == a.wormEatRate);
	CHECK(!b.set("noSuchKey", "1"));
	NOTE("config dump -> set round-trips exactly (hash %016llx)", (unsigned long long)a.hash());
}

void pacing()
{
	int seeds = 2;
	if (const char *e = std::getenv("WORMFARM_PACING_SEEDS"))
		seeds = std::max(1, std::atoi(e));
	std::vector<double> finalCastings;
	for (int k = 0; k < seeds; ++k) {
		const uint64_t seed = 0x5EED0000ULL + uint64_t(k) * 7919;
		Simulation s;
		s.init(defaultConfig(), seed);
		const auto t0 = std::chrono::steady_clock::now();
		const size_t mem0 = s.memoryFootprint();
		int feedingSamples = 0, feedingSum = 0, restSum = 0, glassSum = 0, maxFoods = 0;
		for (int m = 1; m <= 24 * 12; ++m) {
			s.stepN(ticksFor(300));
			const Stats st = s.stats();
			feedingSum += st.feeding;
			restSum += st.byState[size_t(WormState::Rest)];
			glassSum += st.atGlass;
			++feedingSamples;
			maxFoods = std::max(maxFoods, st.foods);
			CHECK(st.foods <= 16);                 // scraps do not pile up
			CHECK(st.worms <= s.config().populationCap);
			CHECK(s.memoryFootprint() <= mem0 + 64 * 1024);
		}
		const Stats st = s.stats();
		const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		NOTE("seed %llx: %d worms (%d young), %d hatched; feedings %d, mists %d; avg feeding %.1f, resting %.1f, at glass %.1f; "
		     "max %d scraps at once; eaten %.1f; castings %.0f; %.0f s wall",
		     (unsigned long long)seed, st.worms, st.juveniles, st.hatched, st.feedings, st.mists, double(feedingSum) / feedingSamples,
		     double(restSum) / feedingSamples, double(glassSum) / feedingSamples, maxFoods, st.eaten, st.castings, secs);
		CHECK(st.feedings == 6);
		CHECK(double(feedingSum) / feedingSamples > 8.0); // there is nearly always a crowd at the food
		CHECK(st.eaten > 5.0);
		CHECK(st.hatched >= 1);
		finalCastings.push_back(st.castings);
	}
	if (finalCastings.size() >= 2)
		CHECK(std::fabs(finalCastings[0] - finalCastings[1]) > 1.0);
}

struct TestCase {
	const char *name;
	std::function<void()> fn;
};

} // namespace

int main(int argc, char **argv)
{
	const std::vector<TestCase> tests = {
		{"fresh_seeds", freshSeeds},
		{"determinism", determinism},
		{"frame_independence", frameIndependence},
		{"worms_stay_in_the_bin", wormsStayInTheBin},
		{"movement_and_visibility", movementAndVisibility},
		{"feeding_and_castings", feedingAndCastings},
		{"life_cycle", lifeCycle},
		{"population_is_bounded", populationIsBounded},
		{"persistence", persistence},
		{"config_round_trip", configRoundTrip},
		{"pacing_24h", pacing},
	};
	std::set<std::string> want(argv + 1, argv + argc);
	int ran = 0;
	for (const TestCase &t : tests) {
		if (!want.empty() && !want.count(t.name))
			continue;
		const int before = g_failures;
		const auto t0 = std::chrono::steady_clock::now();
		std::printf("[ RUN  ] %s\n", t.name);
		std::fflush(stdout);
		t.fn();
		const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		std::printf("[ %s ] %s (%.1f s)\n", g_failures == before ? " OK " : "FAIL", t.name, s);
		std::fflush(stdout);
		++ran;
	}
	std::printf("\n%d test(s), %d failure(s)\n", ran, g_failures);
	return g_failures ? 1 : 0;
}
