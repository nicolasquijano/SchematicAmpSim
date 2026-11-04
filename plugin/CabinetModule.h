#pragma once
// The cabinet as a rack module: the cabinet filter, the impulse responses A and B (with their blend and width), the NAM cabinet and the automatic
// double tracking. The processing itself lives in SasProcessor::processCabinet (it owns the convolutions and the models); this is the part the
// rack sees: it takes a mono signal and leaves a stereo pair.

#include "sas/cab.h"
#include "sas/rack.h"

class SasProcessor;

class CabinetModule final : public sas::Module {
public:
    CabinetModule(SasProcessor& owner, int instance) : owner_(owner), instance_(instance) {}
    int instance() const { return instance_; }

    void prepare(double fs, int) override { filter_ = sas::CabFilter(float(fs)); }
    void process(float* l, float* r, std::size_t n) override;
    bool active() const override { return on_; }  // off: the amp's raw signal goes on
    sas::Channels channels() const override { return sas::Channels::MonoToStereo; }
    double latencySamples() const override { return latency_; }
    void reset() override { filter_.reset(); }

    // Per block, from the processor: what the mode is and how late the convolution is.
    void setBlock(bool filterOn, double latencySamples, bool on = true) {
        on_ = on;
        filterOn_ = filterOn;
        latency_ = latencySamples;
    }
    bool filterOn() const { return filterOn_; }
    sas::CabFilter& filter() { return filter_; }

private:
    SasProcessor& owner_;
    int instance_ = 0;
    sas::CabFilter filter_{48000.0f};
    bool filterOn_ = true, on_ = true;
    double latency_ = 0.0;
};
