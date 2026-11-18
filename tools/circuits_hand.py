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
    for kind, x, y, text in (('C', 84, 136, '34k'), ('C', 84, 276, '34k'), ('L', 150, 128, '1M'), ('L', 150, 320, '1M'), ('L', 212, 80, '100k'), ('L', 212, 365, '100k'),
                             ('L', 652, 80, '100k'), ('C', 240, 106, '0.1u'), ('C', 240, 306, '0.1u'), ('C', 340, 56, '500p'), ('C', 712, 126, '1u'),
                             ('R', 252, 263, '820R'), ('L', 302, 262, '25u'), ('R', 613, 293, '1k5'), ('L', 668, 289, '25u'), ('L', 772, 208, '1M'),
                             ('L', 538, 116, '5n')):
        s.val(kind, x, y, text)
    s.path('next_power', 'M800,142 V158 L822,150 Z', '#8c7a55', 1.5, s.lin(0, 142, 0, 158, [(0, '#d9b866'), (1, '#8a6d2c')]))
    return s


def tweed_5e3_power():
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
    bulb(s, 420, 110, 1.0, 'PA', 'POWER_A', label='6V6GT')
    bulb(s, 420, 320, 1.0, 'PB', 'POWER_B', flip=True, label='6V6GT')
    for kind, x, y, text in (('C', 56, 191, '0.1u'), ('L', 162, 80, '56k'), ('C', 208, 126, '0.1u'), ('C', 208, 256, '0.1u'), ('L', 162, 306, '1k5'), ('L', 162, 354, '56k'),
                             ('C', 338, 98, '1k5'), ('C', 338, 308, '1k5'), ('L', 312, 84, '220k'), ('L', 312, 346, '220k'), ('R', 453, 254, '250R'), ('L', 502, 250, '25u')):
        s.val(kind, x, y, text)
    speaker(s, 800, 190, 'p')
    return s


def netlist_values(amp_id):
    """name -> value of every resistor and capacitor of the amp's preamp netlist."""
    out = {}
    with open(os.path.join(ROOT, amp_id, 'preamp.net'), encoding='utf-8') as f:
        for ln in f:
            t = ln.split()
            if len(t) >= 5 and t[0] in ('R', 'C') and not ln.lstrip().startswith('*') and t[4] not in ('1', '1e12'):
                out[t[1]] = t[4]
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
            s.val(kind, x, y, text)
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
            s.val(kind, x, y, text)
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
            s.val(kind, x, y, text)
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
            s.val(kind, x, y, text)
    s.path('next_power', 'M1030,267 V283 L1052,275 Z', '#8c7a55', 1.5, s.lin(0, 267, 0, 283, [(0, '#d9b866'), (1, '#8a6d2c')]))
    return s


AMPS = {'ac30_normal': (ac30_preamp, None),
        'hiwatt_dr103': (hiwatt_preamp, None),
        'orange_or120': (orange_preamp, None),
        'tweed_5e3': (tweed_5e3_preamp, tweed_5e3_power),
        'plexi_1959': (lambda: two_channel_preamp('plexi_1959'), None),
        'jtm45': (lambda: two_channel_preamp('jtm45'), None),
        'bassman_5f6a': (lambda: two_channel_preamp('bassman_5f6a'), None)}

# The British 800's drawings were made in VectorCraft: its values are added to the exported files (the anchors replace the ones already there).
INJECT = {
    'circuit.svg': (('C', 100, 176, '68k'), ('L', 202, 92, '100k'), ('L', 362, 92, '100k'), ('L', 202, 262, '2.7k'), ('L', 362, 262, '820R'), ('C', 438, 110, '33k'),
                    ('C', 240, 100, '0.022u')),
    'circuit_power.svg': (('C', 56, 164, '0.022u'),),
}


def inject(amp_id, values):
    import re
    for name, vals in values.items():
        path = os.path.join(ROOT, amp_id, 'face', name)
        with open(path, encoding='utf-8', newline='') as f:
            text = f.read()
        text = re.sub(r'\s*<ellipse id="val[LRC]_\d+_[^"]*"[^>]*/>', '', text)
        anchors = ''.join('\n  <ellipse id="val%s_%d_%s" cx="%g" cy="%g" rx="1" ry="1" fill="none" stroke="none"/>' % (k, i + 1, t, x, y) for i, (k, x, y, t) in enumerate(vals))
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
    inject('jcm800_2203', INJECT)
    print('values added to jcm800_2203')


if __name__ == '__main__':
    main()
