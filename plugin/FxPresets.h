#pragma once
// The factory presets of the delay and of the reverb at the end of the chain. Each effect has its own list and a preset only changes its own
// effect (choosing one switches it on; "Off" switches it off), so any delay can be paired with any reverb. Times are free (not tied to the
// host tempo), so a preset sounds the same in any project.

struct DelayPreset {
    const char* name;
    bool on;
    double timeMs, feedback, mix, toneHz;  // feedback and mix in percent
    bool pingPong, analog;
};

struct ReverbPreset {
    const char* name;
    bool on, hall;
    double decaySec, preMs, toneHz, lowCutHz, mix, mod;  // mix and mod in percent
    double shimmer;                                       // percent; 0 = off
    bool shimmerFifth;                                    // the shimmer goes up an octave and a fifth instead of an octave
};

inline constexpr DelayPreset kDelayPresets[] = {
    {"Off", false, 380, 35, 25, 6500, false, false},
    // A single quick repeat that thickens the note: the rockabilly and country sound.
    {"Slapback", true, 120, 12, 28, 7000, false, false},
    // A short, clean echo that stays out of the way of fast playing.
    {"Short echo", true, 250, 25, 22, 7000, false, false},
    // Long dark repeats like a tape unit.
    {"Long tape echo", true, 540, 50, 30, 4200, false, true},
    // Repeats that bounce from ear to ear: wide and open for cleans and leads.
    {"Ping-pong", true, 430, 42, 28, 6000, true, false},
    // A short, dark, slightly wobbly repeat: vintage thickening.
    {"Dark tape slap", true, 160, 18, 30, 3800, false, true},
    // A medium echo for a solo.
    {"Lead echo", true, 380, 35, 24, 5500, false, true},
    // Slow, dark, many repeats that blur into a wash.
    {"Long ambient", true, 800, 62, 32, 3500, true, true},
};
inline constexpr int kDelayPresetCount = int(sizeof(kDelayPresets) / sizeof(kDelayPresets[0]));

inline constexpr ReverbPreset kReverbPresets[] = {
    {"Off", false, false, 2.0, 15, 8000, 150, 22, 30, 0, false},
    // A small room: a sense of space with almost no tail.
    {"Tight room", true, false, 0.6, 5, 7500, 150, 16, 20, 0, false},
    // A presence behind the guitar without washing it.
    {"Short plate", true, false, 1.1, 8, 9000, 150, 20, 25, 0, false},
    // A bright plate: air on top, good for cleans and chords.
    {"Bright plate", true, false, 1.8, 10, 12000, 140, 24, 30, 0, false},
    // A warm, darker plate that sits well behind a distorted guitar.
    {"Warm plate", true, false, 2.2, 20, 5500, 180, 22, 30, 0, false},
    // A big room: slow, soft and far away, for slow chords and swells.
    {"Big hall", true, true, 3.8, 35, 6500, 200, 28, 40, 0, false},
    // A very long hall: for ambient parts and single held notes.
    {"Cathedral", true, true, 7.0, 60, 5000, 250, 30, 50, 0, false},
    // A plate with a long, moving tail that fills the space.
    {"Ambient wash", true, false, 5.5, 30, 6500, 200, 40, 60, 0, false},
    // The tail climbs an octave on every pass: bright, glassy and slowly rising.
    {"Shimmer", true, false, 5.0, 20, 9000, 250, 35, 55, 45, false},
    // The same with an octave and a fifth: more ethereal, for swells and held chords.
    {"Angelic shimmer", true, true, 7.0, 40, 8000, 280, 32, 60, 35, true},
};
inline constexpr int kReverbPresetCount = int(sizeof(kReverbPresets) / sizeof(kReverbPresets[0]));

// The tape echo: the time to the first head, which heads are heard (0 = 1, 1 = 2, 2 = 3, 3 = 1+2, 4 = 2+3, 5 = 1+3, 6 = 1+2+3), and the character of the tape
// (everything else in percent, the tone in Hz). Choosing one switches it on ("Off" switches it off).
struct TapePreset {
    const char* name;
    bool on;
    double timeMs;
    int heads;
    double intensity, mix, toneHz, wow, flutter, saturation;
    bool stereo;
};

inline constexpr TapePreset kTapePresets[] = {
    {"Off", false, 180, 3, 45, 30, 5000, 30, 30, 40, false},
    // One quick, warm repeat that thickens the note: the tape version of the rockabilly slap.
    {"Tape slapback", true, 110, 0, 18, 30, 6500, 15, 15, 30, false},
    // Two heads at once: the repeats come in a pair, a flowing echo that fills the gaps between notes.
    {"Classic echo", true, 180, 3, 45, 30, 5000, 30, 30, 40, false},
    // All three heads: the rolling, rhythmic repeats of a space echo; the heads are spread across the picture.
    {"Rolling triplets", true, 220, 6, 40, 30, 5500, 25, 25, 35, true},
    // Long, dark, swampy repeats that keep returning: for slow parts and dub.
    {"Dark dub", true, 350, 4, 62, 35, 3200, 45, 40, 60, false},
    // Wide and open: the three heads left, middle and right.
    {"Wide stereo", true, 260, 6, 50, 30, 6000, 30, 30, 40, true},
    // Past 100 % the repeats build up into a self-oscillating swell: play a note and let it bloom.
    {"Swell", true, 300, 4, 98, 40, 3500, 40, 35, 70, true},
    // Old, stretched tape: the pitch drifts and wobbles on every repeat.
    {"Worn tape", true, 200, 3, 50, 32, 3800, 80, 70, 55, false},
};
inline constexpr int kTapePresetCount = int(sizeof(kTapePresets) / sizeof(kTapePresets[0]));

// The fuzz: the type (0 = Face, 1 = Muff, 2 = Octave) and its knobs (fuzz, tone and bias on the 0..10 scale of the panel, mix in percent).
// Choosing one switches the fuzz on ("Off" switches it off).
struct FuzzPreset {
    const char* name;
    bool on;
    int type;
    double fuzz, tone, bias, mix, levelDb;
};

inline constexpr FuzzPreset kFuzzPresets[] = {
    {"Off", false, 0, 6, 5, 7, 100, -6},
    // A light fuzz that still lets the guitar's volume knob clean it up: edge without the wall.
    {"Light fuzz", true, 0, 3.5, 6, 8, 100, -4},
    // The ragged, furry sound of the late 1960s: loud, a little unruly.
    {"Vintage face", true, 0, 7, 5, 7, 100, -6},
    // Starved of bias: it sputters and gates as the note dies away.
    {"Gated sputter", true, 0, 8, 4.5, 2, 100, -5},
    // The singing, endless sustain of the big fuzz: smooth and thick for leads.
    {"Sustain lead", true, 1, 7, 4, 5, 100, -8},
    // Dark and heavy: the tone almost all the way to the lows, for slow riffs.
    {"Doom wall", true, 1, 9, 1.5, 5, 100, -7},
    // The tone toward the highs: thin and cutting, so it stays out of the way of a bass.
    {"Bright cut", true, 1, 6, 8, 5, 100, -8},
    // A fuzz with an octave above it: for leads high on the neck.
    {"Octave lead", true, 2, 6.5, 6, 7, 100, -6},
    // Half dry, half fuzz: it keeps the body of the chords and of a bass.
    {"Blended fuzz", true, 0, 8, 5, 7, 55, -4},
};
inline constexpr int kFuzzPresetCount = int(sizeof(kFuzzPresets) / sizeof(kFuzzPresets[0]));

// The wah: its range, its peak, and what moves it. Position is only used by the pedal modes; the pedal presets leave it where a player would park it
// (the knob is the pedal, so it can be rocked from the host), the envelope and LFO ones ignore it. Choosing one switches the wah on ("Off" switches it off).
struct WahPreset {
    const char* name;
    bool on;
    int mode;                        // 0 = pedal, 1 = envelope, 2 = LFO
    double position, heelHz, toeHz, q, mix, levelDb;  // position and mix in percent
    double sensitivity, attackMs, releaseMs;          // envelope; sensitivity in percent
    bool down;                                        // envelope: the loud notes close it
    double rateHz;                                    // LFO
    bool stereo;
    int filter = 0;                                   // 0 band-pass, 1 low-pass, 2 high-pass
};

inline constexpr WahPreset kWahPresets[] = {
    {"Off", false, 0, 50, 400, 2000, 4, 100, 3, 50, 8, 120, false, 1.0, false},
    // The classic rocking pedal: a vocal peak that goes from a dull "oo" to a bright "ah".
    {"Classic pedal", true, 0, 50, 400, 2000, 4, 100, 3, 50, 8, 120, false, 1.0, false},
    // A little wider and brighter, with a harder bite at the top.
    {"Vox style", true, 0, 50, 350, 2300, 3.2, 100, 3, 50, 8, 120, false, 1.0, false},
    // A narrow, singing peak for long solos: the pedal almost speaks.
    {"Throaty lead", true, 0, 50, 350, 2400, 7, 100, 2, 50, 8, 120, false, 1.0, false},
    // The pedal parked half-way and left there: a nasal, hollow tone for a rhythm part or a solo.
    {"Cocked wah", true, 0, 68, 500, 1800, 5.5, 100, 2, 50, 8, 120, false, 1.0, false},
    // A touch of the filter under the dry sound: it colours the note without taking the bass away.
    {"Subtle", true, 0, 40, 400, 2000, 2.5, 45, 0, 50, 8, 120, false, 1.0, false},
    // The funk "quack": every note opens the filter and it falls back before the next one.
    {"Funk quack", true, 1, 50, 300, 2200, 5, 100, 3, 70, 4, 90, false, 1.0, false},
    // The same, closing as you dig in: a duck-like "wow" at the start of each note.
    {"Envelope down", true, 1, 50, 300, 2400, 4.5, 100, 3, 55, 3, 140, true, 1.0, false},
    // A fast, sharp auto-wah that follows soft playing too.
    {"Snappy auto-wah", true, 1, 50, 400, 2800, 6, 100, 3, 85, 2, 60, false, 1.0, false},
    // A slow rocking of the filter by itself: a hypnotic sweep under held chords.
    {"Slow sweep", true, 2, 50, 350, 1800, 3, 100, 3, 50, 8, 120, false, 0.4, false},
    // A fast swirl, a different phase in each ear.
    {"Stereo swirl", true, 2, 50, 400, 2600, 4, 100, 3, 50, 8, 120, false, 2.8, true},
    // An envelope filter in the funk manner: a low-pass that every note throws open and that falls back dull.
    {"Envelope low-pass", true, 1, 50, 200, 2600, 5, 100, 0, 65, 4, 160, false, 1.0, false, 1},
    // The same on its way down: the loud notes close the low-pass, a soft "wow" at the start of each one.
    {"Low-pass duck", true, 1, 50, 200, 2400, 4, 100, 0, 55, 3, 180, true, 1.0, false, 1},
    // A high-pass that thins the sound as it opens: a bright, hollow auto-filter.
    {"Envelope high-pass", true, 1, 50, 250, 3000, 3, 100, 0, 60, 4, 140, false, 1.0, false, 2},
    // A slow sweep of a low-pass: the sound rises and falls from dull to bright on its own.
    {"Low-pass sweep", true, 2, 50, 200, 2400, 3.5, 100, 0, 50, 8, 120, false, 0.3, false, 1},
};
inline constexpr int kWahPresetCount = int(sizeof(kWahPresets) / sizeof(kWahPresets[0]));
