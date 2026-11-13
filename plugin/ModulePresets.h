#pragma once
// Factory presets for the modules that had none: noise gate, compressor, overdrive, phaser, chorus, vibe, tremolo, flanger, pitch, boost and limiter.
// (The delay, reverb, tape echo, fuzz, wah and EQ have their own, richer tables in FxPresets.h and EqPresets.h.)
//
// A preset is the list of parameters it sets, in the units of the parameter (the knobs on their 0..10 scale, percentages, dB, Hz; a choice or a switch
// by its index, 0 or 1). Choosing one switches the module on ("Off" only switches it off, so the knobs keep what the player had set). The modules with
// no switch of their own (the pitch shifter) just take the values. Every preset lists every knob of its module, so one never inherits a leftover from
// the one before.

#include "PluginProcessor.h"

struct PresetValue {
    const char* id = nullptr;
    double value = 0.0;
};

struct ModulePreset {
    const char* name;
    bool on;
    PresetValue v[14];  // unused entries have no id (the ten-band EQ sets twelve)
};

struct PresetList {
    const ModulePreset* items = nullptr;
    int count = 0;
};

// The noise gate: threshold in dB and release in ms.
inline constexpr ModulePreset kGatePresets[] = {
    {"Off", false, {}},
    // Barely there: only the hiss between phrases, the notes ring out to the end.
    {"Light", true, {{ids::gateThreshold, -70}, {ids::gateRelease, 260}}},
    // A sensible default for a quiet rig.
    {"Standard", true, {{ids::gateThreshold, -60}, {ids::gateRelease, 120}}},
    // Closes a little earlier, for hum with the amp turned up.
    {"Noisy pickups", true, {{ids::gateThreshold, -50}, {ids::gateRelease, 100}}},
    // Fast and firm: the palm mutes stop dead between the chugs.
    {"Tight rhythm", true, {{ids::gateThreshold, -52}, {ids::gateRelease, 60}}},
    // The hardest and quickest: for high gain and staccato riffs.
    {"Metal chug", true, {{ids::gateThreshold, -42}, {ids::gateRelease, 35}}},
    // A low threshold and a long tail: sustain for lead lines, the noise only goes at the very end.
    {"Lead sustain", true, {{ids::gateThreshold, -68}, {ids::gateRelease, 400}}},
};

// The compressor: sustain, blend, tone and level on the panel's 0..10 scale, the fast release switch and the model (0 Keeley, 1 Dyna Comp, 2 FET, 3 Opto).
inline constexpr ModulePreset kCompressorPresets[] = {
    {"Off", false, {}},
    // Evens out the playing without being noticed.
    {"Gentle leveling", true, {{ids::compSustain, 3}, {ids::compBlend, 6}, {ids::compTone, 5}, {ids::compLevel, 7.5}, {ids::compFast, 0}, {ids::compModel, 0}}},
    // Notes bloom and last: clean tones that sing.
    {"Clean sustain", true, {{ids::compSustain, 6}, {ids::compBlend, 10}, {ids::compTone, 5}, {ids::compLevel, 7}, {ids::compFast, 0}, {ids::compModel, 0}}},
    // Quick release, a little bright: the attack of every note pops.
    {"Funk snap", true, {{ids::compSustain, 6}, {ids::compBlend, 8}, {ids::compTone, 6.5}, {ids::compLevel, 7}, {ids::compFast, 1}, {ids::compModel, 0}}},
    // Squeezed and twangy, the chicken-picking sound.
    {"Country pluck", true, {{ids::compSustain, 7.5}, {ids::compBlend, 8}, {ids::compTone, 7}, {ids::compLevel, 6.5}, {ids::compFast, 1}, {ids::compModel, 0}}},
    // Hard compression with the dry signal kept in: punch and body together.
    {"Parallel punch", true, {{ids::compSustain, 8}, {ids::compBlend, 5}, {ids::compTone, 5.5}, {ids::compLevel, 7}, {ids::compFast, 1}, {ids::compModel, 0}}},
    // Smooth and thick for slow lead lines, a touch darker.
    {"Lead squash", true, {{ids::compSustain, 9}, {ids::compBlend, 10}, {ids::compTone, 4.5}, {ids::compLevel, 6}, {ids::compFast, 0}, {ids::compModel, 0}}},
    // Holds a strummed part together without flattening the dynamics.
    {"Rhythm glue", true, {{ids::compSustain, 4.5}, {ids::compBlend, 9}, {ids::compTone, 5}, {ids::compLevel, 7.5}, {ids::compFast, 1}, {ids::compModel, 0}}},
    // The Dyna Comp: a fast, firm squeeze; the attack lets the start of each note through.
    {"Dyna squeeze", true, {{ids::compSustain, 6}, {ids::compBlend, 8}, {ids::compTone, 6}, {ids::compLevel, 7}, {ids::compFast, 0}, {ids::compModel, 1}}},
    {"Dyna quack", true, {{ids::compSustain, 7}, {ids::compBlend, 10}, {ids::compTone, 6.5}, {ids::compLevel, 7}, {ids::compFast, 1}, {ids::compModel, 1}}},
    // The FET kind: very fast, punchy, a little edge from the gain cell.
    {"FET punch", true, {{ids::compSustain, 5}, {ids::compBlend, 8}, {ids::compTone, 5.5}, {ids::compLevel, 7}, {ids::compFast, 0}, {ids::compModel, 2}}},
    {"FET slam", true, {{ids::compSustain, 8}, {ids::compBlend, 10}, {ids::compTone, 5}, {ids::compLevel, 6.5}, {ids::compFast, 1}, {ids::compModel, 2}}},
    // The opto kind: slow and smooth, it almost does not sound compressed.
    {"Opto smooth", true, {{ids::compSustain, 4}, {ids::compBlend, 10}, {ids::compTone, 5}, {ids::compLevel, 7.5}, {ids::compFast, 0}, {ids::compModel, 3}}},
    {"Opto glue", true, {{ids::compSustain, 6}, {ids::compBlend, 8}, {ids::compTone, 5}, {ids::compLevel, 7}, {ids::compFast, 0}, {ids::compModel, 3}}},
    {"Opto leveler", true, {{ids::compSustain, 7}, {ids::compBlend, 10}, {ids::compTone, 4.5}, {ids::compLevel, 6.5}, {ids::compFast, 0}, {ids::compModel, 3}}},
};

// The overdrive (Tube Screamer, SD-1 or BD-2, by the model index): drive, tone and level on the 0..10 scale.
inline constexpr ModulePreset kOverdrivePresets[] = {
    {"Off", false, {}},
    // Nearly clean: it only pushes the front of the amp.
    {"Clean push", true, {{ids::tsDrive, 1}, {ids::tsTone, 5.5}, {ids::tsLevel, 8}, {ids::tsModel, 0}}},
    // At the edge of breakup: the guitar's volume knob cleans it up.
    {"Edge of breakup", true, {{ids::tsDrive, 3}, {ids::tsTone, 5}, {ids::tsLevel, 6.5}, {ids::tsModel, 0}}},
    // The tight mid push that holds a high-gain amp together.
    {"Tight boost", true, {{ids::tsDrive, 2}, {ids::tsTone, 6}, {ids::tsLevel, 8}, {ids::tsModel, 0}}},
    // A warm, round overdrive for blues.
    {"Blues drive", true, {{ids::tsDrive, 4.5}, {ids::tsTone, 4}, {ids::tsLevel, 5.5}, {ids::tsModel, 0}}},
    // The textbook setting: drive at noon.
    {"Classic crunch", true, {{ids::tsDrive, 5}, {ids::tsTone, 5}, {ids::tsLevel, 5}, {ids::tsModel, 0}}},
    // More drive and the tone up: singing, a little nasal.
    {"Lead drive", true, {{ids::tsDrive, 7}, {ids::tsTone, 6.5}, {ids::tsLevel, 5.5}, {ids::tsModel, 0}}},
    // Dark and smooth, with the highs rolled off.
    {"Dark smooth", true, {{ids::tsDrive, 5}, {ids::tsTone, 2.5}, {ids::tsLevel, 6}, {ids::tsModel, 0}}},
    // Everything up: a thick, compressed wall.
    {"Max drive", true, {{ids::tsDrive, 9.5}, {ids::tsTone, 5}, {ids::tsLevel, 4.5}, {ids::tsModel, 0}}},
    // The SD-1 kind: a tighter, more compressed drive with asymmetrical clipping and its own tone.
    {"SD-1 push", true, {{ids::tsDrive, 3}, {ids::tsTone, 5}, {ids::tsLevel, 5}, {ids::tsModel, 1}}},
    {"SD-1 crunch", true, {{ids::tsDrive, 6}, {ids::tsTone, 5.5}, {ids::tsLevel, 4.5}, {ids::tsModel, 1}}},
    {"SD-1 lead", true, {{ids::tsDrive, 8.5}, {ids::tsTone, 6}, {ids::tsLevel, 4}, {ids::tsModel, 1}}},
    // The BD-2 kind: open and dynamic, bright, with a mid dip in the gain stages.
    {"BD-2 clean boost", true, {{ids::tsDrive, 2}, {ids::tsTone, 5}, {ids::tsLevel, 5}, {ids::tsModel, 2}}},
    {"BD-2 blues", true, {{ids::tsDrive, 5}, {ids::tsTone, 5}, {ids::tsLevel, 4.5}, {ids::tsModel, 2}}},
    {"BD-2 hot", true, {{ids::tsDrive, 8.5}, {ids::tsTone, 4.5}, {ids::tsLevel, 4}, {ids::tsModel, 2}}},
    // The RAT kind: gritty, hard-clipped and mid-forward; DRIVE is the distortion, TONE is the opposite of the pedal's FILTER (clockwise is brighter).
    {"RAT light", true, {{ids::tsDrive, 3}, {ids::tsTone, 6}, {ids::tsLevel, 4}, {ids::tsModel, 3}}},
    {"RAT rhythm", true, {{ids::tsDrive, 6}, {ids::tsTone, 5}, {ids::tsLevel, 3.5}, {ids::tsModel, 3}}},
    {"RAT lead", true, {{ids::tsDrive, 8.5}, {ids::tsTone, 4.5}, {ids::tsLevel, 3}, {ids::tsModel, 3}}},
};

// The phaser: speed in Hz, intense (feedback), mix in percent, channels (0 mono, 1 stereo).
inline constexpr ModulePreset kPhaserPresets[] = {
    {"Off", false, {}},
    // A slow, wide sweep under held chords.
    {"Slow sweep", true, {{ids::phSpeed, 0.25}, {ids::phIntense, 0}, {ids::phMix, 100}, {ids::phStereo, 0}}},
    // The classic pedal swirl.
    {"Classic swirl", true, {{ids::phSpeed, 0.8}, {ids::phIntense, 0}, {ids::phMix, 100}, {ids::phStereo, 0}}},
    // Half of the effect: it colours the sound without taking over.
    {"Soft phase", true, {{ids::phSpeed, 0.5}, {ids::phIntense, 0}, {ids::phMix, 55}, {ids::phStereo, 0}}},
    // Feedback up: a sharper, jet-like whoosh.
    {"Intense jet", true, {{ids::phSpeed, 1.2}, {ids::phIntense, 1}, {ids::phMix, 100}, {ids::phStereo, 0}}},
    // A different phase in each ear, drifting slowly.
    {"Stereo drift", true, {{ids::phSpeed, 0.4}, {ids::phIntense, 0}, {ids::phMix, 100}, {ids::phStereo, 1}}},
    // Intense and wide: a big, slow wave across the stereo field.
    {"Stereo intense", true, {{ids::phSpeed, 0.7}, {ids::phIntense, 1}, {ids::phMix, 100}, {ids::phStereo, 1}}},
    // Fast, close to a rotary speaker.
    {"Fast whirl", true, {{ids::phSpeed, 4.5}, {ids::phIntense, 0}, {ids::phMix, 100}, {ids::phStereo, 0}}},
    // Quick and throbbing, kept behind the dry sound.
    {"Throb", true, {{ids::phSpeed, 6.5}, {ids::phIntense, 1}, {ids::phMix, 70}, {ids::phStereo, 0}}},
};

// The chorus: mode (0 CE-2, 1 Juno), rate in Hz and depth in percent (CE-2), the Juno button (0 I, 1 II, 2 I+II), mix in percent, channels.
inline constexpr ModulePreset kChorusPresets[] = {
    {"Off", false, {}},
    // The single-voice pedal chorus, as it comes.
    {"CE-2 classic", true, {{ids::chMode, 0}, {ids::chRate, 1.2}, {ids::chDepth, 50}, {ids::chJuno, 0}, {ids::chMix, 100}, {ids::chStereo, 0}}},
    // A shallow, slow shimmer, a little behind the dry sound.
    {"Shimmer", true, {{ids::chMode, 0}, {ids::chRate, 0.6}, {ids::chDepth, 35}, {ids::chJuno, 0}, {ids::chMix, 70}, {ids::chStereo, 1}}},
    // Barely there: it only thickens the note, like a double.
    {"Subtle double", true, {{ids::chMode, 0}, {ids::chRate, 0.9}, {ids::chDepth, 25}, {ids::chJuno, 0}, {ids::chMix, 45}, {ids::chStereo, 1}}},
    // Slow and deep: the pitch rolls like a boat.
    {"Seasick", true, {{ids::chMode, 0}, {ids::chRate, 0.3}, {ids::chDepth, 85}, {ids::chJuno, 0}, {ids::chMix, 100}, {ids::chStereo, 1}}},
    // Quick and shallow: a warble.
    {"Fast warble", true, {{ids::chMode, 0}, {ids::chRate, 5.0}, {ids::chDepth, 40}, {ids::chJuno, 0}, {ids::chMix, 100}, {ids::chStereo, 0}}},
    // The stereo chorus of a 1980s solid-state combo: slow, wide, one voice in each ear.
    {"JC slow", true, {{ids::chMode, 2}, {ids::chRate, 1.0}, {ids::chDepth, 55}, {ids::chJuno, 0}, {ids::chMix, 100}, {ids::chStereo, 0}}},
    // The same, faster and a little shallower.
    {"JC fast", true, {{ids::chMode, 2}, {ids::chRate, 3.0}, {ids::chDepth, 45}, {ids::chJuno, 0}, {ids::chMix, 100}, {ids::chStereo, 0}}},
    // The two-voice synthesiser chorus, button I: slow and gentle.
    {"Juno I", true, {{ids::chMode, 1}, {ids::chRate, 1.2}, {ids::chDepth, 50}, {ids::chJuno, 0}, {ids::chMix, 100}, {ids::chStereo, 0}}},
    // Button II: faster and wider.
    {"Juno II", true, {{ids::chMode, 1}, {ids::chRate, 1.2}, {ids::chDepth, 50}, {ids::chJuno, 1}, {ids::chMix, 100}, {ids::chStereo, 0}}},
    // Both buttons: the lush, shimmering one.
    {"Juno I+II", true, {{ids::chMode, 1}, {ids::chRate, 1.2}, {ids::chDepth, 50}, {ids::chJuno, 2}, {ids::chMix, 100}, {ids::chStereo, 0}}},
};

// The vibe: speed in Hz, intensity in percent, vibrato (0 chorus, 1 vibrato), mix in percent, channels.
inline constexpr ModulePreset kVibePresets[] = {
    {"Off", false, {}},
    // The slow, watery chorus of the pedal.
    {"Slow chorus", true, {{ids::vbSpeed, 2.0}, {ids::vbIntensity, 70}, {ids::vbVibrato, 0}, {ids::vbMix, 100}, {ids::vbStereo, 0}}},
    // Faster and deeper: it pulses.
    {"Pulsing chorus", true, {{ids::vbSpeed, 4.5}, {ids::vbIntensity, 80}, {ids::vbVibrato, 0}, {ids::vbMix, 100}, {ids::vbStereo, 0}}},
    // The pure vibrato: the pitch wobbles with nothing dry underneath.
    {"Vibrato", true, {{ids::vbSpeed, 4.2}, {ids::vbIntensity, 70}, {ids::vbVibrato, 1}, {ids::vbMix, 100}, {ids::vbStereo, 0}}},
    // Slow and wide: a lazy wave in the pitch.
    {"Slow wave", true, {{ids::vbSpeed, 0.8}, {ids::vbIntensity, 90}, {ids::vbVibrato, 1}, {ids::vbMix, 100}, {ids::vbStereo, 0}}},
    // Fast and deep: a throbbing vibrato.
    {"Fast throb", true, {{ids::vbSpeed, 8.0}, {ids::vbIntensity, 60}, {ids::vbVibrato, 1}, {ids::vbMix, 100}, {ids::vbStereo, 0}}},
    // Only a touch of it, behind the dry sound.
    {"Subtle vibe", true, {{ids::vbSpeed, 3.0}, {ids::vbIntensity, 40}, {ids::vbVibrato, 0}, {ids::vbMix, 60}, {ids::vbStereo, 0}}},
    // A different lamp phase in each ear.
    {"Stereo vibe", true, {{ids::vbSpeed, 3.0}, {ids::vbIntensity, 70}, {ids::vbVibrato, 0}, {ids::vbMix, 100}, {ids::vbStereo, 1}}},
};

// The tremolo: rate in Hz, depth in percent, shape (0 sine, 1 triangle, 2 square), channels (1 = auto-pan).
inline constexpr ModulePreset kTremoloPresets[] = {
    {"Off", false, {}},
    // The old amp tremolo: a smooth, shallow pulse.
    {"Vintage sine", true, {{ids::trRate, 4.5}, {ids::trDepth, 45}, {ids::trShape, 0}, {ids::trStereo, 0}}},
    // Slow and deep: a throb you can count.
    {"Slow throb", true, {{ids::trRate, 2.5}, {ids::trDepth, 60}, {ids::trShape, 0}, {ids::trStereo, 0}}},
    // A fast triangle: the wet, rippling surf guitar.
    {"Surf", true, {{ids::trRate, 7.0}, {ids::trDepth, 70}, {ids::trShape, 1}, {ids::trStereo, 0}}},
    // On and off, like a hard gate that opens and shuts.
    {"Choppy square", true, {{ids::trRate, 5.0}, {ids::trDepth, 80}, {ids::trShape, 2}, {ids::trStereo, 0}}},
    // The sound moves from ear to ear.
    {"Auto-pan", true, {{ids::trRate, 3.5}, {ids::trDepth, 70}, {ids::trShape, 0}, {ids::trStereo, 1}}},
    // Fast and light: the sound shimmers.
    {"Fast shimmer", true, {{ids::trRate, 10.0}, {ids::trDepth, 30}, {ids::trShape, 0}, {ids::trStereo, 0}}},
    // Hardly there.
    {"Subtle", true, {{ids::trRate, 4.0}, {ids::trDepth, 20}, {ids::trShape, 0}, {ids::trStereo, 0}}},
    // A deep square pulse for a rhythmic, stuttering effect.
    {"Deep pulse", true, {{ids::trRate, 6.0}, {ids::trDepth, 100}, {ids::trShape, 2}, {ids::trStereo, 0}}},
};

// The flanger: rate in Hz, depth, manual and feedback in percent, mix in percent, channels.
inline constexpr ModulePreset kFlangerPresets[] = {
    {"Off", false, {}},
    // The classic jet sweep.
    {"Classic jet", true, {{ids::flRate, 0.4}, {ids::flDepth, 70}, {ids::flManual, 40}, {ids::flFeedback, 50}, {ids::flMix, 100}, {ids::flStereo, 0}}},
    // A very slow, deep sweep: the sound of a plane going by.
    {"Slow jet", true, {{ids::flRate, 0.12}, {ids::flDepth, 85}, {ids::flManual, 35}, {ids::flFeedback, 60}, {ids::flMix, 100}, {ids::flStereo, 0}}},
    // Half of the effect, little feedback: a gentle swirl.
    {"Soft flange", true, {{ids::flRate, 0.3}, {ids::flDepth, 50}, {ids::flManual, 45}, {ids::flFeedback, 25}, {ids::flMix, 60}, {ids::flStereo, 0}}},
    // High feedback: a ringing, metallic comb.
    {"Metallic", true, {{ids::flRate, 0.5}, {ids::flDepth, 60}, {ids::flManual, 30}, {ids::flFeedback, 85}, {ids::flMix, 100}, {ids::flStereo, 0}}},
    // Negative feedback: hollow, with a different set of notches.
    {"Negative sweep", true, {{ids::flRate, 0.35}, {ids::flDepth, 70}, {ids::flManual, 40}, {ids::flFeedback, -70}, {ids::flMix, 100}, {ids::flStereo, 0}}},
    // Slow and wide across the two ears.
    {"Stereo wash", true, {{ids::flRate, 0.2}, {ids::flDepth, 80}, {ids::flManual, 40}, {ids::flFeedback, 55}, {ids::flMix, 100}, {ids::flStereo, 1}}},
    // A quick, shallow whoosh.
    {"Fast whoosh", true, {{ids::flRate, 2.5}, {ids::flDepth, 40}, {ids::flManual, 50}, {ids::flFeedback, 40}, {ids::flMix, 100}, {ids::flStereo, 0}}},
    // Narrow and resonant: it rings at the top of the sweep.
    {"Resonant", true, {{ids::flRate, 0.6}, {ids::flDepth, 30}, {ids::flManual, 55}, {ids::flFeedback, 80}, {ids::flMix, 100}, {ids::flStereo, 0}}},
};

// The pitch shifter, with no switch of its own: semitones and cents. The first one puts it back at the original pitch.
inline constexpr ModulePreset kPitchPresets[] = {
    {"Original pitch", false, {{ids::pitch, 0}, {ids::pitchFine, 0}}},
    {"Octave up", true, {{ids::pitch, 12}, {ids::pitchFine, 0}}},
    {"Octave down", true, {{ids::pitch, -12}, {ids::pitchFine, 0}}},
    {"Fifth up", true, {{ids::pitch, 7}, {ids::pitchFine, 0}}},
    {"Fourth up", true, {{ids::pitch, 5}, {ids::pitchFine, 0}}},
    {"Fifth down", true, {{ids::pitch, -7}, {ids::pitchFine, 0}}},
    // Hendrix tuning.
    {"Half step down", true, {{ids::pitch, -1}, {ids::pitchFine, 0}}},
    // D standard, or a quick drop D for the low string.
    {"Whole step down", true, {{ids::pitch, -2}, {ids::pitchFine, 0}}},
    // A little sharp: it fits a guitar that is a touch flat.
    {"Slightly sharp", true, {{ids::pitch, 0}, {ids::pitchFine, 12}}},
};

// The harmonizer: mode (0 harmony, 1 pitch, 2 detune), key, scale, the two intervals, detune in cents, and the levels of the guitar and the voices.
inline constexpr ModulePreset kHarmonistPresets[] = {
    {"Off", false, {}},
    // A third above, in key: the classic twin-guitar harmony.
    {"Third above", true, {{ids::harMode, 0}, {ids::harKey, 0}, {ids::harScale, 0}, {ids::harInt1, 2}, {ids::harInt2, 0}, {ids::harDirect, 10}, {ids::harVoice1, 8}, {ids::harVoice2, 0}}},
    // A third and a fifth above: a full triad on every note.
    {"Third and fifth", true, {{ids::harMode, 0}, {ids::harKey, 0}, {ids::harScale, 0}, {ids::harInt1, 2}, {ids::harInt2, 4}, {ids::harDirect, 10}, {ids::harVoice1, 7}, {ids::harVoice2, 6}}},
    // A third below, and a sixth above.
    {"Third below, sixth above", true, {{ids::harMode, 0}, {ids::harKey, 0}, {ids::harScale, 0}, {ids::harInt1, -2}, {ids::harInt2, 5}, {ids::harDirect, 10}, {ids::harVoice1, 7}, {ids::harVoice2, 6}}},
    // The same in a minor key.
    {"Minor thirds", true, {{ids::harMode, 0}, {ids::harKey, 9}, {ids::harScale, 1}, {ids::harInt1, 2}, {ids::harInt2, 0}, {ids::harDirect, 10}, {ids::harVoice1, 8}, {ids::harVoice2, 0}}},
    // The fifth above, fixed: the power chord that does not need a chord.
    {"Fixed fifth", true, {{ids::harMode, 1}, {ids::harInt1, 7}, {ids::harInt2, 0}, {ids::harDirect, 10}, {ids::harVoice1, 7}, {ids::harVoice2, 0}}},
    // A fifth up and an octave down: organ-like.
    {"Fifth and octave down", true, {{ids::harMode, 1}, {ids::harInt1, 7}, {ids::harInt2, -12}, {ids::harDirect, 10}, {ids::harVoice1, 6}, {ids::harVoice2, 7}}},
    // A few cents either side: a thick doubled sound.
    {"Detune", true, {{ids::harMode, 2}, {ids::harDetune, 12}, {ids::harDirect, 10}, {ids::harVoice1, 7}, {ids::harVoice2, 7}}},
};

// The analog octaver: direct, octave 1, octave 2 and tone, all on the 0..10 scale.
inline constexpr ModulePreset kOctaverPresets[] = {
    {"Off", false, {}},
    // One octave down under the guitar: the classic fat bass line.
    {"Octave down", true, {{ids::octDirect, 10}, {ids::octOct1, 7}, {ids::octOct2, 0}, {ids::octTone, 3}}},
    // Both octaves under the guitar: a synth-bass thickness.
    {"Two octaves", true, {{ids::octDirect, 8}, {ids::octOct1, 7}, {ids::octOct2, 6}, {ids::octTone, 3}}},
    // The two octaves alone, no guitar on top.
    {"Sub only", true, {{ids::octDirect, 0}, {ids::octOct1, 8}, {ids::octOct2, 7}, {ids::octTone, 2}}},
    // A bit of the square wave's buzz: the vintage pedal on a low note.
    {"Buzzy octave", true, {{ids::octDirect, 7}, {ids::octOct1, 8}, {ids::octOct2, 0}, {ids::octTone, 8}}},
    // Just a little octave under the playing.
    {"Subtle octave", true, {{ids::octDirect, 10}, {ids::octOct1, 4}, {ids::octOct2, 0}, {ids::octTone, 2}}},
};

// The clean boost: gain in dB, tone on the 0..10 scale, soft clip switch.
inline constexpr ModulePreset kBoostPresets[] = {
    {"Off", false, {}},
    // A small lift for a quiet amp.
    {"Clean lift", true, {{ids::bstGain, 6}, {ids::bstTone, 5}, {ids::bstSoft, 0}}},
    // Pushes the amp into a warmer, louder place.
    {"Amp push", true, {{ids::bstGain, 12}, {ids::bstTone, 5.5}, {ids::bstSoft, 0}}},
    // A solo boost: the level jumps and the highs open a little.
    {"Solo boost", true, {{ids::bstGain, 18}, {ids::bstTone, 6}, {ids::bstSoft, 0}}},
    // A lot of gain with the soft clip catching the peaks: it saturates the amp.
    {"Hot lead", true, {{ids::bstGain, 24}, {ids::bstTone, 6}, {ids::bstSoft, 1}}},
    // The tone turned down: round and woolly.
    {"Dark boost", true, {{ids::bstGain, 14}, {ids::bstTone, 2.5}, {ids::bstSoft, 0}}},
    // The tone turned up: cutting, for a dull amp.
    {"Bright boost", true, {{ids::bstGain, 10}, {ids::bstTone, 8}, {ids::bstSoft, 0}}},
    // Soft clip on: it fattens the signal before the amp.
    {"Fatten", true, {{ids::bstGain, 20}, {ids::bstTone, 5}, {ids::bstSoft, 1}}},
    // The old treble booster: thin and bright, to drive a plexi hard.
    {"Treble booster", true, {{ids::bstGain, 16}, {ids::bstTone, 9}, {ids::bstSoft, 1}}},
};

// The rotary speaker: speed (0 stop, 1 slow, 2 fast), depth and horn / rotor balance on the 0..10 scale, mix in percent.
inline constexpr ModulePreset kLesliePresets[] = {
    {"Off", false, {}},
    // The slow speed: a gentle, wide sway (the chorale).
    {"Chorale", true, {{ids::lesSpeed, 1}, {ids::lesDepth, 7}, {ids::lesBalance, 5}, {ids::lesWidth, 7}, {ids::lesMix, 100}}},
    // The fast speed: the quick, shimmering throb (the tremolo).
    {"Tremolo", true, {{ids::lesSpeed, 2}, {ids::lesDepth, 7}, {ids::lesBalance, 5}, {ids::lesWidth, 8}, {ids::lesMix, 100}}},
    // A little of it, behind the dry sound.
    {"Gentle", true, {{ids::lesSpeed, 1}, {ids::lesDepth, 4}, {ids::lesBalance, 5}, {ids::lesWidth, 5}, {ids::lesMix, 60}}},
    // All the depth and the horn forward: a swirling, airy lead.
    {"Deep and bright", true, {{ids::lesSpeed, 2}, {ids::lesDepth, 10}, {ids::lesBalance, 7.5}, {ids::lesWidth, 9}, {ids::lesMix, 100}}},
    // Rotor forward: a warm, slow bass swell.
    {"Warm rotor", true, {{ids::lesSpeed, 1}, {ids::lesDepth, 8}, {ids::lesBalance, 2.5}, {ids::lesWidth, 7}, {ids::lesMix, 100}}},
    // The brake: the horn stops and the sound sits to one side.
    {"Brake", true, {{ids::lesSpeed, 0}, {ids::lesDepth, 7}, {ids::lesBalance, 5}, {ids::lesWidth, 7}, {ids::lesMix, 100}}},
};

// The spring reverb: tank (0 short, 1 medium, 2 long), tone on the 0..10 scale, mix in percent.
inline constexpr ModulePreset kSpringPresets[] = {
    {"Off", false, {}},
    // A little splash at the back of a clean sound.
    {"Classic", true, {{ids::spType, 1}, {ids::spTone, 5}, {ids::spMix, 30}}},
    // A small combo's short tank: tight and boingy.
    {"Short drip", true, {{ids::spType, 0}, {ids::spTone, 6}, {ids::spMix, 35}}},
    // The long tank: a lazy, dark decay.
    {"Long and dark", true, {{ids::spType, 2}, {ids::spTone, 3}, {ids::spMix, 35}}},
    // Wet and bright, the surf sound.
    {"Surf", true, {{ids::spType, 1}, {ids::spTone, 8}, {ids::spMix, 65}}},
    // Barely there: some air and space.
    {"Subtle room", true, {{ids::spType, 0}, {ids::spTone, 5}, {ids::spMix, 15}}},
    // Everything: a big, long splash.
    {"Splash", true, {{ids::spType, 2}, {ids::spTone, 6}, {ids::spMix, 70}}},
};

// The Klon-style drive: gain, treble and level on the 0..10 scale.
inline constexpr ModulePreset kKlonPresets[] = {
    {"Off", false, {}},
    // Almost clean, a lift that opens the amp up (the way it is most often used).
    {"Transparent boost", true, {{ids::klnGain, 2}, {ids::klnTreble, 5}, {ids::klnLevel, 7}}},
    // A little grit that cleans up with the guitar's volume.
    {"Light drive", true, {{ids::klnGain, 4}, {ids::klnTreble, 5}, {ids::klnLevel, 6}}},
    // Gain at noon: the classic overdrive in front of a clean amp.
    {"Classic", true, {{ids::klnGain, 6}, {ids::klnTreble, 5}, {ids::klnLevel, 5.5}}},
    // More gain, a little brighter: lead sustain.
    {"Hot lead", true, {{ids::klnGain, 8}, {ids::klnTreble, 6}, {ids::klnLevel, 4.5}}},
    // Treble down: round and dark.
    {"Dark", true, {{ids::klnGain, 5}, {ids::klnTreble, 2}, {ids::klnLevel, 6}}},
    // Treble up: cutting, for a dull amp.
    {"Bright", true, {{ids::klnGain, 5}, {ids::klnTreble, 8}, {ids::klnLevel, 5}}},
};

// The acoustic simulator: body type (0 dreadnought, 1 jumbo, 2 parlor, 3 piezo), body, top, air and squash on the 0..10 scale, mix in percent.
inline constexpr ModulePreset kAcousticPresets[] = {
    {"Off", false, {}},
    // The everyday flat-top: full body, strings a little open.
    {"Dreadnought strum", true, {{ids::acType, 0}, {ids::acBody, 6}, {ids::acTop, 5.5}, {ids::acAir, 4}, {ids::acSquash, 4}, {ids::acMix, 100}}},
    // Big and boomy, for chords under a singer.
    {"Jumbo boom", true, {{ids::acType, 1}, {ids::acBody, 7.5}, {ids::acTop, 5}, {ids::acAir, 3}, {ids::acSquash, 4}, {ids::acMix, 100}}},
    // Small and woody, with a short low end: good for fingerstyle.
    {"Parlor fingerstyle", true, {{ids::acType, 2}, {ids::acBody, 6.5}, {ids::acTop, 5.5}, {ids::acAir, 4.5}, {ids::acSquash, 3}, {ids::acMix, 100}}},
    // The thin, snappy sound of an undersaddle pickup played direct.
    {"Piezo direct", true, {{ids::acType, 3}, {ids::acBody, 5}, {ids::acTop, 6}, {ids::acAir, 5}, {ids::acSquash, 6}, {ids::acMix, 100}}},
    // Bright and compressed, to cut through a band.
    {"Bright strum", true, {{ids::acType, 0}, {ids::acBody, 5.5}, {ids::acTop, 7.5}, {ids::acAir, 6}, {ids::acSquash, 5.5}, {ids::acMix, 100}}},
    // Dark and round, for slow things.
    {"Warm ballad", true, {{ids::acType, 1}, {ids::acBody, 7}, {ids::acTop, 3}, {ids::acAir, 2}, {ids::acSquash, 3}, {ids::acMix, 100}}},
    // Half electric, half acoustic.
    {"Electro-acoustic blend", true, {{ids::acType, 0}, {ids::acBody, 6}, {ids::acTop, 5}, {ids::acAir, 4}, {ids::acSquash, 4}, {ids::acMix, 55}}},
};

// The limiter: gain in dB, ceiling in dB, release in ms.
inline constexpr ModulePreset kLimiterPresets[] = {
    {"Off", false, {}},
    // No gain: it only catches the peaks above the ceiling.
    {"Transparent catch", true, {{ids::limGain, 0}, {ids::limCeiling, -1}, {ids::limRelease, 150}}},
    // A lower ceiling and a slow release: a safety net for the output.
    {"Safety net", true, {{ids::limGain, 0}, {ids::limCeiling, -3}, {ids::limRelease, 250}}},
    // A little gain and a long release: it glues the dynamics together.
    {"Smooth glue", true, {{ids::limGain, 4}, {ids::limCeiling, -1.5}, {ids::limRelease, 400}}},
    // Pushed harder: loud and even.
    {"Loud and flat", true, {{ids::limGain, 8}, {ids::limCeiling, -1}, {ids::limRelease, 120}}},
    // Fast and heavy: it flattens everything.
    {"Hard push", true, {{ids::limGain, 14}, {ids::limCeiling, -2}, {ids::limRelease, 70}}},
    // The most aggressive: a brick wall.
    {"Brickwall", true, {{ids::limGain, 20}, {ids::limCeiling, -0.5}, {ids::limRelease, 35}}},
    // Quiet ceiling, just to keep the loudest hits in check.
    {"Gentle ceiling", true, {{ids::limGain, 2}, {ids::limCeiling, -6}, {ids::limRelease, 300}}},
};

// The ten-band graphic EQ: the ten bands (31 Hz to 16 kHz) in dB, then GAIN and VOLUME in dB.
#define TEN_PRESET(name, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, gain, vol)     {name, true, {{ids::tenBand[0], b1}, {ids::tenBand[1], b2}, {ids::tenBand[2], b3}, {ids::tenBand[3], b4}, {ids::tenBand[4], b5}, {ids::tenBand[5], b6}, {ids::tenBand[6], b7},                   {ids::tenBand[7], b8}, {ids::tenBand[8], b9}, {ids::tenBand[9], b10}, {ids::tenGain, gain}, {ids::tenVolume, vol}}}
inline constexpr ModulePreset kTenBandEqPresets[] = {
    {"Off", false, {}},
    TEN_PRESET("Flat", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    // The scooped smile: bass and treble up, the mids down. Fat and glassy for clean and crunch tones.
    TEN_PRESET("Smile", 6, 5, 3, 0, -3, -4, -2, 3, 5, 6, 0, -2),
    // A mid hump to cut through a mix, with the lows and the fizz tamed.
    TEN_PRESET("Lead mids", -6, -5, -3, 0, 3, 6, 5, 2, -2, -5, 2, 0),
    // Tightens a high-gain tone: the boom out, the low mids lifted, the harsh top trimmed.
    TEN_PRESET("Tight metal", -9, -7, -3, 2, 3, 0, 1, 4, 0, -6, 0, 0),
    // Warm and dark for jazz and neck pickups.
    TEN_PRESET("Warm jazz", 2, 3, 4, 3, 1, -1, -4, -6, -8, -9, 0, 1),
    // Bright and airy for single coils and acoustic-like tones.
    TEN_PRESET("Glassy clean", -4, -3, -2, -1, 0, 1, 3, 5, 6, 5, 0, 0),
    // The mids pushed for a solo: a clean boost with character.
    TEN_PRESET("Solo boost", -3, -2, 0, 3, 5, 6, 4, 2, 0, -3, 6, -3),
    // Presence and bite for a bass or a thick rhythm guitar.
    TEN_PRESET("Bass punch", 5, 6, 4, 1, -1, 2, 5, 3, 0, -3, 0, 0),
};
#undef TEN_PRESET

// The BBD analog delay: time ms, feedback %, mix %, tone Hz, mod rate Hz, mod depth %, grit %.
inline constexpr ModulePreset kBbdPresets[] = {
    {"Off", false, {}},
    // The classic 300 ms: warm repeats that darken as they fade.
    {"Classic 300 ms", true, {{ids::bbdTime, 300}, {ids::bbdFeedback, 40}, {ids::bbdMix, 35}, {ids::bbdTone, 5000}, {ids::bbdRate, 0.8}, {ids::bbdDepth, 25}, {ids::bbdGrit, 30}, {ids::bbdStereo, 0}}},
    // Short and tight, a doubling that thickens the note.
    {"Slapback", true, {{ids::bbdTime, 90}, {ids::bbdFeedback, 12}, {ids::bbdMix, 30}, {ids::bbdTone, 6500}, {ids::bbdRate, 0.5}, {ids::bbdDepth, 10}, {ids::bbdGrit, 20}, {ids::bbdStereo, 0}}},
    // Medium repeats at a rhythmic tempo, wide in stereo.
    {"Rhythm echo", true, {{ids::bbdTime, 380}, {ids::bbdFeedback, 45}, {ids::bbdMix, 30}, {ids::bbdTone, 4500}, {ids::bbdRate, 0.6}, {ids::bbdDepth, 20}, {ids::bbdGrit, 30}, {ids::bbdStereo, 1}}},
    // Chorus inside the repeats: swimming, a little seasick.
    {"Chorused echo", true, {{ids::bbdTime, 280}, {ids::bbdFeedback, 50}, {ids::bbdMix, 40}, {ids::bbdTone, 4200}, {ids::bbdRate, 1.6}, {ids::bbdDepth, 60}, {ids::bbdGrit, 30}, {ids::bbdStereo, 1}}},
    // Long and murky: the repeats melt into the dark.
    {"Dark and long", true, {{ids::bbdTime, 520}, {ids::bbdFeedback, 55}, {ids::bbdMix, 35}, {ids::bbdTone, 2500}, {ids::bbdRate, 0.3}, {ids::bbdDepth, 30}, {ids::bbdGrit, 45}, {ids::bbdStereo, 0}}},
    // Past 100 % feedback: the loop swells until it saturates.
    {"Self-oscillation", true, {{ids::bbdTime, 330}, {ids::bbdFeedback, 100}, {ids::bbdMix, 40}, {ids::bbdTone, 3500}, {ids::bbdRate, 0.7}, {ids::bbdDepth, 35}, {ids::bbdGrit, 55}, {ids::bbdStereo, 0}}},
    // Very short and deep: a flanger-like flutter.
    {"Flutter", true, {{ids::bbdTime, 45}, {ids::bbdFeedback, 55}, {ids::bbdMix, 50}, {ids::bbdTone, 8000}, {ids::bbdRate, 0.4}, {ids::bbdDepth, 70}, {ids::bbdGrit, 15}, {ids::bbdStereo, 1}}},
};

// The bitcrusher: bits, rate Hz, anti-alias, tone Hz, dither %, jitter %, mix %.
inline constexpr ModulePreset kBitcrusherPresets[] = {
    {"Off", false, {}},
    // A little grit and a little dullness: a 12-bit sampler of the 1980s.
    {"12-bit sampler", true, {{ids::bcBits, 12}, {ids::bcRate, 26000}, {ids::bcAntiAlias, 1}, {ids::bcTone, 12000}, {ids::bcDither, 30}, {ids::bcJitter, 0}, {ids::bcMix, 100}}},
    // A console of the 1990s: 8 bits, no filter, the aliasing is the sound.
    {"8-bit console", true, {{ids::bcBits, 8}, {ids::bcRate, 11025}, {ids::bcAntiAlias, 0}, {ids::bcTone, 9000}, {ids::bcDither, 0}, {ids::bcJitter, 0}, {ids::bcMix, 100}}},
    // A band-limited, grainy voice: the telephone.
    {"Telephone", true, {{ids::bcBits, 9}, {ids::bcRate, 8000}, {ids::bcAntiAlias, 1}, {ids::bcTone, 3400}, {ids::bcDither, 20}, {ids::bcJitter, 0}, {ids::bcMix, 100}}},
    // Very few bits: the notes fall apart into steps.
    {"Crushed", true, {{ids::bcBits, 4}, {ids::bcRate, 14000}, {ids::bcAntiAlias, 0}, {ids::bcTone, 8000}, {ids::bcDither, 0}, {ids::bcJitter, 0}, {ids::bcMix, 100}}},
    // Full resolution, a very low rate and no filter: only the metallic aliasing.
    {"Aliasing", true, {{ids::bcBits, 16}, {ids::bcRate, 4500}, {ids::bcAntiAlias, 0}, {ids::bcTone, 12000}, {ids::bcDither, 0}, {ids::bcJitter, 0}, {ids::bcMix, 100}}},
    // The clock wobbling: a rough, unstable lo-fi.
    {"Unstable clock", true, {{ids::bcBits, 10}, {ids::bcRate, 9000}, {ids::bcAntiAlias, 0}, {ids::bcTone, 7000}, {ids::bcDither, 15}, {ids::bcJitter, 70}, {ids::bcMix, 100}}},
    // Underneath the clean sound, a gritty copy: parallel crush.
    {"Parallel grit", true, {{ids::bcBits, 6}, {ids::bcRate, 10000}, {ids::bcAntiAlias, 0}, {ids::bcTone, 8000}, {ids::bcDither, 0}, {ids::bcJitter, 0}, {ids::bcMix, 35}}},
};

// The sustain layers: mix, attack, decay, trig, diffusion on the 0..10 scale, and the single, sub octave, hold and channels switches.
inline constexpr ModulePreset kLayersPresets[] = {
    {"Off", false, {}},
    // The default: every pick attack leaves a soft pad that fades in and lingers for a few seconds.
    {"Pad bloom", true, {{ids::lyMix, 7}, {ids::lyAttack, 4}, {ids::lyDecay, 6}, {ids::lyTrig, 5}, {ids::lyDiffusion, 0}, {ids::lySingle, 0}, {ids::lySub, 0}, {ids::lyHold, 0}, {ids::lyStereo, 0}}},
    // A slow swell: each layer takes a second to come in, like a violin or a volume swell.
    {"Slow swell", true, {{ids::lyMix, 7}, {ids::lyAttack, 7.5}, {ids::lyDecay, 7}, {ids::lyTrig, 5}, {ids::lyDiffusion, 2}, {ids::lySingle, 0}, {ids::lySub, 0}, {ids::lyHold, 0}, {ids::lyStereo, 1}}},
    // Short layers that thicken the notes without staying behind.
    {"Short bloom", true, {{ids::lyMix, 5}, {ids::lyAttack, 2}, {ids::lyDecay, 3}, {ids::lyTrig, 6}, {ids::lyDiffusion, 0}, {ids::lySingle, 0}, {ids::lySub, 0}, {ids::lyHold, 0}, {ids::lyStereo, 0}}},
    // An octave below: a layer of bass under the chords.
    {"Sub pad", true, {{ids::lyMix, 6}, {ids::lyAttack, 5}, {ids::lyDecay, 6.5}, {ids::lyTrig, 5}, {ids::lyDiffusion, 1}, {ids::lySingle, 0}, {ids::lySub, 1}, {ids::lyHold, 0}, {ids::lyStereo, 0}}},
    // One layer at a time: each new note takes over the drone.
    {"Single drone", true, {{ids::lyMix, 7}, {ids::lyAttack, 5}, {ids::lyDecay, 8}, {ids::lyTrig, 5}, {ids::lyDiffusion, 1}, {ids::lySingle, 1}, {ids::lySub, 0}, {ids::lyHold, 0}, {ids::lyStereo, 0}}},
    // A blurred, textural cloud spread across the stereo picture.
    {"Diffuse cloud", true, {{ids::lyMix, 8}, {ids::lyAttack, 6}, {ids::lyDecay, 8}, {ids::lyTrig, 5}, {ids::lyDiffusion, 8}, {ids::lySingle, 0}, {ids::lySub, 0}, {ids::lyHold, 0}, {ids::lyStereo, 1}}},
    // No automatic layers: only the Hold switch makes one (a footswitch held down).
    {"Manual hold", true, {{ids::lyMix, 8}, {ids::lyAttack, 3}, {ids::lyDecay, 6}, {ids::lyTrig, 0}, {ids::lyDiffusion, 2}, {ids::lySingle, 0}, {ids::lySub, 0}, {ids::lyHold, 0}, {ids::lyStereo, 0}}},
};

// The distortion pedals: drive, tone, level, and the MT-2 style EQ (low, middle, mid frequency, high) on the 0..10 scale, and the model (0 Boss DS-1, 1 Boss MT-2 style).
inline constexpr ModulePreset kDistortionPresets[] = {
    {"Off", false, {}},
    // The pedal turned down: a crunch that still follows the pick.
    {"DS-1 crunch", true, {{ids::dstModel, 0}, {ids::dstDrive, 3}, {ids::dstTone, 5}, {ids::dstLevel, 4}}},
    // The classic rock sound of the 1980s.
    {"DS-1 classic", true, {{ids::dstModel, 0}, {ids::dstDrive, 6}, {ids::dstTone, 5.5}, {ids::dstLevel, 4}}},
    // A lot of gain and a little more treble for solos.
    {"DS-1 lead", true, {{ids::dstModel, 0}, {ids::dstDrive, 8.5}, {ids::dstTone, 6}, {ids::dstLevel, 3.5}}},
    // Darker, for rhythm guitar with chords.
    {"DS-1 dark rhythm", true, {{ids::dstModel, 0}, {ids::dstDrive, 5}, {ids::dstTone, 3}, {ids::dstLevel, 4}}},
    // The tone open: bright and cutting.
    {"DS-1 bright", true, {{ids::dstModel, 0}, {ids::dstDrive, 4}, {ids::dstTone, 8}, {ids::dstLevel, 3.5}}},
    // The MT-2 style: the scooped metal sound, the middle low and sweeping up.
    {"MT-2 metal rhythm", true, {{ids::dstModel, 1}, {ids::dstDrive, 6}, {ids::dstTone, 5}, {ids::dstLevel, 5}, {ids::dstLow, 6.5}, {ids::dstMid, 3}, {ids::dstMidFreq, 4}, {ids::dstHigh, 6}}},
    {"MT-2 scooped", true, {{ids::dstModel, 1}, {ids::dstDrive, 8}, {ids::dstTone, 5}, {ids::dstLevel, 4.5}, {ids::dstLow, 7}, {ids::dstMid, 1.5}, {ids::dstMidFreq, 5}, {ids::dstHigh, 7}}},
    {"MT-2 mid lead", true, {{ids::dstModel, 1}, {ids::dstDrive, 7}, {ids::dstTone, 5}, {ids::dstLevel, 5}, {ids::dstLow, 5}, {ids::dstMid, 7}, {ids::dstMidFreq, 5.5}, {ids::dstHigh, 5}}},
    {"MT-2 tight", true, {{ids::dstModel, 1}, {ids::dstDrive, 5}, {ids::dstTone, 5}, {ids::dstLevel, 5}, {ids::dstLow, 4}, {ids::dstMid, 5.5}, {ids::dstMidFreq, 4.5}, {ids::dstHigh, 5.5}}},
};

// The presets of a module type (count 0 when it has none of this kind).
inline PresetList modulePresets(sas::ModuleType type) {
    using T = sas::ModuleType;
    auto list = [](const auto& table) { return PresetList{table, int(sizeof(table) / sizeof(table[0]))}; };
    switch (type) {
        case T::Gate: return list(kGatePresets);
        case T::Compressor: return list(kCompressorPresets);
        case T::Overdrive: return list(kOverdrivePresets);
        case T::Phaser: return list(kPhaserPresets);
        case T::Chorus: return list(kChorusPresets);
        case T::Vibe: return list(kVibePresets);
        case T::Tremolo: return list(kTremoloPresets);
        case T::Flanger: return list(kFlangerPresets);
        case T::Pitch: return list(kPitchPresets);
        case T::Boost: return list(kBoostPresets);
        case T::Octaver: return list(kOctaverPresets);
        case T::Harmonist: return list(kHarmonistPresets);
        case T::BbdDelay: return list(kBbdPresets);
        case T::Bitcrusher: return list(kBitcrusherPresets);
        case T::Layers: return list(kLayersPresets);
        case T::Distortion: return list(kDistortionPresets);
        case T::TenBandEq: return list(kTenBandEqPresets);
        case T::Acoustic: return list(kAcousticPresets);
        case T::Klon: return list(kKlonPresets);
        case T::Leslie: return list(kLesliePresets);
        case T::SpringReverb: return list(kSpringPresets);
        case T::Limiter: return list(kLimiterPresets);
        default: return {};
    }
}
