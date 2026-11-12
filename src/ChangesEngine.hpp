// Changes: a chord sequencer that picks the next chord the way a player of
// the chosen style would. Pure C++ (no Rack), so it can be tested offline.
//
// Each style is a list of chords (scale degrees of the key's mode, or fixed
// chords such as a gospel #iv dim7) with a weight, a harmonic function, how
// far up the extensions may go, and a few favourite moves. The next chord is
// a weighted dice roll from the current one; FOCUS raises the weights to a
// power (0 = every chord equal, 1 = as written, 3 = only the favourites).
// COLOUR sets how far up the stack the chord may go (triad, 7th, 9th, 11th,
// 13th), within what the style uses. Voicing: the inversion that moves the
// voices least from the last chord.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace changes {

// ---------------------------------------------------------------- styles
// Same order as Weighted's profiles, so the HARM cable can carry the index.
enum Style {
	S_FLAT, S_FIT, S_FOLK, S_CLASSICAL, S_DOWNBEAT, S_ROCK, S_DETROIT, S_HOUSE,
	S_GOSPEL, S_FUNK, S_SOUL, S_REGGAE, S_PATHET1, S_PATHET2, S_PATHET3, NUM_STYLES
};
static const char* const STYLE_NAMES[NUM_STYLES] = {
	"Flat (every chord in the key)", "Perceived fit (as Classical)", "Folk", "Classical", "Classical downbeats (as Classical)",
	"Rock", "Detroit", "House", "Gospel", "Funk", "Soul", "Reggae",
	"Pathet 1 (drone)", "Pathet 2 (drone)", "Pathet 3 (drone)"};
static const char* const STYLE_SHORT[NUM_STYLES] = {
	"FLAT", "FIT", "FOLK", "CLASSICAL", "DOWNBEAT", "ROCK", "DETROIT", "HOUSE", "GOSPEL", "FUNK", "SOUL", "REGGAE",
	"PATHET 1", "PATHET 2", "PATHET 3"};

enum Mode { M_MAJOR, M_MINOR, M_DORIAN, M_MIXOLYDIAN, NUM_MODES };
static const char* const MODE_NAMES[NUM_MODES] = {"major", "minor", "Dorian", "Mixolydian"};
static const int MODE_SCALE[NUM_MODES][7] = {
	{0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 10}};
// The home key of each style (used when MODE is on "Style")
static const int STYLE_MODE[NUM_STYLES] = {
	M_MAJOR, M_MAJOR, M_MAJOR, M_MAJOR, M_MAJOR, M_MAJOR, M_DORIAN, M_MINOR,
	M_MAJOR, M_MIXOLYDIAN, M_MAJOR, M_MAJOR, M_MAJOR, M_MAJOR, M_MAJOR};
// Notes in a chord when Voices is on Auto
static const int STYLE_VOICES[NUM_STYLES] = {4, 4, 3, 4, 4, 3, 5, 5, 5, 4, 4, 3, 3, 3, 3};
// Styles that leave the root to the bass when a chord has more notes than voices
static const bool STYLE_ROOTLESS[NUM_STYLES] = {false, false, false, false, false, false, false, false,
                                                true, true, true, false, false, false, false};

enum Func { F_TONIC, F_SUB, F_DOM, F_PASS };
enum Qual { Q_DIA, Q_MAJ, Q_DOM, Q_MIN, Q_DIM7 };
enum Flag {
	SUS2 = 1, SUS4 = 2, ADD9 = 4, POWER = 8,  // power chord at the lowest COLOUR
	B9 = 16, S9 = 32,                         // dominant b9 / #9 instead of 9
	HOME = 64,                                // the loop starts here
	INV = 128,                                // may put the 3rd or 5th in the bass
	DOMV = 256                                // V and vii in minor keys take the raised 7th
};

struct Entry {
	int deg;       // 1-7: diatonic in the mode; 0: fixed chord (root + qual)
	int root;      // fixed chords: semitones above the key
	int qual;      // Qual
	float w;       // how common
	int func;      // Func
	int maxExt;    // 0 triad, 1 7th, 2 9th, 3 11th, 4 13th
	int flags;
};
struct Move {
	int from, to;
	float boost;   // 0-1: favourite moves
};
struct StyleDef {
	std::vector<Entry> entries;
	std::vector<Move> moves;
	float stay;    // how often a chord repeats (vamps)
};

inline std::vector<StyleDef> buildStyleDefs() {
	std::vector<StyleDef> defs;
	{
		defs.resize(NUM_STYLES);
		// Flat: every diatonic chord, no favourites
		StyleDef flat;
		for (int d = 1; d <= 7; d++) flat.entries.push_back({d, 0, Q_DIA, 1.f, d == 1 ? F_TONIC : (d == 5 || d == 7 ? F_DOM : F_SUB), 4, d == 1 ? HOME : 0});
		flat.stay = 0.1f;
		defs[S_FLAT] = flat;

		StyleDef classical;
		classical.entries = {
			{1, 0, Q_DIA, 1.0f, F_TONIC, 0, HOME | INV},   // 0 I
			{2, 0, Q_DIA, 0.5f, F_SUB, 1, INV},            // 1 ii(7)
			{4, 0, Q_DIA, 0.6f, F_SUB, 0, INV},            // 2 IV
			{5, 0, Q_DIA, 0.9f, F_DOM, 1, INV | DOMV},     // 3 V(7)
			{6, 0, Q_DIA, 0.4f, F_TONIC, 0, 0},            // 4 vi
			{7, 0, Q_DIA, 0.2f, F_DOM, 1, DOMV},           // 5 vii dim
			{3, 0, Q_DIA, 0.1f, F_TONIC, 0, 0},            // 6 iii
		};
		classical.moves = {{0, 2, .6f}, {0, 1, .5f}, {0, 3, .5f}, {0, 4, .4f}, {1, 3, 1.f}, {2, 3, .8f}, {2, 1, .3f}, {2, 0, .3f},
		                   {3, 0, 1.f}, {3, 4, .35f}, {4, 1, .6f}, {4, 2, .6f}, {5, 0, 1.f}, {6, 4, .7f}};
		classical.stay = 0.05f;
		defs[S_CLASSICAL] = defs[S_FIT] = defs[S_DOWNBEAT] = classical;

		StyleDef folk;
		folk.entries = {
			{1, 0, Q_DIA, 1.0f, F_TONIC, 0, HOME | SUS2 | SUS4 | ADD9},  // 0 I
			{4, 0, Q_DIA, 0.8f, F_SUB, 0, SUS2 | ADD9},                  // 1 IV
			{5, 0, Q_DIA, 0.8f, F_DOM, 1, SUS4},                         // 2 V
			{6, 0, Q_DIA, 0.5f, F_TONIC, 0, ADD9},                       // 3 vi
			{2, 0, Q_DIA, 0.25f, F_SUB, 1, 0},                           // 4 ii
			{0, 10, Q_MAJ, 0.15f, F_SUB, 0, ADD9},                       // 5 bVII
		};
		folk.moves = {{0, 1, .8f}, {0, 2, .7f}, {2, 0, 1.f}, {1, 0, .7f}, {1, 2, .5f}, {3, 1, .8f}, {0, 3, .3f}, {4, 2, .8f},
		              {5, 0, .6f}, {0, 5, .2f}};
		folk.stay = 0.15f;
		defs[S_FOLK] = folk;

		StyleDef rock;
		rock.entries = {
			{1, 0, Q_DIA, 1.0f, F_TONIC, 2, HOME | POWER | ADD9 | SUS4},  // 0 I
			{4, 0, Q_DIA, 0.8f, F_SUB, 2, POWER | ADD9 | SUS4},           // 1 IV
			{5, 0, Q_DIA, 0.7f, F_DOM, 1, POWER | SUS4},                  // 2 V
			{0, 10, Q_MAJ, 0.5f, F_SUB, 0, POWER},                        // 3 bVII
			{6, 0, Q_DIA, 0.5f, F_TONIC, 1, ADD9},                        // 4 vi
			{0, 8, Q_MAJ, 0.15f, F_SUB, 0, POWER},                        // 5 bVI
		};
		rock.moves = {{0, 3, .7f}, {3, 1, .8f}, {1, 0, .7f}, {0, 2, .5f}, {2, 4, .6f}, {4, 1, .8f}, {1, 2, .4f}, {2, 0, .6f},
		              {0, 1, .6f}, {5, 3, .8f}};
		rock.stay = 0.1f;
		defs[S_ROCK] = rock;

		StyleDef detroit;
		detroit.entries = {
			{1, 0, Q_DIA, 1.0f, F_TONIC, 3, HOME},  // 0 i (m7, m9, m11)
			{4, 0, Q_DIA, 0.7f, F_SUB, 2, 0},       // 1 IV (Dorian) / iv
			{7, 0, Q_DIA, 0.5f, F_SUB, 2, 0},       // 2 bVII
			{0, 8, Q_MAJ, 0.3f, F_SUB, 1, 0},       // 3 bVI maj7
			{5, 0, Q_DIA, 0.3f, F_DOM, 2, 0},       // 4 v
		};
		detroit.moves = {{0, 1, .9f}, {1, 0, .9f}, {0, 2, .7f}, {2, 0, .8f}, {0, 3, .3f}, {3, 2, .6f}, {4, 0, .5f}};
		detroit.stay = 0.5f;
		defs[S_DETROIT] = detroit;

		StyleDef house;
		house.entries = {
			{1, 0, Q_DIA, 1.0f, F_TONIC, 2, HOME | SUS2},  // 0 i
			{4, 0, Q_DIA, 0.7f, F_SUB, 2, 0},              // 1 iv
			{5, 0, Q_DIA, 0.3f, F_DOM, 2, 0},              // 2 v
			{6, 0, Q_DIA, 0.6f, F_SUB, 2, 0},              // 3 bVI
			{7, 0, Q_DIA, 0.6f, F_DOM, 1, SUS2},           // 4 bVII
			{3, 0, Q_DIA, 0.3f, F_TONIC, 2, 0},            // 5 bIII
		};
		house.moves = {{0, 3, .8f}, {3, 4, .8f}, {4, 0, .8f}, {0, 1, .8f}, {1, 0, .7f}, {1, 2, .3f}, {2, 0, .5f}, {0, 5, .3f},
		               {5, 4, .5f}, {4, 3, .3f}};
		house.stay = 0.35f;
		defs[S_HOUSE] = house;

		StyleDef gospel;
		gospel.entries = {
			{1, 0, Q_DIA, 1.0f, F_TONIC, 4, HOME | INV},  // 0 I
			{2, 0, Q_DIA, 0.7f, F_SUB, 3, 0},             // 1 ii
			{3, 0, Q_DIA, 0.4f, F_TONIC, 2, 0},           // 2 iii
			{4, 0, Q_DIA, 0.8f, F_SUB, 3, 0},             // 3 IV
			{5, 0, Q_DIA, 0.8f, F_DOM, 4, INV},           // 4 V
			{6, 0, Q_DIA, 0.6f, F_TONIC, 2, 0},           // 5 vi
			{0, 6, Q_DIM7, 0.3f, F_PASS, 1, 0},           // 6 #iv dim7
			{0, 4, Q_DOM, 0.3f, F_PASS, 2, B9},           // 7 III7
			{0, 9, Q_DOM, 0.25f, F_PASS, 2, B9},          // 8 VI7
			{0, 0, Q_DOM, 0.2f, F_PASS, 2, 0},            // 9 I7
		};
		gospel.moves = {{1, 4, 1.f}, {4, 0, 1.f}, {0, 3, .6f}, {3, 6, .6f}, {6, 0, .9f}, {0, 7, .4f}, {7, 5, .9f}, {5, 1, .6f},
		                {0, 8, .3f}, {8, 1, .9f}, {0, 9, .3f}, {9, 3, .9f}, {3, 2, .4f}, {2, 5, .6f}};
		gospel.stay = 0.05f;
		defs[S_GOSPEL] = gospel;

		StyleDef funk;
		funk.entries = {
			{0, 0, Q_DOM, 1.0f, F_TONIC, 4, HOME | S9},  // 0 I7 (9, 13, 7#9)
			{0, 5, Q_DOM, 0.6f, F_SUB, 3, 0},            // 1 IV7
			{2, 0, Q_DIA, 0.25f, F_SUB, 2, 0},           // 2 ii
			{7, 0, Q_DIA, 0.3f, F_SUB, 1, 0},            // 3 bVII
		};
		funk.moves = {{0, 1, .8f}, {1, 0, .9f}, {0, 2, .3f}, {2, 1, .4f}, {3, 0, .8f}, {0, 3, .3f}};
		funk.stay = 0.7f;
		defs[S_FUNK] = funk;

		StyleDef soul;
		soul.entries = {
			{1, 0, Q_DIA, 1.0f, F_TONIC, 2, HOME},  // 0 I
			{2, 0, Q_DIA, 0.7f, F_SUB, 2, 0},       // 1 ii
			{3, 0, Q_DIA, 0.4f, F_TONIC, 1, 0},     // 2 iii
			{4, 0, Q_DIA, 0.8f, F_SUB, 2, 0},       // 3 IV
			{5, 0, Q_DIA, 0.6f, F_DOM, 2, SUS4},    // 4 V
			{6, 0, Q_DIA, 0.6f, F_TONIC, 2, 0},     // 5 vi
		};
		soul.moves = {{1, 4, .9f}, {4, 0, .8f}, {3, 2, .6f}, {2, 1, .6f}, {0, 5, .6f}, {5, 1, .6f}, {0, 3, .6f}, {3, 4, .4f},
		              {3, 0, .4f}};
		soul.stay = 0.1f;
		defs[S_SOUL] = soul;

		StyleDef reggae;
		reggae.entries = {
			{1, 0, Q_DIA, 1.0f, F_TONIC, 0, HOME},  // 0 I
			{4, 0, Q_DIA, 0.8f, F_SUB, 0, 0},       // 1 IV
			{5, 0, Q_DIA, 0.6f, F_DOM, 0, 0},       // 2 V
			{2, 0, Q_DIA, 0.2f, F_SUB, 1, 0},       // 3 ii
			{6, 0, Q_DIA, 0.25f, F_TONIC, 1, 0},    // 4 vi
		};
		reggae.moves = {{0, 1, .9f}, {1, 0, .8f}, {0, 2, .6f}, {2, 0, .7f}, {1, 2, .4f}, {2, 1, .4f}};
		reggae.stay = 0.3f;
		defs[S_REGGAE] = reggae;

		// Pathet: a drone (handled separately); one entry so the loop code works
		StyleDef drone;
		drone.entries = {{1, 0, Q_DIA, 1.f, F_TONIC, 0, HOME}, {1, 0, Q_DIA, 1.f, F_TONIC, 0, 0}};
		drone.stay = 0.3f;
		defs[S_PATHET1] = defs[S_PATHET2] = defs[S_PATHET3] = drone;

		// Every Western style can reach the other diatonic chords, rarely
		// (FOCUS 0 makes them as likely as the rest).
		for (int s = 0; s < NUM_STYLES; s++) {
			if (s == S_FLAT || s >= S_PATHET1) continue;
			for (int d = 1; d <= 7; d++) {
				bool have = false;
				for (const Entry& e : defs[s].entries)
					if (e.deg == d) have = true;
				if (!have) defs[s].entries.push_back({d, 0, Q_DIA, 0.03f, d == 5 || d == 7 ? F_DOM : F_SUB, 1, 0});
			}
		}
	}
	return defs;
}

inline const StyleDef& styleDef(int style) {
	static const std::vector<StyleDef> defs = buildStyleDefs();  // thread-safe one-time init (C++11)
	return defs[std::max(0, std::min(NUM_STYLES - 1, style))];
}

inline bool isDrone(int style) { return style >= S_PATHET1 && style <= S_PATHET3; }

// Pathet drones: the pathet's two main tones, in cents above the key
// (slendro default tuning, as Weighted). nem: 2 and 5; sanga: 5 and 1; manyura: 6 and 2.
static const double DRONE_CENTS[3][2] = {{240.0, 720.0}, {720.0, 0.0}, {960.0, 240.0}};

// ---------------------------------------------------------------- random
struct Rng {
	uint64_t s = 0x2545F4914F6CDD1Dull;
	void seed(uint64_t v) { s = v ? v : 1; }
	uint64_t next() {
		s ^= s >> 12;
		s ^= s << 25;
		s ^= s >> 27;
		return s * 0x2545F4914F6CDD1Dull;
	}
	double uniform() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
};

// ---------------------------------------------------------------- chords
// The parts of a chord, in semitones above its root (-1 = none).
struct Tmpl {
	int root = 0;   // above the key
	int third = 4, fifth = 7, seventh = -1, ninth = 14, eleventh = 17, thirteenth = 21;
};

inline Tmpl tmplOf(const Entry& e, int mode) {
	Tmpl t;
	if (e.deg >= 1) {
		const int* sc = MODE_SCALE[std::max(0, std::min(NUM_MODES - 1, mode))];
		int d = e.deg - 1;
		auto at = [&](int k) { int i = d + k; return sc[i % 7] + 12 * (i / 7) - sc[d]; };
		t.root = sc[d];
		t.third = at(2);
		t.fifth = at(4);
		t.seventh = at(6);
		t.ninth = at(8);
		t.eleventh = at(10);
		t.thirteenth = at(12);
		bool minorKey = mode == M_MINOR || mode == M_DORIAN;
		if ((e.flags & DOMV) && minorKey) {
			if (e.deg == 5) { t.third = 4; t.seventh = 10; t.ninth = 13; }   // V7 (b9) of harmonic minor
			if (e.deg == 7) { t.root = 11; t.third = 3; t.fifth = 6; t.seventh = 9; }  // vii dim7
		}
	}
	else {
		t.root = e.root;
		switch (e.qual) {
			case Q_MAJ: t.third = 4; t.fifth = 7; t.seventh = 11; t.eleventh = 18; break;
			case Q_DOM: t.third = 4; t.fifth = 7; t.seventh = 10; break;
			case Q_MIN: t.third = 3; t.fifth = 7; t.seventh = 10; break;
			case Q_DIM7: t.third = 3; t.fifth = 6; t.seventh = 9; t.ninth = t.eleventh = t.thirteenth = -1; break;
		}
	}
	return t;
}

// A chord as played: root above the key, and the notes above the root.
struct Chord {
	int root = 0;              // semitones above the key
	std::vector<int> iv;       // intervals above the root, root (0) first
	int third = 4, fifth = 7, seventh = -1;  // as used (-1 none); third 2/5 = sus
	int top = 0;               // highest extension: 0 none, 9, 11, 13
	bool power = false, add9 = false, b9 = false, s9 = false, s11 = false;
	int func = F_TONIC;
	int level = 0;             // extension level used (0 triad .. 4 13th)
	bool drone = false;
	double droneCents[3] = {};
};

// The dice a loop slot keeps, so the same slot plays the same chord again.
struct Slot {
	int entry = 0;
	float dice[4] = {0.5f, 0.5f, 0.5f, 0.5f};  // drop a level, sus, add9 / #9, bass inversion
};

inline int colourLevel(double colour) {
	colour = std::max(0.0, std::min(1.0, colour));
	if (colour < 0.2) return 0;
	if (colour < 0.45) return 1;
	if (colour < 0.7) return 2;
	if (colour < 0.85) return 3;
	return 4;
}

inline Chord realize(int style, int mode, const Slot& s, double colour) {
	Chord c;
	const StyleDef& sd = styleDef(style);
	const Entry& e = sd.entries[std::max(0, std::min((int) sd.entries.size() - 1, s.entry))];
	c.func = e.func;
	if (isDrone(style)) {
		int p = style - S_PATHET1;
		int a = s.entry == 0 ? 0 : 1;
		c.drone = true;
		c.droneCents[0] = DRONE_CENTS[p][a];
		c.droneCents[1] = DRONE_CENTS[p][1 - a];
		c.droneCents[2] = DRONE_CENTS[p][a] + 1200.0;
		c.root = (int) std::lround(c.droneCents[0] / 100.0) % 12;
		c.iv = {0, (int) (((std::lround((c.droneCents[1] - c.droneCents[0]) / 100.0)) % 12 + 12) % 12)};
		return c;
	}
	Tmpl t = tmplOf(e, mode);
	c.root = ((t.root % 12) + 12) % 12;
	int L0 = colourLevel(colour);
	int L = std::min(L0, e.maxExt);
	if (L > 0 && s.dice[0] < 0.3f) L--;
	c.level = L;
	if ((e.flags & POWER) && L0 == 0) {
		c.power = true;
		c.third = -1;
		c.iv = {0, 7};
		return c;
	}
	c.third = t.third;
	if ((e.flags & (SUS2 | SUS4)) && L0 >= 1 && s.dice[1] < 0.35f) {
		bool two = (e.flags & SUS2) && (!(e.flags & SUS4) || s.dice[1] < 0.175f);
		c.third = two ? 2 : 5;
	}
	c.fifth = t.fifth;
	c.iv = {0, c.third, c.fifth};
	if (L >= 1 && t.seventh >= 0) {
		c.seventh = t.seventh;
		c.iv.push_back(t.seventh);
	}
	else if ((e.flags & ADD9) && L0 >= 2 && s.dice[2] < 0.5f && c.third != 2) {
		c.add9 = true;
		c.iv.push_back(14);
	}
	if (L >= 2 && c.seventh >= 0 && t.ninth >= 0) {
		int n = t.ninth;
		if ((e.flags & B9) && c.seventh == 10) { n = 13; c.b9 = true; }
		else if ((e.flags & S9) && c.seventh == 10 && s.dice[2] < 0.4f) { n = 15; c.s9 = true; }
		if (!(c.third == 2 && n == 14)) c.iv.push_back(n);
		c.top = 9;
	}
	if (L >= 3 && c.seventh >= 0 && t.eleventh >= 0) {
		// A perfect 11th clashes with a major 3rd: skip it (the 13th follows)
		if (!(c.third == 4 && t.eleventh == 17) && c.third != 5) {
			c.iv.push_back(t.eleventh);
			c.top = 11;
			c.s11 = t.eleventh == 18;
		}
	}
	if (L >= 4 && c.seventh >= 0 && t.thirteenth >= 0) {
		c.iv.push_back(t.thirteenth);
		c.top = 13;
	}
	return c;
}

// ---------------------------------------------------------------- names
static const char* const NOTE_NAMES[12] = {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

inline std::string chordName(const Chord& c, int key) {
	int r = ((key + c.root) % 12 + 12) % 12;
	std::string n = NOTE_NAMES[r];
	if (c.drone) return n + " drone";
	if (c.power) return n + "5";
	bool minor = c.third == 3;
	if (minor && c.fifth == 6) {
		if (c.seventh == 9) return n + "dim7";
		if (c.seventh == 10) return n + "m7b5";
		return n + "dim";
	}
	std::string s;
	if (minor) s = "m";
	if (c.seventh < 0) {
		if (c.add9) s += "add9";
	}
	else {
		std::string num = c.top == 13 ? "13" : c.top == 11 ? "11" : c.top == 9 ? "9" : "7";
		if (c.b9 || c.s9) num = "7";
		if (c.seventh == 11) s += minor ? "(maj7)" : "maj" + num;
		else s += num;
		if (c.b9) s += "b9";
		if (c.s9) s += "#9";
		if (c.s11 && c.top == 11) s += "#11";
	}
	if (c.third == 2) s += "sus2";
	if (c.third == 5) s += "sus4";
	return n + s;
}

inline std::string romanName(const Chord& c) {
	static const char* const R[12] = {"I", "bII", "II", "bIII", "III", "IV", "#IV", "V", "bVI", "VI", "bVII", "VII"};
	if (c.drone) return "drone";
	std::string s = R[((c.root % 12) + 12) % 12];
	if (c.third == 3) for (char& ch : s) ch = (char) std::tolower(ch);
	if (c.third == 3 && c.fifth == 6) s += c.seventh == 10 ? "o7" : "o";
	else if (c.seventh == 10 && c.third == 4) s += "7";
	return s;
}

// ---------------------------------------------------------------- choosing
// Weights for the next chord after `from` (-1 = no current chord).
inline void nextWeights(int style, int from, double focus, double tension, std::vector<double>& out) {
	const StyleDef& sd = styleDef(style);
	int n = (int) sd.entries.size();
	out.assign(n, 0.0);
	focus = std::max(0.0, std::min(3.0, focus));
	for (int j = 0; j < n; j++) {
		double w = sd.entries[j].w;
		if (from >= 0) {
			for (const Move& m : sd.moves)
				if (m.from == from && m.to == j) w *= 1.0 + 4.0 * m.boost;
			if (j == from) w = sd.stay * 4.0 * sd.entries[j].w;
		}
		int f = sd.entries[j].func;
		if (f == F_DOM || f == F_PASS) w *= 1.0 + 2.0 * std::max(0.0, tension);
		out[j] = w > 0 ? std::pow(w, focus) : 0.0;
	}
}

inline int pickWeighted(const std::vector<double>& w, double u) {
	double t = 0;
	for (double x : w) t += x;
	if (t <= 0) return 0;
	double a = u * t;
	for (int i = 0; i < (int) w.size(); i++) {
		a -= w[i];
		if (a < 0) return i;
	}
	return (int) w.size() - 1;
}

inline int homeEntry(int style) {
	const StyleDef& sd = styleDef(style);
	for (int i = 0; i < (int) sd.entries.size(); i++)
		if (sd.entries[i].flags & HOME) return i;
	return 0;
}

inline Slot rollSlot(int style, int from, double focus, double tension, Rng& rng) {
	std::vector<double> w;
	nextWeights(style, from, focus, tension, w);
	Slot s;
	s.entry = pickWeighted(w, rng.uniform());
	for (int i = 0; i < 4; i++) s.dice[i] = (float) rng.uniform();
	return s;
}

// ---------------------------------------------------------------- voicing
// Pitch classes (semitones above the key) to play with `voices` notes: the
// least important notes are dropped first, or doubled when there are too few.
inline std::vector<int> chordTones(const Chord& c, int voices, bool rootless) {
	struct T { int iv; int pri; };
	std::vector<T> ts;
	for (int i = 0; i < (int) c.iv.size(); i++) {
		int v = c.iv[i], pri;
		if (i == 0) pri = rootless ? 3 : 7;
		else if (v == c.third) pri = 10;
		else if (v == c.seventh) pri = 9;
		else if (v == c.fifth) pri = c.power ? 9 : 2;
		else if (i == (int) c.iv.size() - 1) pri = 8;  // the top extension
		else pri = 5;
		ts.push_back({v, pri});
	}
	while ((int) ts.size() > voices) {
		int lo = 0;
		for (int i = 1; i < (int) ts.size(); i++)
			if (ts[i].pri < ts[lo].pri) lo = i;
		ts.erase(ts.begin() + lo);
	}
	std::vector<int> out;
	for (const T& t : ts) out.push_back(c.root + t.iv);
	// Too few: double the root, then the fifth, then the third, an octave up
	int k = 0;
	while ((int) out.size() < voices) {
		int add = k == 0 ? c.root : (k == 1 ? c.root + c.fifth : c.root + (c.third >= 0 ? c.third : 12));
		out.push_back(add + 12);
		k = (k + 1) % 3;
	}
	return out;
}

// Choose the voicing (semitones above C4, key included) that moves least
// from `prev`, close (spread 0) to wide (spread 1), near `center`.
inline std::vector<int> voiceLead(const std::vector<int>& pcsIn, int key, const std::vector<int>& prev, double spread, int center) {
	int n = (int) pcsIn.size();
	std::vector<int> pcs;
	for (int p : pcsIn) pcs.push_back(((key + p) % 12 + 12) % 12);
	std::vector<int> best;
	double bestCost = 1e18;
	for (int r = 0; r < n; r++) {
		std::vector<int> v;
		int last = -1000;
		for (int i = 0; i < n; i++) {
			int pc = pcs[(r + i) % n];
			int x = pc;
			while (x <= last) x += 12;
			if (i == 0) x = pc;
			v.push_back(x);
			last = x;
		}
		// open voicings: drop the 2nd highest (drop 2), then the 4th highest too
		if (spread >= 0.33 && n >= 3) v[n - 2] -= 12;
		if (spread >= 0.66 && n >= 4) v[n - 4] -= 12;
		std::sort(v.begin(), v.end());
		double mean = 0;
		for (int x : v) mean += x;
		mean /= n;
		int base = (int) std::round((center - mean) / 12.0);
		for (int o = base - 1; o <= base + 1; o++) {
			std::vector<int> w = v;
			for (int& x : w) x += 12 * o;
			double m = mean + 12 * o;
			double cost = 0.6 * std::fabs(m - center);
			if (!prev.empty()) {
				int k = std::min((int) prev.size(), n);
				std::vector<int> p = prev;
				std::sort(p.begin(), p.end());
				// compare the lowest k notes of each
				for (int i = 0; i < k; i++) cost += std::fabs(w[i] - p[i]);
			}
			if (w.front() < center - 19 || w.back() > center + 26) cost += 40;
			if (cost < bestCost) { bestCost = cost; best = w; }
		}
	}
	return best;
}

inline double tensionOf(const Chord& c) {
	static const double base[4] = {0.0, 4.0, 7.0, 9.0};
	double t = base[std::max(0, std::min(3, c.func))] + 0.6 * c.level + (c.third == 2 || c.third == 5 ? 1.0 : 0.0);
	if (c.drone) t = 0;
	return std::max(0.0, std::min(10.0, t));
}

// ---------------------------------------------------------------- loops
static const int MAX_LOOP = 8;
static const int NUM_LOOPS = 9;  // one per Long Form section, plus one free

struct Loop {
	int len = 4;
	Slot slots[MAX_LOOP];
	Slot orig[MAX_LOOP];
	uint8_t drifted = 0;  // bit per slot: changed by DRIFT
	bool made = false;
};

inline void makeLoop(Loop& L, int style, int len, double focus, double tension, Rng& rng) {
	L.len = std::max(1, std::min(MAX_LOOP, len));
	L.slots[0] = rollSlot(style, -1, 0.0, 0.0, rng);
	L.slots[0].entry = homeEntry(style);
	for (int i = 1; i < L.len; i++) L.slots[i] = rollSlot(style, L.slots[i - 1].entry, focus, tension, rng);
	for (int i = 0; i < MAX_LOOP; i++) L.orig[i] = L.slots[i];
	L.drifted = 0;
	L.made = true;
}

// Rewrite one chord of the loop (never the first, which keeps it at home).
// Returns the slot changed, or -1.
inline int driftLoop(Loop& L, int style, double focus, double tension, Rng& rng) {
	if (L.len < 2) return -1;
	int i = 1 + (int) (rng.uniform() * (L.len - 1));
	i = std::min(i, L.len - 1);
	int before = L.slots[i].entry;
	for (int tries = 0; tries < 6; tries++) {
		Slot s = rollSlot(style, L.slots[i - 1].entry, focus, tension, rng);
		if (s.entry != before || tries == 5) {
			L.slots[i] = s;
			break;
		}
	}
	L.drifted |= (uint8_t) (1 << i);
	return i;
}

inline void restoreLoop(Loop& L) {
	for (int i = 0; i < MAX_LOOP; i++) L.slots[i] = L.orig[i];
	L.drifted = 0;
}

}  // namespace changes
