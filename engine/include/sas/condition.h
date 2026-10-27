#pragma once
// The physical state of one amp: which output tubes it wears, how worn the tubes and electrolytic
// capacitors are, and which individual unit it is (component tolerances). The condition is applied to
// the netlist text of an AmpDef before the circuits are built, so it costs nothing while playing.

#include "sas/amp_def.h"

namespace sas {

struct Condition {
    int tube = 0;  // index into AmpDef::power.tubes (0 = stock)
    int wear = 0;  // 0..10 tenths: 0 = new parts, 10 = an amp that is due for service
    int unit = 0;  // 0 = every part exactly as drawn; N > 0 = the Nth individual unit (seeded tolerances)

    bool operator==(const Condition&) const = default;
};

// Returns `def` with the condition applied. The Lite fit (def.lite) belongs to the stock amp and is left alone.
//  - tube: the model on every `P` line, and the bias supply scaled so the idle current is the same
//    fraction of the tube's plate-dissipation rating as the stock tube's;
//  - wear: output and preamp tubes lose emission (current), electrolytics (4.7 uF and up) and the supply
//    reservoir lose capacitance;
//  - unit: resistors +-5 %, capacitors +-10 % (electrolytics +-20 %), tubes +-5 % (output tubes +-8 %),
//    drawn from the unit number and the element name.
AmpDef applyCondition(const AmpDef& def, const Condition& c);

}  // namespace sas
