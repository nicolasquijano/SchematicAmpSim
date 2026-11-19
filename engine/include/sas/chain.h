#pragma once
// The signal chain of the plugin without any framework: input gain -> [TS808] -> oversampled amp
// -> cab filter, plus the output gain stage. Port of the processing loop in jcm800/src/lib.rs.
// The cabinet impulse response (a convolution) is applied by the host plugin between
// processAmp() and applyOutput().

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include "sas/graphic_eq.h"
#include "sas/amp.h"
#include "sas/cab.h"
#include "sas/compressor.h"
#include "sas/keeley_comp.h"
#include "sas/nam_model.h"
#include "sas/nam_tone.h"
#include "sas/noise.h"
#include "sas/speaker.h"
#include "sas/oversample.h"
#include "sas/ts808_fast.h"

namespace sas {

// Quality order is part of saved-project compatibility: new qualities are only ever appended.
// Efficient: the preamp at 2x and the power amp at the host rate (SplitAmp). Cheaper than Normal and, unlike a plain 1x amp, it does not choke
// on a hot input (the preamp at the host rate stops converging; docs/PLAN_CPU.md).
enum class Quality { Normal = 0, High = 1, Lite = 2, Eco = 3, Efficient = 4 };

// Knob values are normalised 0..1 here (the plugin shows 0..10).
struct ChainSettings {
    std::vector<double> knobs;       // in AmpDef::preamp.knobs order
    std::vector<double> powerKnobs;  // in AmpDef::power.knobs order
    double presence = 0.5;
    double inputDb = 0.0, outputDb = 0.0;
    Quality quality = Quality::Normal;
    std::size_t jack = 0;
    std::vector<std::size_t> selectors;  // one position per selector of the amp (amp.json "selectors")
    std::array<double, 8> geq{0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5};  // the amp's graphic equaliser sliders, 0..1 (0.5 flat)
    bool jackLevelMatch = true;  // apply the jack's `level_db` at the amp input
    bool vibratoOn = false;      // optical tremolo of amps that have one
    double vibratoSpeed = 0.5;
    bool cabFilter = true;
    // False when the cabinet module of the rack applies the filter after the chain (docs/MODULAR_RACK.md): the chain then leaves it out.
    bool cabFilterInChain = true;
    // A buffering pedal (compressor, overdrive) that the rack runs outside the chain sits in front of the amp: it loads the input like the chain's own.
    bool pedalBefore = false;
    bool tsOn = false;
    bool compOn = false;   // the compressor pedal, in front of everything else
    CompControls comp;
    int pickup = 0;        // guitar impedance: 0 off (plain 7k), 1 single coil, 2 humbucker, 3 P-90
    int cableMeters = 4;   // cable capacitance, 0..14 m in steps of 2 (only with a pickup selected)
    double tsDrive = 0.5, tsTone = 0.5, tsLevel = 0.5;
    double speaker = 0.0;        // speaker dynamics amount (0 = off)
    bool speakerIntense = false;
    double noise = 0.0;    // hiss and hum level: 0 off, 1 a typical amp
    double bias = 1.0;     // output-tube bias supply, 1 = as drawn (amps with a bias control; full circuits only)
    double sag = 1.0;      // the supply's softness: 1 = as drawn, 3 = three times the rectifier's resistance (in the Lite modes, three times the sag)
    // The NAM loader amp (AmpDef::nam): the model to run (owned by the plugin, which keeps it alive until a block after it is replaced; null =
    // the amp passes the signal through), the drive into it and the level out of it in dB, and whether it is brought to a common loudness.
    NamModel* nam = nullptr;
    double namDriveDb = 0.0, namLevelDb = 0.0;
    double namBass = 0.5, namMid = 0.5, namTreble = 0.5;  // tone after the model (or before it, see namPost), 0..1 with 0.5 flat (+-12 dB)
    double namBassHz = NamTone::kBassHz, namMidHz = NamTone::kMidHz, namMidQ = NamTone::kMidQ, namTrebleHz = NamTone::kTrebleHz;  // where the three bands act
    double namPresence = 0.5, namDepth = 0.5;  // high shelf and low bell after the model, 0..1 with 0.5 flat (+-12 dB)
    bool namPost = true;                       // the three tone bands after the model (false: in front of it, after the drive)
    bool namNormalize = true;
};

// Linear ramp to a target over a fixed time; advance one sample or a whole control period.
class Smoother {
public:
    void reset(double value, double fs, double ms) { cur_ = target_ = value; steps_ = std::max(1.0, ms * 0.001 * fs); remaining_ = 0; }
    void setTarget(double t) {
        if (t == target_) return;
        target_ = t;
        remaining_ = steps_;
        inc_ = (t - cur_) / steps_;
    }
    double next() { return nextStep(1); }
    double nextStep(double n) {
        if (remaining_ > 0) {
            const double k = std::min(n, remaining_);
            cur_ += inc_ * k;
            remaining_ -= k;
            if (remaining_ <= 0) cur_ = target_;
        }
        return cur_;
    }

private:
    double cur_ = 0, target_ = 0, inc_ = 0, steps_ = 1, remaining_ = 0;
};

class Chain {
public:
    // Builds all circuits for sample rate `fs` (expensive: solves DC points and settles the supply).
    Chain(const AmpDef& def, double fs);

    void setSettings(const ChainSettings& s);
    // Like setSettings, but the knobs start at these values instead of ramping from their defaults
    // (for a chain that is about to replace a running one).
    void snap(const ChainSettings& s);

    // Round-trip latency in host samples for the active quality.
    double latencySamples() const;
    Quality activeQuality() const { return active_; }
    // Average plate current of one output tube in mA (0 for amps without a bias control, and in the Lite / Eco modes).
    double idleMilliamps() const;
    void reset();

    // The circuit page's meters, since the last call, each as the largest swing divided by its full scale (1 = at the clip level): the guitar at the
    // input, then the preamp's probes (amp.json "probes"; the preamp's output when there are none), then the speaker. Reads and clears. Returns how many
    // were written (at most `max`); 0 for the loader amp, which has no circuits. Call it from the thread that runs processAmp.
    std::size_t takeMeters(float* out, std::size_t max);
    static constexpr double kInputFullScaleVolts = 1.0;  // a hard-picked guitar is about this at the amp's input
    static constexpr double kSpeakerFullScaleVolts = 60.0;

    // `mono` is the host input already averaged to one channel; writes speaker volts / 100 (and
    // the cab filter, when on) to `out`. Safe for in-place use.
    void processAmp(const float* mono, float* out, std::size_t n);
    // Output gain and NaN guard.
    void applyOutput(float* buf, std::size_t n);
    // The same, for a stereo pair (one gain ramp for both channels).
    void applyOutputStereo(float* l, float* r, std::size_t n);

    static constexpr double kOutputFullScaleVolts = 100.0;  // a clean 100 W swing is ~57 V peak
    static constexpr double kTsOutputOhms = 600.0;          // source resistance behind the pedal's buffer

private:
    static constexpr std::size_t kControlEvery = 32;
    void processNam(const float* mono, float* out, std::size_t n);

    AmpDef def_;
    double fs_;
    ChainSettings target_;
    std::unique_ptr<FullAmp> amp2_, amp4_;
    std::unique_ptr<SplitAmp> split_;  // Efficient: preamp at 2x, power amp at the host rate
    std::unique_ptr<LiteAmp> lite_, eco_;  // eco_: Lite power amp + triode tables
    TubeScreamerFast ts1_, ts2_, ts4_;
    Oversampler os2_{1}, os4_{2};
    CabFilter cab_;
    GraphicEq geq_;             // the amp's graphic equaliser (amp.json "graphic_eq"), at the output of the amp
    KeeleyComp comp_;           // the compressor pedal (the model of the Keeley circuit), at the host rate
    SpeakerDynamics speaker_;   // between the amp and the cabinet
    AmpNoise noise2_, noise4_;  // at the 2x and 4x rates
    float ampTrim_ = 1.0f;  // the amp's output_db as a gain
    Smoother namDriveDb_, namLevelDb_, namBass_, namMid_, namTreble_, namBassHz_, namMidHz_, namMidQ_, namTrebleHz_, namPresence_, namDepth_;
    NamTone namTone_, namFinish_;  // the three bands (before or after the model) and presence and depth (after it)
    bool namPost_ = true;
    Quality active_ = Quality::Normal;
    Preamp* activePre();
    double inPeak_ = 0.0, outPeak_ = 0.0;  // the largest guitar and speaker volts since takeMeters
    std::vector<Smoother> knobs_, powerKnobs_;
    // The gain at the amp's input that the jack (when the level match is on) and the selected selector positions ask for, in dB.
    double inputCompDb(std::size_t jack, bool levelMatch, const std::vector<std::size_t>& selectors) const;
    Smoother bias_, sag_, presence_, tsDrive_, tsTone_, tsLevel_, inputDb_, outputDb_, jackCompDb_;
    std::vector<double> knobScratch_, powerScratch_;
};

}  // namespace sas
