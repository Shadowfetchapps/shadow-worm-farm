#include "worm_audio.h"

#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <cmath>

using namespace godot;

namespace {
constexpr int kMaxVoices = 24;
constexpr float kPi = 3.14159265358979f;
} // namespace

void WormAudioSynth::configure(double mix_rate, int64_t seed, int grid_width)
{
	m_rate = float(std::max(8000.0, mix_rate));
	m_gridW = std::max(1, grid_width);
	m_rng.reseed(wormfarm::deriveSeed(uint64_t(seed), wormfarm::Stream::Audio), 7u);
	m_voices.clear();
	m_voices.reserve(kMaxVoices);
	for (int c = 0; c < CategoryCount; ++c)
		m_tokens[size_t(c)] = m_tokenRate[size_t(c)];
}

void WormAudioSynth::set_category_volume(int category, double linear)
{
	if (category >= 0 && category < CategoryCount)
		m_volume[size_t(category)] = float(std::clamp(linear, 0.0, 1.5));
}

double WormAudioSynth::get_category_volume(int category) const
{
	return (category >= 0 && category < CategoryCount) ? double(m_volume[size_t(category)]) : 0.0;
}

void WormAudioSynth::startVoice(int kind, float xNorm, float strength, int category, int delaySamples)
{
	++m_eventsIn;
	if (m_tokens[size_t(category)] < 1.0f || int(m_voices.size()) >= kMaxVoices || m_volume[size_t(category)] <= 0.0f) {
		++m_eventsLimited;
		return;
	}
	m_tokens[size_t(category)] -= 1.0f;
	++m_eventsPlayed;
	Voice v;
	v.kind = kind;
	v.delay = delaySamples;
	v.pan = std::clamp(0.12f + 0.76f * xNorm + m_rng.range(-0.03f, 0.03f), 0.0f, 1.0f);
	const float vol = m_volume[size_t(category)];
	strength = std::clamp(strength, 0.0f, 1.0f);
	switch (kind) {
	case Rustle: // a worm pushing through shredded bedding: a dry, papery crackle
		v.length = int(m_rate * m_rng.range(0.18f, 0.45f));
		v.fc = m_rng.range(2600.0f, 4200.0f);
		v.q = m_rng.range(1.0f, 1.6f);
		v.density = m_rng.range(35.0f, 80.0f) * (0.6f + 0.6f * strength);
		v.gain = 0.1f * (0.5f + 0.5f * strength) * vol;
		break;
	case Squelch: // feeding in wet, rotting food: a soft low wet sound with a small bubble
		v.length = int(m_rate * m_rng.range(0.12f, 0.22f));
		v.fc = m_rng.range(450.0f, 900.0f);
		v.q = 0.55f;
		v.f0 = m_rng.range(260.0f, 420.0f);
		v.f1 = v.f0 * m_rng.range(1.8f, 2.6f);
		v.density = m_rng.range(20.0f, 45.0f);
		v.gain = 0.11f * vol;
		break;
	case Thud: // a handful of scraps landing on the bedding
		v.length = int(m_rate * 0.32f);
		v.fc = m_rng.range(900.0f, 1400.0f);
		v.q = 1.1f;
		v.f0 = m_rng.range(85.0f, 120.0f);
		v.f1 = v.f0 * 0.7f;
		v.density = m_rng.range(60.0f, 110.0f);
		v.gain = 0.2f * std::clamp(0.6f + 0.4f * strength, 0.6f, 1.0f) * vol;
		break;
	case Spray: // one squeeze of a mist bottle: a short breathy hiss
		v.length = int(m_rate * m_rng.range(0.28f, 0.38f));
		v.fc = m_rng.range(5200.0f, 7000.0f);
		v.q = 0.7f;
		v.gain = 0.09f * vol;
		break;
	default: // Drip: a droplet running down the glass and landing
		v.length = int(m_rate * 0.06f);
		v.fc = 2500.0f;
		v.q = 1.0f;
		v.f0 = m_rng.range(1400.0f, 2600.0f);
		v.f1 = v.f0 * m_rng.range(1.3f, 1.8f);
		v.gain = 0.05f * vol;
		break;
	}
	v.nextClick = m_rng.range(0.0f, m_rate / std::max(1.0f, v.density));
	m_voices.push_back(v);
}

void WormAudioSynth::push_events(const PackedFloat32Array &events)
{
	const int64_t n = events.size() / 4;
	const float *e = events.ptr();
	for (int64_t k = 0; k < n; ++k, e += 4) {
		const int type = int(e[0]);
		const float xn = e[1] / float(m_gridW);
		const float s = e[3];
		switch (type) {
		case 0: // FoodDropped
			startVoice(Thud, xn, s, Keeper);
			break;
		case 1: // Mist: three squeezes, then droplets
			for (int p = 0; p < 3; ++p)
				startVoice(Spray, 0.3f + 0.2f * float(p), 1.0f, Keeper, int(m_rate * (0.55f * float(p))));
			for (int d = 0; d < 5; ++d) {
				m_tokens[Keeper] = std::max(m_tokens[Keeper], 1.0f);
				startVoice(Drip, m_rng.uniform(), 1.0f, Keeper, int(m_rate * m_rng.range(1.8f, 6.0f)));
			}
			break;
		case 3: startVoice(Squelch, xn, s, Feeding); break;
		case 4: startVoice(Rustle, xn, s, Crawling); break;
		default: break; // hatching is silent
		}
	}
}

void WormAudioSynth::renderInto(float *out, int frames)
{
	const float dt = 1.0f / m_rate;
	for (int c = 0; c < CategoryCount; ++c)
		m_tokens[size_t(c)] = std::min(m_tokenRate[size_t(c)], m_tokens[size_t(c)] + m_tokenRate[size_t(c)] * dt * float(frames));
	const float clickDecay = std::exp(-1.0f / (0.0004f * m_rate));
	const float crDecay = std::exp(-1.0f / (0.0003f * m_rate));
	const float amb = m_volume[Ambience] * m_master;
	const float crawl = m_volume[Crawling] * m_master;
	const float lpA = 1.0f - std::exp(-2.0f * kPi * 140.0f / m_rate);
	const float crackleRate = std::min(90.0f, float(m_crawlers) * 0.8f); // faint crackles per second

	for (int i = 0; i < frames; ++i) {
		float l = 0, r = 0;
		// Room tone: a very quiet, wide low rumble with a slow drift, and a whisper of air.
		m_brown = 0.998f * m_brown + 0.02f * noise();
		m_brownR = 0.998f * m_brownR + 0.02f * noise();
		m_lp1 += lpA * (m_brown - m_lp1);
		m_lp2 += lpA * (m_lp1 - m_lp2);
		m_lp1R += lpA * (m_brownR - m_lp1R);
		m_lp2R += lpA * (m_lp1R - m_lp2R);
		m_ambPhase += dt * 0.017f;
		if (m_ambPhase > 1.0f)
			m_ambPhase -= 1.0f;
		const float drift = (0.85f + 0.15f * std::sin(m_ambPhase * 2.0f * kPi)) * amb * 0.2f;
		m_hiss += 0.35f * (noise() - m_hiss);
		m_hissR += 0.35f * (noise() - m_hissR);
		l += m_lp2 * drift + m_hiss * 0.0022f * amb;
		r += m_lp2R * drift + m_hissR * 0.0022f * amb;
		// Bedding crackle: sparse, very soft ticks spread across the glass while worms crawl against it.
		if (crawl > 0.0f && crackleRate > 0.0f) {
			m_crackleClock -= 1.0f;
			if (m_crackleClock <= 0.0f) {
				const float lvl = m_rng.range(0.2f, 1.0f) * 0.022f * crawl;
				const float p = m_rng.uniform();
				m_crEnvL += lvl * std::cos(p * 0.5f * kPi);
				m_crEnvR += lvl * std::sin(p * 0.5f * kPi);
				m_crackleClock = -std::log(std::max(1e-6f, m_rng.uniform())) * m_rate / crackleRate;
			}
			const float nL = noise() * m_crEnvL, nR = noise() * m_crEnvR;
			l += nL - m_crHpL;
			r += nR - m_crHpR;
			m_crHpL = nL * 0.7f;
			m_crHpR = nR * 0.7f;
			m_crEnvL *= crDecay;
			m_crEnvR *= crDecay;
		}
		out[i * 2] = l;
		out[i * 2 + 1] = r;
	}

	for (Voice &v : m_voices) {
		const float f = 2.0f * std::sin(kPi * std::min(v.fc, m_rate * 0.2f) / m_rate);
		const float pl = std::cos(v.pan * 0.5f * kPi), pr = std::sin(v.pan * 0.5f * kPi);
		int i = 0;
		if (v.delay > 0) {
			const int skip = std::min(v.delay, frames);
			v.delay -= skip;
			i = skip;
		}
		for (; i < frames && v.age < v.length; ++i, ++v.age) {
			const float t = float(v.age) / float(std::max(1, v.length));
			// Grain clicks (a Poisson process thinning out over the voice).
			float click = 0;
			if (v.density > 0.0f) {
				v.nextClick -= 1.0f;
				if (v.nextClick <= 0.0f) {
					v.clickEnv = 1.0f;
					v.clickLevel = m_rng.range(0.25f, 1.0f);
					const float rate = std::max(1.0f, v.density * (1.0f - 0.7f * t));
					v.nextClick = -std::log(std::max(1e-6f, m_rng.uniform())) * m_rate / rate;
				}
				const float cn = noise() * v.clickEnv * v.clickLevel;
				click = cn - v.hp;
				v.hp = cn * 0.7f;
				v.clickEnv *= clickDecay;
			}
			// Band-limited noise body (state-variable filter).
			v.bpLow += f * v.bpBand;
			const float high = noise() - v.bpLow - v.q * v.bpBand;
			v.bpBand += f * high;
			// Sine blip gliding from f0 to f1 (bubbles, droplets, the thump of a landing).
			float blip = 0;
			if (v.f0 > 0.0f) {
				const float freq = v.f0 + (v.f1 - v.f0) * std::min(1.0f, t * 1.5f);
				v.phase += 2.0f * kPi * freq / m_rate;
				if (v.phase > 2.0f * kPi)
					v.phase -= 2.0f * kPi;
				blip = std::sin(v.phase);
			}
			float body, env;
			switch (v.kind) {
			case Rustle:
				env = std::min(1.0f, float(v.age) / (0.02f * m_rate)) * (1.0f - t);
				body = v.bpBand * 0.35f + click * 0.9f;
				break;
			case Squelch: {
				env = std::min(1.0f, float(v.age) / (0.006f * m_rate)) * std::exp(-4.0f * t);
				const float bubble = blip * std::exp(-9.0f * std::max(0.0f, t - 0.35f)) * (t > 0.35f ? 1.0f : 0.0f);
				v.lp += 0.25f * (v.bpBand - v.lp);
				body = v.lp * 0.9f + bubble * 0.35f + click * 0.2f;
				break;
			}
			case Thud:
				env = 1.0f;
				body = blip * std::exp(-14.0f * t) * 0.9f + (v.bpBand * 0.3f + click * 0.6f) * std::exp(-3.0f * t);
				break;
			case Spray:
				env = std::min(1.0f, float(v.age) / (0.012f * m_rate)) * std::pow(1.0f - t, 1.6f);
				body = high * 0.25f + v.bpBand * 0.5f;
				break;
			default: // Drip
				env = std::exp(-7.0f * t);
				body = blip * 0.8f;
				break;
			}
			const float s = body * env * v.gain * m_master;
			out[i * 2] += s * pl;
			out[i * 2 + 1] += s * pr;
		}
	}
	m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [](const Voice &v) { return v.delay <= 0 && v.age >= v.length; }),
	               m_voices.end());

	for (int i = 0; i < frames * 2; ++i) {
		const float y = std::tanh(out[i] * 1.4f) / 1.4f; // gentle soft limit, never clips
		m_peak = std::max(m_peak, std::fabs(y));
		out[i] = y;
	}
	m_framesOut += frames;
}

PackedVector2Array WormAudioSynth::render(int frames)
{
	PackedVector2Array buf;
	if (frames <= 0)
		return buf;
	m_scratch.assign(size_t(frames) * 2, 0.0f);
	renderInto(m_scratch.data(), frames);
	buf.resize(frames);
	Vector2 *b = buf.ptrw();
	for (int i = 0; i < frames; ++i)
		b[i] = Vector2(m_scratch[size_t(i) * 2], m_scratch[size_t(i) * 2 + 1]);
	return buf;
}

Dictionary WormAudioSynth::get_stats() const
{
	Dictionary d;
	d["events_in"] = m_eventsIn;
	d["events_played"] = m_eventsPlayed;
	d["events_limited"] = m_eventsLimited;
	d["voices"] = int64_t(m_voices.size());
	d["frames_out"] = m_framesOut;
	d["peak"] = m_peak;
	d["mix_rate"] = m_rate;
	return d;
}

void WormAudioSynth::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("configure", "mix_rate", "seed", "grid_width"), &WormAudioSynth::configure);
	ClassDB::bind_method(D_METHOD("set_category_volume", "category", "linear"), &WormAudioSynth::set_category_volume);
	ClassDB::bind_method(D_METHOD("get_category_volume", "category"), &WormAudioSynth::get_category_volume);
	ClassDB::bind_method(D_METHOD("set_master_volume", "linear"), &WormAudioSynth::set_master_volume);
	ClassDB::bind_method(D_METHOD("push_events", "events"), &WormAudioSynth::push_events);
	ClassDB::bind_method(D_METHOD("set_crawlers", "count"), &WormAudioSynth::set_crawlers);
	ClassDB::bind_method(D_METHOD("render", "frames"), &WormAudioSynth::render);
	ClassDB::bind_method(D_METHOD("get_stats"), &WormAudioSynth::get_stats);
	BIND_CONSTANT(Ambience);
	BIND_CONSTANT(Crawling);
	BIND_CONSTANT(Feeding);
	BIND_CONSTANT(Keeper);
}
