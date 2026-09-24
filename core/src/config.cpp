#include "wormfarm/config.hpp"

#include <cstddef>
#include <cstring>
#include <charconv>
#include <cstdio>

namespace wormfarm {

namespace {

struct Field {
	const char *name;
	enum { I, F } type;
	size_t offset;
};

#define FI(n) Field{#n, Field::I, offsetof(SimConfig, n)}
#define FF(n) Field{#n, Field::F, offsetof(SimConfig, n)}

const Field kFields[] = {
	FI(width), FI(height), FF(headspaceFraction), FF(surfaceRoughness), FF(beddingFraction), FI(ticksPerSecond),
	FI(decisionIntervalTicks), FI(minWorms), FI(maxWorms), FI(populationCap), FF(adultLengthMin), FF(adultLengthMax),
	FF(hatchlingLength), FF(bodyRadius), FF(crawlSpeed), FF(turnRate), FF(glassTime), FF(feedingIntervalHours),
	FI(foodPerFeeding), FF(decayHours), FF(wormEatRate), FF(hungerHours), FF(castingsPerCell), FF(scentHalfLife),
	FF(scentDiffusion), FF(burrowRefillMinutes), FF(mistIntervalHours), FF(evaporationPerHour), FF(cocoonsPerAdultPerDay),
	FF(hatchHours), FF(growHours), FI(eventCapacity),
};

std::string trim(const std::string &s)
{
	const auto a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos)
		return {};
	const auto b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

} // namespace

bool SimConfig::set(const std::string &key, const std::string &value)
{
	for (const Field &f : kFields) {
		if (key != f.name)
			continue;
		char *base = reinterpret_cast<char *>(this) + f.offset;
		// Locale-independent parsing (the host process may use a decimal comma).
		const char *b = value.data(), *e = value.data() + value.size();
		if (f.type == Field::I) {
			int v = 0;
			const auto r = std::from_chars(b, e, v);
			if (r.ec != std::errc() || r.ptr != e)
				return false;
			*reinterpret_cast<int *>(base) = v;
		} else {
			float v = 0;
			const auto r = std::from_chars(b, e, v);
			if (r.ec != std::errc() || r.ptr != e)
				return false;
			*reinterpret_cast<float *>(base) = v;
		}
		return true;
	}
	return false;
}

bool SimConfig::loadFile(const std::string &path, std::string *error)
{
	std::FILE *fp = std::fopen(path.c_str(), "rb");
	if (!fp) {
		if (error)
			*error = "cannot open " + path;
		return false;
	}
	std::string text;
	char buf[4096];
	for (size_t got; (got = std::fread(buf, 1, sizeof buf, fp)) > 0;)
		text.append(buf, got);
	std::fclose(fp);
	std::string line;
	int n = 0;
	for (size_t pos = 0; pos < text.size();) {
		const size_t nl = text.find('\n', pos);
		line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		pos = nl == std::string::npos ? text.size() : nl + 1;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		++n;
		line = trim(line.substr(0, line.find('#')));
		if (line.empty())
			continue;
		const auto eq = line.find('=');
		if (eq == std::string::npos || !set(trim(line.substr(0, eq)), trim(line.substr(eq + 1)))) {
			if (error)
				*error = path + ":" + std::to_string(n) + ": invalid setting '" + line + "'";
			return false;
		}
	}
	return true;
}

uint64_t SimConfig::hash() const
{
	uint64_t h = 1469598103934665603ULL; // FNV-1a over the textual dump (portable, order-stable)
	for (char c : dump()) {
		h ^= uint8_t(c);
		h *= 1099511628211ULL;
	}
	return h;
}

std::string SimConfig::dump() const
{
	// Same text as the default "%g"-style stream output, but independent of the process locale.
	std::string o;
	char num[64];
	for (const Field &f : kFields) {
		const char *base = reinterpret_cast<const char *>(this) + f.offset;
		o += f.name;
		o += " = ";
		std::to_chars_result r;
		if (f.type == Field::I)
			r = std::to_chars(num, num + sizeof num, *reinterpret_cast<const int *>(base));
		else
			r = std::to_chars(num, num + sizeof num, *reinterpret_cast<const float *>(base), std::chars_format::general, 6);
		o.append(num, r.ptr);
		o += "\n";
	}
	return o;
}

} // namespace wormfarm
