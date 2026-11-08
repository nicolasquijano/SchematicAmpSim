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
    PresetValue v[8];  // unused entries have no id
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

// The compressor: sustain, blend, tone and level on the panel's 0..10 scale, and the fast release switch.
inline constexpr ModulePreset kCompressorPresets[] = {
    {"Off", false, {}},
    // Evens out the playing without being noticed.
    {"Gentle leveling", true, {{ids::compSustain, 3}, {ids::compBlend, 6}, {ids::compTone, 5}, {ids::compLevel, 7.5}, {ids::compFast, 0}}},
    // Notes bloom and last: clean tones that sing.
    {"Clean sustain", true, {{ids::compSustain, 6}, {ids::compBlend, 10}, {ids::compTone, 5}, {ids::compLevel, 7}, {ids::compFast, 0}}},
    // Quick release, a little bright: the attack of every note pops.
    {"Funk snap", true, {{ids::compSustain, 6}, {ids::compBlend, 8}, {ids::compTone, 6.5}, {ids::compLevel, 7}, {ids::compFast, 1}}},
    // Squeezed and twangy, the chicken-picking sound.
    {"Country pluck", true, {{ids::compSustain, 7.5}, {ids::compBlend, 8}, {ids::compTone, 7}, {ids::compLevel, 6.5}, {ids::compFast, 1}}},
    // Hard compression with the dry signal kept in: punch and body together.
    {"Parallel punch", true, {{ids::compSustain, 8}, {ids::compBlend, 5}, {ids::compTone, 5.5}, {ids::compLevel, 7}, {ids::compFast, 1}}},
    // Smooth and thick for slow lead lines, a touch darker.
    {"Lead squash", true, {{ids::compSustain, 9}, {ids::compBlend, 10}, {ids::compTone, 4.5}, {ids::compLevel, 6}, {ids::compFast, 0}}},
    // Holds a strummed part together without flattening the dynamics.
    {"Rhythm glue", true, {{ids::compSustain, 4.5}, {ids::compBlend, 9}, {ids::compTone, 5}, {ids::compLevel, 7.5}, {ids::compFast, 1}}},
};

// The overdrive (the Tube Screamer): drive, tone and level on the 0..10 scale.
inline constexpr ModulePreset kOverdrivePresets[] = {
    {"Off", false, {}},
    // Nearly clean: it only pushes the front of the amp.
    {"Clean push", true, {{ids::tsDrive, 1}, {ids::tsTone, 5.5}, {ids::tsLevel, 8}}},
    // At the edge of breakup: the guitar's volume knob cleans it up.
    {"Edge of breakup", true, {{ids::tsDrive, 3}, {ids::tsTone, 5}, {ids::tsLevel, 6.5}}},
    // The tight mid push that holds a high-gain amp together.
    {"Tight boost", true, {{ids::tsDrive, 2}, {ids::tsTone, 6}, {ids::tsLevel, 8}}},
    // A warm, round overdrive for blues.
    {"Blues drive", true, {{ids::tsDrive, 4.5}, {ids::tsTone, 4}, {ids::tsLevel, 5.5}}},
    // The textbook setting: drive at noon.
    {"Classic crunch", true, {{ids::tsDrive, 5}, {ids::tsTone, 5}, {ids::tsLevel, 5}}},
    // More drive and the tone up: singing, a little nasal.
    {"Lead drive", true, {{ids::tsDrive, 7}, {ids::tsTone, 6.5}, {ids::tsLevel, 5.5}}},
    // Dark and smooth, with the highs rolled off.
    {"Dark smooth", true, {{ids::tsDrive, 5}, {ids::tsTone, 2.5}, {ids::tsLevel, 6}}},
    // Everything up: a thick, compressed wall.
    {"Max drive", true, {{ids::tsDrive, 9.5}, {ids::tsTone, 5}, {ids::tsLevel, 4.5}}},
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
    {"Chorale", true, {{ids::lesSpeed, 1}, {ids::lesDepth, 7}, {ids::lesBalance, 5}, {ids::lesMix, 100}}},
    // The fast speed: the quick, shimmering throb (the tremolo).
    {"Tremolo", true, {{ids::lesSpeed, 2}, {ids::lesDepth, 7}, {ids::lesBalance, 5}, {ids::lesMix, 100}}},
    // A little of it, behind the dry sound.
    {"Gentle", true, {{ids::lesSpeed, 1}, {ids::lesDepth, 4}, {ids::lesBalance, 5}, {ids::lesMix, 60}}},
    // All the depth and the horn forward: a swirling, airy lead.
    {"Deep and bright", true, {{ids::lesSpeed, 2}, {ids::lesDepth, 10}, {ids::lesBalance, 7.5}, {ids::lesMix, 100}}},
    // Rotor forward: a warm, slow bass swell.
    {"Warm rotor", true, {{ids::lesSpeed, 1}, {ids::lesDepth, 8}, {ids::lesBalance, 2.5}, {ids::lesMix, 100}}},
    // The brake: the horn stops and the sound sits to one side.
    {"Brake", true, {{ids::lesSpeed, 0}, {ids::lesDepth, 7}, {ids::lesBalance, 5}, {ids::lesMix, 100}}},
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
        case T::Acoustic: return list(kAcousticPresets);
        case T::Klon: return list(kKlonPresets);
        case T::Leslie: return list(kLesliePresets);
        case T::SpringReverb: return list(kSpringPresets);
        case T::Limiter: return list(kLimiterPresets);
        default: return {};
    }
}
