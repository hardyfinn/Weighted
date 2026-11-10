// WeightedEngine.hpp - the musical core of the Weighted module.
// No Rack dependencies, so test/engine_test.cpp can check it offline.
//
// The idea: a normal quantizer gives every scale note an equal slice of each
// volt. Weighted makes each slice as wide as that note's weight, taken from
// published scale-degree statistics (or from the gamelan pathet roles), so a
// random voltage lands on strong notes more often than weak ones.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace weighted {

enum Family { WESTERN, SLENDRO, PELOG };

// ---------------------------------------------------------------- scales
// Western scales list semitones above the root. Gamelan scales list tone
// indexes: slendro 0-4 = tones 1 2 3 5 6, pelog 0-6 = tones 1-7.
struct ScaleDef {
	const char* name;
	const char* shortName;
	Family family;
	int n;
	int tones[12];
};

static const ScaleDef SCALES[] = {
	{"Major", "MAJOR", WESTERN, 7, {0, 2, 4, 5, 7, 9, 11}},
	{"Natural minor", "NATURAL MINOR", WESTERN, 7, {0, 2, 3, 5, 7, 8, 10}},
	{"Dorian", "DORIAN", WESTERN, 7, {0, 2, 3, 5, 7, 9, 10}},
	{"Phrygian", "PHRYGIAN", WESTERN, 7, {0, 1, 3, 5, 7, 8, 10}},
	{"Lydian", "LYDIAN", WESTERN, 7, {0, 2, 4, 6, 7, 9, 11}},
	{"Mixolydian", "MIXOLYDIAN", WESTERN, 7, {0, 2, 4, 5, 7, 9, 10}},
	{"Harmonic minor", "HARMONIC MINOR", WESTERN, 7, {0, 2, 3, 5, 7, 8, 11}},
	{"Major pentatonic", "MAJOR PENT", WESTERN, 5, {0, 2, 4, 7, 9}},
	{"Minor pentatonic", "MINOR PENT", WESTERN, 5, {0, 3, 5, 7, 10}},
	{"Blues", "BLUES", WESTERN, 6, {0, 3, 5, 6, 7, 10}},
	{"Chromatic", "CHROMATIC", WESTERN, 12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
	{"Slendro", "SLENDRO", SLENDRO, 5, {0, 1, 2, 3, 4}},
	{"Pelog (all 7 tones)", "PELOG 7", PELOG, 7, {0, 1, 2, 3, 4, 5, 6}},
	{"Pelog bem (1 2 3 5 6)", "PELOG BEM", PELOG, 5, {0, 1, 2, 4, 5}},
	{"Pelog barang (2 3 5 6 7)", "PELOG BARANG", PELOG, 5, {1, 2, 4, 5, 6}},
};
static const int NUM_SCALES = sizeof(SCALES) / sizeof(SCALES[0]);

static const char* const WESTERN_LABELS[12] = {"1", "b2", "2", "b3", "3", "4", "#4", "5", "b6", "6", "b7", "7"};
static const char* const SLENDRO_LABELS[5] = {"1", "2", "3", "5", "6"};
static const char* const PELOG_LABELS[7] = {"1", "2", "3", "4", "5", "6", "7"};
static const char* const ROOT_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Default gamelan tunings in cents. Real gamelan sets all differ: these are
// rough guides (slendro as five equal steps, pelog as a subset of 9-tone
// equal temperament with large steps between 3-4 and 7-1).
static const double SLENDRO_DEFAULT[5] = {0.0, 240.0, 480.0, 720.0, 960.0};
static const double PELOG_DEFAULT[7] = {0.0, 133.33, 266.67, 533.33, 666.67, 800.0, 933.33};

// ---------------------------------------------------------------- profiles
enum Profile {
	P_FLAT,
	P_FIT,        // Krumhansl & Kessler 1982, probe-tone ratings
	P_FOLK,       // Aarden 2003, Essen folk song note counts
	P_CLASSICAL,  // Albrecht & Shanahan 2013
	P_DOWNBEAT,   // Prince & Schmuckler 2014, notes on downbeats
	P_ROCK,       // Temperley & de Clercq 2013, rock vocal melodies
	P_DETROIT,    // designed: minor/Dorian, 1 b3 5 b7 strong
	P_HOUSE,      // designed: Dorian/minor, 1 b3 5 b7 with the 9th
	P_GOSPEL,     // designed: major, 1 3 5 then 6 2 4, a little blue b3/b7
	P_FUNK,       // designed: one-chord vamp, 1 b7 b3 5, 3 rubbing against b3
	P_SOUL,       // designed: major pentatonic, b3 and b7 bending in
	P_REGGAE,     // designed: major pentatonic, bass-led, few 4s and 7s
	P_PATHET1,    // slendro nem / pelog lima (designed from pathet roles)
	P_PATHET2,    // slendro sanga / pelog nem
	P_PATHET3,    // slendro manyura / pelog barang
	P_USER,       // learned from your own playing
	NUM_PROFILES
};

static const char* const PROFILE_NAMES[NUM_PROFILES] = {
	"Flat (plain quantizer)",
	"Perceived fit (Krumhansl & Kessler)",
	"Folk melody (Aarden, Essen)",
	"Classical (Albrecht & Shanahan)",
	"Classical downbeats (Prince & Schmuckler)",
	"Rock melody (Temperley & de Clercq)",
	"Detroit Dorian (designed)",
	"House (designed)",
	"Gospel (designed)",
	"Funk (designed)",
	"Soul (designed)",
	"Reggae (designed)",
	"Pathet 1: slendro nem / pelog lima",
	"Pathet 2: slendro sanga / pelog nem",
	"Pathet 3: slendro manyura / pelog barang",
	"User (learned)",
};
static const char* const PROFILE_SHORT[NUM_PROFILES] = {
	"FLAT", "FIT", "FOLK", "CLASSICAL", "DOWNBEAT", "ROCK", "DETROIT", "HOUSE", "GOSPEL", "FUNK", "SOUL", "REGGAE", "PATHET 1", "PATHET 2", "PATHET 3", "USER"};

// [profile - P_FIT][0 = major, 1 = minor][pitch class above root]
static const double WESTERN_DATA[5][2][12] = {
	{{6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88},
	 {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17}},
	{{17.7661, 0.145624, 14.9265, 0.160186, 19.8049, 11.3587, 0.291248, 22.062, 0.145624, 8.15494, 0.232998, 4.95122},
	 {18.2648, 0.737619, 14.0499, 16.8599, 0.702494, 14.4362, 0.702494, 18.6161, 4.56621, 1.93186, 7.37619, 1.75623}},
	{{0.238, 0.006, 0.111, 0.006, 0.137, 0.094, 0.016, 0.214, 0.009, 0.080, 0.008, 0.081},
	 {0.220, 0.006, 0.104, 0.123, 0.019, 0.103, 0.012, 0.214, 0.062, 0.022, 0.061, 0.052}},
	{{1.0, 0.088610811, 0.569205361, 0.140888014, 0.615384615, 0.481864956, 0.140888014, 0.976815092, 0.156831608, 0.433398971, 0.122721209, 0.427237502},
	 {1.0, 0.127885863, 0.516472114, 0.640207523, 0.174189364, 0.537483787, 0.160311284, 0.989883268, 0.426588846, 0.172114137, 0.430350195, 0.286381323}},
	{{0.223, 0.001, 0.158, 0.015, 0.194, 0.071, 0.002, 0.169, 0.003, 0.119, 0.008, 0.035},
	 {0.317, 0.001, 0.090, 0.159, 0.046, 0.097, 0.007, 0.131, 0.009, 0.047, 0.087, 0.009}},
};
// Designed, not measured: 1 strongest, b3 and 5 next, b7, then 2 4 6 as colour.
static const double DETROIT_DATA[12] = {0.26, 0.004, 0.10, 0.18, 0.01, 0.08, 0.004, 0.17, 0.01, 0.06, 0.12, 0.01};
// Designed style profiles (no published note counts exist for these styles).
// Order: 1 b2 2 b3 3 4 #4 5 b6 6 b7 7
static const double STYLE_DATA[5][12] = {
	{0.24, 0.004, 0.10, 0.18, 0.01, 0.08, 0.004, 0.17, 0.01, 0.05, 0.13, 0.01},   // house
	{0.20, 0.003, 0.12, 0.05, 0.17, 0.10, 0.005, 0.16, 0.005, 0.12, 0.04, 0.04},  // gospel
	{0.25, 0.004, 0.08, 0.14, 0.07, 0.10, 0.01, 0.14, 0.005, 0.06, 0.16, 0.005},  // funk
	{0.19, 0.003, 0.12, 0.07, 0.15, 0.08, 0.005, 0.16, 0.005, 0.13, 0.06, 0.04},  // soul
	{0.24, 0.002, 0.13, 0.03, 0.17, 0.08, 0.003, 0.19, 0.003, 0.13, 0.03, 0.03},  // reggae
};
// Designed from the stressed / strong / avoided tones of each pathet.
static const double SLENDRO_DATA[3][5] = {
	{0.10, 0.30, 0.18, 0.24, 0.18},  // nem: stress 2, strong 5, avoid 1
	{0.24, 0.18, 0.10, 0.30, 0.18},  // sanga: stress 5, strong 1, avoid 3
	{0.18, 0.24, 0.18, 0.10, 0.30},  // manyura: stress 6, strong 2, avoid 5
};
static const double PELOG_DATA[3][7] = {
	{0.28, 0.16, 0.14, 0.05, 0.20, 0.14, 0.03},  // lima: lands on 1
	{0.20, 0.16, 0.12, 0.05, 0.28, 0.16, 0.03},  // nem: lands on 5
	{0.03, 0.20, 0.14, 0.05, 0.16, 0.28, 0.14},  // barang: lands on 6
};

// ---------------------------------------------------------------- helpers
// A small, fast random generator so the engine is deterministic in tests.
struct Rng {
	uint64_t s = 0x9E3779B97F4A7C15ull;
	void seed(uint64_t v) { s = v ? v : 1; }
	uint64_t next() {
		s ^= s << 13;
		s ^= s >> 7;
		s ^= s << 17;
		return s;
	}
	double uniform() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
};

inline double circDist(double a, double b) {
	double d = std::fabs(std::fmod(a - b, 1200.0));
	if (d > 600.0) d = 1200.0 - d;
	return d;
}

// A user profile: weights attached to pitches (cents above the root), so it
// can be applied to any scale.
struct UserPoint {
	double cents;
	double weight;
};

// ---------------------------------------------------------------- settings
struct Settings {
	int root = 0;            // semitones above C
	int scale = 1;           // index into SCALES
	int profile = P_ROCK;    // Profile
	double focus = 1.0;      // 0 = flat, 1 = profile as published, up to 3
	bool invert = false;     // rare notes become common
	double landingBoost = 2.0;  // landing profile: top two weights multiplied by this
	int gamelanTuning = 0;   // 0 = default/loaded table, 1 = snap to 12-TET
	bool operator==(const Settings& o) const {
		return root == o.root && scale == o.scale && profile == o.profile && std::fabs(focus - o.focus) < 1e-4 &&
		       invert == o.invert && landingBoost == o.landingBoost && gamelanTuning == o.gamelanTuning;
	}
	bool operator!=(const Settings& o) const { return !(*this == o); }
};

// The weight table for the current settings. Rebuilt only when a control
// changes, so the per-sample work is a short search.
struct Table {
	int n = 0;                 // tones in the scale
	double nominal[12] = {};   // cents above root, used to match profiles
	double tuning[12] = {};    // cents above root, used for the output pitch
	double raw[12] = {};       // profile weight before Focus (normalised)
	double w[12] = {};         // main weights (Focus applied, sum 1)
	double land[12] = {};      // landing weights (sum 1)
	double maxW = 1.0, maxLand = 1.0;
	std::string labels[12];
};

inline bool scaleIsMinor(const ScaleDef& s) {
	if (s.family == PELOG) return true;  // pelog's small third sits near b3
	if (s.family == SLENDRO) return false;
	bool has3 = false, hasb3 = false;
	for (int i = 0; i < s.n; i++) {
		if (s.tones[i] == 4) has3 = true;
		if (s.tones[i] == 3) hasb3 = true;
	}
	return hasb3 && !has3;
}

inline int nearestIndex(const double* cents, int n, double c) {
	int best = 0;
	double bd = 1e9;
	for (int i = 0; i < n; i++) {
		double d = circDist(cents[i], c);
		if (d < bd) { bd = d; best = i; }
	}
	return best;
}

// Weight of one tone of a scale under a profile, before normalising.
inline double profileWeight(int profile, const ScaleDef& s, int k, double nominalCents, const std::vector<UserPoint>& user) {
	int t = s.tones[k];
	bool minor = scaleIsMinor(s);
	int pc = s.family == WESTERN ? t : ((int) std::lround(nominalCents / 100.0)) % 12;
	switch (profile) {
		case P_FLAT: return 1.0;
		case P_FIT:
		case P_FOLK:
		case P_CLASSICAL:
		case P_DOWNBEAT:
		case P_ROCK: return WESTERN_DATA[profile - P_FIT][minor ? 1 : 0][pc];
		case P_DETROIT: return DETROIT_DATA[pc];
		case P_HOUSE:
		case P_GOSPEL:
		case P_FUNK:
		case P_SOUL:
		case P_REGGAE: return STYLE_DATA[profile - P_HOUSE][pc];
		case P_PATHET1:
		case P_PATHET2:
		case P_PATHET3: {
			int p = profile - P_PATHET1;
			if (s.family == PELOG) return PELOG_DATA[p][t];
			if (s.family == SLENDRO) return SLENDRO_DATA[p][t];
			// Western scale: use the slendro pathet, matched by nearest pitch
			return SLENDRO_DATA[p][nearestIndex(SLENDRO_DEFAULT, 5, nominalCents)];
		}
		case P_USER: {
			if (user.empty()) return 1.0;
			double best = 0.0, bd = 1e9;
			for (const UserPoint& u : user) {
				double d = circDist(u.cents, nominalCents);
				if (d < bd) { bd = d; best = u.weight; }
			}
			return bd <= 60.0 ? best : 0.0;
		}
	}
	return 1.0;
}

inline void normalise(double* v, int n) {
	double s = 0.0;
	for (int i = 0; i < n; i++) s += v[i];
	if (s <= 0.0) {
		for (int i = 0; i < n; i++) v[i] = 1.0 / n;
		return;
	}
	for (int i = 0; i < n; i++) v[i] /= s;
}

// Build the table. customSlendro / customPelog: tuning tables in cents
// (from a Scala file), or null for the defaults.
// mult: optional per-tone multipliers (used by Drift), or null.
inline void buildTable(Table& T, const Settings& st, const std::vector<UserPoint>& user,
                       const double* customSlendro, const double* customPelog, const double* mult = nullptr) {
	const ScaleDef& s = SCALES[std::max(0, std::min(NUM_SCALES - 1, st.scale))];
	T.n = s.n;
	for (int k = 0; k < s.n; k++) {
		int t = s.tones[k];
		if (s.family == WESTERN) {
			T.nominal[k] = T.tuning[k] = 100.0 * t;
			T.labels[k] = WESTERN_LABELS[t];
		}
		else if (s.family == SLENDRO) {
			T.nominal[k] = SLENDRO_DEFAULT[t];
			T.tuning[k] = customSlendro ? customSlendro[t] : SLENDRO_DEFAULT[t];
			T.labels[k] = SLENDRO_LABELS[t];
		}
		else {
			T.nominal[k] = PELOG_DEFAULT[t];
			T.tuning[k] = customPelog ? customPelog[t] : PELOG_DEFAULT[t];
			T.labels[k] = PELOG_LABELS[t];
		}
		if (s.family != WESTERN && st.gamelanTuning == 1) T.tuning[k] = 100.0 * std::lround(T.tuning[k] / 100.0);
		T.raw[k] = std::max(0.0, profileWeight(st.profile, s, k, T.nominal[k], user));
	}
	normalise(T.raw, T.n);
	// Floor: every scale tone stays possible (1% before Focus)
	for (int k = 0; k < T.n; k++) T.raw[k] = std::max(T.raw[k], 0.01);
	if (mult)
		for (int k = 0; k < T.n; k++) T.raw[k] *= mult[k];
	normalise(T.raw, T.n);

	double base[12];
	double mx = 0.0, mn = 1e9;
	for (int k = 0; k < T.n; k++) { mx = std::max(mx, T.raw[k]); mn = std::min(mn, T.raw[k]); }
	for (int k = 0; k < T.n; k++) base[k] = st.invert ? (mx + mn - T.raw[k]) : T.raw[k];
	double f = std::max(0.0, std::min(3.0, st.focus));
	for (int k = 0; k < T.n; k++) T.w[k] = std::pow(base[k], f);
	normalise(T.w, T.n);

	// Landing profile: the two strongest tones are boosted.
	int order[12];
	for (int k = 0; k < T.n; k++) order[k] = k;
	std::sort(order, order + T.n, [&](int a, int b) { return T.w[a] > T.w[b]; });
	for (int k = 0; k < T.n; k++) T.land[k] = T.w[k];
	for (int j = 0; j < std::min(2, T.n); j++) T.land[order[j]] *= st.landingBoost;
	normalise(T.land, T.n);
	T.maxW = T.maxLand = 0.0;
	for (int k = 0; k < T.n; k++) { T.maxW = std::max(T.maxW, T.w[k]); T.maxLand = std::max(T.maxLand, T.land[k]); }
}

// A chosen note: octave (relative to the root's octave) and tone index.
struct Note {
	int oct = 0;
	int k = 0;
	bool operator==(const Note& o) const { return oct == o.oct && k == o.k; }
	bool operator!=(const Note& o) const { return !(*this == o); }
};

inline double noteCents(const Table& T, const Note& n, bool useTuning = true) {
	return 1200.0 * n.oct + (useTuning ? T.tuning[n.k] : T.nominal[n.k]);
}

// ---------------------------------------------------------------- picking
// Bins mode: u in [0, 1) spans `range` octaves starting at octave `lowOct`,
// closed by the root at the top, so the range runs root to root. The bottom
// and top roots get half a root's weight each, so every octave keeps the
// profile's balance. Each candidate gets a slice as wide as its weight, in
// pitch order, so a higher u never gives a lower note (when Step and
// No-repeat are 0).
//
// Step: the slices are reweighted around the previous note by
// K = exp(-0.35 * step * semitones). On its own that would change how often
// each note plays (notes in the middle of the range, and notes near strong
// ones, would gain). So the candidate weights are first balanced: we find v
// with v_i * (K v)_i = base_i, which makes the base weights the long-run
// frequencies of the walk (detailed balance). Step then changes only the
// size of the intervals, not the note counts.
struct StepCache {
	bool valid = false;
	int n = 0, range = 0, lowOct = 0;
	double step = -1.0;
	double weights[12] = {};
	double nominal[12] = {};
	double v[97] = {};  // balanced weights per candidate
};

inline int binCount(const Table& T, int range) { return T.n * range + 1; }

inline Note binNote(const Table& T, int i, int lowOct) {
	Note c;
	c.oct = lowOct + i / T.n;
	c.k = i % T.n;
	return c;
}

inline void balanceStep(const Table& T, const double* weights, int range, int lowOct, double step, StepCache& C) {
	int N = binCount(T, range);
	double base[97], cents[97];
	for (int i = 0; i < N; i++) {
		Note c = binNote(T, i, lowOct);
		base[i] = weights[c.k] * ((i == 0 || i == N - 1) ? 0.5 : 1.0);
		cents[i] = noteCents(T, c, false);
	}
	if (step <= 0.0) {
		for (int i = 0; i < N; i++) C.v[i] = base[i];
	}
	else {
		static thread_local double K[97 * 97];
		for (int i = 0; i < N; i++)
			for (int j = 0; j < N; j++) K[i * N + j] = std::exp(-0.35 * step * std::fabs(cents[i] - cents[j]) / 100.0);
		double v[97];
		for (int i = 0; i < N; i++) v[i] = base[i];
		for (int it = 0; it < 200; it++) {
			double err = 0.0;
			for (int i = 0; i < N; i++) {
				double kv = 0.0;
				for (int j = 0; j < N; j++) kv += K[i * N + j] * v[j];
				double nv = kv > 0.0 ? std::sqrt(v[i] * base[i] / kv) : 0.0;
				if (base[i] > 0.0) err = std::max(err, std::fabs(nv - v[i]) / std::max(nv, 1e-12));
				v[i] = nv;
			}
			if (err < 1e-7) break;
		}
		for (int i = 0; i < N; i++) C.v[i] = v[i];
	}
	C.valid = true;
	C.n = T.n;
	C.range = range;
	C.lowOct = lowOct;
	C.step = step;
	for (int k = 0; k < T.n; k++) { C.weights[k] = weights[k]; C.nominal[k] = T.nominal[k]; }
}

inline bool stepCacheMatches(const StepCache& C, const Table& T, const double* weights, int range, int lowOct, double step) {
	if (!C.valid || C.n != T.n || C.range != range || C.lowOct != lowOct || C.step != step) return false;
	for (int k = 0; k < T.n; k++)
		if (C.weights[k] != weights[k] || C.nominal[k] != T.nominal[k]) return false;
	return true;
}

// prev: the previous note, or null. step: 0-1 preference for small intervals.
// noRepeat: 0-1, how much the previous note's weight is reduced.
// cache: optional; keeps the Step balancing between calls.
inline Note pickBins(const Table& T, const double* weights, double u, int range, int lowOct,
                     const Note* prev, double step, double noRepeat, StepCache* cache = nullptr) {
	range = std::max(1, std::min(8, range));
	u = std::max(0.0, std::min(0.999999999, u));
	int N = binCount(T, range);
	if (!prev) step = 0.0;
	StepCache local;
	StepCache& C = cache ? *cache : local;
	if (!stepCacheMatches(C, T, weights, range, lowOct, step)) balanceStep(T, weights, range, lowOct, step, C);
	double wid[97];
	double total = 0.0;
	double prevC = prev ? noteCents(T, *prev, false) : 0.0;
	for (int i = 0; i < N; i++) {
		Note c = binNote(T, i, lowOct);
		double x = C.v[i];
		if (prev && step > 0.0) x *= std::exp(-0.35 * step * std::fabs(noteCents(T, c, false) - prevC) / 100.0);
		if (prev && noRepeat > 0.0 && c == *prev) x *= (1.0 - 0.95 * noRepeat);
		wid[i] = x;
		total += x;
	}
	double target = u * total, acc = 0.0;
	for (int i = 0; i < N; i++) {
		acc += wid[i];
		if (target < acc) return binNote(T, i, lowOct);
	}
	return binNote(T, N - 1, lowOct);
}

// The relative major or minor: the same notes with a different home.
// Major <-> natural minor, the modes go to the major or minor key that shares
// their notes, major <-> minor pentatonic. The root moves by the smallest
// interval (C major -> A minor goes down a minor third). Returns false (and changes
// nothing) for scales with no relative: harmonic minor, blues, chromatic,
// gamelan.
inline bool relativeKey(int& root, int& scale) {
	int to = -1, shift = 0;
	switch (scale) {
		case 0: to = 1; shift = -3; break;  // C major -> A minor
		case 1: to = 0; shift = 3; break;   // A minor -> C major
		case 2: to = 0; shift = -2; break;  // D Dorian -> C major
		case 3: to = 0; shift = -4; break;  // E Phrygian -> C major
		case 4: to = 1; shift = 4; break;   // F Lydian -> A minor
		case 5: to = 1; shift = 2; break;   // G Mixolydian -> A minor
		case 7: to = 8; shift = -3; break;  // C major pent -> A minor pent
		case 8: to = 7; shift = 3; break;   // A minor pent -> C major pent
		default: return false;
	}
	root += shift;
	scale = to;
	return true;
}

// The scale note nearest to a pitch given in cents above the root.
inline Note nearestNote(const Table& T, double cents) {
	int o0 = (int) std::floor(cents / 1200.0);
	Note best;
	double bd = 1e18;
	for (int o = o0 - 1; o <= o0 + 1; o++) {
		for (int k = 0; k < T.n; k++) {
			double d = std::fabs(1200.0 * o + T.tuning[k] - cents);
			if (d < bd) { bd = d; best.oct = o; best.k = k; }
		}
	}
	return best;
}

// Gravity mode: quantize to the nearest note, then (with probability
// pull) move to the strongest note within `reach` cents if it is stronger.
inline Note gravityTarget(const Table& T, const double* weights, const Note& nearest, double reach) {
	double c0 = noteCents(T, nearest);
	Note best = nearest;
	double bw = weights[nearest.k];
	for (int o = nearest.oct - 1; o <= nearest.oct + 1; o++) {
		for (int k = 0; k < T.n; k++) {
			Note c;
			c.oct = o;
			c.k = k;
			if (std::fabs(noteCents(T, c) - c0) > reach + 1e-6) continue;
			if (weights[k] > bw + 1e-12) { bw = weights[k]; best = c; }
		}
	}
	return best;
}

// Move a note up (or down) by a number of scale steps, e.g. +2 = the next
// "third" in the scale, +4 = the next "fifth".
inline Note addSteps(const Table& T, const Note& n, int steps) {
	int i = n.oct * T.n + n.k + steps;
	Note r;
	r.oct = (int) std::floor((double) i / T.n);
	r.k = i - r.oct * T.n;
	return r;
}

inline int strongestIndex(const double* w, int n) {
	int best = 0;
	for (int k = 1; k < n; k++)
		if (w[k] > w[best]) best = k;
	return best;
}

// The note with tone index k in the octave that puts it nearest to `near`.
inline Note nearestWithTone(const Table& T, int k, const Note& near) {
	double c0 = noteCents(T, near);
	Note best;
	double bd = 1e18;
	for (int o = near.oct - 1; o <= near.oct + 1; o++) {
		Note c;
		c.oct = o;
		c.k = k;
		double d = std::fabs(noteCents(T, c) - c0);
		if (d < bd) { bd = d; best = c; }
	}
	return best;
}

// ---------------------------------------------------------------- Drift
// Each tone's weight is multiplied by a slowly wandering factor. Every
// `period` seconds each tone gets a new random target; the factor glides
// there smoothly. depth 1 = factors between about x0.37 and x2.7.
struct Drift {
	double from[12] = {}, to[12] = {};
	double phase = 1.0;
	void advance(double dt, double period, Rng& rng) {
		phase += dt / std::max(0.1, period);
		if (phase >= 1.0) {
			phase -= std::floor(phase);
			for (int k = 0; k < 12; k++) {
				from[k] = to[k];
				to[k] = 2.0 * rng.uniform() - 1.0;
			}
		}
	}
	void multipliers(double depth, double* out) const {
		double t = phase * phase * (3.0 - 2.0 * phase);
		for (int k = 0; k < 12; k++) out[k] = std::exp(depth * (from[k] + (to[k] - from[k]) * t));
	}
};

// ---------------------------------------------------------------- Scala
// Parse a Scala .scl file into cents (tone 1 = 0). Returns an error string,
// empty on success.
inline std::string parseScala(const std::string& text, std::vector<double>& cents) {
	cents.clear();
	std::vector<std::string> lines;
	size_t p = 0;
	while (p <= text.size()) {
		size_t e = text.find('\n', p);
		if (e == std::string::npos) e = text.size();
		std::string l = text.substr(p, e - p);
		while (!l.empty() && (l.back() == '\r' || l.back() == ' ' || l.back() == '\t')) l.pop_back();
		size_t a = l.find_first_not_of(" \t");
		l = a == std::string::npos ? "" : l.substr(a);
		if (l.empty() || l[0] != '!') lines.push_back(l);
		p = e + 1;
	}
	if (lines.size() < 2) return "Not a Scala file";
	int count = std::atoi(lines[1].c_str());
	if (count < 1 || (int) lines.size() < 2 + count) return "The file lists fewer pitches than it says";
	cents.push_back(0.0);
	for (int i = 0; i < count; i++) {
		std::string v = lines[2 + i];
		size_t sp = v.find_first_of(" \t");
		if (sp != std::string::npos) v = v.substr(0, sp);
		double c;
		if (v.find('.') != std::string::npos) c = std::atof(v.c_str());
		else {
			size_t sl = v.find('/');
			double num = std::atof(v.substr(0, sl).c_str());
			double den = sl == std::string::npos ? 1.0 : std::atof(v.substr(sl + 1).c_str());
			if (num <= 0.0 || den <= 0.0) return "Could not read pitch " + v;
			c = 1200.0 * std::log2(num / den);
		}
		cents.push_back(c);
	}
	cents.pop_back();  // the last entry is the octave (period)
	return "";
}

}  // namespace weighted
