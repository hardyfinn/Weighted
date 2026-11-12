// Offline checks for WeightedEngine.hpp.
//   c++ -std=c++11 -O2 -o test/engine_test test/engine_test.cpp && test/engine_test
#include "../src/WeightedEngine.hpp"
#include <cstdio>

using namespace weighted;

static int failures = 0;
#define CHECK(cond, ...)                         \
	do {                                         \
		if (!(cond)) {                           \
			failures++;                          \
			std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			std::printf(__VA_ARGS__);            \
			std::printf("\n");                   \
		}                                        \
	} while (0)

static Table make(int scale, int profile, double focus = 1.0, bool invert = false) {
	Settings s;
	s.scale = scale;
	s.profile = profile;
	s.focus = focus;
	s.invert = invert;
	Table T;
	std::vector<UserPoint> none;
	buildTable(T, s, none, nullptr, nullptr);
	return T;
}

int main() {
	// 1. Every scale/profile gives a valid table that sums to 1.
	for (int sc = 0; sc < NUM_SCALES; sc++) {
		for (int p = 0; p < NUM_PROFILES; p++) {
			Table T = make(sc, p);
			double s = 0, sl = 0;
			for (int k = 0; k < T.n; k++) {
				CHECK(std::isfinite(T.w[k]) && T.w[k] > 0, "scale %d profile %d tone %d weight %g", sc, p, k, T.w[k]);
				s += T.w[k];
				sl += T.land[k];
			}
			CHECK(std::fabs(s - 1) < 1e-9 && std::fabs(sl - 1) < 1e-9, "scale %d profile %d sums %g %g", sc, p, s, sl);
		}
	}

	// 2. Bins: with uniform input, each note occurs as often as its weight.
	{
		Rng r;
		for (int sc : {0, 1, 8, 11, 12, 14}) {
			for (int p : {P_ROCK, P_FOLK, P_PATHET2, P_FLAT}) {
				Table T = make(sc, p);
				std::vector<double> cnt(T.n, 0.0);
				const int M = 400000;
				for (int i = 0; i < M; i++) {
					Note n = pickBins(T, T.w, r.uniform(), 3, -1, nullptr, 0, 0);
					cnt[n.k] += 1;
				}
				double worst = 0;
				for (int k = 0; k < T.n; k++) worst = std::max(worst, std::fabs(cnt[k] / M - T.w[k]));
				CHECK(worst < 0.004, "scale %d profile %d distribution off by %g", sc, p, worst);
			}
		}
	}

	// 3. Bins keep the contour: higher input never gives a lower note.
	{
		Table T = make(1, P_ROCK, 2.0);
		double last = -1e9;
		bool ok = true;
		for (int i = 0; i < 10000; i++) {
			Note n = pickBins(T, T.w, i / 10000.0, 4, -2, nullptr, 0, 0);
			double c = noteCents(T, n);
			if (c < last) ok = false;
			last = c;
		}
		CHECK(ok, "bins not monotonic");
		Note lo = pickBins(T, T.w, 0.0, 4, -2, nullptr, 0, 0);
		Note hi = pickBins(T, T.w, 0.9999999, 4, -2, nullptr, 0, 0);
		CHECK(lo.oct == -2 && lo.k == 0, "lowest note %d/%d", lo.oct, lo.k);
		CHECK(hi.oct == 2 && hi.k == 0, "highest note should be the closing root: %d/%d", hi.oct, hi.k);
	}

	// 4. Focus 0 is a plain quantizer; higher Focus sharpens the root.
	{
		Table T0 = make(1, P_ROCK, 0.0);
		for (int k = 0; k < T0.n; k++) CHECK(std::fabs(T0.w[k] - 1.0 / T0.n) < 1e-9, "focus 0 not flat");
		Table T1 = make(1, P_ROCK, 1.0), T2 = make(1, P_ROCK, 2.0);
		CHECK(T2.w[0] > T1.w[0] && T1.w[0] > T0.w[0], "focus does not sharpen root: %g %g %g", T0.w[0], T1.w[0], T2.w[0]);
		// Rock minor melody masked to natural minor: root should be about 35%
		CHECK(std::fabs(T1.w[0] - 0.356) < 0.01, "rock minor root weight %g", T1.w[0]);
	}

	// 5. Profile values match the published data (Essen major: 5 > 3 > 1 > 2).
	{
		Table T = make(0, P_FOLK);
		// major scale tones: 1 2 3 4 5 6 7 -> idx 0..6
		CHECK(T.w[4] > T.w[2] && T.w[2] > T.w[0] && T.w[0] > T.w[1] && T.w[1] > T.w[3], "Essen order wrong");
		CHECK(std::fabs(T.w[4] - 0.2228) < 0.003, "Essen 5 = %g", T.w[4]);
	}

	// 6. Invert makes the weakest tone the strongest.
	{
		Table T = make(1, P_ROCK), I = make(1, P_ROCK, 1.0, true);
		int weakest = 0, strongestI = 0;
		for (int k = 0; k < T.n; k++) {
			if (T.w[k] < T.w[weakest]) weakest = k;
			if (I.w[k] > I.w[strongestI]) strongestI = k;
		}
		CHECK(weakest == strongestI, "invert: weakest %d, inverted strongest %d", weakest, strongestI);
	}

	// 7. Landing profile boosts the two strongest tones.
	{
		Table T = make(0, P_CLASSICAL);
		CHECK(T.land[0] > T.w[0] && T.land[4] > T.w[4] && T.land[1] < T.w[1], "landing boost wrong");
	}

	// 8. Gamelan: pathet weights and tunings.
	{
		Table S = make(11, P_PATHET1);  // slendro nem: stress 2, avoid 1
		CHECK(S.w[1] > S.w[3] && S.w[0] < S.w[2], "slendro nem roles");
		CHECK(std::fabs(S.tuning[2] - 480) < 1e-9, "slendro tuning %g", S.tuning[2]);
		Table P = make(12, P_PATHET3);  // pelog barang: tone 1 and 4 rare, 6 strongest
		int top = 0;
		for (int k = 0; k < P.n; k++) if (P.w[k] > P.w[top]) top = k;
		CHECK(top == 5 && P.w[0] < 0.05 && P.w[3] < 0.07, "pelog barang roles top=%d", top);
		Table B = make(14, P_PATHET3);  // pelog barang scale: tones 2 3 5 6 7
		CHECK(B.n == 5 && B.labels[0] == "2" && B.labels[4] == "7", "pelog barang labels");
		Settings s;
		s.scale = 11;
		s.gamelanTuning = 1;
		Table Q;
		std::vector<UserPoint> none;
		buildTable(Q, s, none, nullptr, nullptr);
		CHECK(std::fabs(Q.tuning[1] - 200) < 1e-9 && std::fabs(Q.tuning[2] - 500) < 1e-9, "12-TET snap %g %g", Q.tuning[1], Q.tuning[2]);
	}

	// 9. Gravity: nearest note, then pulled to a stronger neighbour in reach.
	{
		Table T = make(0, P_CLASSICAL);  // C major
		Note n = nearestNote(T, 190.0);  // near D
		CHECK(n.oct == 0 && n.k == 1, "nearest to 190 cents: %d/%d", n.oct, n.k);
		Note g = gravityTarget(T, T.w, n, 300.0);  // C (238) is stronger than D and within reach
		CHECK(g.oct == 0 && g.k == 0, "gravity from D went to %d/%d", g.oct, g.k);
		Note n2 = nearestNote(T, 1150.0);  // B
		CHECK(n2.k == 6 && n2.oct == 0, "nearest to 1150: %d/%d", n2.oct, n2.k);
		Note g2 = gravityTarget(T, T.w, n2, 300.0);  // upper C, one octave up
		CHECK(g2.oct == 1 && g2.k == 0, "gravity from B went to %d/%d", g2.oct, g2.k);
		Note n3 = nearestNote(T, -40.0);
		CHECK(n3.oct == 0 && n3.k == 0, "nearest to -40: %d/%d", n3.oct, n3.k);
	}

	// 10. Step makes intervals smaller on average.
	{
		Table T = make(1, P_ROCK);
		for (double step : {0.0, 1.0}) {
			Rng r;
			Note prev;
			double sum = 0;
			const int M = 50000;
			for (int i = 0; i < M; i++) {
				Note n = pickBins(T, T.w, r.uniform(), 3, -1, i ? &prev : nullptr, step, 0);
				if (i) sum += std::fabs(noteCents(T, n) - noteCents(T, prev));
				prev = n;
			}
			static double first = 0;
			if (step == 0.0) first = sum / M;
			else CHECK(sum / M < 0.6 * first, "step: mean interval %g vs %g cents", sum / M, first);
		}
	}

	// 11. No-repeat reduces repeated notes.
	{
		Table T = make(1, P_ROCK, 2.0);
		int reps[2] = {0, 0};
		for (int m = 0; m < 2; m++) {
			Rng r;
			Note prev;
			for (int i = 0; i < 50000; i++) {
				Note n = pickBins(T, T.w, r.uniform(), 1, 0, i ? &prev : nullptr, 0, m ? 1.0 : 0.0);
				if (i && n == prev) reps[m]++;
				prev = n;
			}
		}
		CHECK(reps[1] < reps[0] / 4, "no-repeat: %d vs %d repeats", reps[1], reps[0]);
	}

	// 12. User profile: learned weights apply by pitch.
	{
		Settings s;
		s.scale = 0;
		s.profile = P_USER;
		std::vector<UserPoint> u = {{0, 10}, {700, 5}, {400, 1}};
		Table T;
		buildTable(T, s, u, nullptr, nullptr);
		CHECK(T.w[0] > T.w[4] && T.w[4] > T.w[2] && T.w[1] < T.w[2], "user profile order");
	}

	// 13. Scala parsing.
	{
		std::vector<double> c;
		std::string err = parseScala("! slendro.scl\n!\nMy slendro\n 5\n!\n 231.0\n 474.5\n 717.0\n 955.0\n 2/1\n", c);
		CHECK(err.empty() && c.size() == 5, "scala: %s size %d", err.c_str(), (int) c.size());
		CHECK(c.size() == 5 && std::fabs(c[2] - 474.5) < 1e-9, "scala cents");
		err = parseScala("x\n2\n3/2\n2/1\n", c);
		CHECK(err.empty() && c.size() == 2 && std::fabs(c[1] - 701.955) < 0.01, "scala ratio %g", c.size() > 1 ? c[1] : 0.0);
		CHECK(!parseScala("hello", c).empty(), "scala garbage accepted");
		double custom[5] = {0, 231, 474.5, 717, 955};
		Settings s;
		s.scale = 11;
		Table T;
		std::vector<UserPoint> none;
		buildTable(T, s, none, custom, nullptr);
		CHECK(std::fabs(T.tuning[3] - 717) < 1e-9 && std::fabs(T.nominal[3] - 720) < 1e-9, "custom slendro tuning");
	}

	// 14. Style profiles: house favours b3 and b7 on Dorian, gospel 3 and 6 on major.
	{
		Table H = make(2, P_HOUSE);  // Dorian: 1 2 b3 4 5 6 b7
		CHECK(H.w[0] > H.w[2] && H.w[2] > H.w[1] && H.w[6] > H.w[5], "house order");
		Table G = make(0, P_GOSPEL);  // major: 1 2 3 4 5 6 7
		CHECK(G.w[2] > G.w[3] && G.w[5] > G.w[6], "gospel order");
		Table R = make(0, P_REGGAE);
		CHECK(R.w[4] > R.w[2] && R.w[6] < 0.05, "reggae order");
		CHECK(std::string(PROFILE_SHORT[P_USER]) == "USER" && NUM_PROFILES == 16, "profile list");
	}

	// 15. Chord steps, strongest tone, phrase-ending helper.
	{
		Table T = make(0, P_CLASSICAL);  // C major
		Note n;
		n.oct = 0;
		n.k = 5;  // A
		Note t = addSteps(T, n, 2), f = addSteps(T, n, 4);
		CHECK(t.oct == 1 && t.k == 0 && f.oct == 1 && f.k == 2, "A minor triad: %d/%d %d/%d", t.oct, t.k, f.oct, f.k);
		Note d = addSteps(T, n, -6);
		CHECK(d.oct == -1 && d.k == 6, "down steps %d/%d", d.oct, d.k);
		CHECK(strongestIndex(T.w, T.n) == 0, "strongest in classical major");
		Table B = make(14, P_PATHET3);  // pelog barang: 2 3 5 6 7, strongest = 6
		CHECK(B.labels[strongestIndex(B.w, B.n)] == "6", "pelog barang strongest");
		Note hi;
		hi.oct = 0;
		hi.k = 6;  // B
		Note r = nearestWithTone(T, 0, hi);
		CHECK(r.oct == 1 && r.k == 0, "nearest root to B is the C above: %d/%d", r.oct, r.k);
	}

	// 16. Drift multipliers stay in range and move smoothly.
	{
		Drift dr;
		Rng r;
		double m[12], prev[12];
		dr.advance(0.0, 1.0, r);
		dr.multipliers(1.0, prev);
		double worstJump = 0, lo = 1e9, hi = 0;
		for (int i = 0; i < 48000; i++) {
			dr.advance(1.0 / 48000.0 * 64, 5.0, r);
			dr.multipliers(1.0, m);
			for (int k = 0; k < 12; k++) {
				worstJump = std::max(worstJump, std::fabs(std::log(m[k]) - std::log(prev[k])));
				lo = std::min(lo, m[k]);
				hi = std::max(hi, m[k]);
				prev[k] = m[k];
			}
		}
		CHECK(lo > 0.36 && hi < 2.72 && worstJump < 0.05, "drift range %g..%g jump %g", lo, hi, worstJump);
		Settings s;
		s.scale = 1;
		s.profile = P_ROCK;
		Table A, D;
		std::vector<UserPoint> none;
		buildTable(A, s, none, nullptr, nullptr);
		double mult[12] = {0.5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
		buildTable(D, s, none, nullptr, nullptr, mult);
		CHECK(D.w[0] < A.w[0], "drift multiplier lowers the root");
	}

	// 17. Step keeps note frequencies equal to the weights, and from the top 7
	//     the walk goes up to the closing root as well as down.
	{
		for (int sc : {0, 1, 8, 12}) {
			Table T = make(sc, P_ROCK);
			for (double step : {0.5, 1.0}) {
				for (int range : {1, 2, 4}) {
					Rng r;
					StepCache cache;
					int lowOct = -(range / 2);
					std::vector<double> cnt(T.n, 0.0);
					Note prev;
					long up71 = 0, from7 = 0;
					const int M = 300000;
					for (int i = 0; i < M; i++) {
						Note n = pickBins(T, T.w, r.uniform(), range, lowOct, i ? &prev : nullptr, step, 0, &cache);
						cnt[n.k] += 1;
						if (i && prev.k == T.n - 1) {
							from7++;
							if (n.k == 0 && n.oct == prev.oct + 1) up71++;
						}
						prev = n;
					}
					double worst = 0;
					for (int k = 0; k < T.n; k++) worst = std::max(worst, std::fabs(cnt[k] / M - T.w[k]));
					CHECK(worst < 0.01, "scale %d step %g range %d: distribution off by %g", sc, step, range, worst);
					CHECK(from7 > 0 && up71 > from7 / 20, "scale %d step %g range %d: 7 to 1 up %ld of %ld", sc, step,
					      range, up71, from7);
				}
			}
		}
	}

	// 19. Root knob follows the circle of fifths.
	{
		const int expect[12] = {0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5};
		for (int i = 0; i < 12; i++) {
			CHECK(fifthsToSemitone(i) == expect[i], "fifths %d -> %d", i, fifthsToSemitone(i));
			CHECK(semitoneToFifths(fifthsToSemitone(i)) == i, "fifths round trip %d", i);
		}
	}

	// 18. Relative major/minor.
	{
		int root = 0, scale = 0;
		CHECK(relativeKey(root, scale) && root == -3 && scale == 1, "C major -> A minor: %d %d", root, scale);
		CHECK(relativeKey(root, scale) && root == 0 && scale == 0, "A minor -> C major: %d %d", root, scale);
		root = 2, scale = 2;
		CHECK(relativeKey(root, scale) && root == 0 && scale == 0, "D Dorian -> C major");
		root = 0, scale = 7;
		CHECK(relativeKey(root, scale) && root == -3 && scale == 8, "C maj pent -> A min pent");
		root = 9, scale = 6;
		CHECK(relativeKey(root, scale) && root == 12 && scale == 0, "A harmonic minor -> C major");
		root = 0, scale = 9;
		CHECK(!relativeKey(root, scale) && root == 0 && scale == 9, "blues unchanged");
		// Same notes before and after, for every scale with a relative.
		for (int sc = 0; sc < NUM_SCALES; sc++) {
			int r2 = 5, s2 = sc;
			if (sc == 6 || !relativeKey(r2, s2)) continue;  // harmonic minor changes one note
			int a = 0, b = 0;
			for (int i = 0; i < SCALES[sc].n; i++) a |= 1 << ((5 + SCALES[sc].tones[i]) % 12);
			for (int i = 0; i < SCALES[s2].n; i++) b |= 1 << (((r2 + SCALES[s2].tones[i]) % 12 + 12) % 12);
			CHECK(a == b, "relative of scale %d changes the notes", sc);
		}
	}

	// HARM chord + Pull (2.5.0)
	{
		// C major, rock; chord = A minor (A C E) given as V/Oct from C4 = 0 V
		float am[3] = {9 / 12.f, 0.f, 4 / 12.f};
		int mask, croot, cthird;
		chordFromVolts(am, 3, 0, mask, croot, cthird);
		CHECK(mask == ((1 << 9) | (1 << 0) | (1 << 4)), "Am mask %x", mask);
		CHECK(croot == 9 && cthird == 0, "Am root %d third %d", croot, cthird);
		// relative to root D (2): A = 7, C = 10, E = 2
		chordFromVolts(am, 3, 2, mask, croot, cthird);
		CHECK(mask == ((1 << 7) | (1 << 10) | (1 << 2)) && croot == 7 && cthird == 10, "Am over D");

		std::vector<UserPoint> none;
		Settings s;
		s.scale = 0;
		s.profile = P_ROCK;
		Table plain, up, down, flat;
		buildTable(plain, s, none, nullptr, nullptr);
		chordFromVolts(am, 3, 0, s.chordMask, s.chordRoot, s.chordThird);
		s.pull = 0.0;
		buildTable(flat, s, none, nullptr, nullptr);
		s.pull = 1.0;
		buildTable(up, s, none, nullptr, nullptr);
		s.pull = -1.0;
		buildTable(down, s, none, nullptr, nullptr);
		double inP = 0, inU = 0, inD = 0, sumU = 0;
		for (int k = 0; k < plain.n; k++) {
			CHECK(std::fabs(flat.w[k] - plain.w[k]) < 1e-12, "pull 0 changes tone %d", k);
			bool in = s.chordMask & (1 << plain.pc[k]);
			if (in) { inP += plain.w[k]; inU += up.w[k]; inD += down.w[k]; }
			sumU += up.w[k];
		}
		CHECK(std::fabs(sumU - 1.0) < 1e-9, "pull table sums to %g", sumU);
		CHECK(inU > inP + 0.15 && inD < inP - 0.15, "chord share plain %.2f up %.2f down %.2f", inP, inU, inD);
		// odds ratio chord : other moves by exactly 4 at full pull
		int a = 5, o = 1;  // tone index 5 = A (in chord), 1 = D (not)
		CHECK(std::fabs((up.w[a] / up.w[o]) / (plain.w[a] / plain.w[o]) - 4.0) < 1e-9, "x4 at full pull");
		// chord landing boosts A and C only
		CHECK(up.cland[5] > up.w[5] && up.cland[0] > up.w[0] && up.cland[2] < up.w[2], "chord landing");
		for (int k = 0; k < plain.n; k++) CHECK(std::fabs(down.cland[k] - down.w[k]) < 1e-12, "no chord landing below noon");
		// stacking chord tones: from A (oct 0, k 5) the next chord tones up are C and E
		Note n;
		n.k = 5;
		Note c1 = addChordTones(up, n, 1, s.chordMask), c2 = addChordTones(up, n, 2, s.chordMask);
		CHECK(c1.oct == 1 && c1.k == 0, "A + 1 chord tone = C above (%d,%d)", c1.oct, c1.k);
		CHECK(c2.oct == 1 && c2.k == 2, "A + 2 chord tones = E above (%d,%d)", c2.oct, c2.k);
	}

	if (failures) {
		std::printf("%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("All engine checks passed\n");
	return 0;
}
