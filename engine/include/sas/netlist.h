#pragma once
// Text netlist -> Circuit. One element or directive per line:
//
//   * comment            ; trailing comment
//   .param name = expr   named value usable in later expressions
//   .nodes n1 n2 ...     creates nodes in this order (node numbering is part of an amp's contract)
//   R name a b value     resistor            C name a b value   capacitor
//   T name g p k MODEL [scale]   triode (+ its interelectrode caps); `scale` identical triodes in parallel
//   P name g1 p g2 k MODEL scale   pentode, `scale` identical tubes in parallel
//   D name anode cathode MODEL     diode    Q name b c e MODEL   NPN (forward-active)
//   O name plus minus out vref MODEL        op-amp biased around vref
//   G name into cp cn gm           voltage-controlled current source
//   V name node ohms volts         DC supply behind `ohms`
//   S name node ohms dc            source driven at run time (dc = its value for the DC solve)
//   I name node ohms               the signal input (a source driven by Solver::process)
//   .xfmr name L=expr / W a b turns group / K group group coupling / .end
//                         coupled windings; L_ij = L * turns_i * turns_j * k(group_i, group_j)
//
// Node "0" or "gnd" is ground. A value is an expression without spaces: numbers with an SI suffix
// (k M G m u n p f; M is mega, m is milli; the schematic form 4k7 = 4.7k, 1M0 = 1M also works), parameters, + - * / ( ) and sqrt exp ln abs.
// MODEL is a built-in name: 12AX7, 12AT7, 6550DS (data sheet fits), ECC83, ECC81, EL34, 6550 (stock Koren), 6L6GC, SIGNAL, RECTIFIER_BRIDGE, C1815, RC4558.

#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

#include "sas/circuit.h"

namespace sas {

struct NetlistError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ParsedNetlist {
    Circuit circuit;
    std::map<std::string, Node> nodes;
    std::map<std::string, ResId> resistors;
    std::map<std::string, SrcId> sources;  // V, S and I elements

    Node node(const std::string& name) const;
    ResId resistor(const std::string& name) const;
    SrcId source(const std::string& name) const;
};

// `origin` names the file in error messages ("preamp.net:12: ...").
ParsedNetlist parseNetlist(std::string_view text, const std::string& origin = "netlist");

// Evaluates one expression (exposed for tests and for amp.json numbers written as text).
double evalExpression(std::string_view expr, const std::map<std::string, double>& params = {});

}  // namespace sas
