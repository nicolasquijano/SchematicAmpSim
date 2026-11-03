#include "sas/rack_modules.h"

#include <algorithm>
#include <cmath>

namespace sas {

void NamPedalModule::process(float* l, float*, std::size_t n) {
    NamModel* model = model_.load();
    if (!model) return;
    // The model reads its input while it writes, so it goes through two scratch blocks; the gains ramp over the block.
    for (std::size_t at = 0; at < n; at += in_.size()) {
        const std::size_t m = std::min(in_.size(), n - at);
        for (std::size_t i = 0; i < m; ++i) in_[i] = l[at + i] * (driveNow_ + (driveTarget_ - driveNow_) * (float(i + 1) / float(m)));
        model->process(in_.data(), out_.data(), m);
        for (std::size_t i = 0; i < m; ++i) l[at + i] = out_[i] * (levelNow_ + (levelTarget_ - levelNow_) * (float(i + 1) / float(m)));
        driveNow_ = driveTarget_;
        levelNow_ = levelTarget_;
    }
}

void NamPedalModule::setParams(bool on, double driveDb, double levelDb) {
    on_ = on;
    driveTarget_ = std::pow(10.0f, float(driveDb) / 20.0f);
    levelTarget_ = std::pow(10.0f, float(levelDb) / 20.0f);
    const bool now = active();
    if (now && !wasActive_)
        if (auto* m = model_.load()) m->reset();
    wasActive_ = now;
}

void AmpModule::compose() {
    work_ = base_;
    if (comp_ && comp_->active()) {
        work_.compOn = true;
        work_.comp = static_cast<CompressorModule*>(comp_)->controls();
    }
    if (ts_ && ts_->active()) {
        const TsControls& t = static_cast<OverdriveModule*>(ts_)->controls();
        work_.tsOn = true;
        work_.tsDrive = t.drive;
        work_.tsTone = t.tone;
        work_.tsLevel = t.level;
    }
    work_.pedalBefore = pedalBefore_;
}

void AmpModule::process(float* l, float*, std::size_t n) {
    Chain* c = chain_.load();
    if (!c) return;
    compose();
    c->setSettings(work_);
    c->processAmp(l, l, n);
}

void AmpModule::snapChain(Chain& chain) {
    compose();
    chain.snap(work_);
}

}  // namespace sas
