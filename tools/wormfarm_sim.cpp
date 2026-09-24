// Headless simulation tool: runs the same core as the application at accelerated speed (more fixed ticks
// per wall-clock second, same timestep) and writes diagnostic maps of the bin.
//
//   wormfarm_sim --seed 0x1234 --hours 24 --maps out/ --map-hours 0,1,4,12,24
//   wormfarm_sim --seed 7 --hours 2 --checkpoint out/ck.wormfarm
//   wormfarm_sim --load out/ck.wormfarm --hours 1

#include "wormfarm/sim.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace wormfarm;

namespace {

bool readFile(const std::string &path, std::vector<uint8_t> &out)
{
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	uint8_t buf[65536];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
		out.insert(out.end(), buf, buf + n);
	std::fclose(f);
	return true;
}

bool writeFile(const std::string &path, const void *data, size_t n)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	const bool ok = std::fwrite(data, 1, n, f) == n;
	return std::fclose(f) == 0 && ok;
}

std::vector<double> parseList(const std::string &s)
{
	std::vector<double> v;
	size_t pos = 0;
	while (pos < s.size()) {
		const size_t c = s.find(',', pos);
		v.push_back(std::atof(s.substr(pos, c == std::string::npos ? std::string::npos : c - pos).c_str()));
		pos = c == std::string::npos ? s.size() : c + 1;
	}
	return v;
}

/// Front view of the bin, scaled up 3×: substrate by material, darkened by castings, lightened where burrowed,
/// foods as coloured discs, cocoons as yellow dots, worms coloured by depth (bright pink at the glass).
void writeMap(const Simulation &s, const std::string &path)
{
	const World &w = s.world();
	const int S = 3, W = w.width() * S, H = w.height() * S;
	std::vector<uint8_t> px(size_t(W) * size_t(H) * 3);
	auto put = [&](int x, int y, float r, float g, float b) {
		if (x < 0 || y < 0 || x >= W || y >= H)
			return;
		const size_t i = (size_t(y) * size_t(W) + size_t(x)) * 3;
		px[i] = uint8_t(std::min(255.0f, r));
		px[i + 1] = uint8_t(std::min(255.0f, g));
		px[i + 2] = uint8_t(std::min(255.0f, b));
	};
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x) {
			const int cx = x / S, cy = y / S;
			float r, g, b;
			switch (w.material(cx, cy)) {
			case Material::Bedding: r = 150, g = 112, b = 70; break;
			case Material::Compost: r = 78, g = 54, b = 36; break;
			case Material::Soil: r = 96, g = 70, b = 48; break;
			default: r = 214, g = 220, b = 226; break;
			}
			if (w.material(cx, cy) != Material::Air) {
				const float cast = float(w.castings()[size_t(w.index(cx, cy))]) / 255.0f;
				const float wet = w.moisture(cx, cy);
				const float k = (1.0f - 0.55f * cast) * (1.1f - 0.35f * wet);
				r *= k, g *= k, b *= k;
				const float open = 1.0f - w.density(cx, cy);
				r *= 1.0f - 0.9f * open, g *= 1.0f - 0.9f * open, b *= 1.0f - 0.9f * open;
			}
			put(x, y, r, g, b);
		}
	for (const Food &f : s.foods()) {
		const float rot = 1.0f - 0.6f * f.decay;
		const float c[][3] = {{220, 190, 60}, {200, 160, 110}, {90, 170, 70}, {230, 120, 40}, {40, 28, 20}, {235, 230, 215}};
		const float *col = c[int(f.type)];
		const float rr = f.radius * std::sqrt(std::max(0.05f, f.mass / f.mass0)) * S;
		for (int dy = -int(rr); dy <= int(rr); ++dy)
			for (int dx = -int(rr); dx <= int(rr); ++dx)
				if (dx * dx + dy * dy <= rr * rr)
					put(int(f.x * S) + dx, int(f.y * S) + dy, col[0] * rot, col[1] * rot, col[2] * rot);
	}
	for (const Cocoon &c : s.cocoons())
		for (int dy = -2; dy <= 2; ++dy)
			for (int dx = -2; dx <= 2; ++dx)
				put(int(c.x * S) + dx, int(c.y * S) + dy, 230, 200, 60);
	for (const Worm &wm : s.worms())
		for (int i = 0; i < kBodyPoints; ++i) {
			const float z = wm.pz[i];
			const float vis = 1.0f - std::min(1.0f, z * 2.2f);
			if (vis <= 0.02f)
				continue;
			const float r = 190 - 60 * (1 - vis), g = 70, b = 80;
			const int rad = wm.grow >= 1.0f ? 1 : 0;
			for (int dy = -rad; dy <= rad; ++dy)
				for (int dx = -rad; dx <= rad; ++dx)
					put(int(wm.px[i] * S) + dx, int(wm.py[i] * S) + dy, r, g + (i == 0 ? 60 : 0), b);
		}
	std::string header = "P6\n" + std::to_string(W) + " " + std::to_string(H) + "\n255\n";
	std::vector<uint8_t> out(header.begin(), header.end());
	out.insert(out.end(), px.begin(), px.end());
	writeFile(path, out.data(), out.size());
}

} // namespace

int main(int argc, char **argv)
{
	uint64_t seed = 1;
	double hours = 1, reportMinutes = 30;
	std::string maps, checkpointOut, configPath, loadPath;
	std::vector<double> mapHours = {0, 0.5, 1, 4, 8, 12, 18, 24};
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (a == "--seed")
			seed = std::strtoull(next().c_str(), nullptr, 0);
		else if (a == "--hours")
			hours = std::atof(next().c_str());
		else if (a == "--maps")
			maps = next();
		else if (a == "--map-hours")
			mapHours = parseList(next());
		else if (a == "--checkpoint")
			checkpointOut = next();
		else if (a == "--config")
			configPath = next();
		else if (a == "--report-minutes")
			reportMinutes = std::atof(next().c_str());
		else if (a == "--load")
			loadPath = next();
		else {
			std::fprintf(stderr, "usage: wormfarm_sim --seed N --hours H [--maps DIR] [--map-hours a,b,c] [--report-minutes M]\n"
			                     "                    [--checkpoint FILE] [--config FILE] [--load CHECKPOINT]\n");
			return 2;
		}
	}
	SimConfig cfg;
	if (!configPath.empty()) {
		std::string err;
		if (!cfg.loadFile(configPath, &err)) {
			std::fprintf(stderr, "%s\n", err.c_str());
			return 2;
		}
	}
	Simulation s;
	s.init(cfg, seed);
	if (!loadPath.empty()) {
		std::vector<uint8_t> data;
		std::string err;
		if (!readFile(loadPath, data) || !s.loadCheckpoint(data, &err)) {
			std::fprintf(stderr, "cannot load %s: %s\n", loadPath.c_str(), err.empty() ? "unreadable" : err.c_str());
			return 2;
		}
		cfg = s.config();
		seed = s.seed();
		std::printf("loaded %s at %.2f h\n", loadPath.c_str(), s.seconds() / 3600.0);
	}
	std::printf("seed 0x%llx  worms %zu  grid %dx%d  config %016llx\n", (unsigned long long)seed, s.worms().size(), cfg.width, cfg.height,
	            (unsigned long long)cfg.hash());
	std::printf("%7s %5s %5s %5s %5s %5s %5s %5s %5s %5s %6s %7s %7s %6s %5s\n", "hours", "worms", "adult", "juv", "glass", "feed", "seek",
	            "rest", "cocn", "foods", "fmass", "eaten", "cast", "hatchd", "wall");
	const auto t0 = std::chrono::steady_clock::now();
	std::set<size_t> mapsDone;
	const uint64_t total = uint64_t(hours * 3600.0 * cfg.ticksPerSecond);
	const uint64_t reportEvery = std::max<uint64_t>(1, uint64_t(double(cfg.ticksPerSecond) * 60.0 * reportMinutes));
	for (uint64_t t = 0; t <= total; ++t) {
		const double h = s.seconds() / 3600.0;
		for (size_t k = 0; k < mapHours.size(); ++k)
			if (!maps.empty() && !mapsDone.count(k) && h >= mapHours[k]) {
				char name[64];
				std::snprintf(name, sizeof name, "/map_%05.2fh.ppm", mapHours[k]);
				writeMap(s, maps + name);
				mapsDone.insert(k);
			}
		if (t % reportEvery == 0) {
			const Stats st = s.stats();
			std::printf("%7.2f %5d %5d %5d %5d %5d %5d %5d %5d %5d %6.2f %7.2f %7.0f %6d %5.0f\n", h, st.worms, st.adults, st.juveniles, st.atGlass,
			            st.feeding, st.byState[size_t(WormState::SeekFood)], st.byState[size_t(WormState::Rest)], st.cocoons, st.foods,
			            st.foodMass, st.eaten, st.castings, st.hatched,
			            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
			std::fflush(stdout);
		}
		if (t < total)
			s.step();
	}
	if (!checkpointOut.empty()) {
		const auto data = s.saveCheckpoint();
		if (!writeFile(checkpointOut, data.data(), data.size())) {
			std::fprintf(stderr, "cannot write %s\n", checkpointOut.c_str());
			return 1;
		}
	}
	const Stats fin = s.stats();
	std::printf("feedings %d  mists %d  humidity %.2f  memory %.1f MB\n", fin.feedings, fin.mists, s.humidity(),
	            double(s.memoryFootprint()) / 1048576.0);
	std::printf("state hash %016llx\n", (unsigned long long)s.stateHash());
	return 0;
}
