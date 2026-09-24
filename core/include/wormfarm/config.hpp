#pragma once
// Simulation configuration. Every value is a display-model tuning parameter chosen to make a glass-sided
// worm bin look and behave believably over a day of streaming; none is a measured biological constant.

#include <cstdint>
#include <string>

namespace wormfarm {

struct SimConfig {
	// ---- habitat (one cell ≈ 1.9 mm; the bin shown is about 60 cm wide) ----
	int width = 320;
	int height = 180;
	float headspaceFraction = 0.1f;   ///< air under the lid, as a fraction of the height
	float surfaceRoughness = 3.0f;    ///< cells of relief along the bedding surface
	float beddingFraction = 0.34f;    ///< share of the substrate depth that starts as loose bedding

	// ---- time ----
	int ticksPerSecond = 30;
	int decisionIntervalTicks = 6;

	// ---- worms (red wigglers) ----
	int minWorms = 90;
	int maxWorms = 150;
	int populationCap = 200;          ///< cocoon laying slows to nothing near this many
	float adultLengthMin = 30.0f;     ///< cells (≈ 5.7 cm)
	float adultLengthMax = 46.0f;     ///< cells (≈ 8.7 cm)
	float hatchlingLength = 9.0f;
	float bodyRadius = 0.95f;         ///< cells, adults (≈ 3.6 mm across)
	float crawlSpeed = 1.4f;          ///< cells per second in loose bedding (≈ 16 cm a minute)
	float turnRate = 1.6f;            ///< radians per second
	float glassTime = 0.32f;          ///< share of time a worm spends pressed against the glass (visible)

	// ---- feeding, decomposition, castings ----
	float feedingIntervalHours = 4.0f;
	int foodPerFeeding = 3;
	float decayHours = 9.0f;          ///< fresh scraps become fully rotten over about this long
	float wormEatRate = 0.000005f;    ///< food mass units per second per feeding adult
	float hungerHours = 3.0f;         ///< a fed worm is hungry again after about this long
	float castingsPerCell = 0.18f;    ///< castings level one casting adds to its cell (0..1)
	float scentHalfLife = 900.0f;     ///< seconds
	float scentDiffusion = 0.12f;

	// ---- burrows and moisture ----
	float burrowRefillMinutes = 15.0f;
	float mistIntervalHours = 3.0f;
	float evaporationPerHour = 0.05f;

	// ---- life cycle ----
	float cocoonsPerAdultPerDay = 0.8f; ///< in an empty bin; laying slows as the bin fills
	float hatchHours = 7.0f;
	float growHours = 20.0f;          ///< hatchling to adult

	int eventCapacity = 2048;

	bool loadFile(const std::string &path, std::string *error);
	bool set(const std::string &key, const std::string &value);
	uint64_t hash() const;
	std::string dump() const;
};

} // namespace wormfarm
