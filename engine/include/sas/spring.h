#pragma once
// Behavioural model of a Fender spring reverb tank. Not a circuit: the two transducers and the
// springs are mechanical. Each spring is a feedback loop of a delay, a chain of all-pass sections
// (the dispersion that makes the "drip": lows travel slower than highs) and a loss filter; the
// echoes pass the chain on every trip and so smear more each time. The tank runs at a low rate of
// its own (the springs carry nothing above a few kilohertz).

#include <vector>

#include "sas/amp_def.h"

namespace sas {

class SpringTank {
public:
    SpringTank(const ReverbDef& def, double fs);

    // One audio sample: volts at the input transducer in, volts from the output transducer out.
    double process(double vin) {
        // Input transducer: band-limit, then average down to the tank rate.
        hp_ += hpA_ * (vin - hp_);
        const double x = vin - hp_;
        lpIn1_ += lpInA_ * (x - lpIn1_);
        lpIn2_ += lpInA_ * (lpIn1_ - lpIn2_);
        acc_ += lpIn2_;
        if (++count_ >= decim_) {
            prevOut_ = lastOut_;
            lastOut_ = stepTank(acc_ / double(decim_));
            acc_ = 0.0;
            count_ = 0;
        }
        // Linear interpolation up to the audio rate, then the output transducer.
        const double t = double(count_) / double(decim_);
        const double y = prevOut_ + (lastOut_ - prevOut_) * t;
        lpOut_ += lpOutA_ * (y - lpOut_);
        return lpOut_ * gain_;
    }
    void reset();

private:
    struct AllPass {
        double a = 0.0, s = 0.0;
        double tick(double x) {
            const double y = a * x + s;
            s = x - a * y;
            return y;
        }
    };
    struct Spring {
        std::vector<AllPass> chain;
        std::vector<double> line;
        std::size_t pos = 0;
        double loss = 0.0, lossA = 0.0, gain = 0.0;
    };

    double stepTank(double x);

    std::vector<Spring> springs_;
    std::size_t decim_ = 1, count_ = 0;
    double gain_;
    double hp_ = 0, hpA_, lpIn1_ = 0, lpIn2_ = 0, lpInA_, lpOut_ = 0, lpOutA_;
    double acc_ = 0, prevOut_ = 0, lastOut_ = 0;
};

}  // namespace sas
