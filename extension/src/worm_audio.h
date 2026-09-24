#pragma once
// Procedural sound for the worm bin. Every sound is synthesised here from filtered noise, clicks and short
// sine blips (no recorded samples), driven by simulation events, voice- and rate-limited, 48 kHz stereo.

#include "wormfarm/rng.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

#include <array>
#include <vector>

namespace godot {

class WormAudioSynth : public RefCounted {
	GDCLASS(WormAudioSynth, RefCounted)

public:
	enum Category { Ambience = 0, Crawling = 1, Feeding = 2, Keeper = 3, CategoryCount = 4 };

	void configure(double mix_rate, int64_t seed, int grid_width);
	void set_category_volume(int category, double linear);
	double get_category_volume(int category) const;
	void set_master_volume(double linear) { m_master = float(linear); }
	/// Feeds simulation events ([type, x, y, strength] quadruples, as WormFarmSim::poll_events returns).
	void push_events(const PackedFloat32Array &events);
	/// Worms crawling against the glass (drives the faint bedding crackle).
	void set_crawlers(int count) { m_crawlers = count; }
	/// Renders frames (the app renders by the clock and sends the same samples to speakers and stream).
	PackedVector2Array render(int frames);
	Dictionary get_stats() const;

protected:
	static void _bind_methods();

private:
	enum Kind { Rustle = 0, Squelch = 1, Thud = 2, Spray = 3, Drip = 4 };
	struct Voice {
		int kind = 0;
		float pan = 0.5f;
		float gain = 0;
		int age = 0, length = 0, delay = 0;
		float bpLow = 0, bpBand = 0;
		float fc = 3000, q = 1.2f;
		float nextClick = 0, clickEnv = 0, clickLevel = 0, density = 0, hp = 0;
		float phase = 0, f0 = 0, f1 = 0; ///< sine blip: start and end frequency
		float lp = 0;
	};

	void startVoice(int kind, float xNorm, float strength, int category, int delaySamples = 0);
	void renderInto(float *out, int frames);
	float noise() { return m_rng.uniform() * 2.0f - 1.0f; }

	float m_rate = 48000;
	int m_gridW = 320;
	wormfarm::Pcg32 m_rng;
	std::vector<Voice> m_voices;
	std::array<float, CategoryCount> m_volume{{0.55f, 0.7f, 0.75f, 0.8f}};
	std::array<float, CategoryCount> m_tokens{};
	std::array<float, CategoryCount> m_tokenRate{{0, 6, 3, 4}};
	float m_master = 0.9f;
	int m_crawlers = 0;
	// ambience state
	float m_brown = 0, m_lp1 = 0, m_lp2 = 0, m_ambPhase = 0;
	float m_brownR = 0, m_lp1R = 0, m_lp2R = 0, m_hiss = 0, m_hissR = 0;
	float m_crackleClock = 0, m_crEnvL = 0, m_crEnvR = 0, m_crHpL = 0, m_crHpR = 0;
	// statistics
	int64_t m_eventsIn = 0, m_eventsPlayed = 0, m_eventsLimited = 0, m_framesOut = 0;
	float m_peak = 0;
	std::vector<float> m_scratch;
};

} // namespace godot
