#pragma once
// The other compressors of the Compressor module (model 0 is the Keeley circuit of sas/compressor.h): three feed-forward designs that share the panel of the
// pedal (Sustain, Blend, Tone, Level, Fast) so that switching the model changes the character and not the controls.
//   1 Dyna Comp   the MXR kind: a fast peak detector and a high ratio with a firm knee. The attack lets the first bit of every note through (the "quack"
//                 of funk and country), then the level is held up and the tail swells.
//   2 FET         the 1176 kind: a very fast attack, a ratio that rises with Sustain (4:1 to 12:1) and a bit of saturation from the gain cell; punchy.
//   3 Opto        the LA-2A kind: a slow attack, a soft knee, a low ratio and a release that depends on the program: after a short compression it lets go
//                 in a fraction of a second, after a long one it takes a second or more. Smooth and almost invisible.
// All of them: the level is followed in dB (full-wave peak, one-pole attack and release), the gain computer has a soft knee, Sustain lowers the threshold
// (and raises the make-up gain so that the loudness stays), Blend mixes the compressed signal with the dry one (parallel compression), Tone tilts the
// highs of the compressed path and Level is the output. Fast shortens the release. Input and output are in volts, like the pedals.

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "sas/compressor.h"

namespace sas {

inline constexpr int kCompModelCount = 4;  // 0 Keeley, 1 Dyna Comp, 2 FET, 3 Opto

class PedalCompressor {
public:
    void prepare(double fs) {
        fs_ = fs;
        a_ = float(1.0 - std::exp(-6.283185307179586 * 1500.0 / fs));
        reset();
    }
    void reset() {
        envDb_ = -120.0f;
        engage_ = 0.0f;
        lp_ = 0.0f;
        gainDb_ = 0.0f;
    }
    void setControls(const CompControls& c) { c_ = c; }

    float process(float x) {
        const int model = std::clamp(c_.model, 1, 3);
        const double sus = std::clamp(c_.sustain, 0.0, 1.0), blend = std::clamp(c_.blend, 0.0, 1.0), tone = std::clamp(c_.tone, 0.0, 1.0), lvl = std::clamp(c_.level, 0.0, 1.0);
        // The voicing of each model: attack and release in seconds, ratio, knee in dB, the threshold range in dB re 1 V (the guitar is about -20 dBV).
        double attack, release, ratio, knee;
        switch (model) {
            case 1: attack = c_.fastRelease ? 0.0008 : 0.0015; release = c_.fastRelease ? 0.045 : 0.11; ratio = 8.0; knee = 5.0; break;
            case 2: attack = 0.0003; release = c_.fastRelease ? 0.035 : 0.12; ratio = 4.0 + 8.0 * sus; knee = 3.0; break;
            default: attack = c_.fastRelease ? 0.006 : 0.012; release = 0.06; ratio = 3.0; knee = 10.0; break;
        }
        const double thresholdDb = -12.0 - 30.0 * sus;
        if (model == 3) {  // the opto cell remembers: the more it has compressed lately, the slower it lets go
            release = 0.06 + (c_.fastRelease ? 0.5 : 1.3) * double(std::clamp(engage_, 0.0f, 1.0f));
        }
        const float kA = float(1.0 - std::exp(-1.0 / (attack * fs_))), kR = float(1.0 - std::exp(-1.0 / (release * fs_)));
        // The level detector (full-wave peak) in dB.
        const float lvlDb = 20.0f * std::log10(std::max(std::fabs(x), 1e-6f));
        envDb_ += (lvlDb > envDb_ ? kA : kR) * (lvlDb - envDb_);
        // The gain computer with a soft knee.
        const double over = double(envDb_) - thresholdDb;
        double grDb = 0.0;
        const double slope = 1.0 / ratio - 1.0;
        if (2.0 * over < -knee) grDb = 0.0;
        else if (2.0 * std::fabs(over) <= knee) grDb = slope * (over + knee / 2.0) * (over + knee / 2.0) / (2.0 * knee);
        else grDb = slope * over;
        if (model == 3) {  // how long it has been working: a slow average of the reduction
            const float target = float(std::clamp(-grDb / 8.0, 0.0, 1.0));
            engage_ += (target - engage_) * float(1.0 - std::exp(-1.0 / ((target > engage_ ? 1.5 : 4.0) * fs_)));
        }
        gainDb_ = float(grDb);
        // Make-up: half of what a note at the reference level (-20 dBV) loses, so the loudness is about the same at any Sustain.
        const double refOver = -20.0 - thresholdDb;
        const double refGr = refOver > 0.0 ? -slope * refOver : 0.0;
        const double makeupDb = 0.5 * refGr;
        const double levelDb = (lvl - 0.7) * 30.0;
        double wet = double(x) * std::pow(10.0, (grDb + makeupDb) / 20.0);
        if (model == 2) wet = std::tanh(wet * 1.2 / 1.0) / 1.2;  // the FET cell's soft saturation (volts: it only matters on loud notes)
        // Tone: a tilt around 1.5 kHz (the highs of the compressed path only).
        lp_ += a_ * (float(wet) - lp_);
        const double tilt = (tone - 0.5) * 1.0;
        wet += tilt * (wet - double(lp_));
        const double out = double(x) * (1.0 - blend) + wet * blend;
        return float(out * std::pow(10.0, levelDb / 20.0));
    }

    // The gain reduction now, in dB (negative), for tests and meters.
    float gainReductionDb() const { return gainDb_; }

private:
    double fs_ = 48000.0;
    float a_ = 0.18f, envDb_ = -120.0f, engage_ = 0.0f, lp_ = 0.0f, gainDb_ = 0.0f;
    CompControls c_;
};

}  // namespace sas
