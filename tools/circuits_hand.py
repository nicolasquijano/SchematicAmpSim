#!/usr/bin/env python3
"""The circuit page's schematics that follow an amp's real circuit instead of the generic signal path of gen_circuits.py.

    py tools/circuits_hand.py          writes amps/<id>/face/circuit.svg and circuit_power.svg of the amps below

Each drawing is the amp's own topology with the values of its netlist. The ids are what the plugin reads (plugin/CircuitPage.cpp):
wire_<STAGE>[-n] wires take the colour of the stage's level, glow_<STAGE> tubes glow with it, pot_<param> are the knobs (with ptr_<param> their
pointer), lbl_<param> where a knob's name and value go when not under it, next_power the arrow that leads to the power amp.
"""
import os

from gen_circuits import Svg, bulb, cap, ground, jack, pot, resistor_h, resistor_v, speaker, ROOT, WIRE


def cap_h(s, cx, cy, name):
    """A capacitor in a vertical wire: the plates are horizontal."""
    f = s.lin(0, cy - 6, 0, cy + 6, [(0, '#f0d27a'), (1, '#a67c2e')])
    s.rect(name + 'a', cx - 12, cy - 6, 24, 4, f, '#5a4630', 0.8, 1)
    s.rect(name + 'b', cx - 12, cy + 2, 24, 4, f, '#5a4630', 0.8, 1)


def anchor(s, param, x, y):
    s.ellipse('lbl_%s' % param, x, y, 1, 1, 'none', 'none', 0)


def ground_at(s, x, y):
    ground(s, x, y)


def tweed_5e3_preamp():
    s = Svg(840, 430)
    p = s.path
    p('rail', 'M150,36 H760', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    p('rail_bottom', 'M150,400 H260', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    p('wire_static_rail', 'M200,36 V60 M200,381 V400 M640,36 V60', '#7a6a4c', 3)
    # signal wires: the guitar, the plates of V1 and their way to the volume pots, the pots' output, V2's plate, the way out
    p('wire_INPUT', 'M43,150 H66 M102,150 H140 V208 H164 M43,290 H66 M102,290 H140 V232 H164')
    p('wire_V1', 'M200,96 V184 M200,130 H234 M246,130 H362 M300,130 V80 H334 M346,80 H420 V40 H520 V52 M200,345 V256 M200,330 H234 M246,330 H362')
    p('wire_V1-m', 'M380,112 V95 H470 M380,312 V290 H470 M470,70 V290 M470,215 H604 M470,70 H502')
    p('wire_V2', 'M640,96 V179 M640,150 H706')
    p('wire_OUT', 'M718,150 H800 M760,150 V190')
    p('wire_static', 'M236,220 H270 V235 M262,235 H290 M262,235 V245 M262,281 V296 H290 V266 M290,235 V254 M380,148 V165 M380,348 V362 M520,88 V104 M520,122 V138 '
      'M140,150 V144 M140,108 V96 M140,290 V300 M140,336 V346 M640,255 V265 M625,265 H655 M625,265 V275 M625,311 V325 H655 V295 M640,325 V330 M655,265 V283 M760,226 V240', '#5a4d3a', 3)
    for gx, gy in ((140, 96), (140, 346), (380, 165), (380, 362), (520, 138), (276, 296), (640, 330), (760, 240)):
        ground_at(s, gx, gy)
    # the parts
    s.ellipse('jack_BRIGHT', 30, 150, 13, 13, s.rad(30, 150, 13, [(0, '#d9d0b8'), (1, '#5f5846')]), '#9a8760', 1.5)
    s.ellipse('jack_BRIGHT_hole', 30, 150, 5, 5, '#14110d', '#3b3329', 1)
    s.ellipse('jack_NORMAL', 30, 290, 13, 13, s.rad(30, 290, 13, [(0, '#d9d0b8'), (1, '#5f5846')]), '#9a8760', 1.5)
    s.ellipse('jack_NORMAL_hole', 30, 290, 5, 5, '#14110d', '#3b3329', 1)
    resistor_h(s, 66, 150, 'res_34k_b')
    resistor_h(s, 66, 290, 'res_34k_n')
    resistor_v(s, 140, 108, 'res_1M_b')
    resistor_v(s, 140, 300, 'res_1M_n')
    resistor_v(s, 200, 60, 'res_100k_a')
    resistor_v(s, 200, 345, 'res_100k_b')
    resistor_v(s, 262, 245, 'res_820')
    resistor_v(s, 640, 60, 'res_100k_v2')
    resistor_v(s, 625, 275, 'res_1k5')
    resistor_v(s, 760, 190, 'res_1M_o')
    cap(s, 240, 130, 'cap_c1b')
    cap(s, 240, 330, 'cap_c1n')
    cap(s, 340, 80, 'cap_c500')
    cap(s, 712, 150, 'cap_cout')
    cap_h(s, 290, 260, 'cap_ck1')
    cap_h(s, 655, 289, 'cap_ck2')
    cap_h(s, 520, 113, 'cap_c5n')
    bulb(s, 200, 220, 1.125, 'V1', 'V1', pi=True, label='12AY7')
    bulb(s, 640, 215, 1.0, 'V2', 'V2', label='12AX7')
    pot(s, 380, 130, 'volume', 1.0, 0.5)
    pot(s, 380, 330, 'volume2', 1.0, 0.5)
    pot(s, 520, 70, 'treble', 1.0, 0.6)
    anchor(s, 'volume', 438, 130)
    anchor(s, 'volume2', 438, 330)
    anchor(s, 'treble', 578, 70)
    # the values of the netlist, in small type
    for kind, x, y, text, comp in (('C', 84, 136, '34k', 'R:RH_B'), ('C', 84, 276, '34k', 'R:RH_N'), ('L', 150, 128, '1M', 'R:RG_B'), ('L', 150, 320, '1M', 'R:RG_N'),
                                   ('L', 212, 80, '100k', 'R:R_PB'), ('L', 212, 365, '100k', 'R:R_PN'), ('L', 652, 80, '100k', 'R:R_P2A'), ('C', 240, 106, '0.1u', 'C:C1B'),
                                   ('C', 240, 306, '0.1u', 'C:C1N'), ('C', 340, 56, '500p', 'C:C500'), ('C', 712, 126, '1u', 'C:C_OUT'), ('R', 252, 263, '820R', 'R:RK1'),
                                   ('L', 302, 262, '25u', 'C:CK1'), ('R', 613, 293, '1k5', 'R:RK2A'), ('L', 668, 289, '25u', 'C:CK2A'), ('L', 772, 208, '1M', 'R:R_O'),
                                   ('L', 538, 116, '5n', 'C:C5N')):
        s.val(kind, x, y, text, comp)
    s.path('next_power', 'M800,142 V158 L822,150 Z', '#8c7a55', 1.5, s.lin(0, 142, 0, 158, [(0, '#d9b866'), (1, '#8a6d2c')]))
    return s


# the names a cathodyne power amp's netlist gives to the parts tweed_5e3_power draws: the input capacitor, the phase inverter's plate and cathode resistors, the two
# coupling capacitors, the grid leaks, the stoppers, the output tubes' cathode resistor and its capacitor, and the tube
CATHODYNE = {
    'tweed_5e3': dict(cin='C2', rpp='R_PP', rk1='RK2B', rk2='RK2C', c3='C3', c4='C4', leaks=('R220A', 'R220B'), stops=('R1500A', 'R1500B'), rkc='R250', ckc='C25', tube='6V6GT'),
    'bassbreaker_1830': dict(cin='C11', rpp='R21', rk1='R20', rk2='R22', c3='C13', c4='C14', leaks=('R23', 'R24'), stops=('RS_A', 'RS_B'), rkc='RK', ckc='C15', tube='EL84'),
}


def tweed_5e3_power(amp_id='tweed_5e3'):
    nm = CATHODYNE[amp_id]
    vals = {}
    with open(os.path.join(ROOT, amp_id, 'poweramp.net'), encoding='utf-8') as f:
        for ln in f:
            w = ln.split()
            if len(w) >= 5 and w[0] in ('R', 'C') and not ln.lstrip().startswith('*'):
                vals[w[1]] = w[4]
                _TYPES[(amp_id + ':power', w[1])] = w[0]

    def txt(name):
        v = vals.get(name, '')
        return v.split('/')[0]

    def comp(name):
        return ('%s:%s' % (_TYPES[(amp_id + ':power', name)], name)) if name in vals and '/' not in vals[name] else None
    s = Svg(840, 430)
    p = s.path
    p('wire_static_rail', 'M150,36 V60', '#7a6a4c', 3)
    p('rail', 'M120,36 H200', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    p('wire_OUT-2', 'M20,215 H50 M62,215 H118')
    p('wire_PHASE', 'M150,96 V183 M150,150 H200 M216,150 H300 V110 H388 M150,257 V280 H200 M216,280 H300 V320 H388')
    p('wire_POWER', 'M420,78 V56 H640 V110 H660 M420,352 V374 H640 V320 H660 M660,110 a15,15 0 0 1 0,30 a15,15 0 0 1 0,30 a15,15 0 0 1 0,30 a15,15 0 0 1 0,30 '
      'a15,15 0 0 1 0,30 a15,15 0 0 1 0,30 a15,15 0 0 1 0,30')
    p('wire_SPEAKER', 'M720,155 H800 V190 M720,155 a15,15 0 0 0 0,30 a15,15 0 0 0 0,30 a15,15 0 0 0 0,30 a15,15 0 0 0 0,30')
    p('wire_static_power', 'M150,280 V290 M150,324 V336 M150,372 V382 M300,110 V102 M300,66 V56 M300,320 V328 M300,364 V374 M420,152 V278 M420,215 H470 V226 '
      'M462,226 H490 M462,226 V236 M462,272 V280 H490 V254 M490,246 V226 M476,280 V286 M660,215 H600 M720,275 V290', '#5a4d3a', 3)
    p('ot_core', 'M686,105 V325 M694,105 V325', '#8c7a55', 3)
    s.ellipse('rail_tap', 600, 215, 4, 4, '#7a6a4c')
    for gx, gy in ((150, 382), (300, 56), (300, 374), (476, 286), (720, 290)):
        ground(s, gx, gy)
    cap(s, 56, 215, 'cap_c2')
    cap(s, 208, 150, 'cap_c3')
    cap(s, 208, 280, 'cap_c4')
    resistor_v(s, 150, 60, 'res_56k')
    resistor_v(s, 150, 288, 'res_1k5')
    resistor_v(s, 150, 336, 'res_56k_k')
    resistor_h(s, 320, 110, 'res_1k5_a')
    resistor_h(s, 320, 320, 'res_1k5_b')
    resistor_v(s, 300, 66, 'res_220k_a')
    resistor_v(s, 300, 328, 'res_220k_b')
    resistor_v(s, 462, 236, 'res_250')
    cap_h(s, 490, 250, 'cap_ck')
    bulb(s, 150, 215, 1.0, 'PI', 'PHASE', label='12AX7')
    bulb(s, 420, 110, 1.0, 'PA', 'POWER_A', label=nm['tube'])
    bulb(s, 420, 320, 1.0, 'PB', 'POWER_B', flip=True, label=nm['tube'])
    for kind, x, y, name in (('C', 56, 191, nm['cin']), ('L', 162, 80, nm['rpp']), ('C', 208, 126, nm['c3']), ('C', 208, 256, nm['c4']), ('L', 162, 306, nm['rk1']),
                             ('L', 162, 354, nm['rk2']), ('C', 338, 98, nm['stops'][0]), ('C', 338, 308, nm['stops'][1]), ('L', 312, 84, nm['leaks'][0]),
                             ('L', 312, 346, nm['leaks'][1]), ('R', 453, 254, nm['rkc']), ('L', 502, 250, nm['ckc'])):
        s.val(kind, x, y, txt(name), comp(name))
    speaker(s, 800, 190, 'p')
    return s


_TYPES = {}


def comp_of(amp_id, vals, names):
    """The component a value of the drawing belongs to ("R:R12": its kind and its name in the netlist), for the plugin's value editing."""
    name = next((n for n in names if n in vals), None)
    return '%s:%s' % (_TYPES[(amp_id, name)], name) if name else None


def netlist_values(amp_id):
    """name -> value of every resistor and capacitor of the amp's preamp netlist."""
    out = {}
    with open(os.path.join(ROOT, amp_id, 'preamp.net'), encoding='utf-8') as f:
        for ln in f:
            t = ln.split()
            if len(t) >= 5 and t[0] in ('R', 'C') and not ln.lstrip().startswith('*') and t[4] not in ('1', '1e12'):
                out[t[1]] = t[4]
                _TYPES[(amp_id, t[1])] = t[0]
    return out


def tube_model(amp_id, name):
    with open(os.path.join(ROOT, amp_id, 'preamp.net'), encoding='utf-8') as f:
        for ln in f:
            t = ln.split()
            if len(t) >= 6 and t[0] == 'T' and t[1] == name:
                return t[5].replace('ECC83', '12AX7')
    return ''


def two_channel_preamp(amp_id):
    """The Plexi / JTM45 / 5F6-A family: a bright and a normal channel through V1, the two volume pots mixed into V2A, a cathode follower (V2B) and
    the bass-middle-treble stack. The values come from the netlist, by the names the family uses."""
    vals = netlist_values(amp_id)

    def v(*names):
        return next((vals[n] for n in names if n in vals), None)

    s = Svg(1020, 430)
    p = s.path
    p('rail', 'M150,36 H720', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    p('rail_bottom', 'M150,400 H260', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    p('wire_static_rail', 'M200,36 V60 M200,381 V400 M560,36 V60 M680,36 V183', '#7a6a4c', 3)
    p('wire_INPUT', 'M43,150 H66 M102,150 H140 V208 H164 M43,290 H66 M102,290 H140 V232 H164')
    p('wire_V1', 'M200,96 V184 M200,130 H234 M246,130 H362 M300,130 V80 H334 M346,80 H404 V130 M200,345 V256 M200,330 H234 M246,330 H362')
    p('wire_V1-m', 'M398,130 H410 M446,130 H480 V215 M398,330 H410 M446,330 H480 V215 M480,215 H528')
    p('wire_V2', 'M560,96 V183 M560,120 H610 V215 H648 M680,257 V275 M680,275 H720')
    p('wire_OUT', 'M756,275 H985 M800,275 V312 M870,275 V312 M940,275 V312')
    p('wire_static', 'M236,220 H270 V235 M262,235 H290 M262,235 V245 M262,281 V296 H290 V266 M290,235 V254 M380,148 V165 M380,348 V362 '
      'M140,150 V144 M140,108 V96 M140,290 V300 M140,336 V346 M560,257 V265 M545,265 H575 M545,265 V275 M545,311 V325 H575 V295 M560,325 V330 M575,265 V283 '
      'M680,290 V275 M680,326 V340 M800,348 V362 M870,348 V362 M940,348 V362', '#5a4d3a', 3)
    for gx, gy in ((140, 96), (140, 346), (380, 165), (380, 362), (276, 296), (560, 330), (680, 340), (800, 362), (870, 362), (940, 362)):
        ground(s, gx, gy)
    s.ellipse('jack_BRIGHT', 30, 150, 13, 13, s.rad(30, 150, 13, [(0, '#d9d0b8'), (1, '#5f5846')]), '#9a8760', 1.5)
    s.ellipse('jack_BRIGHT_hole', 30, 150, 5, 5, '#14110d', '#3b3329', 1)
    s.ellipse('jack_NORMAL', 30, 290, 13, 13, s.rad(30, 290, 13, [(0, '#d9d0b8'), (1, '#5f5846')]), '#9a8760', 1.5)
    s.ellipse('jack_NORMAL_hole', 30, 290, 5, 5, '#14110d', '#3b3329', 1)
    resistor_h(s, 66, 150, 'res_68k_b')
    resistor_h(s, 66, 290, 'res_68k_n')
    resistor_v(s, 140, 108, 'res_leak_b')
    resistor_v(s, 140, 300, 'res_leak_n')
    resistor_v(s, 200, 60, 'res_pb')
    resistor_v(s, 200, 345, 'res_pn')
    resistor_v(s, 262, 245, 'res_k1')
    resistor_h(s, 410, 130, 'res_mix_b')
    resistor_h(s, 410, 330, 'res_mix_n')
    resistor_v(s, 560, 60, 'res_p2a')
    resistor_v(s, 545, 275, 'res_k2a')
    resistor_v(s, 680, 290, 'res_cf')
    resistor_h(s, 720, 275, 'res_56k')
    cap(s, 240, 130, 'cap_cb')
    cap(s, 240, 330, 'cap_cn')
    cap(s, 340, 80, 'cap_bright')
    cap_h(s, 290, 260, 'cap_k1')
    cap_h(s, 575, 289, 'cap_k2a')
    cap(s, 835, 275, 'cap_tb')
    cap(s, 905, 275, 'cap_tm')
    bulb(s, 200, 220, 1.125, 'V1', 'V1', pi=True, label=tube_model(amp_id, 'V1A') or '12AX7')
    bulb(s, 560, 215, 1.0, 'V2', 'V2', label=tube_model(amp_id, 'V2A') or '12AX7')
    bulb(s, 680, 215, 1.0, 'V2B', 'V2B', label='CF')
    pot(s, 380, 130, 'volume', 1.0, 0.5)
    pot(s, 380, 330, 'volume2', 1.0, 0.5)
    anchor(s, 'volume', 440, 176)
    anchor(s, 'volume2', 440, 376)
    pot(s, 800, 330, 'treble', 1.0, 0.6)
    pot(s, 870, 330, 'middle', 1.0, 0.5)
    pot(s, 940, 330, 'bass', 1.0, 0.5)
    for kind, x, y, names in (('C', 84, 136, ('R68_B', 'R68_B1')), ('C', 84, 276, ('R68_N', 'R68_N1')), ('L', 150, 128, ('RLEAK_B',)), ('L', 150, 320, ('RLEAK_N',)),
                              ('L', 212, 80, ('R_PB',)), ('L', 212, 365, ('R_PN',)), ('C', 240, 106, ('C_B',)), ('C', 240, 306, ('C_N',)), ('C', 340, 56, ('C_BRIGHT',)),
                              ('R', 252, 263, ('RK_N', 'RK1')), ('L', 302, 262, ('CK_N', 'CK1')), ('C', 428, 116, ('R470_B', 'R270_B')), ('C', 428, 316, ('R470_N', 'R270_N')),
                              ('L', 572, 80, ('R_P2A',)), ('R', 533, 293, ('RK2A',)), ('L', 588, 289, ('CK2A',)), ('L', 692, 308, ('R100K', 'R13')),
                              ('C', 738, 260, ('R56K', 'R33')), ('C', 835, 256, ('C02A', 'C022A')), ('C', 905, 256, ('C02B', 'C022B'))):
        text = v(*names)
        if text:
            s.val(kind, x, y, text, comp_of(amp_id, vals, names))
    s.path('next_power', 'M985,267 V283 L1007,275 Z', '#8c7a55', 1.5, s.lin(0, 267, 0, 283, [(0, '#d9b866'), (1, '#8a6d2c')]))
    return s


def orange_preamp(amp_id='orange_or120'):
    """The Orange OR120: one gain stage into a bass-treble network, the gain pot, a second stage and the bank of filter capacitors."""
    vals = netlist_values(amp_id)

    def v(*names):
        return next((vals[n] for n in names if n in vals), None)

    s = Svg(1020, 430)
    p = s.path
    p('rail', 'M150,36 H720', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    p('wire_static_rail', 'M200,36 V60 M640,36 V60', '#7a6a4c', 3)
    p('wire_INPUT', 'M43,190 H66 M102,190 H164')
    p('wire_V1', 'M200,96 V158 M200,130 H234 M246,130 H542 M330,130 V197 M420,130 V197')
    p('wire_V2', 'M578,130 H600 V190 H608 M640,96 V158 M640,130 H706')
    p('wire_OUT', 'M718,130 H985 M940,130 V150')
    p('wire_static', 'M140,190 V184 M140,148 V132 M200,232 V240 M185,240 H215 M185,240 V250 M215,240 V258 M185,286 V298 H215 V270 '
      'M640,232 V240 M625,240 H655 M625,240 V250 M655,240 V258 M625,286 V298 H655 V270 M330,233 V247 M420,233 V247 M560,148 V165 M940,186 V200', '#5a4d3a', 3)
    for gx, gy in ((140, 132), (200, 298), (640, 298), (330, 247), (420, 247), (560, 165), (940, 200)):
        ground(s, gx, gy)
    jack(s, 30, 190)
    resistor_h(s, 66, 190, 'res_68k')
    resistor_v(s, 140, 148, 'res_leak')
    resistor_v(s, 200, 60, 'res_p1')
    resistor_v(s, 640, 60, 'res_p2')
    resistor_v(s, 185, 250, 'res_k1')
    resistor_v(s, 625, 250, 'res_k2')
    resistor_v(s, 940, 150, 'res_pi')
    cap(s, 240, 130, 'cap_c68a')
    cap(s, 375, 130, 'cap_1n5')
    cap(s, 712, 130, 'cap_c68b')
    for i, x in enumerate((760, 820, 880)):
        cap(s, x, 130, 'cap_f%d' % i)
    cap_h(s, 215, 264, 'cap_k1')
    cap_h(s, 655, 264, 'cap_k2')
    bulb(s, 200, 190, 1.0, 'V1', 'V1', label=tube_model(amp_id, 'V1A') or '12AX7')
    bulb(s, 640, 190, 1.0, 'V2', 'V2', label=tube_model(amp_id, 'V2A') or '12AX7')
    pot(s, 560, 130, 'volume', 1.0, 0.5)
    pot(s, 330, 215, 'bass', 1.0, 0.5)
    pot(s, 420, 215, 'treble', 1.0, 0.6)
    anchor(s, 'volume', 560, 205)
    for kind, x, y, names in (('C', 84, 176, ('R68',)), ('L', 150, 168, ('R1M',)), ('L', 212, 80, ('R220A',)), ('C', 240, 106, ('C68A',)), ('C', 375, 106, ('C1N5',)),
                              ('R', 173, 268, ('RK1',)), ('L', 227, 264, ('CK1',)), ('L', 652, 80, ('R220B',)), ('R', 613, 268, ('RK2',)), ('L', 667, 264, ('CK2',)),
                              ('C', 712, 106, ('C68B',)), ('C', 760, 106, ('CF1',)), ('C', 820, 106, ('CF3',)), ('C', 880, 106, ('CF5',)), ('L', 952, 168, ('RPI',))):
        text = v(*names)
        if text:
            s.val(kind, x, y, text, comp_of(amp_id, vals, names))
    s.path('next_power', 'M985,122 V138 L1007,130 Z', '#8c7a55', 1.5, s.lin(0, 122, 0, 138, [(0, '#d9b866'), (1, '#8a6d2c')]))
    return s


def hiwatt_preamp(amp_id='hiwatt_dr103'):
    """The Hiwatt DR103: bright and normal channels (grids straight to the jacks), the two halves of V2 with joined plates, the passive tone stack,
    the master volume and V3a."""
    vals = netlist_values(amp_id)

    def v(*names):
        return next((vals[n] for n in names if n in vals), None)

    s = Svg(1140, 430)
    p = s.path
    p('rail', 'M150,36 H1050', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    p('rail_bottom', 'M150,400 H260', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    p('wire_static_rail', 'M200,36 V60 M200,381 V400 M560,36 V60 M1000,36 V60', '#7a6a4c', 3)
    p('wire_INPUT', 'M43,150 H140 V208 H164 M43,290 H140 V232 H164')
    p('wire_V1', 'M200,96 V184 M200,130 H234 M246,130 H362 M200,345 V256 M200,330 H234 M246,330 H362')
    p('wire_V1-m', 'M398,130 H480 V215 M398,330 H480 V215 M480,215 H528')
    p('wire_V2', 'M560,96 V183 M560,120 H606 M618,120 H882 M700,120 V172 M770,120 V172 M840,120 V172 M918,120 H940 V215 H968')
    p('wire_V3', 'M1000,96 V183 M1000,120 H1034')
    p('wire_OUT', 'M1046,120 H1110')
    p('wire_static', 'M236,220 H270 V235 M262,235 H290 M262,235 V245 M262,281 V296 H290 V266 M290,235 V254 M380,148 V165 M380,348 V362 '
      'M140,150 V144 M140,108 V96 M140,290 V300 M140,336 V346 M560,257 V265 M545,265 H575 M545,265 V275 M545,311 V325 H575 V295 M560,325 V330 M575,265 V283 '
      'M700,208 V222 M770,208 V222 M840,208 V222 M900,138 V152 M1000,257 V270 M1000,306 V320', '#5a4d3a', 3)
    for gx, gy in ((140, 96), (140, 346), (380, 165), (380, 362), (276, 296), (560, 330), (700, 222), (770, 222), (840, 222), (900, 152), (1000, 320)):
        ground(s, gx, gy)
    s.ellipse('jack_BRIGHT', 30, 150, 13, 13, s.rad(30, 150, 13, [(0, '#d9d0b8'), (1, '#5f5846')]), '#9a8760', 1.5)
    s.ellipse('jack_BRIGHT_hole', 30, 150, 5, 5, '#14110d', '#3b3329', 1)
    s.ellipse('jack_NORMAL', 30, 290, 13, 13, s.rad(30, 290, 13, [(0, '#d9d0b8'), (1, '#5f5846')]), '#9a8760', 1.5)
    s.ellipse('jack_NORMAL_hole', 30, 290, 5, 5, '#14110d', '#3b3329', 1)
    resistor_v(s, 140, 108, 'res_leak_b')
    resistor_v(s, 140, 300, 'res_leak_n')
    resistor_v(s, 200, 60, 'res_pb')
    resistor_v(s, 200, 345, 'res_pn')
    resistor_v(s, 262, 245, 'res_k1')
    resistor_v(s, 560, 60, 'res_p2')
    resistor_v(s, 545, 275, 'res_k2')
    resistor_v(s, 1000, 60, 'res_p3')
    resistor_v(s, 1000, 270, 'res_k3')
    cap(s, 240, 130, 'cap_cb')
    cap(s, 240, 330, 'cap_cn')
    cap_h(s, 290, 260, 'cap_k1')
    cap_h(s, 575, 289, 'cap_k2')
    cap(s, 612, 120, 'cap_t')
    cap(s, 1040, 120, 'cap_out')
    bulb(s, 200, 220, 1.125, 'V1', 'V1', pi=True, label='12AX7')
    bulb(s, 560, 215, 1.0, 'V2', 'V2', label='12AX7x2')
    bulb(s, 1000, 215, 1.0, 'V3', 'V3', label='12AX7')
    pot(s, 380, 130, 'volume', 1.0, 0.5)
    pot(s, 380, 330, 'volume2', 1.0, 0.5)
    anchor(s, 'volume', 440, 176)
    anchor(s, 'volume2', 440, 376)
    pot(s, 700, 190, 'treble', 1.0, 0.6)
    pot(s, 770, 190, 'middle', 1.0, 0.5)
    pot(s, 840, 190, 'bass', 1.0, 0.5)
    pot(s, 900, 120, 'master', 1.0, 0.7)
    for kind, x, y, names in (('L', 150, 128, ('RLEAK_B',)), ('L', 150, 320, ('RLEAK_N',)), ('L', 212, 80, ('R220B',)), ('L', 212, 365, ('R220N',)),
                              ('C', 240, 106, ('C_B',)), ('C', 240, 306, ('C_N',)), ('R', 252, 263, ('RK1',)), ('L', 302, 262, ('CK1',)), ('L', 572, 80, ('R100P',)),
                              ('R', 533, 293, ('RK2A',)), ('L', 588, 289, ('CK2B',)), ('C', 612, 96, ('C1000T',)), ('L', 1012, 80, ('R100V',)), ('R', 988, 296, ('RK3',)),
                              ('C', 1040, 96, ('C47O',))):
        text = v(*names)
        if text:
            s.val(kind, x, y, text, comp_of(amp_id, vals, names))
    s.path('next_power', 'M1110,112 V128 L1132,120 Z', '#8c7a55', 1.5, s.lin(0, 112, 0, 128, [(0, '#d9b866'), (1, '#8a6d2c')]))
    return s


def ac30_preamp(amp_id='ac30_normal'):
    """The Vox AC30: V8 (brilliant on top, normal below), the brilliant channel through V7a and the cathode follower into the treble-bass network, the normal
    channel straight to the output mix."""
    vals = netlist_values(amp_id)

    def v(*names):
        return next((vals[n] for n in names if n in vals), None)

    s = Svg(1060, 430)
    p = s.path
    p('rail', 'M150,36 H720', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    p('rail_bottom', 'M150,400 H260', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    p('wire_static_rail', 'M200,36 V60 M200,381 V400 M560,36 V60 M680,36 V183', '#7a6a4c', 3)
    p('wire_INPUT', 'M43,150 H66 M102,150 H140 V208 H164 M43,290 H66 M102,290 H140 V232 H164')
    p('wire_V8', 'M200,96 V184 M200,130 H234 M246,130 H362 M300,130 V80 H334 M346,80 H404 V130 M200,345 V256 M200,330 H234 M246,330 H362')
    p('wire_V8-m', 'M398,130 H480 V215 H528 M398,330 H410 M446,330 H470 V395 H980 V275')
    p('wire_V7', 'M560,96 V183 M560,120 H610 V215 H648 M680,257 V275 M680,275 H720 M756,275 H940 M800,275 V312 M870,275 V312')
    p('wire_OUT', 'M940,275 H980 M980,275 H1030')
    p('wire_static', 'M236,220 H270 V235 M262,235 H290 M262,235 V245 M262,281 V296 H290 V266 M290,235 V254 M380,148 V165 M380,348 V362 '
      'M140,150 V144 M140,108 V96 M140,290 V300 M140,336 V346 M560,257 V265 M545,265 H575 M545,265 V275 M545,311 V325 H575 V295 M560,325 V330 M575,265 V283 '
      'M680,290 V275 M680,326 V340 M800,348 V362 M870,348 V362', '#5a4d3a', 3)
    for gx, gy in ((140, 96), (140, 346), (380, 165), (380, 362), (276, 296), (560, 330), (680, 340), (800, 362), (870, 362)):
        ground(s, gx, gy)
    s.ellipse('jack_BRILLIANT', 30, 150, 13, 13, s.rad(30, 150, 13, [(0, '#d9d0b8'), (1, '#5f5846')]), '#9a8760', 1.5)
    s.ellipse('jack_BRILLIANT_hole', 30, 150, 5, 5, '#14110d', '#3b3329', 1)
    s.ellipse('jack_NORMAL', 30, 290, 13, 13, s.rad(30, 290, 13, [(0, '#d9d0b8'), (1, '#5f5846')]), '#9a8760', 1.5)
    s.ellipse('jack_NORMAL_hole', 30, 290, 5, 5, '#14110d', '#3b3329', 1)
    resistor_h(s, 66, 150, 'res_68k_b')
    resistor_h(s, 66, 290, 'res_68k_n')
    resistor_v(s, 140, 108, 'res_leak_b')
    resistor_v(s, 140, 300, 'res_leak_n')
    resistor_v(s, 200, 60, 'res_pb')
    resistor_v(s, 200, 345, 'res_pn')
    resistor_v(s, 262, 245, 'res_k8')
    resistor_h(s, 410, 330, 'res_mix_n')
    resistor_v(s, 560, 60, 'res_p7')
    resistor_v(s, 545, 275, 'res_k7')
    resistor_v(s, 680, 290, 'res_cf')
    resistor_h(s, 720, 275, 'res_100k')
    cap(s, 240, 130, 'cap_cb')
    cap(s, 240, 330, 'cap_cn')
    cap(s, 340, 80, 'cap_bright')
    cap_h(s, 290, 260, 'cap_k8')
    cap_h(s, 575, 289, 'cap_k7')
    cap(s, 835, 275, 'cap_t1')
    cap(s, 905, 275, 'cap_t2')
    bulb(s, 200, 220, 1.125, 'V8', 'V8', pi=True, label='12AX7')
    bulb(s, 560, 215, 1.0, 'V7', 'V7', label='12AX7')
    bulb(s, 680, 215, 1.0, 'V7B', 'V7B', label='CF')
    pot(s, 380, 130, 'volume', 1.0, 0.5)
    pot(s, 380, 330, 'volume2', 1.0, 0.5)
    anchor(s, 'volume', 440, 176)
    anchor(s, 'volume2', 440, 376)
    pot(s, 800, 330, 'treble', 1.0, 0.6)
    pot(s, 870, 330, 'bass', 1.0, 0.5)
    for kind, x, y, names in (('C', 84, 136, ('R34',)), ('C', 84, 276, ('R31',)), ('L', 150, 128, ('R32',)), ('L', 150, 320, ('R29',)), ('L', 212, 80, ('R57',)),
                              ('L', 212, 365, ('R58',)), ('C', 240, 106, ('C9',)), ('C', 240, 306, ('C11',)), ('C', 340, 56, ('C8',)), ('R', 252, 263, ('R10',)),
                              ('L', 302, 262, ('C10',)), ('C', 428, 316, ('R6',)), ('L', 572, 80, ('R55',)), ('R', 533, 293, ('R9',)), ('L', 588, 289, ('C7',)),
                              ('L', 692, 308, ('R8',)), ('C', 738, 260, ('R35',)), ('C', 835, 256, ('C4',)), ('C', 905, 256, ('C5',))):
        text = v(*names)
        if text:
            s.val(kind, x, y, text, comp_of(amp_id, vals, names))
    s.path('next_power', 'M1030,267 V283 L1052,275 Z', '#8c7a55', 1.5, s.lin(0, 267, 0, 283, [(0, '#d9b866'), (1, '#8a6d2c')]))
    return s


def fender_two_channel_preamp(amp_id):
    """The Deluxe Reverb / Twin Reverb: the normal channel (V1) and the vibrato channel (V2), each a gain triode, the treble-bass(-middle) network, the
    volume pot and a second triode; the two are mixed into V4b. The vibrato oscillator and the spring reverb are other circuits: their knobs are only shown."""
    vals = netlist_values(amp_id)
    ui = {u['param'] for u in __import__('json').load(open(os.path.join(ROOT, amp_id, 'amp.json'), encoding='utf-8'))['ui']}
    s = Svg(1220, 540)
    p = s.path
    tone = ['treble', 'bass'] + (['middle'] if 'middle' in ui else [])
    tone_x = {'treble': 330, 'bass': 400, 'middle': 470}
    rows = ((150, 'V1', 'V1A', 'V1B', 'volume2', 1), (400, 'V2', 'V2A', 'V2B', 'volume', 2))
    p('rail1', 'M150,28 H760', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    p('rail2', 'M150,278 H760', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    p('rail3', 'M900,93 H1020', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    static = ''
    gnd = []
    inputs = ''
    for cy, key, ta, tb, vol, n in rows:
        yj, rail = cy - 60, cy - 122
        inputs += 'M43,%d H164 ' % cy
        sig = ('M200,%d V%d M200,%d H234 M246,%d H542 ' % (cy - 66, cy - 32, yj, yj))
        sig += ' '.join('M%d,%d V%d' % (tone_x[t], yj, yj + 62) for t in tone)
        sig += ' M578,%d H620 V%d H648 M680,%d V%d M680,%d H724 M736,%d H860' % (yj, cy, cy - 66, cy - 32, yj, yj)
        p('wire_%s' % key, sig)
        static += 'M200,%d V%d M680,%d V%d ' % (rail, cy - 102, rail, cy - 102)
        static += 'M140,%d V%d M140,%d V%d ' % (cy, cy - 6, cy - 42, cy - 58)
        for cx in (200, 680):  # the two cathodes
            static += 'M%d,%d V%d M%d,%d H%d M%d,%d V%d M%d,%d V%d M%d,%d V%d H%d V%d ' % (
                cx, cy + 42, cy + 50, cx - 15, cy + 50, cx + 15, cx - 15, cy + 50, cy + 60, cx + 15, cy + 50, cy + 68,
                cx - 15, cy + 96, cy + 108, cx + 15, cy + 80)
            gnd.append((cx, cy + 108))
        for t in tone:
            static += 'M%d,%d V%d ' % (tone_x[t], yj + 98, yj + 112)
            gnd.append((tone_x[t], yj + 112))
        static += 'M560,%d V%d ' % (yj + 18, yj + 34)
        gnd.append((560, yj + 34))
        gnd.append((140, cy - 58))
    p('wire_INPUT', inputs)
    p('wire_V4', 'M860,90 V400 M860,215 H928 M960,%d V%d' % (113 + 36, 183) if False else 'M860,90 V340 M860,215 H928 M960,149 V183')
    p('wire_OUT', 'M960,155 H1100')
    static += 'M960,93 V113 M960,257 V265 M945,265 H975 M945,265 V275 M975,265 V283 M945,311 V325 H975 V295'
    gnd.append((960, 325))
    p('wire_static', static, '#5a4d3a', 3)
    for gx, gy in gnd:
        ground(s, gx, gy)
    for cy, key, ta, tb, vol, n in rows:
        yj = cy - 60
        name = 'NORMAL' if n == 1 else 'VIBRATO'
        s.ellipse('jack_%s' % name, 30, cy, 13, 13, s.rad(30, cy, 13, [(0, '#d9d0b8'), (1, '#5f5846')]), '#9a8760', 1.5)
        s.ellipse('jack_%s_hole' % name, 30, cy, 5, 5, '#14110d', '#3b3329', 1)
        resistor_h(s, 66, cy, 'res_68k_%d' % n)
        resistor_v(s, 140, cy - 42, 'res_leak_%d' % n)
        for cx, tag in ((200, ta), (680, tb)):
            resistor_v(s, cx, cy - 102, 'res_p_%s' % tag)
            resistor_v(s, cx - 15, cy + 60, 'res_k_%s' % tag)
            cap_h(s, cx + 15, cy + 74, 'cap_k_%s' % tag)
            bulb(s, cx, cy, 1.0, tag, key, label='12AX7')
        cap(s, 240, yj, 'cap_c2_%d' % n)
        cap(s, 730, yj, 'cap_out_%d' % n)
        for t in tone:
            pot(s, tone_x[t], yj + 80, t + ('' if n == 1 else '-2'), 1.0, 0.5)
        pot(s, 560, yj, vol, 1.0, 0.5)
        anchor(s, vol, 560, yj + 70)
    # the mixer and its output
    resistor_v(s, 960, 113, 'res_p_v4')
    resistor_v(s, 945, 275, 'res_k_v4')
    cap_h(s, 975, 289, 'cap_k_v4')
    bulb(s, 960, 215, 1.0, 'V4B', 'V4', label='12AX7')
    for i, (param, x) in enumerate((('vib_speed', 880), ('vib_intensity', 960), ('reverb', 1040))):
        if param in ui:
            pot(s, x, 460, param, 1.0, 0.5)
    for kind, x, y, names in (('L', 212, 66, ('R4',)), ('L', 692, 66, ('R11',)), ('R', 173, 214, ('R5',)), ('L', 227, 230, ('C1',)), ('C', 240, 36, ('C2',)),
                              ('L', 212, 316, ('R16',)), ('L', 692, 316, ('R29',)), ('R', 173, 464, ('R17',)), ('L', 227, 480, ('C6',)), ('C', 240, 286, ('C7',))):
        text = next((vals[n] for n in names if n in vals), None)
        if text:
            s.val(kind, x, y, text, comp_of(amp_id, vals, names))
    s.path('next_power', 'M1100,147 V163 L1122,155 Z', '#8c7a55', 1.5, s.lin(0, 147, 0, 163, [(0, '#d9b866'), (1, '#8a6d2c')]))
    return s


def bassbreaker_preamp(amp_id='bassbreaker_1830'):
    """The Bassbreaker 18/30: V1a, the cathode follower V1b, the treble-middle-bass stack, the two volumes and V2a."""
    vals = netlist_values(amp_id)
    s = Svg(1100, 430)
    p = s.path
    p('rail', 'M150,36 H1050', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    p('wire_static_rail', 'M200,36 V60 M320,36 V158 M1000,36 V60', '#7a6a4c', 3)
    p('wire_INPUT', 'M43,190 H66 M102,190 H168')
    p('wire_V1', 'M200,96 V158 M200,130 H260 V190 H288 M320,222 V232 M320,232 V275 M320,275 H358 M392,275 H900 '
      'M520,275 V312 M590,275 V312 M660,275 V312 M730,275 V312 M800,275 V312 M870,275 V312')
    p('wire_V2', 'M900,275 H930 V200 H968 M1000,96 V168 M1000,130 H1034')
    p('wire_OUT', 'M1046,130 H1085')
    p('wire_static', 'M140,190 V184 M140,148 V132 M200,222 V230 M185,230 H215 M185,230 V240 M215,230 V248 M185,276 V288 H215 V260 M320,290 V275 M320,326 V340 '
      'M520,348 V362 M590,348 V362 M660,348 V362 M730,348 V362 M800,348 V362 M870,348 V362 M1000,232 V240 M985,240 H1015 M985,240 V250 M1015,240 V258 M985,286 V298 H1015 V270', '#5a4d3a', 3)
    for gx, gy in ((140, 132), (200, 288), (320, 340), (520, 362), (590, 362), (660, 362), (730, 362), (800, 362), (870, 362), (1000, 298)):
        ground(s, gx, gy)
    jack(s, 30, 190)
    resistor_h(s, 66, 190, 'res_33k')
    resistor_v(s, 140, 148, 'res_leak')
    resistor_v(s, 200, 60, 'res_p1')
    resistor_v(s, 185, 240, 'res_k1')
    resistor_v(s, 320, 290, 'res_k2')
    resistor_v(s, 1000, 60, 'res_p2')
    resistor_v(s, 985, 250, 'res_k3')
    cap_h(s, 215, 254, 'cap_k1')
    cap_h(s, 1015, 264, 'cap_k3')
    cap(s, 375, 275, 'cap_c2')
    cap(s, 1040, 130, 'cap_out')
    bulb(s, 200, 190, 1.0, 'V1A', 'V1', label='12AX7')
    bulb(s, 320, 190, 1.0, 'V1B', 'V1', label='CF')
    bulb(s, 1000, 200, 1.0, 'V2A', 'V2', label='12AX7')
    for param, x in (('treble', 520), ('middle', 590), ('bass', 660), ('volume', 730), ('volume2', 800), ('clean_treble', 870)):
        pot(s, x, 330, param, 1.0, 0.5)
    for kind, x, y, names in (('C', 84, 176, ('R2',)), ('L', 150, 168, ('R1',)), ('L', 212, 80, ('R3',)), ('R', 173, 262, ('R4',)), ('L', 227, 258, ('C1',)),
                              ('L', 332, 308, ('R5',)), ('C', 375, 251, ('C2',)), ('L', 1012, 80, ('R16',)), ('R', 973, 272, ('R17',)), ('L', 1027, 268, ('C9',))):
        text = next((vals[n] for n in names if n in vals), None)
        if text:
            s.val(kind, x, y, text, comp_of(amp_id, vals, names))
    s.path('next_power', 'M1085,122 V138 L1107,130 Z', '#8c7a55', 1.5, s.lin(0, 122, 0, 138, [(0, '#d9b866'), (1, '#8a6d2c')]))
    return s


def jcm800_2203_preamp(amp_id='jcm800_2203'):
    """The JCM800 2203 preamp drawn part by part: V1A with its cathode bypass, the volume network with the bright cap, V1B, V2A with its coupling network, the V2B
    cathode follower and the bass / middle / treble stack with the master. (Left out: the pickup and cable network, the Low jack's contact, the supply filter and
    the mods' switches.) Every part carries its netlist value, so the page can edit it and show what a preset changes."""
    vals = netlist_values(amp_id)
    s = Svg(1200, 720)
    p = s.path

    def lab(name, kind, x, y):
        if name in vals:
            s.val(kind, x, y, vals[name], '%s:%s' % (_TYPES[(amp_id, name)], name))

    def res_v(name, cx, y0):
        resistor_v(s, cx, y0, 'res_' + name)
        lab(name, 'L', cx + 12, y0 + 22)

    def res_h(name, x0, cy):
        resistor_h(s, x0, cy, 'res_' + name)
        lab(name, 'C', x0 + 18, cy - 14)

    def cap_v(name, cx, cy):  # a capacitor in a horizontal wire
        cap(s, cx, cy, 'cap_' + name)
        lab(name, 'C', cx, cy - 26)

    def cap_d(name, cx, cy, side='L'):  # a capacitor in a vertical wire
        cap_h(s, cx, cy, 'cap_' + name)
        lab(name, side, cx + 16 if side == 'L' else cx - 16, cy + 4)

    # rail and its drops
    p('rail', 'M120,40 H1130', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    p('wire_static_rail', 'M250,40 V80 M610,40 V80 M930,40 V80 M1090,40 V214', '#7a6a4c', 3)
    # signal: the guitar into V1A, V1A's plate to the volume network, V1B, V1B's plate to V2A, V2A's plate to the cathode follower
    p('wire_INPUT', 'M43,250 H100 M136,250 H218 M62,250 V270')
    p('wire_INPUT-2', 'M250,116 V214 M250,150 H314 M326,150 H370 M370,150 H400 M436,150 H480 M385,150 V112 H408 M428,112 H451 V150 M480,150 H514 M526,150 H570 V250 H578 M480,150 V197')
    p('wire_V1', 'M610,116 V214 M610,150 H684 M696,150 H760 M760,150 H790 M826,150 H860 M775,150 V112 H798 M818,112 H841 V150 M860,150 V250 H898')
    p('wire_V2', 'M930,116 V214 M930,150 H990 V250 H1058')
    p('wire_TONE', 'M1090,292 V310 M1090,310 V395 H80 V500 M80,430 H134 M158,430 H300 M300,430 V447 M80,500 H108 M144,500 H200 M200,500 H244 M256,500 H300 M300,483 V522 M300,558 V580 M300,580 V612 M200,500 V612 M200,624 V630 H282 M318,465 H540 M540,465 V522')
    p('wire_MASTER', 'M558,540 H640')
    p('wire_static', 'M62,306 V318 M610,292 V304 M610,340 V352 M930,292 V304 M930,340 V352 M860,270 V280 M860,306 V322 M1090,310 H1130 V330 M1130,366 V378 '
      'M250,292 V304 M234,304 H266 M234,304 V316 M266,304 V319 M266,331 V352 M234,352 H266 M250,352 V356 M480,233 V260 M300,648 V665 M540,558 V580 M860,250 V270 M234,340 V352', '#5a4d3a', 3)
    for gx, gy in ((62, 318), (610, 352), (930, 352), (860, 322), (1130, 378), (250, 356), (480, 260), (300, 665), (540, 580)):
        ground(s, gx, gy)
    s.ellipse('jack_in', 30, 250, 13, 13, s.rad(30, 250, 13, [(0, '#d9d0b8'), (1, '#5f5846')]), '#9a8760', 1.5)
    s.ellipse('jack_hole', 30, 250, 5, 5, '#14110d', '#3b3329', 1)
    # V1A
    res_v('R2', 62, 270)
    res_h('R3', 100, 250)
    res_v('R4', 250, 80)
    res_v('R1', 234, 304)
    cap_d('C1', 266, 325, 'L')
    cap_v('C3', 320, 150)
    res_h('R5', 400, 150)
    cap_v('C4', 418, 112)
    cap_v('C5', 520, 150)
    # V1B
    res_v('R7', 610, 80)
    res_v('R6', 610, 304)
    cap_v('C7', 690, 150)
    res_h('R10', 790, 150)
    cap_v('C8', 808, 112)
    res_v('R11', 860, 270)
    # V2A, V2B
    res_v('R12', 930, 80)
    res_v('R9', 930, 304)
    res_v('R13', 1130, 330)
    bulb(s, 250, 250, 1.0, 'V1A', 'V1A', label=tube_model(amp_id, 'V1A') or '12AX7')
    bulb(s, 610, 250, 1.0, 'V1B', 'V1', label=tube_model(amp_id, 'V1B') or '12AX7')
    bulb(s, 930, 250, 1.0, 'V2', 'V2', label=tube_model(amp_id, 'V2A') or '12AX7')
    bulb(s, 1090, 250, 1.0, 'V2B', 'V2B', label='CF')
    # the tone stack
    res_h('R15', 108, 500)
    cap_v('C10', 146, 430)
    cap_v('C11', 250, 500)
    cap_d('C12', 200, 618, 'R')
    pot(s, 480, 215, 'volume', 1.0, 0.5)
    pot(s, 300, 465, 'treble', 1.0, 0.6)
    pot(s, 300, 540, 'bass', 1.0, 0.5)
    pot(s, 300, 630, 'middle', 1.0, 0.5)
    pot(s, 540, 540, 'master', 1.0, 0.5)
    for param, x, y in (('volume', 540, 261), ('treble', 360, 511), ('bass', 360, 586), ('middle', 360, 676), ('master', 600, 586)):
        anchor(s, param, x, y)
    s.path('next_power', 'M640,532 V548 L662,540 Z', '#8c7a55', 1.5, s.lin(0, 532, 0, 548, [(0, '#d9b866'), (1, '#8a6d2c')]))
    return s



POWER_SPECS = {
    # the names each amp's poweramp.net gives the parts of the classic power amp: a long-tailed ECC83 phase inverter (the tube with the first grid, then the second),
    # its plate loads, the coupling and the bias network into two EL34 / 6L6GC pairs, the output transformer and the feedback from the secondary into f2
    'jcm800_2203': dict(pi=('V3A', 'V3B'), cin='C9', rga='R17', rgb='R19', rk='R16', rt='R20', cfb='C15', rgnd='R23', pres=('shunt', 'C17', 'VR6'),
                        rpa='R18', rpb='R21', cpa='C14', cpb='C16', cx=('C18', 'grid'), rbias=('R24', 'R25'),
                        bias=[[('C', 'C19')], [('R', 'R27'), ('C', 'C20')], [('R', 'R26'), ('R', 'RV1')]],
                        stop=('R31_33', 'R32_34'), screen=('R35_37', 'R36_38'), nfb=([('R', 'R22')], 'tap')),
    'jcm800_2203kk': dict(pi=('V3A', 'V3B'), cin='C9', rga='R17', rgb='R19', rk='R16', rt='R20', cfb='C15', rgnd='R23', pres=('shunt', 'C17', 'VR6'),
                          rpa='R18', rpb='R21', cpa='C14', cpb='C16', cx=('C18', 'grid'), rbias=('R24', 'R25'),
                          bias=[[('C', 'C19')], [('R', 'R27'), ('C', 'C20')], [('R', 'R26'), ('R', 'RV1')]],
                          stop=('R31_33', 'R32_34'), screen=('R35_37', 'R36_38'), nfb=([('R', 'R22')], 'tap')),
    'plexi_1959': dict(pi=('V3A', 'V3B'), cin='C9', rga='R17', rgb='R19', rk='R16', rt='R20', cfb='C15', pres=('series', 'VR_PT', 'C_PRES'),
                       rpa='R18', rpb='R21', cpa='C14', cpb='C16', cx=('C47', 'plate'), rbias=('R220A', 'R220B'),
                       bias=[[('C', 'C8B')], [('R', 'R47K'), ('R', 'RTRIM')], [('R', 'R15K'), ('C', 'C8A')]],
                       stop=('R5K6_45', 'R5K6_67'), screen=('RS45', 'RS67'), nfb=([('R', 'R_NFB')], 'top')),
    'jtm45': dict(pi=('V3A', 'V3B'), cin='C9', rga='R17', rgb='R19', rk='R16', rt='R20', cfb='C15', pres=('series', 'VR_PT', 'C_PRES'),
                  rpa='R18', rpb='R21', cpa='C14', cpb='C16', cx=('C47P', 'plate'), rbias=('R220A', 'R220B'),
                  bias=[[('C', 'C8B')], [('R', 'R56K')], [('R', 'R15K'), ('C', 'C8A')]],
                  stop=('R1K5_A', 'R1K5_B'), screen=('RS1', 'RS2'), nfb=([('R', 'R_NFB')], 'top')),
    'superlead_100': dict(pi=('V5B', 'V5A'), cin='C17', rga='R31', rgb='R34', rk='R33', rt='R32', cfb='C18', rgnd='R44', pres=('shunt', 'C19', 'VR_P8'),
                          rpa='R36', rpb='R37', cpa='C21', cpb='C22', cx=('C20', 'plate'), rbias=('R38', 'R39'),
                          bias=[[('C', 'C45')], [('R', 'RBIAS'), ('C', 'C46')]],
                          stop=('R40', 'R41'), screen=('R42', 'R43'), nfb=([('R', 'R35')], 'tap')),
    'modbe_100': dict(pi=('V4A', 'V4B'), cin='C31', rga='R37', rgb='R41', rk='R39', rt='R40', cfb='C35', rgnd='R_VR9', pres=('shunt', 'C33', 'VR_PRES'),
                      rpa='R36', rpb='R35', cpa='C18', cpb='C34', cx=('C32', 'plate'), rbias=('R44', 'R45'),
                      bias=[[('C', 'C41')], [('R', 'RBIAS'), ('C', 'C40')]],
                      stop=('R66', 'R67'), screen=('R46', 'R47'), nfb=([('R', 'R50'), ('RC', ('R51', 'C36'))], 'tap')),
    'jubilee_2555': dict(pi=('V3A', 'V3B'), cin='C28', rga='R39', rgb='R41', rk='R40', rt='R17', cfb='C29', rgnd='R20', pres=('shunt', 'C14', 'VR7'),
                         rpa='R45', rpb='R46', cpa='C30', cpb='C31', cx=('C15', 'grid'), rbias=('R21', 'R22'),
                         bias=[[('C', 'C16')], [('R', 'R47'), ('C', 'C17')], [('R', 'RV1'), ('R', 'R23')]],
                         stop=('RG45', 'RG67'), screen=('R53_54', 'R55_56'), nfb=([('R', 'R44')], 'tap')),
    'afd100': dict(pi=('V9A', 'V9B'), cin='C68', rga='R101', rgb='R103', rk='R102', rt='R100', cfb='C73', rgnd='R104', pres=('shunt', 'C81', 'VR6A'),
                   rpa='R73', rpb='R63', cpa='C31_32', cpb='C33_30', cx=('C51', 'plate'), rbias=('R50_46', 'R48_44'), bias=[[('C', 'CBIAS')]],
                   stop=('R45_47', 'R49_43'), screen=('R39_40', 'R41_42'), nfb=([('K', 'power'), ('R', 'R99'), ('C', 'C72')], 'tap')),
    'american_5153': dict(pi=('V8A', 'V8B'), cin='C64P', rga='R150', rgb='R151', rk='R148', rt='R147', cfb='C63', rgnd='R138', pres=('shunt', 'C3_11', 'VRPR'),
                          rpa='R133', rpb='R184', cpa='C52', cpb='C47', cx=('C71', 'plate'), rbias=('R111', 'R110'), bias=[[('C', 'CBIAS')]],
                          stop=('R143_4', 'R145_6'), screen=('R134_5', 'R136_7'), nfb=([('K', 'resonance'), ('R', 'R162'), ('C', 'C126')], 'tap')),
    'german_trio': dict(pi=('V5A', 'V5B'), cin='CIN', rga='RG1', rgb='RG2', rk='RKT', rt='R10T', cfb='C10F', rgnd='R3K32', pres=None,
                        rpa='R82', rpb='R100', rpb2='RSYM', cpa='C22A', cpb='C22B', cx=None, rbias=('R221A', 'R221B'), bias=[[('C', 'CBIAS')]],
                        stop=('R3K32A', 'R3K32B'), screen=('R470A', 'R470B'),
                        nfb=([('R', 'RN1'), ('S', 'presence'), ('C', 'C2N2'), ('R', 'R27K4')], 'top')),
}
POWER_SPECS['german_trio_ii'] = POWER_SPECS['german_quad'] = POWER_SPECS['german_trio']
POWER_SPECS['bassman_5f6a'] = dict(pi=('V3A', 'V3B'), cin='C9', rga='R17', rgb='R19', rk='R16', rt='R20', cfb='C15', pres=('series', 'VR_PT', 'C_PRES'),
                                   rpa='R18', rpb='R21', cpa='C14', cpb='C16', cx=('C47P', 'plate'), rbias=('R220A', 'R220B'),
                                   bias=[[('C', 'C8B')], [('R', 'R56K')], [('R', 'R15K'), ('C', 'C8A')]],
                                   stop=('R1K5_A', 'R1K5_B'), screen=('RS1', 'RS2'), nfb=([('R', 'R_NFB')], 'top'))
POWER_SPECS['deluxe_reverb'] = dict(pi=('V6A', 'V6B'), cin='C25', rga='R49', rgb='R52', rk='R51', rt='R50', cfb='C26', rgnd='R53', pres=None,
                                    rpa='R54', rpb='R55', cpa='C27', cpb='C28', cx=None, rbias=('R56', 'R57'), bias=[[('C', 'C29')]],
                                    stop=('R60', 'R61'), screen=('R62', 'R63'), nfb=([('R', 'R64')], 'top'))
POWER_SPECS['twin_65'] = dict(pi=('V6A', 'V6B'), cin='C20', rga='R37', rgb='R38', rk='R41', rt='R36', cfb='C21', rgnd='R40', pres=None,
                              rpa='R42', rpb='R43', cpa='C22', cpb='C23', cx=None, rbias=('R44', 'R45'), bias=[[('C', 'C36')]],
                              stop=('R46', 'R48'), screen=('R50', 'R52'), nfb=([('RC', ('R39', 'C39'))], 'top'))


def classic_power(amp_id):
    """A classic Marshall-style power amp drawn part by part from its netlist (POWER_SPECS gives the names): the long-tailed phase inverter (V3A / V3B with their
    common cathode, the tail resistors and the feedback node f2), the coupling and bias network into the EL34 / 6L6GC pairs, the output transformer and the
    feedback from its secondary through the amp's NFB resistor. (Left out: the HT supply.)"""
    sp = POWER_SPECS[amp_id]
    vals, model = {}, {}
    key = amp_id + ':power'
    with open(os.path.join(ROOT, amp_id, 'poweramp.net'), encoding='utf-8') as f:
        for ln in f:
            t = ln.split()
            if ln.lstrip().startswith('*') or len(t) < 5:
                continue
            if t[0] in ('R', 'C'):
                vals[t[1]] = t[4]
                _TYPES[(key, t[1])] = t[0]
            elif t[0] in ('T', 'P') and len(t) >= 6:
                model[t[1]] = t[5].replace('ECC83', '12AX7').replace('6L6GCDS', '6L6GC').replace('6550DS', '6550')
                if t[0] == 'P':
                    model['_power'] = model[t[1]]
    s = Svg(1100, 820)
    p = s.path

    def lab(name, kind, x, y):
        v = vals.get(name)
        if v is None:
            return
        if '/' in v:  # a pair's half: shown, not editable
            s.val(kind, x, y, v.split('/')[0])
        else:
            s.val(kind, x, y, v, '%s:%s' % (_TYPES[(key, name)], name))

    def res_v(name, cx, y0):
        resistor_v(s, cx, y0, 'res_' + name)
        lab(name, 'L', cx + 12, y0 + 22)

    def res_h(name, x0, cy):
        resistor_h(s, x0, cy, 'res_' + name)
        lab(name, 'C', x0 + 18, cy - 14)

    def cap_v(name, cx, cy):  # in a horizontal wire
        cap(s, cx, cy, 'cap_' + name)
        lab(name, 'C', cx, cy - 26)

    def cap_d(name, cx, cy, side='L'):  # in a vertical wire
        cap_h(s, cx, cy, 'cap_' + name)
        lab(name, side, cx + 16 if side == 'L' else cx - 16, cy + 4)

    stat = []  # the dark wires
    grounds = []
    p('rail', 'M130,40 H700', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    p('rail_bottom', 'M130,580 H700', '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    p('wire_static_rail', 'M200,40 V80 M640,40 V56 M200,510 V580 M640,562 V580', '#7a6a4c', 3)
    p('wire_MASTER-2', 'M30,170 H84 M96,170 H168')
    p('wire_PHASE', 'M200,116 V134 M200,125 H334 M346,125 H560 M596,125 H668 M200,456 V474 M200,465 H334 M346,465 H560 M596,465 H668')
    p('wire_POWER', 'M700,89 V70 H860 V150 H880 M700,501 V540 H860 V360 H880 '
      'M880,150 a15,15 0 0 1 0,30 a15,15 0 0 1 0,30 a15,15 0 0 1 0,30 a15,15 0 0 1 0,30 a15,15 0 0 1 0,30 a15,15 0 0 1 0,30 a15,15 0 0 1 0,30')
    p('wire_SPEAKER', 'M940,200 H1020 V230 M940,200 a15,15 0 0 0 0,30 a15,15 0 0 0 0,30 a15,15 0 0 0 0,30 a15,15 0 0 0 0,30')
    p('ot_core', 'M906,140 V370 M914,140 V370', '#8c7a55', 3)
    s.ellipse('rail_tap', 820, 255, 4, 4, '#7a6a4c')
    # the common cathode, the tail bus f1 (R16, R17, R19), the feedback node f2 along the bottom and what hangs from it
    stat += ['M200,212 V378', 'M200,300 H156', 'M120,300 H70', 'M110,170 V190', 'M110,226 V240 H70', 'M70,240 V380', 'M130,400 V420', 'M130,364 V356 H70',
             'M70,416 V650', 'M130,420 H168', 'M130,420 V474', 'M130,486 V650', 'M70,650 H330']
    # the stoppers, the screens, the cathodes, the transformer's centre tap and the secondary's ground
    stat += ['M640,56 V92', 'M640,92 V104 H670', 'M640,562 V526', 'M640,526 V490 H670', 'M700,167 V180', 'M700,423 V410', 'M880,255 H820', 'M940,320 V340']
    grounds += [(700, 180), (700, 410), (940, 340)]
    # the cross capacitor, between the plates (before the coupling caps) or between the grid lines (after them)
    cxn, where = sp['cx'] or (None, None)
    xc = 280 if where == 'plate' else 420
    ya, yb = (125, 465)
    if cxn:
        stat += ['M%d,%d V284' % (xc, ya), 'M%d,296 V%d' % (xc, yb)]
        if where == 'plate':
            stat += ['M200,125 H%d' % xc]
    # the grid-leak resistors to the bias bus
    stat += ['M500,125 V180', 'M500,216 V394', 'M500,465 V430']
    # the bias bus branches: each a chain from the bus, horizontal parts then a vertical one to ground
    branches = sp['bias']
    ys = [250, 315, 380][:len(branches)] if len(branches) > 1 else [300]
    for (by, chain) in zip(ys, branches):
        if len(chain) == 2:
            kind, nm = chain[0]
            if kind == 'R':
                stat += ['M500,%d H560' % by, 'M596,%d H640' % by]
                res_h(nm, 560, by)
            else:
                stat += ['M500,%d H534' % by, 'M546,%d H640' % by]
                cap_v(nm, 540, by)
            x, chain = 640, chain[1:]
        else:
            stat.append('M500,%d H540' % by)
            x = 540
        kind, nm = chain[0]
        if kind == 'R':
            stat += ['M%d,%d V%d' % (x, by, by + 8), 'M%d,%d V%d' % (x, by + 44, by + 54)]
            res_v(nm, x, by + 8)
            grounds.append((x, by + 54))
        else:
            stat += ['M%d,%d V%d' % (x, by, by + 14), 'M%d,%d V%d' % (x, by + 26, by + 36)]
            cap_d(nm, x, by + 20, 'L')
            grounds.append((x, by + 36))
    # the presence network on f2
    pk, pa_, pb_ = sp['pres'] or (None, None, None)
    if pk:
        stat += ['M230,650 V672']
    if pk is None:
        pass
    elif pk == 'shunt':
        cap_d(pa_, 230, 678, 'L')
        stat += ['M230,684 V690']
        pot(s, 230, 708, 'presence', 1.0, 0.5)
        stat += ['M230,726 V736']
        grounds.append((230, 736))
        anchor(s, 'presence', 290, 700)
    else:
        pot(s, 230, 690, 'presence', 1.0, 0.5)
        stat += ['M230,708 V730']
        cap_d(pb_, 230, 736, 'L')
        stat += ['M230,742 V756']
        grounds.append((230, 756))
        anchor(s, 'presence', 290, 690)
    if sp.get('rgnd'):
        stat += ['M150,650 V670', 'M150,706 V716']
        grounds.append((150, 716))
    # the feedback from the secondary
    nfb_items = []
    if sp['nfb']:  # the parts along y=650 from the secondary to the node f2: resistors, capacitors, a knob in the line (K) or one that shunts it (S)
        chain, frm = sp['nfb']
        stat.append('M%s H980 V650 H900' % ('940,260' if frm == 'tap' else '980,200'))
        cur = 900
        for k, item in enumerate(chain):
            x = [840, 730, 620, 510][k]
            hw = {'R': 18, 'C': 6, 'K': 18, 'S': 0, 'RC': 30}[item[0]]
            stat.append('M%d,650 H%d' % (cur, x + hw))
            cur = x - hw
            nfb_items.append((item, x))
        stat.append('M%d,650 H330' % cur)
    p('wire_static_power', ' '.join(stat), '#5a4d3a', 3)
    for gx, gy in grounds:
        ground(s, gx, gy)
    # the parts
    bulb(s, 200, 170, 1.0, 'PI_A', 'PHASE', label=model.get(sp['pi'][0]) or '12AX7')
    bulb(s, 200, 420, 1.0, 'PI_B', 'PHASE_B', flip=True, label=model.get(sp['pi'][1]) or '12AX7')
    cap_v(sp['cin'], 90, 170)
    res_v(sp['rpa'], 200, 80)
    res_v(sp['rpb'], 200, 474)
    res_v(sp['rga'], 110, 190)
    res_v(sp['rgb'], 130, 364)
    res_h(sp['rk'], 120, 300)
    res_v(sp['rt'], 70, 380)
    cap_d(sp['cfb'], 130, 480, 'R')
    if sp.get('rgnd'):
        res_v(sp['rgnd'], 150, 670)
    cap_v(sp['cpa'], 340, 125)
    cap_v(sp['cpb'], 340, 465)
    if cxn:
        cap_d(cxn, xc, 290, 'L')
    if sp.get('rpb2'):
        stat += ['M200,510 V516', 'M200,552 V580']
        res_v(sp['rpb2'], 200, 516)
    res_v(sp['rbias'][0], 500, 180)
    res_v(sp['rbias'][1], 500, 394)
    res_h(sp['stop'][0], 560, 125)
    res_h(sp['stop'][1], 560, 465)
    res_v(sp['screen'][0], 640, 56)
    res_v(sp['screen'][1], 640, 526)
    tube = model.get('_power', 'EL34')
    bulb(s, 700, 125, 1.0, 'PA', 'POWER_A', label=tube)
    bulb(s, 700, 465, 1.0, 'PB', 'POWER_B', flip=True, label=tube)
    for (kind, nm), x in nfb_items:
        if kind == 'R':
            res_h(nm, x - 18, 650)
        elif kind == 'C':
            cap_v(nm, x, 650)
        elif kind == 'RC':  # a resistor with a capacitor across it
            res_h(nm[0], x - 18, 650)
            p('wire_nfb_%d' % x, 'M%d,650 V622 H%d M%d,622 H%d V650' % (x - 30, x - 6, x + 6, x + 30), '#5a4d3a', 3)
            cap_v(nm[1], x, 622)
        elif kind == 'K':
            pot(s, x, 650, nm, 1.0, 0.5)
            anchor(s, nm, x + 50, 690)
        else:
            stat_s = 'M%d,650 V672 M%d,708 V724' % (x, x)
            p('wire_nfb_%d' % x, stat_s, '#5a4d3a', 3)
            ground(s, x, 724)
            pot(s, x, 690, nm, 1.0, 0.5)
            anchor(s, nm, x + 50, 700)
    speaker(s, 1020, 230, 'p')
    return s


AMPS = {'bassbreaker_1830': (bassbreaker_preamp, lambda: tweed_5e3_power('bassbreaker_1830')),
        'deluxe_reverb': (lambda: fender_two_channel_preamp('deluxe_reverb'), None),
        'twin_65': (lambda: fender_two_channel_preamp('twin_65'), None),
        'ac30_normal': (ac30_preamp, None),
        'hiwatt_dr103': (hiwatt_preamp, None),
        'orange_or120': (orange_preamp, None),
        'tweed_5e3': (tweed_5e3_preamp, lambda: tweed_5e3_power('tweed_5e3')),
        'plexi_1959': (lambda: two_channel_preamp('plexi_1959'), lambda: classic_power('plexi_1959')),
        'jtm45': (lambda: two_channel_preamp('jtm45'), lambda: classic_power('jtm45')),
        'bassman_5f6a': (lambda: two_channel_preamp('bassman_5f6a'), None),
        'jcm800_2203': (jcm800_2203_preamp, lambda: classic_power('jcm800_2203')),
        'jcm800_2203kk': (None, lambda: classic_power('jcm800_2203kk')),
        'superlead_100': (None, lambda: classic_power('superlead_100')),
        'modbe_100': (None, lambda: classic_power('modbe_100'))}

# The British 800's drawings were made in VectorCraft: its values are added to the exported files (the anchors replace the ones already there).
INJECT = {
    'circuit.svg': (('C', 100, 176, '68k', 'R:R3'), ('L', 202, 92, '100k', 'R:R4'), ('L', 362, 92, '100k', 'R:R12'), ('L', 202, 262, '2.7k', 'R:R1'),
                    ('L', 362, 262, '820R', 'R:R9'), ('C', 438, 110, '33k', 'R:R15'), ('C', 240, 100, '0.022u', 'C:C3')),
    'circuit_power.svg': (('C', 56, 164, '0.022u', None),),
}


def inject(amp_id, values):
    import re
    for name, vals in values.items():
        path = os.path.join(ROOT, amp_id, 'face', name)
        with open(path, encoding='utf-8', newline='') as f:
            text = f.read()
        text = re.sub(r'\s*<ellipse id="val[LRC]_\d+_[^"]*"[^>]*/>', '', text)
        anchors = ''.join('\n  <ellipse id="val%s_%d_%s%s" cx="%g" cy="%g" rx="1" ry="1" fill="none" stroke="none"/>' % (k, i + 1, t, ('@' + c) if c else '', x, y) for i, (k, x, y, t, c) in enumerate(vals))
        text = text.replace('</svg>', anchors.lstrip('\n') + '\n</svg>') if '</svg>' in text else text
        with open(path, 'w', encoding='utf-8', newline='') as f:
            f.write(text)


def main():
    for amp_id, (pre, power) in AMPS.items():
        face = os.path.join(ROOT, amp_id, 'face')
        os.makedirs(face, exist_ok=True)
        for name, fn in (('circuit.svg', pre), ('circuit_power.svg', power)):
            if fn is None:  # the power amp keeps the generated drawing
                continue
            with open(os.path.join(face, name), 'w', encoding='utf-8', newline='\n') as f:
                f.write(fn().text())
        print('wrote', amp_id)


if __name__ == '__main__':
    main()
