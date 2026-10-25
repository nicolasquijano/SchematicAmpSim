#pragma once
// The reverb of a Fender amp: driver tube + transformer and recovery tube as a small circuit of their
// own, with the spring tank (sas/spring.h) between them. See ReverbDef for how it is wired to the preamp.
// Everything here only has to carry what the springs do (a few kilohertz), so it runs at the tank's
// rate (~16 kHz), fed with the average of the audio samples and read back with linear interpolation.

#include <cstdint>

#include "sas/amp_def.h"
#include "sas/circuit.h"
#include "sas/netlist.h"
#include "sas/spring.h"

namespace sas {

class Reverb {
public:
    Reverb(const ReverbDef& def, double fs, double railVolts, bool useTables);

    void setRail(double volts) { solver_.setSource(rail_, volts); }
    // One audio sample: the preamp node voltage that feeds the driver grid in, the recovery tube's plate
    // signal (volts, DC removed) out.
    double process(double gridVolts) {
        acc_ += gridVolts;
        if (++count_ >= decim_) {
            solver_.setSource(grid_, acc_ / double(decim_));
            solver_.setSource(tankOut_, tank_.process(solver_.voltage(tankIn_)));
            solver_.step();
            const double v = solver_.voltage(out_);
            dc_ += dcA_ * (v - dc_);
            prev_ = last_;
            last_ = v - dc_;
            acc_ = 0.0;
            count_ = 0;
        }
        return prev_ + (last_ - prev_) * (double(count_) / double(decim_));
    }
    std::uint64_t newtonFailures() const { return solver_.newtonFailures; }

private:
    ReverbDef def_;
    ParsedNetlist net_;
    std::size_t decim_;
    Solver solver_;
    SpringTank tank_;
    SrcId rail_, grid_, tankOut_;
    Node tankIn_, out_;
    double dc_, dcA_;
    double acc_ = 0.0, prev_ = 0.0, last_ = 0.0;
    std::size_t count_ = 0;
};

}  // namespace sas
