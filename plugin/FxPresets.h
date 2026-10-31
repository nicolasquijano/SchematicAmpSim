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
    {"Off", false, false, 2.0, 15, 8000, 150, 22, 30},
    // A small room: a sense of space with almost no tail.
    {"Tight room", true, false, 0.6, 5, 7500, 150, 16, 20},
    // A presence behind the guitar without washing it.
    {"Short plate", true, false, 1.1, 8, 9000, 150, 20, 25},
    // A bright plate: shimmer on top, good for cleans and chords.
    {"Bright plate", true, false, 1.8, 10, 12000, 140, 24, 30},
    // A warm, darker plate that sits well behind a distorted guitar.
    {"Warm plate", true, false, 2.2, 20, 5500, 180, 22, 30},
    // A big room: slow, soft and far away, for slow chords and swells.
    {"Big hall", true, true, 3.8, 35, 6500, 200, 28, 40},
    // A very long hall: for ambient parts and single held notes.
    {"Cathedral", true, true, 7.0, 60, 5000, 250, 30, 50},
    // A plate with a long, moving tail that fills the space.
    {"Ambient wash", true, false, 5.5, 30, 6500, 200, 40, 60},
};
inline constexpr int kReverbPresetCount = int(sizeof(kReverbPresets) / sizeof(kReverbPresets[0]));
