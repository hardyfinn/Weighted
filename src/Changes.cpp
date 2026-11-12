// The Changes: chords the way the style plays them.
// A chord sequencer that leads Weighted over the HARM cable and follows Long
// Form over LINK. The chord logic is in ChangesEngine.hpp.
#include "plugin.hpp"
#include "ChangesEngine.hpp"
#include <atomic>

using namespace changes;

static const float RATE_BARS[5] = {0.5f, 1.f, 2.f, 4.f, 8.f};
static const int LOOP_LENS[4] = {0, 2, 4, 8};
static const int PPQN_TICKS[4] = {16, 96, 8, 4};  // clock ticks per bar: 16ths, 24 PPQN, 8ths, quarters

template <typename TBase = GrayModuleLightWidget>
struct TOrangeLight : TBase {
	TOrangeLight() { this->addBaseColor(nvgRGB(0xef, 0x7a, 0x37)); }
};

struct Changes : Module {
	enum ParamId {
		STYLE_PARAM, ROOT_PARAM, MODE_PARAM, RATE_PARAM, LOOP_PARAM,
		FOCUS_PARAM, COLOUR_PARAM, SPREAD_PARAM, DRIFT_PARAM,
		HOLD_PARAM, NEW_PARAM, LOCK_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		FOCUS_INPUT, COLOUR_INPUT, SPREAD_INPUT, DRIFT_INPUT,
		CLOCK_INPUT, RESET_INPUT, CHANGE_INPUT, HOLD_INPUT, TRANSPOSE_INPUT, LINK_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		CHORD_OUTPUT, BASS_OUTPUT, ROOT_OUTPUT, TOP_OUTPUT, GATE_OUTPUT, CHANGE_OUTPUT, TENSION_OUTPUT, HARM_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		HOLD_LIGHT, NEW_LIGHT, LOCK_LIGHT, ENUMS(TENSION_LIGHT, 6),
		LIGHTS_LEN
	};

	// menu settings
	int voicesSetting = 0;   // 0 auto (by style), 1-4 = 3-6 voices
	int bassInversions = 1;  // classical and gospel may put the 3rd or 5th in the bass
	int ppqn = 0;            // CLOCK: 16ths, 24 PPQN, 8ths, quarters
	int changeTiming = 0;    // CHANGE input: 0 at once, 1 on the next beat
	int energyDrives = 2;    // LINK energy: 0 colour, 1 tension, 2 both, 3 off

	// loops: one per Long Form section, plus one free (index 8)
	Loop loops[NUM_LOOPS];
	int curLoop = 8;
	int pos = -1;
	Slot cur;
	bool haveChord = false;
	Rng rng;

	// what the loops were made with
	int madeStyle = -1, madeMode = -1, madeLen = -1;

	// clock
	dsp::SchmittTrigger clockTrig, resetTrig, changeTrig, linkBeat, linkChange, linkReset, newButton;
	long tickCount = -1;
	bool changePending = false, newPending = false;

	// chord being played
	Chord chord;
	std::vector<int> voicing, prevVoicing;
	float outChord[6] = {};
	int outN = 0;
	float outBass = 0, outRoot = 0, outTop = 0, outTension = 0;
	int harmTones[6] = {};
	int harmN = 0;
	dsp::PulseGenerator changePulse, gateGap;
	int recalcCounter = 0;
	bool needRecalc = true;
	int lastVoices = -1;
	float lastColour = -1, lastSpread = -1;
	int lastKey = -100, lastMode = -1, lastStyle = -1, lastBassInv = -1;

	// for the display (UI thread reads these)
	int shownStyle = S_HOUSE, shownKey = 5, shownMode = M_MINOR, shownSection = -1;
	float shownColour = 0.5f;
	bool shownHold = false, shownLink = false;
	float driftFlash = 0.f;
	int driftFlashSlot = -1;
	std::atomic<int> rerollRequest{-1};
	std::atomic<int> setRequest{-1};  // slot * 64 + entry

	Changes() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		std::vector<std::string> styles(STYLE_NAMES, STYLE_NAMES + NUM_STYLES);
		configSwitch(STYLE_PARAM, 0.f, NUM_STYLES - 1, S_HOUSE, "Style", styles);
		std::vector<std::string> roots;
		for (int i = 0; i < 12; i++) roots.push_back(NOTE_NAMES[(i * 7) % 12]);
		configSwitch(ROOT_PARAM, 0.f, 11.f, 11.f, "Key (circle of fifths from C)", roots);  // 11 = F
		configSwitch(MODE_PARAM, 0.f, 4.f, 0.f, "Mode", {"Style (its home key)", "Major", "Minor", "Dorian", "Mixolydian"});
		configSwitch(RATE_PARAM, 0.f, 4.f, 1.f, "Bars per chord (when clocked)", {"1/2", "1", "2", "4", "8"});
		configSwitch(LOOP_PARAM, 0.f, 3.f, 2.f, "Loop", {"Off (a new chord every time)", "2 chords", "4 chords", "8 chords"});
		configParam(FOCUS_PARAM, 0.f, 3.f, 1.f, "Focus (0 = any chord in the key, 1 = the style as written, 3 = only its favourite moves)");
		configParam(COLOUR_PARAM, 0.f, 1.f, 0.5f, "Colour (triad, 7th, 9th, 11th, 13th)", "%", 0.f, 100.f);
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.f, "Spread (close to wide voicing)", "%", 0.f, 100.f);
		configParam(DRIFT_PARAM, 0.f, 1.f, 0.2f, "Drift (chance one chord of the loop is rewritten after each pass)", "%", 0.f, 100.f);
		configSwitch(HOLD_PARAM, 0.f, 1.f, 0.f, "Hold (stay on this chord)", {"Off", "On"});
		configButton(NEW_PARAM, "New loop (on the next bar line)");
		configSwitch(LOCK_PARAM, 0.f, 1.f, 0.f, "Lock (no drift edits)", {"Off", "On"});
		configInput(FOCUS_INPUT, "Focus CV (10 V = full range)");
		configInput(COLOUR_INPUT, "Colour CV (10 V = full range)");
		configInput(SPREAD_INPUT, "Spread CV (10 V = full range)");
		configInput(DRIFT_INPUT, "Drift CV (10 V = full range)");
		configInput(CLOCK_INPUT, "Clock (16ths; right-click for other rates). Ignored when LINK is patched");
		configInput(RESET_INPUT, "Reset (back to the first chord of the loop)");
		configInput(CHANGE_INPUT, "Change (next chord now)");
		configInput(HOLD_INPUT, "Hold gate (stay on this chord while high)");
		configInput(TRANSPOSE_INPUT, "Transpose (1 V/oct, rounded to semitones)");
		configInput(LINK_INPUT, "Link from Long Form (beat, section change, energy, reset, section number)");
		configOutput(CHORD_OUTPUT, "Chord (poly 1 V/oct)");
		configOutput(BASS_OUTPUT, "Bass (an octave below the chord)");
		configOutput(ROOT_OUTPUT, "Root (the chord's root, whatever the inversion)");
		configOutput(TOP_OUTPUT, "Top (the highest voice)");
		configOutput(GATE_OUTPUT, "Gate (high while the chord plays, with a short gap at each change)");
		configOutput(CHANGE_OUTPUT, "Change trigger");
		configOutput(TENSION_OUTPUT, "Tension (0-10 V: how far the chord is from home)");
		configOutput(HARM_OUTPUT, "Harm (to Weighted's HARM input)");
		rng.seed(random::u64());
	}

	void onReset() override {
		voicesSetting = 0;
		bassInversions = 1;
		ppqn = 0;
		changeTiming = 0;
		energyDrives = 2;
		for (Loop& L : loops) L.made = false;
		madeStyle = -1;
		curLoop = 8;
		pos = -1;
		haveChord = false;
		tickCount = -1;
		prevVoicing.clear();
		needRecalc = true;
	}

	int modeOf(int style) {
		int m = (int) params[MODE_PARAM].getValue();
		return m == 0 ? STYLE_MODE[style] : clamp(m - 1, 0, NUM_MODES - 1);
	}
	int styleNow() { return clamp((int) params[STYLE_PARAM].getValue(), 0, NUM_STYLES - 1); }
	int loopLenNow() { return LOOP_LENS[clamp((int) params[LOOP_PARAM].getValue(), 0, 3)]; }
	float cvParam(int p, int in, float range) {
		return params[p].getValue() + inputs[in].getVoltage() / 10.f * range;
	}

	Loop& loopNow() {
		Loop& L = loops[clamp(curLoop, 0, NUM_LOOPS - 1)];
		return L;
	}
	void ensureLoop(Loop& L, double focus, double tension) {
		int len = std::max(1, loopLenNow());
		if (!L.made) makeLoop(L, styleNow(), len, focus, tension, rng);
	}

	// Next chord.
	void advance(double focus, double tension, double drift, bool lock) {
		int style = styleNow();
		int len = loopLenNow();
		if (len == 0) {
			cur = haveChord ? rollSlot(style, cur.entry, focus, tension, rng) : rollSlot(style, -1, 0.0, 0.0, rng);
			if (!haveChord) cur.entry = homeEntry(style);
		}
		else {
			Loop& L = loopNow();
			ensureLoop(L, focus, tension);
			pos = pos + 1;
			if (pos >= L.len) {
				pos = 0;
				if (!lock && rng.uniform() < drift) {
					int i = driftLoop(L, style, focus, tension, rng);
					if (i >= 0) { driftFlash = 1.f; driftFlashSlot = i; }
				}
			}
			cur = L.slots[pos];
		}
		haveChord = true;
		prevVoicing = voicing;
		needRecalc = true;
		changePulse.trigger(1e-3f);
		gateGap.trigger(5e-3f);
	}

	void startLoop(double focus, double tension) {
		pos = -1;
		Loop& L = loopNow();
		ensureLoop(L, focus, tension);
	}

	void recalc(int style, int mode, int key, double colour, double spread) {
		chord = realize(style, mode, cur, colour);
		int voices = voicesSetting == 0 ? STYLE_VOICES[style] : voicesSetting + 2;
		if (chord.drone) {
			outN = 3;
			for (int i = 0; i < 3; i++) outChord[i] = (float) ((key * 100.0 + chord.droneCents[i] - 1200.0) / 1200.0);
			outBass = (float) ((key * 100.0 + chord.droneCents[0] - 2400.0) / 1200.0);
			outRoot = (float) ((key * 100.0 + chord.droneCents[0]) / 1200.0);
			outTop = outChord[2];
			voicing.clear();
			harmN = 2;
			harmTones[0] = (int) std::lround(key + chord.droneCents[0] / 100.0);
			harmTones[1] = (int) std::lround(key + chord.droneCents[1] / 100.0);
		}
		else {
			std::vector<int> tones = chordTones(chord, voices, STYLE_ROOTLESS[style]);
			voicing = voiceLead(tones, key, prevVoicing, spread, 5);
			outN = std::min(6, (int) voicing.size());
			for (int i = 0; i < outN; i++) outChord[i] = voicing[i] / 12.f;
			int rootPc = ((key + chord.root) % 12 + 12) % 12;
			int bassPc = rootPc;
			const StyleDef& sd = styleDef(style);
			const Entry& e = sd.entries[clamp(cur.entry, 0, (int) sd.entries.size() - 1)];
			if (bassInversions && (e.flags & INV) && chord.third >= 0 && chord.third != 2 && chord.third != 5) {
				if (cur.dice[3] < 0.1f) bassPc = (rootPc + chord.fifth) % 12;
				else if (cur.dice[3] < 0.3f) bassPc = (rootPc + chord.third) % 12;
			}
			outBass = (-24 + bassPc) / 12.f;
			outRoot = rootPc / 12.f;
			outTop = voicing.empty() ? 0.f : voicing.back() / 12.f;
			harmN = 0;
			for (int iv : chord.iv) {
				int pc = ((key + chord.root + iv) % 12 + 12) % 12;
				bool dup = false;
				for (int j = 0; j < harmN; j++)
					if (harmTones[j] == pc) dup = true;
				if (!dup && harmN < 6) harmTones[harmN++] = pc;
			}
		}
		outTension = (float) tensionOf(chord);
	}

	void process(const ProcessArgs& args) override {
		int style = styleNow();
		int mode = modeOf(style);
		int key = (((int) params[ROOT_PARAM].getValue()) * 7) % 12;
		int transpose = (int) std::round(inputs[TRANSPOSE_INPUT].getVoltage() * 12.f);
		int len = loopLenNow();
		double focus = clamp(cvParam(FOCUS_PARAM, FOCUS_INPUT, 3.f), 0.f, 3.f);
		double colour = clamp(cvParam(COLOUR_PARAM, COLOUR_INPUT, 1.f), 0.f, 1.f);
		double spread = clamp(cvParam(SPREAD_PARAM, SPREAD_INPUT, 1.f), 0.f, 1.f);
		double drift = clamp(cvParam(DRIFT_PARAM, DRIFT_INPUT, 1.f), 0.f, 1.f);
		bool lock = params[LOCK_PARAM].getValue() > 0.5f;
		bool hold = params[HOLD_PARAM].getValue() > 0.5f || inputs[HOLD_INPUT].getVoltage() >= 1.f;

		// LINK from Long Form: 9 beat, 10 section change, 11 energy, 12 reset, 14 section
		int lc = inputs[LINK_INPUT].isConnected() ? inputs[LINK_INPUT].getChannels() : 0;
		bool link = lc >= 12;
		double energy = link ? clamp(inputs[LINK_INPUT].getVoltage(10) / 10.f, 0.f, 1.f) : 0.0;
		double tension = 0.0;
		if (link && (energyDrives == 0 || energyDrives == 2)) colour = std::min(1.0, colour + 0.5 * energy);
		if (link && (energyDrives == 1 || energyDrives == 2)) tension = energy;

		// The loops follow the style, mode and loop length: new ones when those change.
		if (style != madeStyle || mode != madeMode || len != madeLen) {
			for (Loop& L : loops) L.made = false;
			bool first = madeStyle < 0;
			madeStyle = style;
			madeMode = mode;
			madeLen = len;
			if (len > 0) {
				Loop& L = loopNow();
				ensureLoop(L, focus, tension);
				pos = first ? -1 : (pos < 0 ? -1 : pos % L.len);
				if (pos >= 0) { cur = L.slots[pos]; needRecalc = true; }
			}
		}
		if (!haveChord) {
			// Before the first clock: the loop's first chord is ready
			if (len > 0) {
				Loop& L = loopNow();
				ensureLoop(L, focus, tension);
				cur = L.slots[0];
			}
			else cur.entry = homeEntry(style);
			needRecalc = true;
		}

		// Requests from the display
		int rr = rerollRequest.exchange(-1);
		if (rr >= 0 && len > 0) {
			Loop& L = loopNow();
			ensureLoop(L, focus, tension);
			if (rr < L.len) {
				if (rr == 0) L.slots[0] = rollSlot(style, -1, focus, tension, rng);
				else L.slots[rr] = rollSlot(style, L.slots[rr - 1].entry, focus, tension, rng);
				L.orig[rr] = L.slots[rr];
				if (rr == pos) { cur = L.slots[rr]; needRecalc = true; }
			}
		}
		int sr = setRequest.exchange(-1);
		if (sr >= 0 && len > 0) {
			int slot = sr / 64, entry = sr % 64;
			Loop& L = loopNow();
			ensureLoop(L, focus, tension);
			if (slot < L.len && entry < (int) styleDef(style).entries.size()) {
				L.slots[slot].entry = entry;
				L.orig[slot] = L.slots[slot];
				if (slot == pos) { cur = L.slots[slot]; needRecalc = true; }
			}
		}

		// ---- clock
		bool clocked = link || inputs[CLOCK_INPUT].isConnected();
		if (newButton.process(params[NEW_PARAM].getValue() > 0.f)) newPending = true;
		if (newPending && !clocked) {
			// No clock: a new loop at once
			newPending = false;
			if (len > 0) {
				loopNow().made = false;
				startLoop(focus, tension);
				advance(focus, tension, drift, lock);
			}
		}
		int ticksPerBar = link ? 4 : PPQN_TICKS[clamp(ppqn, 0, 3)];
		int ticksPerBeat = std::max(1, ticksPerBar / 4);
		bool tick = false, reset = false, section = false;
		if (link) {
			tick = linkBeat.process(inputs[LINK_INPUT].getVoltage(8), 0.1f, 1.f);
			reset = linkReset.process(inputs[LINK_INPUT].getVoltage(11), 0.1f, 1.f);
			section = linkChange.process(inputs[LINK_INPUT].getVoltage(9), 0.1f, 1.f);
		}
		else if (inputs[CLOCK_INPUT].isConnected())
			tick = clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) reset = true;
		shownLink = link;

		if (reset) {
			tickCount = -1;
			startLoop(focus, tension);
			haveChord = false;
			changePending = false;
		}
		bool advanced = false;
		if (section) {
			// A new section of Long Form: its own loop, from the top
			int sec = lc >= 14 ? clamp((int) std::round(inputs[LINK_INPUT].getVoltage(13)), 0, 7) : -1;
			if (sec >= 0) curLoop = sec;
			else {
				curLoop = 8;
				loops[8].made = false;
			}
			shownSection = sec;
			startLoop(focus, tension);
			advance(focus, tension, drift, lock);
			tickCount = 0;
			advanced = true;
		}
		if (tick && !advanced) {
			tickCount++;
			bool bar = tickCount % ticksPerBar == 0;
			bool beat = tickCount % ticksPerBeat == 0;
			long changeTicks = std::max(1L, (long) std::lround(RATE_BARS[clamp((int) params[RATE_PARAM].getValue(), 0, 4)] * ticksPerBar));
			if (tickCount == 0) {
				advance(focus, tension, drift, lock);
				advanced = true;
			}
			else {
				if (newPending && bar) {
					newPending = false;
					if (len > 0) {
						loopNow().made = false;
						startLoop(focus, tension);
					}
					advance(focus, tension, drift, lock);
					advanced = true;
				}
				else if (tickCount % changeTicks == 0 && !hold) {
					advance(focus, tension, drift, lock);
					advanced = true;
				}
				if (changePending && beat && !advanced) {
					advance(focus, tension, drift, lock);
					advanced = true;
				}
				if (beat) changePending = false;
			}
		}
		if (changeTrig.process(inputs[CHANGE_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (changeTiming == 0 || (!tick && !link && !inputs[CLOCK_INPUT].isConnected())) {
				if (!advanced) advance(focus, tension, drift, lock);
			}
			else changePending = true;
		}
		// No clock at all and nothing played yet: the first chord just plays
		if (!haveChord && !link && !inputs[CLOCK_INPUT].isConnected()) {
			haveChord = true;
			needRecalc = true;
		}

		// ---- recalculate the chord when anything it depends on changes
		int voices = voicesSetting == 0 ? STYLE_VOICES[style] : voicesSetting + 2;
		int keyT = ((key + transpose) % 12 + 12) % 12;
		if (++recalcCounter >= 32) {
			recalcCounter = 0;
			if (std::fabs(colour - lastColour) > 1e-3 || std::fabs(spread - lastSpread) > 1e-3 || voices != lastVoices || keyT != lastKey ||
			    mode != lastMode || style != lastStyle || bassInversions != lastBassInv)
				needRecalc = true;
		}
		if (needRecalc) {
			needRecalc = false;
			lastColour = (float) colour;
			lastSpread = (float) spread;
			lastVoices = voices;
			lastKey = keyT;
			lastMode = mode;
			lastStyle = style;
			lastBassInv = bassInversions;
			// Transposing moves the whole chord: voice lead in the untransposed key
			recalc(style, mode, key, colour, spread);
		}
		float tr = transpose / 12.f;

		// ---- outputs
		bool playing = haveChord;
		outputs[CHORD_OUTPUT].setChannels(std::max(1, outN));
		for (int i = 0; i < outN; i++) outputs[CHORD_OUTPUT].setVoltage(outChord[i] + tr, i);
		outputs[BASS_OUTPUT].setVoltage(outBass + tr);
		outputs[ROOT_OUTPUT].setVoltage(outRoot + tr);
		outputs[TOP_OUTPUT].setVoltage(outTop + tr);
		bool gap = gateGap.process(args.sampleTime);
		outputs[GATE_OUTPUT].setVoltage(playing && !gap ? 10.f : 0.f);
		bool pulse = changePulse.process(args.sampleTime);
		outputs[CHANGE_OUTPUT].setVoltage(pulse ? 10.f : 0.f);
		outputs[TENSION_OUTPUT].setVoltage(outTension);

		// HARM: 1-6 chord tones (1 = root), 7 count, 8 key, 9 mode, 10 style, 11 change
		Output& h = outputs[HARM_OUTPUT];
		h.setChannels(11);
		for (int i = 0; i < 6; i++) {
			int pc = harmN > 0 ? harmTones[i < harmN ? i : 0] : keyT;
			int p = ((pc + transpose) % 12 + 12) % 12;
			h.setVoltage(p / 12.f, i);
		}
		h.setVoltage((float) std::max(1, harmN), 6);
		h.setVoltage(keyT / 12.f, 7);
		h.setVoltage((float) mode, 8);
		h.setVoltage(style * 0.5f, 9);
		h.setVoltage(pulse ? 10.f : 0.f, 10);

		// lights and display state
		lights[HOLD_LIGHT].setBrightness(hold ? 1.f : 0.f);
		lights[LOCK_LIGHT].setBrightness(lock ? 1.f : 0.f);
		lights[NEW_LIGHT].setBrightness(newPending ? (std::fmod(args.frame * args.sampleTime, 0.25f) < 0.125f ? 1.f : 0.f) : 0.f);
		for (int i = 0; i < 6; i++) lights[TENSION_LIGHT + i].setBrightnessSmooth(outTension >= 10.f * (i + 0.5f) / 6.f ? 1.f : 0.f, args.sampleTime);
		shownStyle = style;
		shownKey = keyT;
		shownMode = mode;
		shownColour = (float) colour;
		shownHold = hold;
		if (driftFlash > 0.f) driftFlash = std::max(0.f, driftFlash - args.sampleTime);
	}

	// ---- save
	json_t* slotJson(const Slot& s) {
		json_t* a = json_array();
		json_array_append_new(a, json_integer(s.entry));
		for (int i = 0; i < 4; i++) json_array_append_new(a, json_real(s.dice[i]));
		return a;
	}
	void slotFrom(json_t* a, Slot& s) {
		if (!a || json_array_size(a) < 5) return;
		s.entry = (int) json_integer_value(json_array_get(a, 0));
		for (int i = 0; i < 4; i++) s.dice[i] = (float) json_number_value(json_array_get(a, i + 1));
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "voices", json_integer(voicesSetting));
		json_object_set_new(r, "bassInversions", json_integer(bassInversions));
		json_object_set_new(r, "ppqn", json_integer(ppqn));
		json_object_set_new(r, "changeTiming", json_integer(changeTiming));
		json_object_set_new(r, "energyDrives", json_integer(energyDrives));
		json_object_set_new(r, "style", json_integer(madeStyle));
		json_object_set_new(r, "mode", json_integer(madeMode));
		json_object_set_new(r, "len", json_integer(madeLen));
		json_object_set_new(r, "curLoop", json_integer(curLoop));
		json_t* ls = json_array();
		for (const Loop& L : loops) {
			json_t* o = json_object();
			json_object_set_new(o, "made", json_boolean(L.made));
			json_object_set_new(o, "len", json_integer(L.len));
			json_object_set_new(o, "drifted", json_integer(L.drifted));
			json_t* s = json_array();
			json_t* g = json_array();
			for (int i = 0; i < MAX_LOOP; i++) {
				json_array_append_new(s, slotJson(L.slots[i]));
				json_array_append_new(g, slotJson(L.orig[i]));
			}
			json_object_set_new(o, "slots", s);
			json_object_set_new(o, "orig", g);
			json_array_append_new(ls, o);
		}
		json_object_set_new(r, "loops", ls);
		return r;
	}

	void dataFromJson(json_t* r) override {
		auto geti = [&](const char* k, int& dst) {
			json_t* j = json_object_get(r, k);
			if (j) dst = (int) json_integer_value(j);
		};
		geti("voices", voicesSetting);
		geti("bassInversions", bassInversions);
		geti("ppqn", ppqn);
		geti("changeTiming", changeTiming);
		geti("energyDrives", energyDrives);
		geti("style", madeStyle);
		geti("mode", madeMode);
		geti("len", madeLen);
		geti("curLoop", curLoop);
		curLoop = clamp(curLoop, 0, NUM_LOOPS - 1);
		json_t* ls = json_object_get(r, "loops");
		for (int k = 0; k < NUM_LOOPS && ls && k < (int) json_array_size(ls); k++) {
			json_t* o = json_array_get(ls, k);
			Loop& L = loops[k];
			json_t* m = json_object_get(o, "made");
			L.made = m && json_is_true(m);
			json_t* l = json_object_get(o, "len");
			if (l) L.len = clamp((int) json_integer_value(l), 1, MAX_LOOP);
			json_t* d = json_object_get(o, "drifted");
			if (d) L.drifted = (uint8_t) json_integer_value(d);
			json_t* s = json_object_get(o, "slots");
			json_t* g = json_object_get(o, "orig");
			for (int i = 0; i < MAX_LOOP; i++) {
				if (s) slotFrom(json_array_get(s, i), L.slots[i]);
				if (g) slotFrom(json_array_get(g, i), L.orig[i]);
			}
		}
		pos = -1;
		haveChord = false;
		tickCount = -1;
		needRecalc = true;
	}
};

// ---------------------------------------------------------------- display
static const NVGcolor D_BG = nvgRGB(0x1d, 0x1e, 0x22);
static const NVGcolor D_INK = nvgRGB(0xf3, 0xef, 0xe8);
static const NVGcolor D_SOFT = nvgRGB(0x8a, 0x85, 0x7d);
static const NVGcolor D_ORANGE = nvgRGB(0xef, 0x7a, 0x37);
static const NVGcolor D_AMBER = nvgRGB(0xf0, 0xb0, 0x30);
static const NVGcolor D_CELL = nvgRGB(0x2c, 0x2c, 0x2f);
static const NVGcolor D_CYAN = nvgRGB(0x3f, 0xd0, 0xe0);

struct ChangesDisplay : OpaqueWidget {
	Changes* module = nullptr;
	float boxTop = 0, boxH = 0, boxLeft = 0, boxW = 0;
	int boxN = 0;

	void text(const DrawArgs& a, float x, float y, const std::string& s, NVGcolor c, int align, float size) {
		nvgFontSize(a.vg, size);
		nvgTextAlign(a.vg, align);
		nvgFillColor(a.vg, c);
		nvgText(a.vg, x, y, s.c_str(), NULL);
	}
	void rrect(const DrawArgs& a, float x, float y, float w, float h, float r, NVGcolor c) {
		nvgBeginPath(a.vg);
		nvgRoundedRect(a.vg, x, y, w, h, r);
		nvgFillColor(a.vg, c);
		nvgFill(a.vg);
	}

	int slotAt(Vec p) {
		if (boxN <= 0 || p.y < boxTop || p.y > boxTop + boxH) return -1;
		int i = (int) std::floor((p.x - boxLeft) / boxW);
		return i >= 0 && i < boxN ? i : -1;
	}

	void onButton(const ButtonEvent& e) override {
		if (!module || e.action != GLFW_PRESS) { OpaqueWidget::onButton(e); return; }
		int i = slotAt(e.pos);
		if (i < 0) { OpaqueWidget::onButton(e); return; }
		e.consume(this);
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) module->rerollRequest = i;
		else if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			Changes* m = module;
			int style = m->shownStyle, mode = m->shownMode, key = m->shownKey;
			double colour = m->shownColour;
			Menu* menu = createMenu();
			menu->addChild(createMenuLabel(string::f("Chord %d of the loop", i + 1)));
			menu->addChild(createMenuItem("Roll again", "", [=]() { m->rerollRequest = i; }));
			menu->addChild(new MenuSeparator);
			const StyleDef& sd = styleDef(style);
			for (int k = 0; k < (int) sd.entries.size(); k++) {
				Slot s;
				s.entry = k;
				Chord c = realize(style, mode, s, colour);
				std::string label = chordName(c, key) + "   " + romanName(c);
				if (sd.entries[k].w < 0.05f) label += "  (rare)";
				menu->addChild(createMenuItem(label, "", [=]() { m->setRequest = i * 64 + k; }));
			}
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) { OpaqueWidget::drawLayer(args, layer); return; }
		float W = box.size.x, H = box.size.y;
		rrect(args, 0, 0, W, H, mm2px(1.2f), D_BG);
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);
		float pad = mm2px(3.f);
		if (!module) {
			text(args, pad, mm2px(10.5f), "Fm9", D_INK, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, mm2px(8.f));
			text(args, mm2px(31.f), mm2px(6.2f), "i9  -  F minor", D_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, mm2px(3.f));
			text(args, mm2px(31.f), mm2px(10.4f), "HOUSE", D_ORANGE, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, mm2px(3.f));
			return;
		}
		Changes* m = module;
		int style = m->shownStyle, mode = m->shownMode, key = m->shownKey;
		double colour = m->shownColour;
		Chord c = realize(style, mode, m->cur, colour);
		text(args, pad, mm2px(10.5f), chordName(c, key), D_INK, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, mm2px(8.f));
		std::string info = romanName(c) + "  \xc2\xb7  " + NOTE_NAMES[key] + " " + MODE_NAMES[mode];
		text(args, mm2px(40.f), mm2px(6.2f), info, D_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, mm2px(3.f));
		std::string st = STYLE_SHORT[style];
		if (m->shownHold) st += "   HOLD";
		if (m->newPending) st += "   NEW..";
		if (m->shownLink && m->shownSection >= 0) st += string::f("   SECTION %d", m->shownSection + 1);
		text(args, mm2px(40.f), mm2px(10.4f), st, D_ORANGE, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, mm2px(3.f));

		int len = m->loopLenNow();
		const Loop& L = m->loops[clamp(m->curLoop, 0, NUM_LOOPS - 1)];
		// next chord
		std::string next = "?";
		if (len > 0 && L.made) {
			int np = (m->pos + 1) % std::max(1, L.len);
			next = chordName(realize(style, mode, L.slots[np], colour), key);
		}
		text(args, W - pad, mm2px(6.2f), "next", D_SOFT, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, mm2px(2.6f));
		text(args, W - pad, mm2px(10.4f), next, D_SOFT, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, mm2px(3.4f));

		// the loop
		boxTop = mm2px(13.5f);
		boxH = mm2px(5.6f);
		boxLeft = pad;
		if (len > 0 && L.made) {
			boxN = L.len;
			boxW = (W - 2 * pad) / boxN;
			float fs = boxN > 4 ? mm2px(2.3f) : mm2px(2.9f);
			for (int i = 0; i < boxN; i++) {
				float x = boxLeft + i * boxW;
				bool on = i == m->pos;
				bool flash = m->driftFlash > 0.f && i == m->driftFlashSlot && std::fmod(m->driftFlash, 0.2f) < 0.1f;
				rrect(args, x + 1.f, boxTop, boxW - 2.f, boxH, mm2px(0.6f), on ? D_ORANGE : (flash ? D_AMBER : D_CELL));
				if (L.drifted & (1 << i)) {
					nvgBeginPath(args.vg);
					nvgCircle(args.vg, x + boxW - mm2px(2.f), boxTop + mm2px(1.3f), mm2px(0.6f));
					nvgFillColor(args.vg, D_AMBER);
					nvgFill(args.vg);
				}
				std::string nm = chordName(realize(style, mode, L.slots[i], colour), key);
				text(args, x + boxW / 2, boxTop + boxH * 0.72f, nm, on ? D_BG : nvgRGB(0xd8, 0xd2, 0xc8), NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, fs);
			}
		}
		else {
			boxN = 0;
			text(args, pad, boxTop + boxH * 0.72f, "LOOP OFF: a new chord every time", D_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, mm2px(2.8f));
		}
		if (m->shownLink)
			text(args, W - pad, boxTop - mm2px(0.8f), "LINK", D_CYAN, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, mm2px(2.2f));
	}
};

// ---------------------------------------------------------------- panel
struct ChangesWidget : ModuleWidget {
	ChangesWidget(Changes* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Changes.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Positions in mm, matching res/make_changes_panel.py (28 HP)
		const float W = 142.24f;
		ChangesDisplay* d = createWidget<ChangesDisplay>(mm2px(Vec(8.f, 18.5f)));
		d->box.size = mm2px(Vec(W - 16.f, 21.f));
		d->module = module;
		addChild(d);

		const int row1[5] = {Changes::STYLE_PARAM, Changes::ROOT_PARAM, Changes::MODE_PARAM, Changes::RATE_PARAM, Changes::LOOP_PARAM};
		for (int i = 0; i < 5; i++) {
			auto* k = createParamCentered<RoundBlackKnob>(mm2px(Vec(15.f + i * (W - 30.f) / 4.f, 50.f)), module, row1[i]);
			k->snap = true;
			addParam(k);
		}
		const float pair = (W - 16.f) / 4.f;
		const int big[4] = {Changes::FOCUS_PARAM, Changes::COLOUR_PARAM, Changes::SPREAD_PARAM, Changes::DRIFT_PARAM};
		const int cv[4] = {Changes::FOCUS_INPUT, Changes::COLOUR_INPUT, Changes::SPREAD_INPUT, Changes::DRIFT_INPUT};
		for (int i = 0; i < 4; i++) {
			float x = 8.f + pair * i + pair / 2.f - 5.f;
			addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(x, 68.f)), module, big[i]));
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x + 13.f, 69.5f)), module, cv[i]));
		}
		float bx[5];
		for (int i = 0; i < 5; i++) bx[i] = 15.f + i * (W - 30.f) / 4.f;
		addParam(createLightParamCentered<VCVLightBezelLatch<WhiteLight>>(mm2px(Vec(bx[0], 87.f)), module, Changes::HOLD_PARAM, Changes::HOLD_LIGHT));
		addParam(createLightParamCentered<VCVLightBezel<YellowLight>>(mm2px(Vec(bx[1], 87.f)), module, Changes::NEW_PARAM, Changes::NEW_LIGHT));
		addParam(createLightParamCentered<VCVLightBezelLatch<WhiteLight>>(mm2px(Vec(bx[2], 87.f)), module, Changes::LOCK_PARAM, Changes::LOCK_LIGHT));
		float tx = (bx[3] + bx[4]) / 2.f;
		for (int i = 0; i < 6; i++)
			addChild(createLightCentered<SmallLight<TOrangeLight<>>>(mm2px(Vec(tx - 6.25f + i * 2.5f, 87.f)), module, Changes::TENSION_LIGHT + i));

		const int ins[6] = {Changes::CLOCK_INPUT, Changes::RESET_INPUT, Changes::CHANGE_INPUT, Changes::HOLD_INPUT, Changes::TRANSPOSE_INPUT, Changes::LINK_INPUT};
		for (int i = 0; i < 6; i++)
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(14.f + i * (W - 28.f) / 5.f, 103.f)), module, ins[i]));
		const int outs[8] = {Changes::CHORD_OUTPUT, Changes::BASS_OUTPUT, Changes::ROOT_OUTPUT, Changes::TOP_OUTPUT,
		                     Changes::GATE_OUTPUT, Changes::CHANGE_OUTPUT, Changes::TENSION_OUTPUT, Changes::HARM_OUTPUT};
		for (int i = 0; i < 8; i++)
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(14.f + i * (W - 28.f) / 7.f, 117.5f)), module, outs[i]));
	}

	void appendContextMenu(Menu* menu) override {
		Changes* m = getModule<Changes>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Voices", {"Auto (by style)", "3", "4", "5", "6"}, &m->voicesSetting));
		menu->addChild(createIndexPtrSubmenuItem("Bass inversions (Classical, Gospel)", {"Off (always the root)", "On"}, &m->bassInversions));
		menu->addChild(createIndexPtrSubmenuItem("Clock input", {"16ths", "24 PPQN", "8ths", "Quarter notes"}, &m->ppqn));
		menu->addChild(createIndexPtrSubmenuItem("Change input", {"At once", "On the next beat"}, &m->changeTiming));
		menu->addChild(createIndexPtrSubmenuItem("Long Form energy drives", {"Colour", "Tension (more V and passing chords)", "Both", "Nothing"}, &m->energyDrives));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Restore this loop (undo drift)", "", [=]() { restoreLoop(m->loops[clamp(m->curLoop, 0, NUM_LOOPS - 1)]); }));
		menu->addChild(createMenuItem("New loops for every section", "", [=]() {
			for (Loop& L : m->loops) L.made = false;
		}));
		menu->addChild(createMenuLabel("Click a chord in the display to roll it again; right-click to choose"));
	}
};

Model* modelChanges = createModel<Changes, ChangesWidget>("TheChanges");
