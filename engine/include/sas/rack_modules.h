#pragma once
// The rack's modules: thin wrappers over the existing effects (docs/MODULAR_RACK.md, section 3.4). They add the Module interface and the rules
// the plugin used to apply by hand in processBlock (when an effect is skipped, when a model is reset, the ramps of a gain).

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

#include "sas/chain.h"
#include "sas/acoustic.h"
#include "sas/klon.h"
#include "sas/leslie.h"
#include "sas/spring_reverb.h"
#include "sas/boost.h"
#include "sas/chorus.h"
#include "sas/limiter.h"
#include "sas/tape.h"
#include "sas/bbd.h"
#include "sas/bitcrusher.h"
#include "sas/layers.h"
#include "sas/ten_band_eq.h"
#include "sas/octaver.h"
#include "sas/harmonist.h"
#include "sas/fuzz.h"
#include "sas/flanger.h"
#include "sas/tremolo.h"
#include "sas/wah.h"
#include "sas/compressor.h"
#include "sas/comp_models.h"
#include "sas/delay.h"
#include "sas/drive_circuit.h"
#include "sas/eq.h"
#include "sas/gate.h"
#include "sas/nam_model.h"
#include "sas/oversample.h"
#include "sas/phaser.h"
#include "sas/pitch_shifter.h"
#include "sas/plate.h"
#include "sas/rack.h"
#include "sas/ts808_fast.h"
#include "sas/vibe.h"

namespace sas {

// The effects that already take (l, r, n) and know whether they are active: one template.
template <class Dsp, class Params, Channels C>
class FxModule final : public Module {
public:
    void prepare(double fs, int) override { dsp_.prepare(fs); }
    void process(float* l, float* r, std::size_t n) override { dsp_.process(l, r, n); }
    bool active() const override { return dsp_.active(); }
    Channels channels() const override { return C; }
    void reset() override { dsp_.reset(); }
    void setParams(const Params& p) { dsp_.setParams(p); }
    Dsp& dsp() { return dsp_; }

private:
    Dsp dsp_;
};

// The same with a Mix control: the effect runs on the signal and the output is the dry signal plus `mix` of what the effect changed (mix 1 is the
// effect as it is, and costs nothing extra; the knob is ramped over the block so moving it does not click). For the effects that already mix a dry
// signal inside (phaser, chorus, flanger, vibe), this is how much of that comes out.
template <class Dsp, class Params, Channels C>
class MixFxModule final : public Module {
public:
    void prepare(double fs, int maxBlock) override {
        dsp_.prepare(fs);
        dryL_.assign(std::size_t(std::max(1, maxBlock)), 0.0f);
        dryR_.assign(std::size_t(std::max(1, maxBlock)), 0.0f);
    }
    void process(float* l, float* r, std::size_t n) override {
        const float target = float(std::clamp(mix_, 0.0, 1.0));
        if (target >= 1.0f && now_ >= 1.0f) {  // the effect as it is
            dsp_.process(l, r, n);
            return;
        }
        const std::size_t len = std::min(n, dryL_.size());
        std::copy(l, l + len, dryL_.begin());
        if (r) std::copy(r, r + len, dryR_.begin());
        dsp_.process(l, r, n);
        for (std::size_t i = 0; i < len; ++i) {
            const float m = now_ + (target - now_) * float(i + 1) / float(len);
            l[i] = dryL_[i] + (l[i] - dryL_[i]) * m;
            if (r) r[i] = dryR_[i] + (r[i] - dryR_[i]) * m;
        }
        now_ = target;
    }
    bool active() const override { return dsp_.active(); }
    Channels channels() const override { return C; }
    void reset() override { dsp_.reset(); }
    void setParams(const Params& p) {
        mix_ = p.mix;
        dsp_.setParams(p);
    }
    Dsp& dsp() { return dsp_; }

private:
    Dsp dsp_;
    double mix_ = 1.0;
    float now_ = 1.0f;  // the mix in force at the end of the last block
    std::vector<float> dryL_, dryR_;
};

using PhaserModule = MixFxModule<Phaser, PhaserParams, Channels::Flexible>;
using ChorusModule = MixFxModule<Chorus, ChorusParams, Channels::Flexible>;
using VibeModule = MixFxModule<Vibe, VibeParams, Channels::Flexible>;
using TremoloModule = FxModule<Tremolo, TremoloParams, Channels::Flexible>;  // (its depth already is the mix)
using FlangerModule = MixFxModule<Flanger, FlangerParams, Channels::Flexible>;
using WahModule = FxModule<Wah, WahParams, Channels::Flexible>;
using BoostModule = FxModule<Boost, BoostParams, Channels::Flexible>;
using TenBandEqModule = FxModule<TenBandEq, TenBandEqParams, Channels::Flexible>;
using OctaverModule = FxModule<Octaver, OctaverParams, Channels::MonoOnly>;  // (monophonic, like the pedal: a stereo pair is summed first)
using AcousticModule = MixFxModule<Acoustic, AcousticParams, Channels::Flexible>;
using LeslieModule = FxModule<Leslie, LeslieParams, Channels::MonoToStereo>;  // (it has its own Mix)
using SpringReverbModule = FxModule<SpringReverb, SpringReverbParams, Channels::Stereo>;  // (it has its own Mix)

// A limiter: it looks ahead, so it delays the signal by that time (reported as latency while it works).
class LimiterModule final : public Module {
public:
    void prepare(double fs, int) override { dsp_.prepare(fs); }
    void process(float* l, float* r, std::size_t n) override { dsp_.process(l, r, n); }
    bool active() const override { return dsp_.active(); }
    Channels channels() const override { return Channels::Flexible; }
    double latencySamples() const override { return dsp_.latencySamples(); }
    void reset() override { dsp_.reset(); }
    void setParams(const LimiterParams& p) { dsp_.setParams(p); }
    double reductionDb() const { return dsp_.reductionDb(); }

private:
    Limiter dsp_;
};
using EqModule = FxModule<ParametricEq, EqParams, Channels::Stereo>;
using DelayModule = FxModule<StereoDelay, DelayParams, Channels::Stereo>;
using BitcrusherModule = MixFxModule<Bitcrusher, BitcrusherParams, Channels::Flexible>;
using LayersModule = FxModule<Layers, LayersParams, Channels::Stereo>;
using BbdDelayModule = FxModule<BbdDelay, BbdParams, Channels::Stereo>;
using TapeEchoModule = FxModule<TapeEcho, TapeParams, Channels::Stereo>;
using ReverbModule = FxModule<PlateReverb, PlateParams, Channels::Stereo>;

// Noise gate: active while it is on or its gain is still recovering to 1. Mono (the detector listens to one channel).
class GateModule final : public Module {
public:
    void prepare(double fs, int) override { gate_.prepare(fs); }
    void process(float* l, float*, std::size_t n) override { gate_.process(l, n); }
    bool active() const override { return on_ || gate_.gain() < 0.9999; }
    Channels channels() const override { return Channels::MonoOnly; }
    void reset() override { gate_.reset(); }
    void setParams(bool on, double thresholdDb, double releaseMs) {
        on_ = on;
        gate_.setParams(on, thresholdDb, releaseMs);
    }
    double gain() const { return gate_.gain(); }

private:
    NoiseGate gate_;
    bool on_ = false;
};

// The compressor pedal circuit at the host rate. The circuit is only touched while it is on.
class CompressorModule final : public Module {
public:
    void prepare(double fs, int) override {
        comp_ = std::make_unique<Compressor>(fs);
        alt_.prepare(fs);
    }
    void process(float* l, float*, std::size_t n) override {
        if (controls_.model == 0) {
            for (std::size_t i = 0; i < n; ++i) l[i] = float(comp_->process(double(l[i])));
        } else {
            for (std::size_t i = 0; i < n; ++i) l[i] = alt_.process(l[i]);
        }
    }
    bool active() const override { return on_ && comp_; }
    bool buffered() const override { return true; }
    // Only the Keeley circuit is run by the amp (its chain has the circuit); the other models are a module of their own, like the overdrive circuits.
    Fusion fusion() const override { return controls_.model == 0 ? Fusion::Compressor : Fusion::None; }
    Channels channels() const override { return Channels::MonoOnly; }
    void reset() override { alt_.reset(); }
    const CompControls& controls() const { return controls_; }
    void setParams(bool on, const CompControls& c) {
        const bool modelChanged = c.model != controls_.model;
        controls_ = c;
        on_ = on;
        if (modelChanged) alt_.reset();  // a new model starts from rest
        if (on && comp_) {
            if (c.model == 0) comp_->setControls(c);
            else alt_.setControls(c);
        }
    }

private:
    std::unique_ptr<Compressor> comp_;
    PedalCompressor alt_;
    CompControls controls_;
    bool on_ = false;
};

// The Klon-style overdrive circuit (sas/klon.h): mono, in front of the amp, with its buffer (it loads the guitar like the other pedals) and the latency of its 2x
// oversampling. The circuit is only touched while it is on.
class KlonModule final : public Module {
public:
    void prepare(double fs, int) override { klon_ = std::make_unique<Klon>(fs); }
    void process(float* l, float*, std::size_t n) override { klon_->process(l, n); }
    bool active() const override { return on_ && klon_; }
    bool buffered() const override { return true; }
    Channels channels() const override { return Channels::MonoOnly; }
    double latencySamples() const override { return klon_ ? double(klon_->latencySamples()) : 0.0; }
    void reset() override {
        if (klon_) klon_->reset();
    }
    void setParams(bool on, const KlonControls& c) {
        on_ = on;
        if (on && klon_) klon_->setControls(c);
    }

private:
    std::unique_ptr<Klon> klon_;
    bool on_ = false;
};

// A fuzz pedal (sas/fuzz.h): mono, in front of the amp. Like the Tube Screamer it loads the guitar, and it has the latency of its oversampling.
class FuzzModule final : public Module {
public:
    void prepare(double fs, int) override { dsp_.prepare(fs); }
    void process(float* l, float*, std::size_t n) override { dsp_.process(l, n); }
    bool active() const override { return dsp_.active(); }
    bool buffered() const override { return true; }
    Channels channels() const override { return Channels::MonoOnly; }
    double latencySamples() const override { return dsp_.latencySamples(); }
    void reset() override { dsp_.reset(); }
    void setParams(const FuzzParams& p) { dsp_.setParams(p); }

private:
    Fuzz dsp_;
};

// The overdrive pedal, at twice the host rate (the one the amp runs inside its oversampled loop is not this one): a Tube Screamer (the analytic model; fused into the amp
// when it sits right before it), or the circuits of an SD-1 style, a BD-2 style and a RAT style pedal (sas/drive_circuit.h; those run as a module of their own). All three take drive,
// tone and level.
class OverdriveModule final : public Module {
public:
    void prepare(double fs, int) override {
        fs_ = fs;
        ts_ = std::make_unique<TubeScreamerFast>(fs * 2.0);
        sd1_ = std::make_unique<DriveCircuit>(DriveModel::SuperOverdrive, fs);
        bd2_ = std::make_unique<DriveCircuit>(DriveModel::BluesDriver, fs);
        rat_ = std::make_unique<DriveCircuit>(DriveModel::Rat, fs);
        os_.reset();
        drive_.reset(target_.drive, fs, 40.0);
        tone_.reset(target_.tone, fs, 40.0);
        level_.reset(target_.level, fs, 40.0);
    }
    void process(float* l, float*, std::size_t n) override {
        constexpr std::size_t kControlEvery = 32;
        DriveCircuit* circuit = model_ == DriveModel::SuperOverdrive ? sd1_.get() : model_ == DriveModel::BluesDriver ? bd2_.get() : model_ == DriveModel::Rat ? rat_.get() : nullptr;
        for (std::size_t i = 0; i < n; i += kControlEvery) {
            const std::size_t m = std::min(kControlEvery, n - i);
            const double steps = double(kControlEvery);
            const TsControls c{std::clamp(drive_.nextStep(steps), 0.0, 1.0), std::clamp(tone_.nextStep(steps), 0.0, 1.0), std::clamp(level_.nextStep(steps), 0.0, 1.0)};
            if (circuit) {
                circuit->setControls(c);
                circuit->process(l + i, m);
            } else {
                ts_->setControls(c);
                for (std::size_t j = 0; j < m; ++j) l[i + j] = os_.process(l[i + j], [&](float s) { return float(ts_->process(double(s))); });
            }
        }
    }
    bool active() const override { return on_ && ts_; }
    bool buffered() const override { return true; }
    // Only the Tube Screamer is run by the amp (its analytic model is in the chain's front end); the circuits are modules of their own.
    Fusion fusion() const override { return model_ == DriveModel::TubeScreamer ? Fusion::Overdrive : Fusion::None; }
    const TsControls& controls() const { return target_; }
    DriveModel model() const { return model_; }
    Channels channels() const override { return Channels::MonoOnly; }
    double latencySamples() const override { return double(os_.latencySamples()); }
    void reset() override {
        os_.reset();
        if (sd1_) sd1_->reset();
        if (bd2_) bd2_->reset();
        if (rat_) rat_->reset();
    }
    // The pedal model (a change starts the new one from rest).
    void setModel(DriveModel m) {
        if (m == model_) return;
        model_ = m;
        reset();
    }
    void setParams(bool on, const TsControls& c) {
        on_ = on;
        target_ = c;
        drive_.setTarget(c.drive);
        tone_.setTarget(c.tone);
        level_.setTarget(c.level);
    }
    // Jumps the controls to their targets (a module that is about to replace a running one).
    void snapControls() {
        drive_.reset(target_.drive, fs_, 40.0);
        tone_.reset(target_.tone, fs_, 40.0);
        level_.reset(target_.level, fs_, 40.0);
    }

private:
    double fs_ = 48000.0;
    std::unique_ptr<TubeScreamerFast> ts_;
    std::unique_ptr<DriveCircuit> sd1_, bd2_, rat_;
    DriveModel model_ = DriveModel::TubeScreamer;
    Oversampler os_{1};
    Smoother drive_, tone_, level_;
    TsControls target_;
    bool on_ = false;
};

// The distortion pedals of the Distortion module (the circuits of sas/drive_circuit.h from DriveModel::Ds1 on), mono, in front of the amp. Like the overdrive circuits they take drive,
// tone and level, buffer the guitar and run at their own oversampled rate; the amp does not run them.
class DistortionModule final : public Module {
public:
    void prepare(double fs, int) override {
        fs_ = fs;
        for (int i = 0; i < kDistortionModelCount; ++i) circuit_[std::size_t(i)] = std::make_unique<DriveCircuit>(DriveModel(int(DriveModel::Ds1) + i), fs);
        drive_.reset(target_.drive, fs, 40.0);
        tone_.reset(target_.tone, fs, 40.0);
        level_.reset(target_.level, fs, 40.0);
    }
    void process(float* l, float*, std::size_t n) override {
        constexpr std::size_t kControlEvery = 32;
        DriveCircuit* c = circuit_[std::size_t(model_)].get();
        if (!c) return;
        for (std::size_t i = 0; i < n; i += kControlEvery) {
            const std::size_t m = std::min(kControlEvery, n - i);
            const double steps = double(kControlEvery);
            c->setControls({std::clamp(drive_.nextStep(steps), 0.0, 1.0), std::clamp(tone_.nextStep(steps), 0.0, 1.0), std::clamp(level_.nextStep(steps), 0.0, 1.0)});
            c->process(l + i, m);
        }
    }
    bool active() const override { return on_ && circuit_[std::size_t(model_)]; }
    bool buffered() const override { return true; }
    Channels channels() const override { return Channels::MonoOnly; }
    double latencySamples() const override { return circuit_[std::size_t(model_)] ? double(circuit_[std::size_t(model_)]->latencySamples()) : 0.0; }
    void reset() override {
        for (auto& c : circuit_)
            if (c) c->reset();
    }
    // The pedal model (a change starts the new one from rest).
    void setModel(int m) {
        m = std::clamp(m, 0, kDistortionModelCount - 1);
        if (m == model_) return;
        model_ = m;
        reset();
    }
    void setParams(bool on, const TsControls& c) {
        on_ = on;
        target_ = c;
        drive_.setTarget(c.drive);
        tone_.setTarget(c.tone);
        level_.setTarget(c.level);
    }

private:
    double fs_ = 48000.0;
    std::array<std::unique_ptr<DriveCircuit>, kDistortionModelCount> circuit_;
    int model_ = 0;
    Smoother drive_, tone_, level_;
    TsControls target_;
    bool on_ = false;
};

// A Neural Amp Modeler model of a pedal. The model is owned by the plugin; the module only borrows it (a plain pointer set between blocks).
class NamPedalModule final : public Module {
public:
    void prepare(double, int maxBlock) override {
        in_.assign(std::size_t(maxBlock), 0.0f);
        out_.assign(std::size_t(maxBlock), 0.0f);
    }
    void process(float* l, float*, std::size_t n) override;
    bool active() const override { return on_ && model_.load() != nullptr; }
    Channels channels() const override { return Channels::MonoOnly; }
    void reset() override {
        if (auto* m = model_.load()) m->reset();
    }
    void setModel(NamModel* model) { model_.store(model); }
    // Drive into the model and level out of it, in dB. The model is reset when the pedal is switched on.
    void setParams(bool on, double driveDb, double levelDb);

private:
    std::atomic<NamModel*> model_{nullptr};
    std::vector<float> in_, out_;
    float driveNow_ = 1.0f, levelNow_ = 1.0f, driveTarget_ = 1.0f, levelTarget_ = 1.0f;
    bool on_ = false, wasActive_ = false;
};

// Pitch shifter: adds its delay only while it shifts, and clears its memory when it starts.
class PitchModule final : public Module {
public:
    void prepare(double fs, int) override { pitch_.prepare(fs); }
    void process(float* l, float*, std::size_t n) override { pitch_.process(l, l, n, ratio_); }
    bool active() const override { return active_; }
    Channels channels() const override { return Channels::MonoOnly; }
    double latencySamples() const override { return active_ ? pitch_.latencySamples() : 0.0; }
    void reset() override { pitch_.reset(); }
    void setSemitones(double semitones) {
        const bool now = std::fabs(semitones) > 1e-6;
        if (now && !active_) pitch_.reset();
        active_ = now;
        ratio_ = PitchShifter::ratioFromSemitones(semitones);
    }

private:
    PitchShifter pitch_;
    double ratio_ = 1.0;
    bool active_ = false;
};

// The harmonizer (sas/harmonist.h): it delays the guitar to line it up with its voices, and adds that delay only while it is on.
class HarmonistModule final : public Module {
public:
    void prepare(double fs, int) override { dsp_.prepare(fs); }
    void process(float* l, float*, std::size_t n) override { dsp_.process(l, nullptr, n); }
    bool active() const override { return dsp_.active(); }
    Channels channels() const override { return Channels::MonoOnly; }
    double latencySamples() const override { return dsp_.active() ? dsp_.latencySamples() : 0.0; }
    void reset() override { dsp_.reset(); }
    void setParams(const HarmonistParams& p) { dsp_.setParams(p); }

private:
    Harmonist dsp_;
};

// The two ends of a parallel: the rack itself runs the lanes between them. The splitter has nothing to say; the mixer keeps the levels, the pans and
// the phase of the two lanes.
class SplitterModule final : public Module {
public:
    void prepare(double, int) override {}
    void process(float*, float*, std::size_t) override {}
    bool active() const override { return false; }
    Channels channels() const override { return Channels::Flexible; }
    void reset() override {}
};

class MixerModule final : public Module {
public:
    void prepare(double, int) override {}
    void process(float*, float*, std::size_t) override {}
    bool active() const override { return false; }
    Channels channels() const override { return Channels::Flexible; }
    void reset() override {}
    void setParams(const MixerParams& p) { params_ = p; }
    const MixerParams* mixerParams() const override { return &params_; }

private:
    MixerParams params_;
};

// The amp: runs the Chain (the circuits, the speaker and, unless the cabinet module takes it, the cabinet filter). The chain is owned by the
// plugin, which builds a new one off the audio thread when the amp or its condition changes and swaps it in with setChain().
class AmpModule final : public Module {
public:
    void prepare(double fs, int maxBlock) override {
        step_ = float(1.0 / (0.008 * fs));  // 8 ms to fade the amp in or out
        dry_.assign(std::size_t(std::max(1, maxBlock)), 0.0f);
    }
    void process(float* l, float*, std::size_t n) override;
    // The amp can be switched off: the rack then skips it (and the pedals next to it run on their own), after a short fade to the dry signal.
    bool active() const override { return chain_.load() != nullptr && (on_.load() || gain_.load() > 0.0f); }
    void setOn(bool on) { on_.store(on); }
    Channels channels() const override { return Channels::MonoOnly; }
    double latencySamples() const override {
        const Chain* c = chain_.load();
        return c ? c->latencySamples() : 0.0;
    }
    void reset() override {
        if (Chain* c = chain_.load()) c->reset();
    }
    bool isAmp() const override { return true; }
    void setFrontEnd(Module* compressor, Module* overdrive, bool pedalBefore) override {
        comp_ = compressor;
        ts_ = overdrive;
        pedalBefore_ = pedalBefore;
    }
    void setChain(Chain* chain) { chain_.store(chain); }
    Chain* chain() const { return chain_.load(); }
    // The settings of the block (audio thread); the front end's pedals are added on top of them.
    void setSettings(const ChainSettings& s) { base_ = s; }
    // Starts a new chain at the current settings (the chain about to replace a running one).
    void snapChain(Chain& chain);

private:
    void compose();
    std::atomic<Chain*> chain_{nullptr};
    std::atomic<bool> on_{true};
    std::atomic<float> gain_{1.0f};  // how much of the amp is in the signal: 1 on, 0 off, in between while it fades
    float step_ = 0.005f;
    std::vector<float> dry_;
    ChainSettings base_, work_;
    Module *comp_ = nullptr, *ts_ = nullptr;
    bool pedalBefore_ = false;
};

}  // namespace sas
