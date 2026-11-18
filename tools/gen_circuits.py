#!/usr/bin/env python3
"""Draws the circuit page's schematics for the amps that have none and writes their "probes" into amp.json.

    py tools/gen_circuits.py            draw every amp without a hand-made drawing and add its probes
    py tools/gen_circuits.py --svg-only draw the pictures again and leave amp.json (and its calibrated probes) alone
    py tools/gen_circuits.py --force    draw them again (hand-made ones, listed in HAND_MADE, are never touched)
    py tools/gen_circuits.py --calibrate cal.txt   set each probe's full_scale from the CAL lines the [.calibrate] test prints

The drawings are the simplified signal path of the amp: the guitar, one tube per stage of the preamp (found in its netlists), the pots of the panel, then
the power amp (phase inverter, output tubes, output transformer, speaker and the feedback pots). The look is that of the British 800's, drawn by hand in
VectorCraft (amps/jcm800_2203/face/circuit*.svg). Wires and glows carry the ids the plugin colours (see plugin/CircuitPage.cpp).
"""
import json
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'amps')
HAND_MADE = {'jcm800_2203', 'tweed_5e3', 'plexi_1959', 'jtm45', 'bassman_5f6a', 'orange_or120', 'hiwatt_dr103', 'ac30_normal', 'deluxe_reverb', 'twin_65', 'bassbreaker_1830'}  # drawn by hand (VectorCraft) or by circuits_hand.py
NO_DRAWING = {'nam_loader', 'gk_800rb', 'jazz_clean_120'}  # no tubes: the page shows blocks
MAX_TUBES = 8
POWER_PARAMS = {'presence'}

WIRE = '#6b5c44'


class Svg:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.defs, self.body, self.n = [], [], 0

    def gid(self):
        self.n += 1
        return 'g%d' % self.n

    def lin(self, x1, y1, x2, y2, stops):
        i = self.gid()
        s = ''.join('<stop offset="%s" stop-color="%s"%s/>' % (o, c, (' stop-opacity="%s"' % op) if op is not None else '') for o, c, op in
                    [(t + (None,))[:3] if len(t) == 2 else t for t in stops])
        self.defs.append('<linearGradient id="%s" x1="%g" y1="%g" x2="%g" y2="%g" gradientUnits="userSpaceOnUse">%s</linearGradient>' % (i, x1, y1, x2, y2, s))
        return 'url(#%s)' % i

    def rad(self, cx, cy, r, stops):
        i = self.gid()
        s = ''.join('<stop offset="%s" stop-color="%s"%s/>' % (o, c, (' stop-opacity="%s"' % op) if op is not None else '') for o, c, op in
                    [(t + (None,))[:3] if len(t) == 2 else t for t in stops])
        self.defs.append('<radialGradient id="%s" cx="%g" cy="%g" r="%g" gradientUnits="userSpaceOnUse">%s</radialGradient>' % (i, cx, cy, r, s))
        return 'url(#%s)' % i

    def add(self, s):
        self.body.append(s)

    def val(self, kind, x, y, text):
        """A component value the plugin writes in small type: an invisible anchor, `kind` L (starts at x), R (ends at x) or C (centred on x)."""
        self.nv = getattr(self, 'nv', 0) + 1
        self.add('<ellipse id="val%s_%d_%s" cx="%g" cy="%g" rx="1" ry="1" fill="none" stroke="none"/>' % (kind, self.nv, text, x, y))

    def path(self, name, d, stroke=WIRE, w=3.5, fill='none', extra=''):
        self.add('<path id="%s" d="%s" fill="%s" stroke="%s" stroke-width="%g" stroke-linecap="round" stroke-linejoin="round"%s/>' % (name, d, fill, stroke, w, extra))

    def ellipse(self, name, cx, cy, rx, ry, fill, stroke='none', sw=1):
        self.add('<ellipse id="%s" cx="%g" cy="%g" rx="%g" ry="%g" fill="%s" stroke="%s" stroke-width="%g"/>' % (name, cx, cy, rx, ry, fill, stroke, sw))

    def rect(self, name, x, y, w, h, fill, stroke='none', sw=1, rx=0):
        self.add('<rect id="%s" x="%g" y="%g" width="%g" height="%g" rx="%g" fill="%s" stroke="%s" stroke-width="%g"/>' % (name, x, y, w, h, rx, fill, stroke, sw))

    def text(self):
        return ('<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n<defs>\n%s\n</defs>\n%s\n</svg>\n'
                % (self.w, self.h, self.w, self.h, '\n'.join(self.defs), '\n'.join(self.body)))


# ---- parts -------------------------------------------------------------------------------------------------------------------------------

def resistor_v(s, cx, y0, name):
    f = s.lin(cx - 7, 0, cx + 7, 0, [(0, '#e6c98c'), (1, '#a88a52')])
    s.rect(name, cx - 7, y0, 14, 36, f, '#5a4630', 1, 3)
    s.path(name + '_bands', ' '.join('M%g,%g H%g' % (cx - 7, y0 + k, cx + 7) for k in (8, 14, 20)), '#7a3b1f', 3)


def resistor_h(s, x0, cy, name):
    f = s.lin(0, cy - 7, 0, cy + 7, [(0, '#e6c98c'), (1, '#a88a52')])
    s.rect(name, x0, cy - 7, 36, 14, f, '#5a4630', 1, 3)
    s.path(name + '_bands', ' '.join('M%g,%g V%g' % (x0 + k, cy - 7, cy + 7) for k in (8, 14, 20)), '#7a3b1f', 3)


def cap(s, cx, cy, name):
    f = s.lin(cx - 6, 0, cx + 6, 0, [(0, '#f0d27a'), (1, '#a67c2e')])
    s.rect(name + 'a', cx - 6, cy - 18, 4, 36, f, '#5a4630', 0.8, 1)
    s.rect(name + 'b', cx + 2, cy - 18, 4, 36, f, '#5a4630', 0.8, 1)


def ground(s, cx, y, w=8):
    s.path('ground_%d_%d' % (cx, y), 'M%g,%g H%g M%g,%g H%g M%g,%g H%g' % (cx - w, y, cx + w, cx - w * .6, y + 5, cx + w * .6, cx - w * .25, y + 10, cx + w * .25), '#8c7a55', 2)


def jack(s, cx, cy):
    s.ellipse('jack_in', cx, cy, 13, 13, s.rad(cx, cy, 13, [(0, '#d9d0b8'), (1, '#5f5846')]), '#9a8760', 1.5)
    s.ellipse('jack_hole', cx, cy, 5, 5, '#14110d', '#3b3329', 1)


def bulb(s, cx, cy, k, tube_id, glow_id, flip=False, pi=False, label=None):
    """A tube: the glass, the plate(s), the grid(s), the glowing filament and the base. `k` scales it. flip: plate at the bottom."""
    r = 32 * k
    sg = -1 if flip else 1
    s.ellipse('tube_%s' % (tube_id if label is None else '%s-%s' % (tube_id, label)), cx, cy, r, r,
              s.rad(cx, cy, r, [(0, '#463d30'), (1, '#14110d')]), '#a08b63', 2)
    if pi:
        for off in (-1, 1):
            s.rect('plate_%s%d' % (tube_id, off), cx - 12 * k, cy + off * 20 * k - 3 * k, 24 * k, 6 * k, '#9a917d', '#5a5446', 1, 1)
        s.path('grids_%s' % tube_id, 'M%g,%g H%g M%g,%g H%g' % (cx - 16 * k, cy - 9 * k, cx + 16 * k, cx - 16 * k, cy + 9 * k, cx + 16 * k), '#cdbd92', 1.8, extra=' stroke-dasharray="3 3"')
        gy = cy
        gh = 9 * k
    else:
        s.rect('plate_%s' % tube_id, cx - 12 * k, cy - sg * 22 * k - 3 * k, 24 * k, 6 * k, '#9a917d', '#5a5446', 1, 1)
        s.path('grid_%s' % tube_id, 'M%g,%g H%g' % (cx - 18 * k, cy, cx + 18 * k), '#cdbd92', 1.8, extra=' stroke-dasharray="3 3"')
        gy = cy + sg * 16 * k
        gh = 10 * k
    gf = s.rad(cx, gy, 16 * k, [(0, '#ffb347', 0.95), (1, '#c1441a', 0)])
    s.ellipse('glow_%s' % glow_id, cx, gy, 16 * k, gh, gf)
    s.path('filament_%s' % tube_id, 'M%g,%g H%g' % (cx - 8 * k, gy, cx + 8 * k), '#ffae42', 2.6)
    s.path('shine_%s' % tube_id, 'M%g,%g A%g,%g 0 0 1 %g,%g' % (cx - 22 * k, cy - 12 * k, 26 * k, 26 * k, cx - 6 * k, cy - 26 * k), '#ffffff', 3, extra=' opacity="0.35"')
    if not pi:
        by = cy + (r - 4 * k if not flip else -r - 10 * k)
        bf = s.lin(0, by, 0, by + 14 * k, [(0, '#3a332a'), (1, '#16130f')])
        s.rect('base_%s' % tube_id, cx - 16 * k, by, 32 * k, 14 * k, bf, '#5a4d3a', 1, 3)


def pot(s, cx, cy, param, k=1.0, value=0.5):
    r = 18 * k
    s.ellipse('pot_%s' % param, cx, cy, r, r, s.rad(cx, cy, r, [(0, '#5b5246'), (1, '#1a1612')]), '#9a8760', 1.5)
    s.ellipse('dial_%s' % param, cx, cy, 11 * k, 11 * k, '#26211c', '#3b3329', 1)
    a = (-135 + 270 * value) * 3.14159265 / 180
    s.path('ptr_%s' % param, 'M%g,%g L%g,%g' % (cx, cy, cx + 11 * k * math_sin(a), cy - 11 * k * math_cos(a)), '#f0b53a', 2.6)


def math_sin(a):
    import math
    return math.sin(a)


def math_cos(a):
    import math
    return math.cos(a)


def speaker(s, cx, y, name):
    mf = s.lin(0, y, 0, y + 10, [(0, '#6b6350'), (1, '#2a251d')])
    s.rect('spk_magnet_%s' % name, cx - 10, y, 20, 10, mf, '#9a8760', 1.5, 2)
    cf = s.lin(0, y + 10, 0, y + 52, [(0, '#4a4338'), (1, '#16130f')])
    s.path('spk_cone%s' % ('_p' if name == 'p' else ''), 'M%g,%g H%g L%g,%g H%g Z' % (cx - 10, y + 10, cx + 10, cx + 24, y + 52, cx - 24), '#9a8760', 1.5, cf)
    s.ellipse('spk_dust_%s' % name, cx, y + 45, 10, 3, '#0d0c0a')


# ---- the amp -------------------------------------------------------------------------------------------------------------------------------

def read_amp(amp_id):
    path = os.path.join(ROOT, amp_id, 'amp.json')
    with open(path, encoding='utf-8', newline='') as f:
        raw = f.read()
    return path, raw, json.loads(raw)


def netlist_lines(amp_id, name):
    p = os.path.join(ROOT, amp_id, name)
    if not os.path.exists(p):
        return []
    with open(p, encoding='utf-8') as f:
        return [ln.strip() for ln in f if ln.strip() and not ln.lstrip().startswith('*')]


def stage_active(jacks_of_stage, when, jack_name, positions):
    """The preamp's own rule: a stage is solved when the jack is one of its jacks (all, when it names none) and every selector it names is at a listed position."""
    if jacks_of_stage and jack_name not in jacks_of_stage:
        return False
    return all(positions.get(name, 0) in listed for name, listed in (when or {}).items())


def channels_of(amp_id, d):
    """The channels of the preamp as the circuit page shows them: [{label, files, jacks | selector+position}]. By the 'channel' selector when the amp has one,
    else by input jack (jacks that run the same stages, or share a 'chN_' prefix, are one channel). One entry (or none) when the preamp has no channels."""
    pre = d['preamp']
    jacks = pre.get('jacks', [])
    stages = pre.get('stages', [])
    main = pre['netlist']

    def files_for(jack_name, positions):
        out = [main] if stage_active(pre.get('main_jacks'), pre.get('main_when_selector'), jack_name, positions) else []
        out += [st['netlist'] for st in stages if stage_active(st.get('jacks'), st.get('when_selector'), jack_name, positions)]
        return out

    chan_sel = next((s for s in pre.get('selectors', []) if s['name'] == 'channel'), None)
    if chan_sel and len(chan_sel['positions']) > 1:
        res = []
        for p, pos in enumerate(chan_sel['positions']):
            files = files_for(jacks[0]['name'] if jacks else '', {'channel': p})
            if res and res[-1]['files'] == files:  # neighbouring positions that run the same stages are one channel
                res[-1]['positions'].append(p)
                res[-1]['label'] += ' / ' + pos['label'].upper()
            else:
                res.append({'label': pos['label'].upper(), 'selector': 'channel', 'positions': [p], 'files': files})
    elif len(jacks) > 1:
        groups = {}
        for i, j in enumerate(jacks):
            m = re.match(r'^(ch\d+)_', j['name'])
            key = (tuple(files_for(j['name'], {})), m.group(1) if m else None)
            groups.setdefault(key, {'jacks': [], 'files': list(key[0]), 'prefix': key[1], 'label': (key[1] or j.get('label') or j['name']).upper()})['jacks'].append(i)
        if not key[1]:
            g = groups[key]
            g['label'] = ' / '.join((jacks[i2].get('label') or jacks[i2]['name']).upper() for i2 in g['jacks'])
        res = list(groups.values())
    else:
        return []
    if len(res) < 2 or len({tuple(c['files']) for c in res}) < 2:  # all the same circuit: one drawing
        return []
    return res


def preamp_tubes(amp_id, d, files=None):
    """The tubes of the preamp in signal order: [(label, probe node)]. One per physical tube (V1A and V1B are the V1); a cathode follower is
    probed at its cathode, since its plate is the rail."""
    pre = d['preamp']
    if files is None:
        files = [pre['netlist']] + [st['netlist'] for st in pre.get('stages', [])]
    rail_node = None
    first = netlist_lines(amp_id, pre['netlist'])
    for ln in first:
        t = ln.split()
        if t[0] == 'V' and t[1] == pre.get('rail_source', 'VRAIL'):
            rail_node = t[2]
            rail_volts = float(t[-1]) if re.match(r'^[\d.]+$', t[-1]) else 300.0
    groups = {}
    order = []
    for fn in files:
        for ln in netlist_lines(amp_id, fn):
            t = ln.split()
            if t[0] != 'T' or len(t) < 6:
                continue
            name = t[1]
            m = re.match(r'^(V\d+)[A-Za-z]?$', name)
            key = m.group(1) if m else name
            plate, cath = t[3], t[4]
            node = cath if plate == rail_node else plate
            if key not in groups:
                order.append(key)
            groups[key] = node
    if all(re.match(r'^V\d+$', k) for k in order):  # the tubes are numbered along the signal
        order.sort(key=lambda k: int(k[1:]))
    tubes = [(k, groups[k]) for k in order]
    if len(tubes) > MAX_TUBES:  # keep the first, the last and an even spread between
        pick = sorted(set(round(i * (len(tubes) - 1) / (MAX_TUBES - 1)) for i in range(MAX_TUBES)))
        tubes = [tubes[i] for i in pick]
    return tubes, (0.3 * rail_volts if rail_node else 120.0)


def power_info(amp_id, d):
    lines = netlist_lines(amp_id, d['power']['netlist'])
    pent = [ln.split() for ln in lines if ln.startswith('P ')]
    if not pent:
        return None
    t = pent[0]
    name = (d['power'].get('tubes') or [{'name': t[6] if len(t) > 6 else 'PWR'}])[0]['name']
    return {'grid': t[2], 'plate': t[3], 'tube': re.sub(r'[^A-Za-z0-9]', '', name)}


def classify_pots(d):
    pre, power = [], []
    for u in d['ui']:
        if u.get('power_knob') or u['param'] in POWER_PARAMS:
            power.append(u)
        else:
            pre.append(u)
    return pre, power


VALUE = re.compile(r'^[0-9.]+[kKMRunp]?$')


def component_values(amp_id, d):
    """Resistors and capacitors of the preamp's netlists: [(kind, node1, node2, value)], with the rail's node name."""
    pre = d['preamp']
    files = [pre['netlist']] + [st['netlist'] for st in pre.get('stages', [])]
    comps, rail = [], None
    for fn in files:
        for ln in netlist_lines(amp_id, fn):
            t = ln.split()
            if t[0] == 'V' and len(t) > 2 and t[1] == pre.get('rail_source', 'VRAIL') and rail is None:
                rail = t[2]
            if t[0] in ('R', 'C') and len(t) >= 5 and VALUE.match(t[4]) and t[4] != '1':  # 1 ohm is a switch contact
                comps.append((t[0], t[2], t[3], t[4]))
    return comps, rail


def tube_values(amp_id, d, label, comps, rail):
    """What surrounds a tube in the netlist: its plate load, its cathode resistor and the capacitor that takes the plate on, as drawn values."""
    pre = d['preamp']
    files = [pre['netlist']] + [st['netlist'] for st in pre.get('stages', [])]
    plate = cath = grid = None
    for fn in files:
        for ln in netlist_lines(amp_id, fn):
            t = ln.split()
            if t[0] == 'T' and len(t) >= 6:
                m = re.match(r'^(V\d+)[A-Za-z]?$', t[1])
                if (m.group(1) if m else t[1]) == label:
                    grid, plate, cath = t[2], t[3], t[4]
    out = {}
    if plate is None:
        return out
    for kind, a, b, v in comps:
        ends = {a, b}
        if kind == 'R' and plate in ends and rail in ends and 'plate' not in out:
            out['plate'] = v
        if kind == 'R' and cath in ends and ends & {'0', 'gnd'} and 'cath' not in out:
            out['cath'] = v
        if kind == 'C' and plate in ends and not ends & {'0', 'gnd', cath, rail} and 'couple' not in out:
            out['couple'] = v
        if kind == 'C' and cath in ends and ends & {'0', 'gnd'} and 'bypass' not in out:
            out['bypass'] = v
        if kind == 'R' and grid in ends and ends & {'0', 'gnd'} and 'leak' not in out:
            out['leak'] = v
        if kind == 'R' and grid in ends and not ends & {'0', 'gnd', cath, rail} and 'stopper' not in out:
            out['stopper'] = v
    return out


def draw_preamp(amp_id, d, tubes, pots=None):
    s = Svg(840, 415)
    n = len(tubes)
    W = min(150.0, 560.0 / n)
    k = min(1.0, W / 96.0)
    r = 32 * k
    cy = 175
    x0 = 160.0
    cxs = [x0 + i * W for i in range(n)]
    top = cy - r
    yj = top - 28
    base_bottom = cy + r - 4 * k + 14 * k
    pre_pots = pots if pots is not None else classify_pots(d)[0]
    bus = 300
    npot = len(pre_pots)
    pk = 1.0 if npot <= 8 else 0.8
    px = [420.0] if npot == 1 else [60 + i * (720.0 / (npot - 1)) for i in range(npot)] if npot else []
    pcy = 335
    s.path('rail', 'M%g,40 H%g' % (cxs[0] - 40, cxs[-1] + 40), '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
    # wires first: the parts sit on top of them
    s.path('wire_INPUT', 'M43,%g H%g' % (cy, cxs[0] - r))
    for i, (label, _) in enumerate(tubes):
        cx = cxs[i]
        yr = yj - 6                    # the bottom of the plate resistor
        path = 'M%g,%g V%g' % (cx, yr, top)
        if i + 1 < n:
            xc = cx + W / 2
            path += ' M%g,%g H%g M%g,%g H%g V%g H%g' % (cx, yj, xc - 4, xc + 4, yj, xc + 14, cy, cxs[i + 1] - r)
        s.path('wire_%s' % label, path)
    s.path('wire_static_rail', ' '.join('M%g,40 V%g' % (cx, yj - 42) for cx in cxs), '#7a6a4c', 3)
    last = cxs[-1]
    outx = last + 70
    out = 'M%g,%g H%g V%g H%g' % (last, yj, outx, bus, 800)
    for x in px:
        out += ' M%g,%g V%g' % (x, bus, pcy - 18 * pk)
    s.path('wire_OUT', out)
    # cathodes
    cath = ''
    for cx in cxs:
        cath += 'M%g,%g V%g M%g,%g V%g ' % (cx, base_bottom, base_bottom + 8, cx, base_bottom + 44, base_bottom + 54)
    s.path('wire_static_gnd', cath, '#5a4d3a', 3)
    for cx in cxs:
        ground(s, int(cx), int(base_bottom + 54))
    for x in px:
        s.path('wire_static_pot_%d' % x, 'M%g,%g V%g' % (x, pcy + 18 * pk, pcy + 18 * pk + 10), '#5a4d3a', 3)
        ground(s, int(x), int(pcy + 18 * pk + 10))
    # parts
    jack(s, 30, cy)
    resistor_h(s, 66, cy, 'res_in')
    comps, rail = component_values(amp_id, d)
    first = [ln.split() for fn in [d['preamp']['netlist']] for ln in netlist_lines(amp_id, fn) if ln.split()[0] == 'R' and ln.split()[2] == 'in'
             and len(ln.split()) >= 5 and VALUE.match(ln.split()[4]) and ln.split()[4] != '1']
    if first:
        s.val('C', 84, cy - 14, first[0][4].replace('u', '\u00b5'))
    for i, (label, _) in enumerate(tubes):
        cx = cxs[i]
        resistor_v(s, cx, yj - 42, 'res_plate%d' % i)
        resistor_v(s, cx, base_bottom + 8, 'res_cath%d' % i)
        if i + 1 < n:
            cap(s, cx + W / 2, yj, 'cap%d' % i)
        bulb(s, cx, cy, k, label, label)
        v = tube_values(amp_id, d, label, comps, rail)
        if 'plate' in v:
            s.val('L', cx + 10, yj - 24, v['plate'])
        if 'cath' in v:
            s.val('L', cx + 10, base_bottom + 26, v['cath'])
        if 'bypass' in v:
            s.val('L', cx + 10, base_bottom + 40, v['bypass'])
        if 'couple' in v and i + 1 < n:
            s.val('C', cx + W / 2, yj - 24, v['couple'])
        if i > 0 and ('stopper' in v or 'leak' in v):  # what sits in front of the grid: the stopper in series, the leak to ground
            s.val('C', cx - r - 20, cy - 12, '/'.join(x for x in (v.get('stopper'), v.get('leak')) if x))
    for i, u in enumerate(pre_pots):
        pot(s, px[i], pcy, u['param'], pk)
    # the way on to the power amp
    s.path('next_power', 'M806,%g V%g L826,%g Z' % (bus - 8, bus + 8, bus), '#8c7a55', 1.5, s.lin(0, bus - 8, 0, bus + 8, [(0, '#d9b866'), (1, '#8a6d2c')]))
    return s


def draw_power(amp_id, d, info):
    s = Svg(840, 415)
    _, pots = classify_pots(d)
    tube = info['tube']
    # the wires
    s.path('wire_OUT-2', 'M20,190 H52 M60,190 H98')
    s.path('wire_PHASE', 'M130,158 V110 H298 M130,222 V270 H298')
    s.path('wire_POWER', 'M330,78 V56 H560 V100 H580 M330,302 V324 H560 V280 H580 M580,100 a15,15 0 0 1 0,30 a15,15 0 0 1 0,30 a15,15 0 0 1 0,30 a15,15 0 0 1 0,30 a15,15 0 0 1 0,30 a15,15 0 0 1 0,30')
    npot = len(pots)
    px = [700 - 80 * i for i in range(npot)]
    sp = 'M640,130 H740 V170 M700,130 V%d M640,250 V268 M640,130 a15,15 0 0 0 0,30 a15,15 0 0 0 0,30 a15,15 0 0 0 0,30 a15,15 0 0 0 0,30' % (270 if npot else 345)
    s.path('wire_SPEAKER', sp)
    ret = 'M330,152 V228 M330,190 H360 V200 M580,190 H520 M200,345 V190 H162'
    if npot:
        ret += ' M%g,270 H700' % px[-1]
        for x in px:
            ret += ' M%g,270 V292 M%g,328 V345' % (x, x)
        ret += ' M%g,345 H200' % px[0]
        ret += ' M%g,345 H%g' % (px[-1], px[0])
    else:
        ret += ' M700,345 H200'
    s.path('wire_static_power', ret, '#5a4d3a', 3)
    s.path('ground_power', 'M352,200 H368 M355,205 H365 M358,210 H362 M632,268 H648 M635,273 H645 M638,278 H642', '#8c7a55', 2)
    s.path('ot_core', 'M606,95 V285 M614,95 V285', '#8c7a55', 3)
    s.ellipse('rail_tap', 520, 190, 4, 4, '#7a6a4c')
    cap(s, 56, 190, 'cap_in')
    bulb(s, 130, 190, 1.0, 'PI', 'PHASE', pi=True, label='PI')
    bulb(s, 330, 110, 1.0, 'PA', 'POWER_A', label=tube)
    bulb(s, 330, 270, 1.0, 'PB', 'POWER_B', flip=True, label=tube)
    for i, u in enumerate(pots):
        pot(s, px[i], 310, u['param'], 1.0)
    speaker(s, 740, 170, 'p')
    return s


def probes_text(tubes, fs_tube, power, nl, indent='  '):
    def row(label, node, fs):
        return '%s  { "label": "%s", "node": "%s", "full_scale": %s }' % (indent, label, node, fs)
    parts = [row(l, n, round(fs_tube)) for l, n in tubes]
    parts.append(row('OUT', '@OUT', 10))
    s = indent + '"probes": [' + nl + (',' + nl).join(parts) + nl + indent + '],' + nl
    if power:
        s += indent + '"power_probes": [' + nl + row('PHASE', power['grid'], 60) + ',' + nl + row('POWER', power['plate'], 350) + nl + indent + '],' + nl
    return s


def pots_for_channel(d, ch):
    """The preamp pots that do something in a channel: with a channel selector, the ones whose "active_when" lists the channel (or has none)."""
    pots = classify_pots(d)[0]
    if ch.get('selector'):
        pots = [u for u in pots if ch['selector'] not in (u.get('active_when') or {}) or set(ch['positions']) & set(u['active_when'][ch['selector']])]
    return pots


CHAIN_AMPS = {'american_5153', 'superlead_100', 'lead_c_plus', 'triplestack_3ch', 'british_dual100', 'jcm900_2100', 'highgain_120', 'german_ultra', 'german_trio',
              'modbe_100', 'jcm800_2203kk', 'afd100', 'svt_bass300', 'boutique_od'}  # drawn from the netlist as a chain with its networks (gen_chain.py)


def draw_any(amp_id, d, files, tubes, pots):
    """The chain drawing for the amps in CHAIN_AMPS (when their netlists give one), else the plain row of tubes."""
    if amp_id in CHAIN_AMPS:
        try:
            import gen_chain
            return gen_chain.Chain(amp_id, d, files, pots).s
        except Exception as e:  # the netlists do not read as a chain: the plain drawing
            print('  %s: plain drawing (%s: %s)' % (amp_id, type(e).__name__, e))
    return draw_preamp(amp_id, d, tubes, pots)


def write_drawings(amp_id, d, probe_tubes, power):
    """circuit.svg (circuit_1.svg... for further channels) and circuit_power.svg. Returns the channels, [] for an amp with one drawing."""
    face = os.path.join(ROOT, amp_id, 'face')
    os.makedirs(face, exist_ok=True)
    names = {label for label, _ in probe_tubes}
    chans = channels_of(amp_id, d)
    drawings = []
    all_files = [d['preamp']['netlist']] + [st['netlist'] for st in d['preamp'].get('stages', [])]
    if not chans:
        drawings.append(draw_any(amp_id, d, all_files, probe_tubes, classify_pots(d)[0]))
    else:
        for ch in chans:
            tubes, _ = preamp_tubes(amp_id, d, ch['files'])
            tubes = [t for t in tubes if t[0] in names] or probe_tubes
            drawings.append(draw_any(amp_id, d, ch['files'], tubes, pots_for_channel(d, ch)))
    for i in range(1, 8):  # drawings of an earlier run with more channels
        stale = os.path.join(face, 'circuit_%d.svg' % i)
        if i >= len(drawings) and os.path.exists(stale):
            os.remove(stale)
    for i, svg in enumerate(drawings):
        with open(os.path.join(face, 'circuit.svg' if i == 0 else 'circuit_%d.svg' % i), 'w', encoding='utf-8', newline='\n') as f:
            f.write(svg.text())
    if power:
        with open(os.path.join(face, 'circuit_power.svg'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(draw_power(amp_id, d, power).text())
    return chans


def channels_text(chans, nl, indent='  '):
    rows = []
    for ch in chans:
        if ch.get('selector'):
            rows.append('%s  { "label": "%s", "selector": "%s", "positions": [%s] }' % (indent, ch['label'], ch['selector'], ', '.join(str(x) for x in ch['positions'])))
        else:
            rows.append('%s  { "label": "%s", "jacks": [%s] }' % (indent, ch['label'], ', '.join(str(j) for j in ch['jacks'])))
    return indent + '"circuit_channels": [' + nl + (',' + nl).join(rows) + nl + indent + '],' + nl


def set_channels_json(path, raw, d, chans):
    """Writes "circuit_channels" into amp.json when it is not there (or has changed)."""
    if not chans and 'circuit_channels' not in d:
        return
    nl = '\r\n' if '\r\n' in raw else '\n'
    if 'circuit_channels' in d:
        raw = re.sub(r'\n[ \t]*"circuit_channels"\s*:\s*\[.*?\],[ \t]*\r?\n', '\n', raw, count=1, flags=re.S)
    if chans:
        m = re.search(r'(\r?\n)([ \t]*)"preamp"\s*:\s*\{', raw)
        raw = raw[:m.start() + len(m.group(1))] + channels_text(chans, nl) + raw[m.start() + len(m.group(1)):]
    json.loads(raw)
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(raw)


def process(amp_id, force, svg_only=False):
    path, raw, d = read_amp(amp_id)
    if svg_only:
        tubes, _ = preamp_tubes(amp_id, d)
        if not tubes or d.get('nam'):
            return None
        keep = {p['label'] for p in d.get('probes', [])}
        tubes = [t for t in tubes if t[0] in keep] or tubes
        chans = write_drawings(amp_id, d, tubes, power_info(amp_id, d))
        set_channels_json(path, raw, d, chans)
        return 'svg only, %d channels' % len(chans)
    if d.get('nam') or 'probes' in d and not force:
        return None
    nl = '\r\n' if '\r\n' in raw else '\n'
    tubes, fs_tube = preamp_tubes(amp_id, d)
    if not tubes:
        return 'no tubes'
    power = power_info(amp_id, d)
    chans = write_drawings(amp_id, d, tubes, power)
    out_node = d['preamp']['output_node']
    text = probes_text(tubes, fs_tube, power, nl).replace('@OUT', out_node)
    if 'probes' in d:  # --force: drop the old ones
        raw = re.sub(r'\n[ \t]*"probes"\s*:\s*\[.*?\],[ \t]*\r?\n[ \t]*("power_probes"\s*:\s*\[.*?\],[ \t]*\r?\n)?', '\n', raw, count=1, flags=re.S)
    m = re.search(r'(\r?\n)([ \t]*)"preamp"\s*:\s*\{', raw)
    if not m:
        return 'no preamp block'
    raw = raw[:m.start() + len(m.group(1))] + text + raw[m.start() + len(m.group(1)):]
    json.loads(raw)
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(raw)
    set_channels_json(path, raw, json.loads(raw), chans)
    return '%d tubes%s, %d channels' % (len(tubes), ', power' if power else '', len(chans))


def calibrate(cal_path):
    """CAL lines: `CAL <amp> <label> <ratio> <full_scale>` from a loud signal: the new full_scale puts that peak at 0.35 (about -9 dB: the presets with more gain than the defaults, and the hardest picks, still have room before the limit)."""
    updates = {}
    for ln in open(cal_path, encoding='utf-8'):
        if not ln.startswith('CAL '):
            continue
        _, amp, label, ratio, fs = ln.split()
        ratio, fs = float(ratio), float(fs)
        if ratio * fs > 0.5:  # a stage that hardly swings (another channel's, a node that is not on the signal) keeps its guess
            updates.setdefault(amp, {})[label] = min(fs * ratio / 0.35, {'PHASE': 130.0, 'POWER': 450.0}.get(label, 1e9))  # a plate cannot swing past the supply
    for amp, labels in updates.items():
        if amp in HAND_MADE:
            continue
        path, raw, d = read_amp(amp)
        for label, fs in labels.items():
            digits = 2 - int(__import__('math').floor(__import__('math').log10(fs))) if fs > 0 else 0
            v = round(fs, digits)
            v = int(v) if v == int(v) else v
            raw = re.sub(r'("label"\s*:\s*"%s"\s*,\s*"node"\s*:\s*"[^"]*"\s*,\s*"full_scale"\s*:\s*)[0-9.eE+-]+' % re.escape(label), r'\g<1>%s' % v, raw)
        json.loads(raw)
        with open(path, 'w', encoding='utf-8', newline='') as f:
            f.write(raw)
        print('calibrated', amp, ', '.join('%s=%g' % (k, round(v, 1)) for k, v in labels.items()))


def main():
    if '--calibrate' in sys.argv:
        calibrate(sys.argv[sys.argv.index('--calibrate') + 1])
        return
    force = '--force' in sys.argv
    svg_only = '--svg-only' in sys.argv
    for amp_id in sorted(os.listdir(ROOT)):
        if not os.path.exists(os.path.join(ROOT, amp_id, 'amp.json')) or amp_id in HAND_MADE or amp_id in NO_DRAWING:
            continue
        try:
            r = process(amp_id, force, svg_only)
        except Exception as e:  # keep going: report the amp
            r = 'ERROR %s: %s' % (type(e).__name__, e)
        print('%-18s %s' % (amp_id, r if r is not None else 'skipped'))


if __name__ == '__main__':
    main()
