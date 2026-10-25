#include "sas/reverb.h"

#include <algorithm>
#include <cmath>

namespace sas {

Reverb::Reverb(const ReverbDef& def, double fs, double railVolts, bool useTables)
    : def_(def), net_(parseNetlist(def.netlistText, def.netlist)),
      decim_(std::max<std::size_t>(1, std::size_t(std::lround(fs / def.tankRateHz)))),
      solver_([&] {
          net_.circuit.setSourceDc(net_.source(def.railSource), railVolts);
          return Solver(std::move(net_.circuit), fs / double(decim_), useTables);
      }()),
      tank_(def, fs / double(decim_)), rail_(net_.source(def.railSource)), grid_(net_.source(def.gridSource)),
      tankOut_(net_.source(def.tankOutSource)), tankIn_(net_.node(def.tankInNode)), out_(net_.node(def.outNode)),
      dc_(0.0), dcA_(1.0 - std::exp(-2.0 * 3.14159265358979323846 * 1.0 / (fs / double(decim_)))) {
    solver_.tolerance = 1e-4;
    solver_.refresh();
    dc_ = solver_.voltage(out_);  // the plate DC point: only the swing around it is passed on
}

}  // namespace sas
