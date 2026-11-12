// Offline checks for ChangesEngine.hpp.
//   c++ -std=c++11 -O2 -o test/changes_test test/changes_test.cpp && test/changes_test
#include "../src/ChangesEngine.hpp"
#include <cstdio>
#include <set>

using namespace changes;

static int failures = 0;
#define CHECK(cond, ...)                                     \
	do {                                                     \
		if (!(cond)) {                                       \
			failures++;                                      \
			std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			std::printf(__VA_ARGS__);                        \
			std::printf("\n");                               \
		}                                                    \
	} while (0)

static Chord at(int style, int mode, int entry, double colour, float d0 = 0.9f, float d1 = 0.9f, float d2 = 0.9f) {
	Slot s;
	s.entry = entry;
	s.dice[0] = d0; s.dice[1] = d1; s.dice[2] = d2;
	return realize(style, mode, s, colour);
}

int main() {
	Rng rng;
	rng.seed(42);
	// 1. Every style, mode, entry and colour gives a sane chord and a name.
	for (int st = 0; st < NUM_STYLES; st++)
		for (int m = 0; m < NUM_MODES; m++)
			for (int e = 0; e < (int) styleDef(st).entries.size(); e++)
				for (double col = 0; col <= 1.0; col += 0.1) {
					Chord c = at(st, m, e, col);
					CHECK(!c.iv.empty() && c.iv[0] == 0, "style %d entry %d no root", st, e);
					std::set<int> pcs;
					for (int v : c.iv) pcs.insert(((v % 12) + 12) % 12);
					CHECK(pcs.size() == c.iv.size(), "style %d mode %d entry %d colour %.1f doubled pitch class", st, m, e, col);
					CHECK(!chordName(c, 0).empty(), "name");
					for (int voices = 3; voices <= 6; voices++) {
						std::vector<int> t = chordTones(c, voices, STYLE_ROOTLESS[st]);
						CHECK((int) t.size() == voices, "voices %d got %d", voices, (int) t.size());
					}
				}
	// 2. Names in C.
	CHECK(chordName(at(S_HOUSE, M_MINOR, 0, 0.6), 5) == "Fm9", "house i at 60%% in F: %s", chordName(at(S_HOUSE, M_MINOR, 0, 0.6), 5).c_str());
	CHECK(chordName(at(S_HOUSE, M_MINOR, 3, 0.6), 5) == "Dbmaj9", "house bVI: %s", chordName(at(S_HOUSE, M_MINOR, 3, 0.6), 5).c_str());
	CHECK(chordName(at(S_FUNK, M_MIXOLYDIAN, 0, 1.0), 0) == "C13", "funk I: %s", chordName(at(S_FUNK, M_MIXOLYDIAN, 0, 1.0), 0).c_str());
	CHECK(chordName(at(S_FUNK, M_MIXOLYDIAN, 0, 0.6, 0.9f, 0.9f, 0.1f), 0) == "C7#9", "funk 7#9: %s", chordName(at(S_FUNK, M_MIXOLYDIAN, 0, 0.6, 0.9f, 0.9f, 0.1f), 0).c_str());
	CHECK(chordName(at(S_REGGAE, M_MAJOR, 0, 1.0), 0) == "C", "reggae I stays a triad: %s", chordName(at(S_REGGAE, M_MAJOR, 0, 1.0), 0).c_str());
	CHECK(chordName(at(S_ROCK, M_MAJOR, 0, 0.0), 0) == "C5", "rock power: %s", chordName(at(S_ROCK, M_MAJOR, 0, 0.0), 0).c_str());
	CHECK(chordName(at(S_CLASSICAL, M_MINOR, 3, 0.3), 9) == "E7", "classical V7 in A minor: %s", chordName(at(S_CLASSICAL, M_MINOR, 3, 0.3), 9).c_str());
	CHECK(chordName(at(S_GOSPEL, M_MAJOR, 6, 0.5), 0) == "F#dim7", "gospel #iv dim7: %s", chordName(at(S_GOSPEL, M_MAJOR, 6, 0.5), 0).c_str());
	CHECK(chordName(at(S_GOSPEL, M_MAJOR, 7, 0.6), 0) == "E7b9", "gospel III7b9: %s", chordName(at(S_GOSPEL, M_MAJOR, 7, 0.6), 0).c_str());
	CHECK(chordName(at(S_GOSPEL, M_MAJOR, 0, 1.0), 0) == "Cmaj13", "gospel I: %s", chordName(at(S_GOSPEL, M_MAJOR, 0, 1.0), 0).c_str());
	CHECK(romanName(at(S_HOUSE, M_MINOR, 0, 0.6)) == "i", "roman i");
	CHECK(romanName(at(S_GOSPEL, M_MAJOR, 6, 0.5)) == "#ivo", "roman #ivo: %s", romanName(at(S_GOSPEL, M_MAJOR, 6, 0.5)).c_str());
	// 3. Loops start at home; FOCUS 3 mostly takes the favourite move.
	Loop L;
	makeLoop(L, S_GOSPEL, 8, 1.0, 0.0, rng);
	CHECK(L.slots[0].entry == homeEntry(S_GOSPEL), "loop starts home");
	int fav = 0, total = 2000;
	for (int i = 0; i < total; i++) {
		Slot s = rollSlot(S_GOSPEL, 1, 3.0, 0.0, rng);  // after ii
		if (s.entry == 4) fav++;                          // V
	}
	CHECK(fav > total * 0.8, "focus 3: ii goes to V %d/%d", fav, total);
	int fav0 = 0;
	for (int i = 0; i < total; i++)
		if (rollSlot(S_GOSPEL, 1, 0.0, 0.0, rng).entry == 4) fav0++;
	CHECK(fav0 < total * 0.2, "focus 0: about even %d/%d", fav0, total);
	// 4. Drift changes one chord, never the first; restore brings it back.
	Loop D;
	makeLoop(D, S_HOUSE, 4, 1.0, 0.0, rng);
	Loop before = D;
	int i = driftLoop(D, S_HOUSE, 1.0, 0.0, rng);
	CHECK(i >= 1 && i < 4 && (D.drifted & (1 << i)), "drift slot %d", i);
	CHECK(D.slots[0].entry == before.slots[0].entry, "drift keeps the first chord");
	restoreLoop(D);
	for (int k = 0; k < 4; k++) CHECK(D.slots[k].entry == before.slots[k].entry, "restore slot %d", k);
	// 5. Voice leading: ii9 to V13 to Imaj9 moves little.
	std::vector<int> prev;
	int moved = 0, steps = 0;
	int seq[3] = {1, 4, 0};
	for (int k = 0; k < 3; k++) {
		Chord c = at(S_GOSPEL, M_MAJOR, seq[k], 0.6);
		std::vector<int> v = voiceLead(chordTones(c, 4, true), 0, prev, 0.0, 5);
		CHECK((int) v.size() == 4, "4 voices");
		if (!prev.empty()) {
			std::vector<int> p = prev;
			std::sort(p.begin(), p.end());
			for (int j = 0; j < 4; j++) moved += std::abs(v[j] - p[j]);
			steps++;
		}
		prev = v;
	}
	CHECK(moved <= 4 * steps * 3, "voice leading moved %d semitones in %d changes", moved, steps);
	// 6. Drone
	Chord dr = at(S_PATHET1, M_MAJOR, 0, 1.0);
	CHECK(dr.drone && std::fabs(dr.droneCents[0] - 240.0) < 1e-9, "drone");

	if (failures) {
		std::printf("%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("All Changes checks passed\n");
	return 0;
}
