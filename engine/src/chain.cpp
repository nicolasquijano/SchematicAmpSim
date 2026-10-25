#include "sas/chain.h"

#include <algorithm>
#include <cmath>

namespace sas {

namespace {
inline double dbToGain(double db) { return std::pow(10.0, db / 20.0); }
}  // namespace

Chain::Chain(const AmpDef& def, double fs)
    : def_(def), fs_(fs),
      amp2_(std::make_unique<FullAmp>(def, fs * 2.0)),
      amp4_(std::make_unique<FullAmp>(def, fs * 4.0)),
      lite_(std::make_unique<LiteAmp>(def, fs * 2.0)),
      eco_(std::make_unique<LiteAmp>(def, fs * 2.0, true)),
      ts2_(fs * 2.0), ts4_(fs * 4.0), cab_(float(fs)) {
    ampTrim_ = float(dbToGain(def.outputDb));
    target_.knobs.resize(def.preamp.knobs.size());
    knobs_.resize(def.preamp.knobs.size());
    knobScratch_.resize(def.preamp.knobs.size());
    for (std::size_t i = 0; i < knobs_.size(); ++i) {
        target_.knobs[i] = def.preamp.knobs[i].def;
        knobs_[i].reset(def.preamp.knobs[i].def, fs, 40.0);
    }
    target_.powerKnobs.resize(def.power.knobs.size());
    powerKnobs_.resize(def.power.knobs.size());
    powerScratch_.resize(def.power.knobs.size());
    for (std::size_t i = 0; i < powerKnobs_.size(); ++i) {
        target_.powerKnobs[i] = def.power.knobs[i].def;
        powerKnobs_[i].reset(def.power.knobs[i].def, fs, 40.0);
    }
    presence_.reset(def.power.presenceDefault, fs, 40.0);
    tsDrive_.reset(0.5, fs, 40.0);
    tsTone_.reset(0.5, fs, 40.0);
    tsLevel_.reset(0.5, fs, 40.0);
    inputDb_.reset(0.0, fs, 20.0);
    outputDb_.reset(0.0, fs, 20.0);
    jackCompDb_.reset(def.preamp.jacks.empty() ? 0.0 : def.preamp.jacks[0].levelDb, fs, 20.0);
}

void Chain::setSettings(const ChainSettings& s) {
    for (std::size_t i = 0; i < knobs_.size() && i < s.knobs.size(); ++i)
        knobs_[i].setTarget(std::clamp(s.knobs[i], 0.0, 1.0));
    for (std::size_t i = 0; i < powerKnobs_.size() && i < s.powerKnobs.size(); ++i)
        powerKnobs_[i].setTarget(std::clamp(s.powerKnobs[i], 0.0, 1.0));
    presence_.setTarget(std::clamp(s.presence, 0.0, 1.0));
    tsDrive_.setTarget(std::clamp(s.tsDrive, 0.0, 1.0));
    tsTone_.setTarget(std::clamp(s.tsTone, 0.0, 1.0));
    tsLevel_.setTarget(std::clamp(s.tsLevel, 0.0, 1.0));
    inputDb_.setTarget(s.inputDb);
    outputDb_.setTarget(s.outputDb);
    target_.quality = s.quality;
    target_.jack = std::min(s.jack, def_.preamp.jacks.size() - 1);
    target_.cabFilter = s.cabFilter;
    target_.tsOn = s.tsOn;
    jackCompDb_.setTarget(s.jackLevelMatch ? def_.preamp.jacks[target_.jack].levelDb : 0.0);
}

void Chain::snap(const ChainSettings& s) {
    for (std::size_t i = 0; i < knobs_.size() && i < s.knobs.size(); ++i) knobs_[i].reset(std::clamp(s.knobs[i], 0.0, 1.0), fs_, 40.0);
    for (std::size_t i = 0; i < powerKnobs_.size() && i < s.powerKnobs.size(); ++i)
        powerKnobs_[i].reset(std::clamp(s.powerKnobs[i], 0.0, 1.0), fs_, 40.0);
    presence_.reset(std::clamp(s.presence, 0.0, 1.0), fs_, 40.0);
    tsDrive_.reset(std::clamp(s.tsDrive, 0.0, 1.0), fs_, 40.0);
    tsTone_.reset(std::clamp(s.tsTone, 0.0, 1.0), fs_, 40.0);
    tsLevel_.reset(std::clamp(s.tsLevel, 0.0, 1.0), fs_, 40.0);
    inputDb_.reset(s.inputDb, fs_, 20.0);
    outputDb_.reset(s.outputDb, fs_, 20.0);
    jackCompDb_.reset(s.jackLevelMatch ? def_.preamp.jacks[std::min(s.jack, def_.preamp.jacks.size() - 1)].levelDb : 0.0, fs_, 20.0);
    setSettings(s);
}

double Chain::latencySamples() const {
    return std::ceil(target_.quality == Quality::High ? os4_.latencySamples() : os2_.latencySamples());
}

void Chain::reset() {
    os2_.reset();
    os4_.reset();
    cab_.reset();
}

void Chain::processAmp(const float* mono, float* out, std::size_t n) {
    if (target_.quality != active_) {
        active_ = target_.quality;
        os2_.reset();
        os4_.reset();
    }
    const Quality quality = active_;
    const bool tsOn = target_.tsOn;
    const bool cabOn = target_.cabFilter;
    const double sourceOhms = tsOn ? kTsOutputOhms : def_.preamp.sourceOhmsDefault;
    std::size_t sinceUpdate = kControlEvery;

    for (std::size_t i = 0; i < n; ++i) {
        if (sinceUpdate >= kControlEvery) {
            const double steps = double(sinceUpdate);
            for (std::size_t k = 0; k < knobs_.size(); ++k) knobScratch_[k] = std::clamp(knobs_[k].nextStep(steps), 0.0, 1.0);
            for (std::size_t k = 0; k < powerKnobs_.size(); ++k) powerScratch_[k] = std::clamp(powerKnobs_[k].nextStep(steps), 0.0, 1.0);
            const double presence = std::clamp(presence_.nextStep(steps), 0.0, 1.0);
            const TsControls ts{std::clamp(tsDrive_.nextStep(steps), 0.0, 1.0), std::clamp(tsTone_.nextStep(steps), 0.0, 1.0),
                                std::clamp(tsLevel_.nextStep(steps), 0.0, 1.0)};
            switch (quality) {
                case Quality::Normal:
                    amp2_->setKnobs(knobScratch_, presence, powerScratch_);
                    amp2_->setJack(target_.jack);
                    amp2_->setSourceOhms(sourceOhms);
                    ts2_.setControls(ts);
                    break;
                case Quality::High:
                    amp4_->setKnobs(knobScratch_, presence, powerScratch_);
                    amp4_->setJack(target_.jack);
                    amp4_->setSourceOhms(sourceOhms);
                    ts4_.setControls(ts);
                    break;
                case Quality::Lite:
                    lite_->setKnobs(knobScratch_, presence, powerScratch_);
                    lite_->setJack(target_.jack);
                    lite_->setSourceOhms(sourceOhms);
                    ts2_.setControls(ts);
                    break;
                case Quality::Eco:
                    eco_->setKnobs(knobScratch_, presence, powerScratch_);
                    eco_->setJack(target_.jack);
                    eco_->setSourceOhms(sourceOhms);
                    ts2_.setControls(ts);
                    break;
            }
            sinceUpdate = 0;
        }
        ++sinceUpdate;

        const float vin = float(double(mono[i]) * dbToGain(inputDb_.next()));
        // The pedal sits in front of the amp, inside the oversampled loop; it costs nothing while off.
        // `comp` is the jack's level match: extra gain at the amp's input (after the pedal), 1 when off.
        const double comp = dbToGain(jackCompDb_.next());
        float volts;
        switch (quality) {
            case Quality::Normal:
                volts = os2_.process(vin, [&](float s) {
                    return float(amp2_->process((tsOn ? ts2_.process(double(s)) : double(s)) * comp));
                });
                break;
            case Quality::High:
                volts = os4_.process(vin, [&](float s) {
                    return float(amp4_->process((tsOn ? ts4_.process(double(s)) : double(s)) * comp));
                });
                break;
            case Quality::Eco:
                volts = os2_.process(vin, [&](float s) {
                    return float(eco_->process((tsOn ? ts2_.process(double(s)) : double(s)) * comp));
                });
                break;
            default:  // Lite
                volts = os2_.process(vin, [&](float s) {
                    return float(lite_->process((tsOn ? ts2_.process(double(s)) : double(s)) * comp));
                });
                break;
        }
        float y = volts / float(kOutputFullScaleVolts) * ampTrim_;
        if (cabOn) y = cab_.process(y);
        out[i] = y;
    }
}

void Chain::applyOutput(float* buf, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        float y = buf[i] * float(dbToGain(outputDb_.next()));
        buf[i] = std::isfinite(y) ? y : 0.0f;
    }
}

}  // namespace sas
