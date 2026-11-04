#pragma once
// The rack's modules: thin wrappers over the existing effects (docs/MODULAR_RACK.md, section 3.4). They add the Module interface and the rules
// the plugin used to apply by hand in processBlock (when an effect is skipped, when a model is reset, the ramps of a gain).

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

#include "sas/chain.h"
#include "sas/chorus.h"
#include "sas/compressor.h"
#include "sas/delay.h"
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

using PhaserModule = FxModule<Phaser, PhaserParams, Channels::Flexible>;
using ChorusModule = FxModule<Chorus, ChorusParams, Channels::Flexible>;
using VibeModule = FxModule<Vibe, VibeParams, Channels::Flexible>;
using EqModule = FxModule<ParametricEq, EqParams, Channels::Stereo>;
using DelayModule = FxModule<StereoDelay, DelayParams, Channels::Stereo>;
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
    void prepare(double fs, int) override { comp_ = std::make_unique<Compressor>(fs); }
    void process(float* l, float*, std::size_t n) override {
        for (std::size_t i = 0; i < n; ++i) l[i] = float(comp_->process(double(l[i])));
    }
    bool active() const override { return on_ && comp_; }
    bool buffered() const override { return true; }
    Fusion fusion() const override { return Fusion::Compressor; }
    Channels channels() const override { return Channels::MonoOnly; }
    void reset() override {}
    const CompControls& controls() const { return controls_; }
    void setParams(bool on, const CompControls& c) {
        controls_ = c;
        on_ = on;
        if (on && comp_) comp_->setControls(c);
    }

private:
    std::unique_ptr<Compressor> comp_;
    CompControls controls_;
    bool on_ = false;
};

// A Tube Screamer of its own, at twice the host rate (the one the amp runs inside its oversampled loop is not this one).
class OverdriveModule final : public Module {
public:
    void prepare(double fs, int) override {
        fs_ = fs;
        ts_ = std::make_unique<TubeScreamerFast>(fs * 2.0);
        os_.reset();
        drive_.reset(target_.drive, fs, 40.0);
        tone_.reset(target_.tone, fs, 40.0);
        level_.reset(target_.level, fs, 40.0);
    }
    void process(float* l, float*, std::size_t n) override {
        constexpr std::size_t kControlEvery = 32;
        for (std::size_t i = 0; i < n; ++i) {
            if (i % kControlEvery == 0) {
                const double steps = double(kControlEvery);
                ts_->setControls({std::clamp(drive_.nextStep(steps), 0.0, 1.0), std::clamp(tone_.nextStep(steps), 0.0, 1.0),
                                  std::clamp(level_.nextStep(steps), 0.0, 1.0)});
            }
            l[i] = os_.process(l[i], [&](float s) { return float(ts_->process(double(s))); });
        }
    }
    bool active() const override { return on_ && ts_; }
    bool buffered() const override { return true; }
    Fusion fusion() const override { return Fusion::Overdrive; }
    const TsControls& controls() const { return target_; }
    Channels channels() const override { return Channels::MonoOnly; }
    double latencySamples() const override { return double(os_.latencySamples()); }
    void reset() override { os_.reset(); }
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
    Oversampler os_{1};
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
    void prepare(double, int) override {}
    void process(float* l, float*, std::size_t n) override;
    bool active() const override { return chain_.load() != nullptr; }
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
    ChainSettings base_, work_;
    Module *comp_ = nullptr, *ts_ = nullptr;
    bool pedalBefore_ = false;
};

}  // namespace sas
