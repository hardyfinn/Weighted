// Weighted: a quantizer that picks notes by weight.
// Random CV in, quantized pitch out, with strong notes chosen more often than
// weak ones, using published scale-degree statistics or gamelan pathet roles.
#include "plugin.hpp"
#include "WeightedEngine.hpp"
#include <osdialog.h>
#include <fstream>
#include <mutex>
#include <sstream>

using namespace weighted;

static const int HISTORY = 256;

struct Weighted : Module {
	enum ParamId {
		ROOT_PARAM,
		SCALE_PARAM,
		PROFILE_PARAM,
		FOCUS_PARAM,
		FOCUS_ATT_PARAM,
		STEP_PARAM,
		RANGE_PARAM,
		LENGTH_PARAM,
		STRONG_PARAM,
		GRAVITY_PARAM,
		INVERT_PARAM,
		LEARN_PARAM,
		RELATIVE_PARAM,
		OCTAVE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CV_INPUT,
		TRIG_INPUT,
		ACCENT_INPUT,
		ROOT_INPUT,
		SCALE_INPUT,
		PROFILE_INPUT,
		FOCUS_INPUT,
		RELATIVE_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		VOCT_OUTPUT,
		TRIG_OUTPUT,
		DEGREE_OUTPUT,
		STABILITY_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		GRAVITY_LIGHT,
		INVERT_LIGHT,
		LEARN_LIGHT,
		ACCENT_LIGHT,
		RELATIVE_LIGHT,
		LIGHTS_LEN
	};

	// Settings from the menu
	int inputRange = 3;      // 0 = -5..+5 V, 1 = 0..10 V, 2 = 0..5 V, 3 = auto
	int noRepeatSetting = 0; // 0 off, 1 low, 2 medium, 3 high
	int boostSetting = 1;    // landing boost: 0 = x1.5, 1 = x2, 2 = x3
	int gamelanTuning = 0;   // 0 = default or loaded table, 1 = snap to 12-TET
	int trigMode = 0;        // 0 = every new note, 1 = only when the pitch changes
	int phraseEnd = 0;       // 0 off, 1 = last step of the cycle lands on the first scale tone, 2 = on the strongest tone
	int driftSetting = 0;    // 0 off, 1 slow, 2 medium, 3 fast
	int chordSetting = 0;    // 0 off, 1 add 3rd, 2 add 3rd and 5th, 3 add 5th
	int relativeTiming = 0;  // 0 = Relative changes at once, 1 = at the end of the accent cycle
	bool relativeApplied = false;
	bool relativeShown = false, relativePending = false;  // for the display

	// Drawing on the display (UI thread) hands new user weights to the audio thread
	std::mutex drawMutex;
	std::vector<UserPoint> pendingUser;
	bool pendingFlag = false;

	Drift drift;
	double driftMult[12];
	int driftCounter = 0;

	// Loaded Scala tunings
	bool haveSlendro = false, havePelog = false;
	double slendroCents[5] = {};
	double pelogCents[7] = {};
	std::string slendroName, pelogName;

	// User profile
	std::vector<UserPoint> user;
	int userVersion = 0;

	// Learning
	bool learning = false;
	double learnCounts[12] = {};
	int learnTotal = 0;
	int learnScale = -1;
	Note learnLast;
	bool learnHasLast = false;
	dsp::BooleanTrigger learnButton;

	// Engine state
	Settings settings;
	int tableVersion = -1;   // bumps when user or tunings change
	int builtVersion = -2;
	Table table;
	StepCache stepCacheW, stepCacheLand;  // Step balancing for the main and landing weights
	int lastRoot = 0;

	struct Channel {
		dsp::SchmittTrigger trig;
		dsp::PulseGenerator pulse;
		Note last;
		bool hasLast = false;
		Note gravityNearest;
		bool hasGravity = false;
		int counter = -1;
		bool accent = false;
		float out = 0.f;
		// Auto input range: the lowest and highest CV seen lately
		float autoLo = 0.f, autoHi = 0.f;
		bool autoInit = false;
	};
	Channel ch[16];

	// For the display (channel 1)
	int history[HISTORY];
	int histPos = 0, histCount = 0;
	int lastK = -1;
	bool lastAccent = false;
	int histScale = -1;
	Rng rng;

	Weighted() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		std::vector<std::string> roots;
		for (int i = 0; i < 12; i++) roots.push_back(ROOT_NAMES[fifthsToSemitone(i)]);
		configSwitch(ROOT_PARAM, 0.f, 11.f, 0.f, "Root (circle of fifths from C)", roots);
		std::vector<std::string> scales;
		for (int i = 0; i < NUM_SCALES; i++) scales.push_back(SCALES[i].name);
		configSwitch(SCALE_PARAM, 0.f, NUM_SCALES - 1, 1.f, "Scale", scales);
		std::vector<std::string> profiles(PROFILE_NAMES, PROFILE_NAMES + NUM_PROFILES);
		configSwitch(PROFILE_PARAM, 0.f, NUM_PROFILES - 1, P_ROCK, "Profile (where the weights come from)", profiles);
		configParam(FOCUS_PARAM, 0.f, 3.f, 1.f, "Focus (0 = plain quantizer, 1 = profile as published, 3 = exaggerated)");
		configParam(FOCUS_ATT_PARAM, -1.f, 1.f, 0.f, "Focus CV amount", "%", 0.f, 100.f);
		configParam(STEP_PARAM, 0.f, 1.f, 0.f, "Step (prefer small intervals)", "%", 0.f, 100.f);
		configParam(RANGE_PARAM, 1.f, 5.f, 2.f, "Range", " octaves");
		paramQuantities[RANGE_PARAM]->snapEnabled = true;
		configParam(LENGTH_PARAM, 2.f, 16.f, 4.f, "Accent cycle length", " steps");
		paramQuantities[LENGTH_PARAM]->snapEnabled = true;
		std::vector<std::string> strong = {"Off"};
		for (int i = 1; i <= 16; i++) strong.push_back(string::f("Step %d", i));
		configSwitch(STRONG_PARAM, 0.f, 16.f, 0.f, "Strong step (landing beat of the cycle)", strong);
		configSwitch(GRAVITY_PARAM, 0.f, 1.f, 0.f, "Mode", {"Bins (re-weight the whole range)", "Gravity (nearest note, pulled to strong notes)"});
		configSwitch(INVERT_PARAM, 0.f, 1.f, 0.f, "Invert (rare notes become common)", {"Off", "On"});
		configSwitch(RELATIVE_PARAM, 0.f, 1.f, 0.f, "Relative (same notes, home moves to the relative minor or major)", {"Off", "On"});
		configParam(OCTAVE_PARAM, -3.f, 3.f, 0.f, "Octave (shifts V/Oct, applied last)", " oct");
		getParamQuantity(OCTAVE_PARAM)->snapEnabled = true;
		configButton(LEARN_PARAM, "Learn: press, play some notes into CV in, press again");

		configInput(CV_INPUT, "CV (random voltage, or pitch in Gravity mode)");
		configInput(TRIG_INPUT, "Trigger (take a new note; unpatched = follow CV continuously)");
		configInput(ACCENT_INPUT, "Accent (high = use the landing profile)");
		configInput(ROOT_INPUT, "Root CV (1 V/oct, rounded to semitones)");
		configInput(SCALE_INPUT, "Scale CV (10 V sweeps the list)");
		configInput(PROFILE_INPUT, "Profile CV (10 V sweeps the list)");
		configInput(FOCUS_INPUT, "Focus CV (with the amount trimmer)");
		configInput(RELATIVE_INPUT, "Relative gate (high flips the Relative switch)");
		configOutput(VOCT_OUTPUT, "Pitch (1 V/oct, root at 0 V = C4 when Root is C)");
		configOutput(TRIG_OUTPUT, "Trigger on each new note");
		configOutput(DEGREE_OUTPUT, "Scale degree (1 V per scale step, first tone = 0 V)");
		configOutput(STABILITY_OUTPUT, "Stability (0-10 V: how strong the chosen note is)");
		for (int i = 0; i < HISTORY; i++) history[i] = -1;
		rng.seed(random::u64());
		for (int k = 0; k < 12; k++) driftMult[k] = 1.0;
	}

	void onReset() override {
		inputRange = 3;
		noRepeatSetting = 0;
		boostSetting = 1;
		gamelanTuning = 0;
		trigMode = 0;
		phraseEnd = 0;
		driftSetting = 0;
		chordSetting = 0;
		relativeTiming = 0;
		haveSlendro = havePelog = false;
		user.clear();
		learning = false;
		tableVersion++;
		clearHistory();
	}

	void clearHistory() {
		for (int i = 0; i < HISTORY; i++) history[i] = -1;
		histPos = histCount = 0;
		lastK = -1;
	}

	int cvIndex(int param, int input, int count) {
		float v = params[param].getValue();
		if (inputs[input].isConnected()) v += inputs[input].getVoltage() / 10.f * count;
		return clamp((int) std::round(v), 0, count - 1);
	}

	void startLearning() {
		learning = true;
		learnTotal = 0;
		learnHasLast = false;
		for (int i = 0; i < 12; i++) learnCounts[i] = 0;
		learnScale = settings.scale;
	}

	void finishLearning() {
		learning = false;
		if (learnTotal < 8) return;
		user.clear();
		for (int k = 0; k < table.n; k++)
			if (learnCounts[k] > 0) user.push_back({table.nominal[k], learnCounts[k]});
		userVersion++;
		tableVersion++;
		params[PROFILE_PARAM].setValue(P_USER);
	}

	static double noRepeatAmount(int s) {
		static const double v[4] = {0.0, 0.3, 0.6, 0.9};
		return v[clamp(s, 0, 3)];
	}

	void process(const ProcessArgs& args) override {
		// ---- settings
		Settings s;
		int rootCv = 0;
		if (inputs[ROOT_INPUT].isConnected()) rootCv = (int) std::round(inputs[ROOT_INPUT].getVoltage() * 12.f);
		s.root = fifthsToSemitone((int) params[ROOT_PARAM].getValue()) + rootCv;
		s.scale = cvIndex(SCALE_PARAM, SCALE_INPUT, NUM_SCALES);
		s.profile = cvIndex(PROFILE_PARAM, PROFILE_INPUT, NUM_PROFILES);
		// Relative: the gate flips the switch, so a sequencer can move in and out.
		bool relative = params[RELATIVE_PARAM].getValue() > 0.5f;
		if (inputs[RELATIVE_INPUT].isConnected()) relative = relative != (inputs[RELATIVE_INPUT].getVoltage() >= 1.f);
		// "At the end of the cycle": a change waits until the last step of the
		// accent cycle has played, so the next phrase starts in the new key.
		{
			int cycle = std::max(1, (int) params[LENGTH_PARAM].getValue());
			bool atBoundary = !inputs[TRIG_INPUT].isConnected() || ch[0].counter < 0 || ch[0].counter == cycle - 1;
			if (relativeTiming == 0 || atBoundary) relativeApplied = relative;
		}
		bool relativeWaiting = relative != relativeApplied;
		relative = relativeApplied;
		bool relativeActive = relative && relativeKey(s.root, s.scale);
		relativeShown = relativeActive;
		relativePending = relativeWaiting;
		double focus = params[FOCUS_PARAM].getValue();
		if (inputs[FOCUS_INPUT].isConnected())
			focus += inputs[FOCUS_INPUT].getVoltage() / 10.f * 3.f * params[FOCUS_ATT_PARAM].getValue();
		s.focus = clamp((float) focus, 0.f, 3.f);
		s.invert = params[INVERT_PARAM].getValue() > 0.5f;
		static const double boosts[3] = {1.5, 2.0, 3.0};
		s.landingBoost = boosts[clamp(boostSetting, 0, 2)];
		s.gamelanTuning = gamelanTuning;
		// Root only shifts the output, so it doesn't need a rebuild.
		Settings key = s;
		key.root = 0;
		if (pendingFlag && drawMutex.try_lock()) {
			user = pendingUser;
			pendingFlag = false;
			drawMutex.unlock();
			userVersion++;
			tableVersion++;
			params[PROFILE_PARAM].setValue(P_USER);
			key.profile = s.profile = P_USER;
		}
		bool driftOn = driftSetting > 0;
		if (driftOn && ++driftCounter >= 64) {
			driftCounter = 0;
			static const double periods[4] = {1.0, 60.0, 20.0, 5.0};
			drift.advance(64.0 * args.sampleTime, periods[clamp(driftSetting, 0, 3)], rng);
			drift.multipliers(1.0, driftMult);
			tableVersion++;
		}
		if (key != settings || builtVersion != tableVersion) {
			bool scaleChanged = key.scale != settings.scale;
			// Keep each channel's last pitch across a scale change (e.g. the
			// Relative switch), so Step carries on from where the melody was.
			double lastAbs[16];
			if (scaleChanged) {
				for (int c = 0; c < 16; c++) lastAbs[c] = 100.0 * lastRoot + noteCents(table, ch[c].last, false);
				clearHistory();
				for (int c = 0; c < 16; c++) ch[c].hasGravity = false;
				if (learning) startLearning();
			}
			settings = key;
			buildTable(table, settings, user, haveSlendro ? slendroCents : nullptr, havePelog ? pelogCents : nullptr,
			           driftOn ? driftMult : nullptr);
			if (scaleChanged)
				for (int c = 0; c < 16; c++) ch[c].last = nearestNote(table, lastAbs[c] - 100.0 * s.root);
			builtVersion = tableVersion;
		}

		// ---- learn button
		if (learnButton.process(params[LEARN_PARAM].getValue() > 0.f)) {
			if (learning) finishLearning();
			else startLearning();
		}

		bool gravity = params[GRAVITY_PARAM].getValue() > 0.5f;
		int range = (int) params[RANGE_PARAM].getValue();
		int lowOct = -(range / 2);
		int len = (int) params[LENGTH_PARAM].getValue();
		int strong = (int) params[STRONG_PARAM].getValue();
		double step = params[STEP_PARAM].getValue();
		double noRepeat = noRepeatAmount(noRepeatSetting);
		float lo = inputRange == 0 ? -5.f : 0.f;
		float hi = inputRange == 1 ? 10.f : 5.f;
		lastRoot = s.root;
		// Auto range: widen at once to any new extreme, shrink back slowly
		// (about 30 s) so a source that changes its range is followed.
		float autoShrink = 1.f - std::exp(-args.sampleTime / 30.f);
		bool triggered = inputs[TRIG_INPUT].isConnected();
		double rootVolts = s.root / 12.0;
		double octaveVolts = std::round(params[OCTAVE_PARAM].getValue());

		static const int chordSteps[4][3] = {{0}, {0, 2}, {0, 2, 4}, {0, 4}};
		static const int chordSize[4] = {1, 2, 3, 2};
		int voices = chordSize[clamp(chordSetting, 0, 3)];
		int channels = std::max(1, inputs[CV_INPUT].getChannels());
		channels = std::min(channels, 16 / voices);
		for (int c = 0; c < channels; c++) {
			Channel& C = ch[c];
			float v = inputs[CV_INPUT].getVoltage(c);
			if (inputRange == 3) {
				if (!C.autoInit) { C.autoLo = C.autoHi = v; C.autoInit = true; }
				C.autoLo = std::min(v, C.autoLo + (v - C.autoLo) * autoShrink);
				C.autoHi = std::max(v, C.autoHi + (v - C.autoHi) * autoShrink);
			}
			bool take = true;
			if (triggered) take = C.trig.process(inputs[TRIG_INPUT].getPolyVoltage(c), 0.1f, 1.f);
			if (take) {
				// accent: input wins, otherwise the internal counter
				bool accent;
				if (triggered) C.counter = (C.counter + 1) % std::max(1, len);
				if (inputs[ACCENT_INPUT].isConnected()) accent = inputs[ACCENT_INPUT].getPolyVoltage(c) >= 1.f;
				else accent = triggered && strong > 0 && C.counter == (strong - 1) % std::max(1, len);
				const double* w = accent ? table.land : table.w;

				Note n;
				if (!gravity) {
					double u;
					if (inputRange == 3) {
						// at least 1 V wide, so a still or barely moving CV sits in the middle
						float span = C.autoHi - C.autoLo;
						float mid = 0.5f * (C.autoHi + C.autoLo);
						float a = span < 1.f ? mid - 0.5f : C.autoLo;
						float b = span < 1.f ? mid + 0.5f : C.autoHi;
						u = (v - a) / (b - a);
					}
					else u = (v - lo) / (hi - lo);
					bool useHistory = triggered && C.hasLast;
					n = pickBins(table, w, u, range, lowOct, useHistory ? &C.last : nullptr, triggered ? step : 0.0,
					             triggered ? noRepeat : 0.0, accent ? &stepCacheLand : &stepCacheW);
				}
				else {
					double cents = (v - rootVolts) * 1200.0;
					Note near = nearestNote(table, cents);
					// Continuous mode: only re-decide when the nearest note changes,
					// so the output doesn't flicker.
					if (triggered || !C.hasGravity || near != C.gravityNearest) {
						double pull = std::min(1.0, s.focus / 2.0);
						n = rng.uniform() < pull ? gravityTarget(table, w, near, 300.0) : near;
						C.gravityNearest = near;
						C.hasGravity = true;
					}
					else n = C.last;
				}

				// Phrase ending: the last step of the accent cycle lands home.
				if (triggered && phraseEnd > 0 && C.counter == std::max(1, len) - 1) {
					int home = phraseEnd == 1 ? 0 : strongestIndex(table.w, table.n);
					n = nearestWithTone(table, home, C.hasLast ? C.last : n);
				}

				bool changed = !C.hasLast || n != C.last;
				if (triggered ? (trigMode == 0 || changed) : changed) C.pulse.trigger(1e-3f);
				if (c == 0 && (changed || triggered)) {
					history[histPos] = n.k;
					histPos = (histPos + 1) % HISTORY;
					histCount = std::min(HISTORY, histCount + 1);
					lastK = n.k;
					lastAccent = accent;
				}
				// Learning counts the notes you play in (the input pitch,
				// quantized to the scale), not the module's own output.
				if (learning && c == 0) {
					Note heard = nearestNote(table, (v - rootVolts) * 1200.0);
					if (triggered || !learnHasLast || heard != learnLast) {
						learnCounts[heard.k] += 1;
						learnTotal++;
					}
					learnLast = heard;
					learnHasLast = true;
				}
				C.last = n;
				C.hasLast = true;
				C.accent = accent;
			}
			const double* w = C.accent ? table.land : table.w;
			double mx = C.accent ? table.maxLand : table.maxW;
			float trigV = C.pulse.process(args.sampleTime) ? 10.f : 0.f;
			for (int j = 0; j < voices; j++) {
				Note cn = addSteps(table, C.last, chordSteps[clamp(chordSetting, 0, 3)][j]);
				outputs[VOCT_OUTPUT].setVoltage(rootVolts + noteCents(table, cn) / 1200.0 + octaveVolts, c * voices + j);
				outputs[TRIG_OUTPUT].setVoltage(trigV, c * voices + j);
			}
			outputs[DEGREE_OUTPUT].setVoltage((float) C.last.k, c);
			outputs[STABILITY_OUTPUT].setVoltage(mx > 0 ? (float) (10.0 * w[C.last.k] / mx) : 0.f, c);
		}
		outputs[VOCT_OUTPUT].setChannels(channels * voices);
		outputs[TRIG_OUTPUT].setChannels(channels * voices);
		outputs[DEGREE_OUTPUT].setChannels(channels);
		outputs[STABILITY_OUTPUT].setChannels(channels);

		lights[GRAVITY_LIGHT].setBrightness(gravity ? 1.f : 0.f);
		lights[INVERT_LIGHT].setBrightness(s.invert ? 1.f : 0.f);
		lights[LEARN_LIGHT].setBrightnessSmooth(learning ? (0.5f + 0.5f * std::sin(args.frame * args.sampleTime * 8.f)) : 0.f, args.sampleTime);
		// Waiting for the cycle to end: the light blinks.
		if (relativeWaiting) lights[RELATIVE_LIGHT].setBrightness(std::fmod(args.frame * args.sampleTime, 0.25f) < 0.125f ? 1.f : 0.f);
		else lights[RELATIVE_LIGHT].setBrightness(relative ? (relativeActive ? 1.f : 0.25f) : 0.f);
		lights[ACCENT_LIGHT].setBrightnessSmooth(ch[0].accent ? 1.f : 0.f, args.sampleTime);
	}

	// ---- Scala loading
	std::string loadScala(const std::string& path) {
		std::ifstream f(path);
		if (!f) return "Could not open the file";
		std::stringstream ss;
		ss << f.rdbuf();
		std::vector<double> c;
		std::string err = parseScala(ss.str(), c);
		if (!err.empty()) return err;
		std::string name = system::getStem(path);
		if (c.size() == 5) {
			for (int i = 0; i < 5; i++) slendroCents[i] = c[i];
			haveSlendro = true;
			slendroName = name;
		}
		else if (c.size() == 7) {
			for (int i = 0; i < 7; i++) pelogCents[i] = c[i];
			havePelog = true;
			pelogName = name;
		}
		else return string::f("The file has %d notes per octave. Use 5 for slendro or 7 for pelog.", (int) c.size());
		tableVersion++;
		return "";
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "profileLayout", json_integer(2));
		json_object_set_new(r, "rootLayout", json_integer(2));
		json_object_set_new(r, "inputRange", json_integer(inputRange));
		json_object_set_new(r, "noRepeat", json_integer(noRepeatSetting));
		json_object_set_new(r, "boost", json_integer(boostSetting));
		json_object_set_new(r, "gamelanTuning", json_integer(gamelanTuning));
		json_object_set_new(r, "trigMode", json_integer(trigMode));
		json_object_set_new(r, "phraseEnd", json_integer(phraseEnd));
		json_object_set_new(r, "drift", json_integer(driftSetting));
		json_object_set_new(r, "chord", json_integer(chordSetting));
		json_object_set_new(r, "relativeTiming", json_integer(relativeTiming));
		if (haveSlendro) {
			json_t* a = json_array();
			for (int i = 0; i < 5; i++) json_array_append_new(a, json_real(slendroCents[i]));
			json_object_set_new(r, "slendro", a);
			json_object_set_new(r, "slendroName", json_string(slendroName.c_str()));
		}
		if (havePelog) {
			json_t* a = json_array();
			for (int i = 0; i < 7; i++) json_array_append_new(a, json_real(pelogCents[i]));
			json_object_set_new(r, "pelog", a);
			json_object_set_new(r, "pelogName", json_string(pelogName.c_str()));
		}
		json_t* u = json_array();
		for (const UserPoint& p : user) {
			json_t* e = json_array();
			json_array_append_new(e, json_real(p.cents));
			json_array_append_new(e, json_real(p.weight));
			json_array_append_new(u, e);
		}
		json_object_set_new(r, "user", u);
		return r;
	}

	void dataFromJson(json_t* r) override {
		// Patches from 2.0.0 had Pathet 1-3 and User at 7-10; five style
		// profiles were inserted before them in 2.1.0.
		// Before 2.3.0 the Root knob was chromatic; it now follows the circle of fifths.
		if (!json_object_get(r, "rootLayout"))
			params[ROOT_PARAM].setValue(semitoneToFifths((int) std::round(params[ROOT_PARAM].getValue())));
		if (!json_object_get(r, "profileLayout")) {
			float p = params[PROFILE_PARAM].getValue();
			if (p >= 7.f) params[PROFILE_PARAM].setValue(p + 5.f);
		}
		auto geti = [&](const char* k, int& dst) {
			json_t* j = json_object_get(r, k);
			if (j) dst = json_integer_value(j);
		};
		geti("inputRange", inputRange);
		geti("noRepeat", noRepeatSetting);
		geti("boost", boostSetting);
		geti("gamelanTuning", gamelanTuning);
		geti("trigMode", trigMode);
		geti("phraseEnd", phraseEnd);
		geti("drift", driftSetting);
		geti("chord", chordSetting);
		geti("relativeTiming", relativeTiming);
		json_t* a = json_object_get(r, "slendro");
		haveSlendro = a && json_array_size(a) == 5;
		if (haveSlendro)
			for (int i = 0; i < 5; i++) slendroCents[i] = json_number_value(json_array_get(a, i));
		a = json_object_get(r, "pelog");
		havePelog = a && json_array_size(a) == 7;
		if (havePelog)
			for (int i = 0; i < 7; i++) pelogCents[i] = json_number_value(json_array_get(a, i));
		json_t* n = json_object_get(r, "slendroName");
		if (n) slendroName = json_string_value(n);
		n = json_object_get(r, "pelogName");
		if (n) pelogName = json_string_value(n);
		user.clear();
		json_t* u = json_object_get(r, "user");
		if (u) {
			for (size_t i = 0; i < json_array_size(u); i++) {
				json_t* e = json_array_get(u, i);
				user.push_back({json_number_value(json_array_get(e, 0)), json_number_value(json_array_get(e, 1))});
			}
		}
		userVersion++;
		tableVersion++;
	}
};

// ---------------------------------------------------------------- display
// Paper panel: black display, paper-white bars, red for the note just played
static const NVGcolor C_BG = nvgRGB(0x15, 0x15, 0x15);
static const NVGcolor C_INK = nvgRGB(0xEC, 0xE8, 0xDF);
static const NVGcolor C_SOFT = nvgRGB(0x77, 0x73, 0x6B);
static const NVGcolor C_BAR = nvgRGB(0xEC, 0xE8, 0xDF);
static const NVGcolor C_LAND = nvgRGB(0xD7, 0x36, 0x2B);
static const NVGcolor C_HIT = nvgRGB(0x7F, 0xA8, 0xC9);
static const NVGcolor C_PLAYED = nvgRGB(0xD7, 0x36, 0x2B);

struct WeightDisplay : Widget {
	Weighted* module = nullptr;
	// bar geometry from the last draw, for clicking
	float barTop = 0, barBottom = 0, barLeft = 0, barW = 0;
	int barN = 0;
	double barMax = 1.0;
	// drawing state
	bool drawing = false;
	Vec dragPos;
	std::vector<UserPoint> edit;
	double editMax = 1.0;

	void beginEdit() {
		Weighted* m = module;
		const Table& T = m->table;
		edit.clear();
		for (int k = 0; k < T.n; k++) edit.push_back({T.nominal[k], T.w[k]});
		editMax = barMax;
		drawing = true;
	}

	void editAt(Vec p) {
		if (!module || barN <= 0 || barW <= 0 || (int) edit.size() != barN) return;
		int k = (int) std::floor((p.x - barLeft) / barW);
		if (k < 0 || k >= barN) return;
		double frac = clamp((barBottom - p.y) / std::max(1.f, barBottom - barTop), 0.f, 1.f);
		edit[k].weight = std::max(0.001, frac * editMax);
		std::lock_guard<std::mutex> lock(module->drawMutex);
		module->pendingUser = edit;
		module->pendingFlag = true;
	}

	void onButton(const ButtonEvent& e) override {
		if (module && e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && e.pos.y >= barTop && e.pos.y <= barBottom) {
			e.consume(this);
			beginEdit();
			dragPos = e.pos;
			editAt(dragPos);
			return;
		}
		Widget::onButton(e);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (!drawing || e.button != GLFW_MOUSE_BUTTON_LEFT) return;
		dragPos = dragPos.plus(e.mouseDelta.div(getAbsoluteZoom()));
		editAt(dragPos);
	}

	void onDragEnd(const DragEndEvent& e) override {
		drawing = false;
	}

	void text(const DrawArgs& a, float x, float y, const std::string& s, NVGcolor c, int align, float size) {
		nvgFontSize(a.vg, size);
		nvgTextAlign(a.vg, align);
		nvgFillColor(a.vg, c);
		nvgText(a.vg, x, y, s.c_str(), NULL);
	}
	void rect(const DrawArgs& a, float x, float y, float w, float h, NVGcolor c) {
		nvgBeginPath(a.vg);
		nvgRect(a.vg, x, y, w, h);
		nvgFillColor(a.vg, c);
		nvgFill(a.vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) { Widget::drawLayer(args, layer); return; }
		float W = box.size.x, H = box.size.y;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, W, H, mm2px(0.6f));
		nvgFillColor(args.vg, C_BG);
		nvgFill(args.vg);
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);
		float pad = mm2px(1.6f);
		float fs = mm2px(2.4f), fsSmall = mm2px(2.0f);

		if (!module) {
			text(args, W / 2, H / 2, "WEIGHTED", C_INK, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, fs);
			return;
		}
		Weighted* m = module;
		const Table& T = m->table;
		const ScaleDef& sc = SCALES[m->settings.scale];
		int root = ((m->lastRoot % 12) + 12) % 12;

		float ty = pad + fs * 0.5f;
		text(args, pad, ty, string::f("%s %s", ROOT_NAMES[root], sc.shortName), C_INK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, fs);
		text(args, W - pad, ty, PROFILE_SHORT[m->settings.profile], C_BAR, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, fs);

		// bars: target weight (filled), recent output share (teal tick)
		int n = T.n;
		if (n <= 0) return;
		float top = pad + fs * 1.4f, bottom = H - pad - fsSmall * 2.6f;
		float bw = (W - 2 * pad) / n;
		double mx = 0.0;
		double share[12] = {};
		int total = 0;
		for (int i = 0; i < m->histCount; i++) {
			int k = m->history[i];
			if (k >= 0 && k < n) { share[k] += 1; total++; }
		}
		for (int k = 0; k < n; k++) {
			mx = std::max(mx, T.w[k]);
			mx = std::max(mx, T.land[k]);
			if (total) mx = std::max(mx, share[k] / total);
		}
		if (mx <= 0) mx = 1;
		barTop = top;
		barBottom = bottom;
		barLeft = pad;
		barW = bw;
		barN = n;
		barMax = mx;
		float hgt = bottom - top;
		for (int k = 0; k < n; k++) {
			float x = pad + k * bw;
			float h = (float) (hgt * T.w[k] / mx);
			bool hit = k == m->lastK;
			NVGcolor col = hit ? C_PLAYED : C_BAR;
			rect(args, x + bw * 0.18f, bottom - h, bw * 0.64f, h, hit ? col : nvgTransRGBA(col, 200));
			// landing level as a thin orange line
			float hl = (float) (hgt * T.land[k] / mx);
			rect(args, x + bw * 0.12f, bottom - hl - 0.5f, bw * 0.76f, 1.0f, nvgTransRGBA(C_LAND, 150));
			if (total > 8) {
				float hs = (float) (hgt * (share[k] / total) / mx);
				rect(args, x + bw * 0.3f, bottom - hs - 1.0f, bw * 0.4f, 2.0f, C_HIT);
			}
			text(args, x + bw / 2, bottom + fsSmall * 0.9f, T.labels[k], hit ? C_PLAYED : C_SOFT, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, fsSmall);
		}

		// bottom line
		float by = H - pad - fsSmall * 0.4f;
		std::string left;
		if (m->learning) left = string::f("LEARNING  %d notes", m->learnTotal);
		else if (drawing) left = "DRAWING USER WEIGHTS";
		else left = string::f("FOCUS %.2f%s  %s%s%s", m->settings.focus, m->settings.invert ? " INV" : "",
		                      m->params[Weighted::GRAVITY_PARAM].getValue() > 0.5f ? "GRAVITY" : "BINS",
		                      m->driftSetting > 0 ? "  DRIFT" : "",
		                      m->relativePending ? "  REL.." : (m->relativeShown ? "  REL" : ""));
		text(args, pad, by, left, m->learning ? C_LAND : C_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, fsSmall);
		if (m->lastK >= 0 && m->lastK < n)
			text(args, W - pad, by, string::f("%s%s  %2.0f%%", m->lastAccent ? "LAND " : "", T.labels[m->lastK].c_str(), 100.0 * T.w[m->lastK]),
			     m->lastAccent ? C_LAND : C_INK, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, fsSmall);
	}
};

// ---------------------------------------------------------------- panel
struct WeightedWidget : ModuleWidget {
	WeightedWidget(Weighted* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Weighted.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Positions in mm, matching res/make_panel.py (20 HP)
		WeightDisplay* d = createWidget<WeightDisplay>(mm2px(Vec(4.5, 14.0)));
		d->box.size = mm2px(Vec(92.6, 22.0));
		d->module = module;
		addChild(d);

		float col[4], c5[5], ix[8];
		for (int i = 0; i < 4; i++) col[i] = 12.7f + 25.4f * i;
		for (int i = 0; i < 5; i++) c5[i] = 12.7f + 19.05f * i;
		for (int i = 0; i < 8; i++) ix[i] = 7.6f + 12.343f * i;
		const float rowA = 45.0, rowB = 65.0, rowC = 82.5, rowIn = 102.0, rowOut = 117.6;

		addParam(createParamCentered<Davies1900hWhiteKnob>(mm2px(Vec(col[0], rowA)), module, Weighted::ROOT_PARAM));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(mm2px(Vec(col[1], rowA)), module, Weighted::RELATIVE_PARAM, Weighted::RELATIVE_LIGHT));
		addParam(createParamCentered<Davies1900hWhiteKnob>(mm2px(Vec(col[2], rowA)), module, Weighted::SCALE_PARAM));
		addParam(createParamCentered<Davies1900hWhiteKnob>(mm2px(Vec(col[3], rowA)), module, Weighted::PROFILE_PARAM));

		addParam(createParamCentered<Davies1900hRedKnob>(mm2px(Vec(col[0], rowB)), module, Weighted::FOCUS_PARAM));
		addParam(createParamCentered<Davies1900hWhiteKnob>(mm2px(Vec(col[1], rowB)), module, Weighted::STEP_PARAM));
		addParam(createParamCentered<Davies1900hWhiteKnob>(mm2px(Vec(col[2], rowB)), module, Weighted::RANGE_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(col[3], rowB)), module, Weighted::FOCUS_ATT_PARAM));

		addParam(createParamCentered<BefacoTinyKnob>(mm2px(Vec(c5[0], rowC)), module, Weighted::LENGTH_PARAM));
		addParam(createParamCentered<BefacoTinyKnob>(mm2px(Vec(c5[1], rowC)), module, Weighted::STRONG_PARAM));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(c5[1] + 5.6f, rowC - 4.0f)), module, Weighted::ACCENT_LIGHT));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(c5[2], rowC)), module, Weighted::GRAVITY_PARAM, Weighted::GRAVITY_LIGHT));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(c5[3], rowC)), module, Weighted::INVERT_PARAM, Weighted::INVERT_LIGHT));
		addParam(createLightParamCentered<VCVLightBezel<YellowLight>>(mm2px(Vec(c5[4], rowC)), module, Weighted::LEARN_PARAM, Weighted::LEARN_LIGHT));

		const int ins[8] = {Weighted::CV_INPUT, Weighted::TRIG_INPUT, Weighted::ACCENT_INPUT, Weighted::ROOT_INPUT,
		                    Weighted::SCALE_INPUT, Weighted::RELATIVE_INPUT, Weighted::PROFILE_INPUT, Weighted::FOCUS_INPUT};
		for (int i = 0; i < 8; i++)
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(ix[i], rowIn)), module, ins[i]));
		addParam(createParamCentered<BefacoTinyKnob>(mm2px(Vec(c5[0], rowOut)), module, Weighted::OCTAVE_PARAM));
		const int outs[4] = {Weighted::VOCT_OUTPUT, Weighted::TRIG_OUTPUT, Weighted::DEGREE_OUTPUT, Weighted::STABILITY_OUTPUT};
		for (int i = 0; i < 4; i++)
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(c5[i + 1], rowOut)), module, outs[i]));
	}

	void appendContextMenu(Menu* menu) override {
		Weighted* m = getModule<Weighted>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("CV input range (Bins mode)", {"-5 V to +5 V", "0 V to 10 V", "0 V to 5 V", "Auto (follows your CV)"}, &m->inputRange));
		menu->addChild(createIndexPtrSubmenuItem("No-repeat (triggered mode)", {"Off", "Low", "Medium", "High"}, &m->noRepeatSetting));
		menu->addChild(createIndexPtrSubmenuItem("Landing boost (accented notes)", {"Top two tones x1.5", "Top two tones x2", "Top two tones x3"}, &m->boostSetting));
		menu->addChild(createIndexPtrSubmenuItem("Trigger output", {"Every new note", "Only when the pitch changes"}, &m->trigMode));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Shape"));
		menu->addChild(createIndexPtrSubmenuItem("Phrase ending (last step of the accent cycle)",
			{"Off", "Lands on the first scale tone (root)", "Lands on the strongest tone"}, &m->phraseEnd));
		menu->addChild(createIndexPtrSubmenuItem("Drift (weights wander on their own)",
			{"Off", "Slow (about a minute)", "Medium (about 20 s)", "Fast (about 5 s)"}, &m->driftSetting));
		menu->addChild(createIndexPtrSubmenuItem("Relative changes",
			{"At once", "At the end of the accent cycle (with Trig patched)"}, &m->relativeTiming));
		menu->addChild(createIndexPtrSubmenuItem("Chord on V/Oct (polyphonic)",
			{"Off (single note)", "Add the 3rd above (in the scale)", "Add the 3rd and 5th", "Add the 5th"}, &m->chordSetting));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Gamelan tuning"));
		menu->addChild(createIndexPtrSubmenuItem("Slendro and pelog pitches", {"Gamelan tuning (default or loaded)", "Snap to 12-TET (play with Western gear)"}, &m->gamelanTuning));
		menu->addChild(createMenuItem("Load Scala tuning (.scl, 5 or 7 notes)...", "", [=]() {
			osdialog_filters* filters = osdialog_filters_parse("Scala:scl");
			char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
			osdialog_filters_free(filters);
			if (!path) return;
			std::string err = m->loadScala(path);
			std::free(path);
			if (!err.empty()) osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, err.c_str());
		}));
		menu->addChild(createMenuLabel(std::string("Slendro: ") + (m->haveSlendro ? m->slendroName : "default (5 equal steps)")));
		menu->addChild(createMenuLabel(std::string("Pelog: ") + (m->havePelog ? m->pelogName : "default (9-TET subset)")));
		menu->addChild(createMenuItem("Reset gamelan tunings to default", "", [=]() {
			m->haveSlendro = m->havePelog = false;
			m->tableVersion++;
		}));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(m->user.empty() ? "User profile: empty (use Learn)" : string::f("User profile: %d tones learned", (int) m->user.size())));
		menu->addChild(createMenuItem("Clear user profile", "", [=]() {
			m->user.clear();
			m->tableVersion++;
		}));
	}
};

Model* modelWeighted = createModel<Weighted, WeightedWidget>("Weighted");
