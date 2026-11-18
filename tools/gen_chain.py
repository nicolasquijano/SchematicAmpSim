#!/usr/bin/env python3
"""A preamp drawn from its netlist as a chain: the tubes in signal order, and between them the network that really connects them (the series path from one
plate to the next grid, with what hangs off it to ground: grid leaks, tone-stack pots and caps, bypasses). Used by gen_circuits.py for the amps in CHAIN_AMPS.

How it reads a channel's netlists:
  * the stages are joined as amp.json says (a source driven from a node of an earlier stage makes the two nodes one);
  * the supply (the rail and the nodes behind a resistor and a capacitor to ground) and the tubes' own terminals are not part of the signal path;
  * from the input, the nearest unused grid is the next tube; its plate (its cathode for a cathode follower) is where the next network starts;
  * the shortest path between two tubes is the series part; every other element at a node of that path hangs from it (up to three in a row) and ends
    at ground or is left open;
  * pots (amp.json knobs) are drawn as knobs and are live: pot_<param>.
A long chain wraps into rows (the wire that carries on goes along the top of the next row), so it never gets wider than MAXW.
"""
import re
from collections import deque

from gen_circuits import Svg, bulb, cap, ground, jack, netlist_lines, pot, resistor_h, resistor_v, component_values

GND = {'0', 'gnd'}
OPEN = {'1e12', '1G', '1e9'}
CY, R, YJ, BASE_BOTTOM = 215, 32, 155, 257
OP_BOX = 66  # the room an op-amp's feedback network takes to the left of its triangle
MAXW, ROW_H = 1500, 460
W_R, W_C, W_POT, W_SHORT, W_NODE = 84, 56, 76, 12, 44  # the room of a resistor, a capacitor, a knob, a closed switch and of each branch at a node


def cap_h(s, cx, cy, name):
    f = s.lin(0, cy - 6, 0, cy + 6, [(0, '#f0d27a'), (1, '#a67c2e')])
    s.rect(name + 'a', cx - 12, cy - 6, 24, 4, f, '#5a4630', 0.8, 1)
    s.rect(name + 'b', cx - 12, cy + 2, 24, 4, f, '#5a4630', 0.8, 1)


def diode(s, cx, cy, key, anode_first, horizontal):
    """A diode: the triangle (it glows with the signal that reaches it: diode_<stage>_<n>) and the bar at its cathode. `anode_first`: the anode is the
    top (or left) end."""
    s.n += 1
    sg = 1 if anode_first else -1
    if horizontal:
        tri = 'M%g,%g V%g L%g,%g Z' % (cx - 6 * sg, cy - 8, cy + 8, cx + 7 * sg, cy)
        bar = 'M%g,%g V%g' % (cx + 9 * sg, cy - 9, cy + 9)
    else:
        tri = 'M%g,%g H%g L%g,%g Z' % (cx - 8, cy - 6 * sg, cx + 8, cx, cy + 7 * sg)
        bar = 'M%g,%g H%g' % (cx - 9, cy + 9 * sg, cx + 9)
    s.path('diode_%s_%d' % (key, s.n), tri, '#cdbd92', 1.5, '#3a332a')
    s.path('diode_bar_%d' % s.n, bar, '#cdbd92', 2)


def switch(s, cx, cy, name, closed_default, horizontal):
    """A switch with its two blades: swc_<1|0>_<name> (closed) and swo_<1|0>_<name> (open); the page shows the one the amp's setting asks for."""
    d = 1 if closed_default else 0
    if horizontal:
        a, b = (cx - 12, cy), (cx + 12, cy)
        open_tip = (cx + 9, cy - 11)
    else:
        a, b = (cx, cy - 11), (cx, cy + 11)
        open_tip = (cx + 11, cy + 5)
    for p in (a, b):
        s.ellipse('sw_pin_%s_%d_%d' % (name, p[0], p[1]), p[0], p[1], 2.6, 2.6, '#cdbd92')
    s.path('swc_%d_%s' % (d, name), 'M%g,%g L%g,%g' % (a[0], a[1], b[0], b[1]), '#f0b53a', 2.4)
    s.path('swo_%d_%s' % (d, name), 'M%g,%g L%g,%g' % (a[0], a[1], open_tip[0], open_tip[1]), '#8c7a55', 2.4)


def anchor(s, param, x, y):
    s.ellipse('lbl_%s' % param, x, y, 1, 1, 'none', 'none', 0)


class Net:
    def __init__(self, amp_id, d, files):
        pre = d['preamp']
        self.pre = pre
        self.elems, self.tubes, self.rail = [], [], None
        names = set()
        for fn in files:
            for ln in netlist_lines(amp_id, fn):
                t = ln.split()
                if t[0] == 'V' and len(t) > 2 and t[1] == pre.get('rail_source', 'VRAIL') and self.rail is None:
                    self.rail = t[2]
                if t[0] in ('R', 'C') and len(t) >= 5 and t[1] not in names:
                    names.add(t[1])
                    self.elems.append({'type': t[0], 'name': t[1], 'a': t[2], 'b': t[3], 'val': t[4]})
                if t[0] == 'D' and len(t) >= 4 and t[1] not in names:  # a diode: anode, cathode, model
                    names.add(t[1])
                    self.elems.append({'type': 'D', 'name': t[1], 'a': t[2], 'b': t[3], 'val': t[4] if len(t) > 4 else ''})
                if t[0] == 'O' and len(t) >= 7 and not any(x['name'] == t[1] for x in self.tubes):  # an op-amp: + input (the "grid"), - input (the "cathode"), output (the "plate")
                    inv = t[2] in GND and t[3] not in GND  # + input at ground: the signal comes in on the - input (an inverting stage)
                    self.tubes.append({'name': t[1], 'grid': t[3] if inv else t[2], 'plate': t[4], 'cath': t[2] if inv else t[3], 'model': t[6], 'key': t[1], 'opamp': True, 'inv': inv})
                if t[0] == 'T' and len(t) >= 6 and not any(x['name'] == t[1] for x in self.tubes):
                    m = re.match(r'^(V\d+)[A-Za-z]?$', t[1])
                    self.tubes.append({'name': t[1], 'grid': t[2], 'plate': t[3], 'cath': t[4], 'model': t[5].replace('ECC83', '12AX7'), 'key': m.group(1) if m else t[1]})
        # the stages are joined by sources driven from a node of an earlier stage: those two nodes are one
        alias = {}
        for st in pre.get('stages', []):
            if st['netlist'] not in files:
                continue
            links = list(st.get('links', []))
            if st.get('link_source'):
                links.append({'from': st.get('link_from'), 'source': st['link_source']})
            for lk in links:
                for ln in netlist_lines(amp_id, st['netlist']):
                    t = ln.split()
                    if t[0] == 'S' and len(t) > 2 and t[1] == lk['source']:
                        alias[t[2]] = lk['from']
        if alias:
            for e in self.elems:
                e['a'], e['b'] = alias.get(e['a'], e['a']), alias.get(e['b'], e['b'])
            for t in self.tubes:
                for k in ('grid', 'plate', 'cath'):
                    t[k] = alias.get(t[k], t[k])
        self.supply = self._supply()
        # the switches the amp sets (the resistors its jacks and selectors name): drawn as switches, open or closed as they are set
        self.settable = set()
        for j in pre.get('jacks', []):
            self.settable |= set((j.get('resistors') or {}).keys())
        for sd in pre.get('selectors', []):
            for pos in sd.get('positions', []):
                self.settable |= set((pos.get('resistors') or {}).keys())
        # the knobs: pot section -> (parameter, role)
        self.pots, self.partner = {}, {}
        ui = {u.get('knob'): u['param'] for u in d['ui'] if u.get('knob')}
        for k in pre.get('knobs', []):
            param = ui.get(k['name'])
            p = k['pot']
            sections = [(p['top'], 'T'), (p['bottom'], 'B')] if p['type'] == 'wiper' else [(p['resistor'], 'V')]
            for n, role in sections:
                self.pots.setdefault(n, (param, role))
            for extra in p.get('also', []):  # the ganged copies turn with the same knob
                for n, role in ((extra.get('top'), 'T'), (extra.get('bottom'), 'B'), (extra.get('resistor'), 'V')):
                    if n:
                        self.pots.setdefault(n, (param, role))
            if p['type'] == 'wiper':
                self.partner[p['top']] = p['bottom']
                self.partner[p['bottom']] = p['top']

    def _supply(self):
        sup = {self.rail} if self.rail else set()
        grew = True
        while grew:
            grew = False
            for e in self.elems:
                if e['type'] != 'R':
                    continue
                for a, b in ((e['a'], e['b']), (e['b'], e['a'])):
                    decoupled = any(c['type'] == 'C' and b in (c['a'], c['b']) and (c['a'] in GND or c['b'] in GND) for c in self.elems)
                    if a in sup and b not in sup and b not in GND and decoupled:
                        sup.add(b)
                        grew = True
        return sup

    def is_open(self, e):
        return e['val'] in OPEN

    def is_switch(self, e):
        return e['type'] == 'R' and e['name'] in self.settable and e['name'] not in self.pots

    def is_short(self, e):
        return e['type'] == 'R' and e['val'] == '1' and e['name'] not in self.pots and not e['name'].startswith('VR')

    def touching(self, n):
        return [e for e in self.elems if n in (e['a'], e['b']) and not self.is_open(e)]

    def out_node(self, t):
        return t['cath'] if t['plate'] in self.supply and not t.get('opamp') else t['plate']

    def blocked_for(self, goal):
        out = set(GND) | self.supply
        for t in self.tubes:
            out |= {t['plate'], t['cath']}
            if t['grid'] != goal:
                out.add(t['grid'])
        return out

    def bfs(self, start, goal, blocked):
        prev = {start: None}
        q = deque([start])
        while q:
            n = q.popleft()
            if n == goal:
                path = []
                while prev[n] is not None:
                    n0, e = prev[n]
                    path.append((e, n0, n))
                    n = n0
                return list(reversed(path))
            for e in self.touching(n):
                m = e['b'] if e['a'] == n else e['a']
                if m in prev or (m in blocked and m != goal):
                    continue
                prev[m] = (n, e)
                q.append(m)
        return None


def chain_of(net, start):
    cur, used, chain = start, set(), []
    while True:
        best = None
        for t in net.tubes:
            if t['name'] in used:
                continue
            p = net.bfs(cur, t['grid'], net.blocked_for(t['grid']) - {cur})
            if p is not None and (best is None or len(p) < len(best[1])):
                best = (t, p)
        if best is None:
            return chain, cur
        t, p = best
        used.add(t['name'])
        chain.append((t, p))
        cur = net.out_node(t)


def other(e, n):
    return e['b'] if e['a'] == n else e['a']


def branches_of(net, path, blocked, nodes=None):
    """What hangs off the nodes of a path: {node: [(elements, ends at ground)]}, at most three at a node."""
    if nodes is None:
        nodes = ([path[0][1]] + [n for _, _, n in path]) if path else []
    on_path = {id(e) for e, _, _ in path}
    out = {}
    for n in nodes:
        found = []
        for e in net.touching(n):
            if id(e) in on_path or (net.is_short(e) and not net.is_switch(e)):
                continue
            m = other(e, n)
            if m in nodes or (m in blocked and m not in GND):
                continue
            seq, at, seen = [e], m, {n, m}
            while at not in GND and len(seq) < 3:
                nxt = [x for x in net.touching(at) if x not in seq and (not net.is_short(x) or net.is_switch(x)) and other(x, at) not in seen
                       and (other(x, at) not in blocked or other(x, at) in GND)]
                if not nxt:
                    break
                nxt.sort(key=lambda x: other(x, at) not in GND)
                x = nxt[0]
                seq.append(x)
                at = other(x, at)
                seen.add(at)
            found.append((seq, at in GND, n))
        for e in net.elems:  # the switches that are open in the netlist (the signal does not pass them, the drawing shows them)
            if net.is_switch(e) and net.is_open(e) and n in (e['a'], e['b']):
                m = other(e, n)
                if m in nodes or (m in blocked and m not in GND):
                    continue
                found.append(([e], m in GND, n))
        out[n] = found[:3]
    return out


def triangle(s, cx, cy, t):
    """An op-amp: a triangle pointing right, + above and - below on the input side; it glows with its level like a tube."""
    key = t['key']
    f = s.lin(cx - R, 0, cx + R, 0, [(0, '#3d4a52'), (1, '#161c20')])
    plus, minus = (cy + 14, cy - 14) if t.get('inv') else (cy - 14, cy + 14)
    s.path('tube_%s-%s' % (key, t['model']), 'M%g,%g L%g,%g L%g,%g Z' % (cx - R, cy - R, cx - R, cy + R, cx + R, cy), '#8fb4c4', 2, f)
    g = s.rad(cx - 4, cy, 24, [(0, '#7ee0ff', 0.9), (1, '#2a6f8a', 0)])
    s.path('glow_%s' % key, 'M%g,%g L%g,%g L%g,%g Z' % (cx - R + 6, cy - R + 11, cx - R + 6, cy + R - 11, cx + R - 10, cy), 'none', 0, g)
    s.path('opsign_%s' % key, 'M%g,%g H%g M%g,%g V%g M%g,%g H%g' % (cx - R + 5, plus, cx - R + 13, cx - R + 9, plus - 4, plus + 4, cx - R + 5, minus, cx - R + 13), '#cfe6ee', 1.6)


class Chain:
    def __init__(self, amp_id, d, files, pots):
        self.amp_id, self.d = amp_id, d
        self.net = Net(amp_id, d, files)
        pre = d['preamp']
        self.start = 'in' if any('in' in (e['a'], e['b']) for e in self.net.elems) else 'gin'
        self.chain, last = chain_of(self.net, self.start)
        if len(self.chain) < 2:
            raise ValueError('no chain')
        self.tail = self.net.bfs(last, pre['output_node'], self.net.blocked_for(None) - {last}) or []
        self.pots_left = list(pots)
        self.draw()

    def local(self, t):
        """The values around one triode: its plate load, its cathode resistor and the bypass across it."""
        net, out = self.net, {}
        for e in net.elems:
            ends = {e['a'], e['b']}
            if e['type'] == 'R' and t['plate'] in ends and ends & net.supply and 'plate' not in out:
                out['plate'] = e['val']
                out['plate_n'] = e['name']
            if e['type'] == 'R' and t['cath'] in ends and ends & GND and 'cath' not in out and not net.is_short(e):
                out['cath'] = e['val']
                out['cath_n'] = e['name']
            if e['type'] == 'C' and t['cath'] in ends and ends & GND and 'bypass' not in out:
                out['bypass'] = e['val']
                out['bypass_n'] = e['name']
        return out

    def edge_width(self, e):
        net = self.net
        if net.is_switch(e):
            return 44
        if net.is_short(e):
            return W_SHORT
        if e['name'] in net.pots and net.pots[e['name']][1] in ('T', 'V') and net.pots[e['name']][0]:
            return W_POT
        return W_R if e['type'] == 'R' else W_C

    def seg_width(self, path, br):
        nodes = ([path[0][1]] + [n for _, _, n in path]) if path else []
        w = 0
        for i, n in enumerate(nodes):
            w += max(16, W_NODE * len(br.get(n, [])))
            if i < len(path):
                w += self.edge_width(path[i][0])
        return w

    def draw(self):
        net = self.net
        # the segments: the network in front of each tube, and the one after the last
        segs, prev_key = [], 'INPUT'
        for t, p in self.chain:
            segs.append((prev_key, p, branches_of(net, p, net.blocked_for(t['grid']))))
            prev_key = t['key']
        segs.append((prev_key, self.tail, branches_of(net, self.tail, net.blocked_for(None)) if self.tail else {}))
        n_tubes = len(self.chain)
        row, seg_x, tube_x, wrapped = [0] * len(segs), [0] * len(segs), [0] * n_tubes, [False] * len(segs)
        x, r = 110.0, 0  # the first network starts clear of the jack and its name
        for i, (key, path, br) in enumerate(segs):
            w = self.seg_width(path, br)
            sx = x if i == 0 else x + R + 66  # and clear of the tube
            if i > 0 and sx + w + 2 * R + 60 > MAXW:  # a new row
                r += 1
                sx = 46.0
                wrapped[i] = True
            row[i], seg_x[i] = r, sx
            end = sx + w
            if i < n_tubes:
                tube_x[i] = end + 24 + R + (OP_BOX if self.chain[i][0].get('opamp') else 0)  # an op-amp has its feedback network beside it
                x = tube_x[i]
            else:
                x = end
        rows = r + 1
        width = int(max(max(seg_x[i] + self.seg_width(segs[i][1], segs[i][2]) for i in range(len(segs))), max(tube_x) + R) + 140)
        s = self.s = Svg(max(width, 700), (rows - 1) * ROW_H + 600)
        wires, statics, grounds, parts = {}, [], [], []
        rails = {}
        pot_count = {}

        def wire(key, dpath):
            wires.setdefault(key, []).append(dpath)

        def pot_param(name):
            param, _ = net.pots[name]
            n = pot_count.get(param, 0)
            pot_count[param] = n + 1
            return param, ('%s-%d' % (param, n + 1) if n else param), n == 0

        def hang(x0, y0, seq, end_gnd, top, key):
            """A branch hanging down from (x0, y0): its elements one under the other, to ground or open."""
            y = y0
            at = top
            for e in seq:
                name = e['name']
                info = net.pots.get(name)
                anode_top = e['a'] == at
                at = e['b'] if e['a'] == at else e['a']
                if info and info[1] == 'B':  # a pot's bottom section hangs with its top: the knob is drawn once
                    continue
                if e['type'] == 'D':
                    statics.append('M%g,%g V%g M%g,%g V%g' % (x0, y, y + 10, x0, y + 32, y + 42))
                    parts.append(('diode_v', x0, y + 21, key, anode_top))
                    y += 42
                elif net.is_switch(e):
                    statics.append('M%g,%g V%g M%g,%g V%g' % (x0, y, y + 8, x0, y + 34, y + 42))
                    parts.append(('sw_v', x0, y + 21, name, 0 if net.is_open(e) else 1))
                    y += 42
                elif info and info[0] and info[1] in ('T', 'V'):
                    param, pid, _ = pot_param(name)
                    statics.append('M%g,%g V%g M%g,%g V%g' % (x0, y, y + 8, x0, y + 36, y + 46))
                    parts.append(('pot', x0, y + 22, pid, param))
                    y += 46
                elif e['type'] == 'R':
                    parts.append(('res_v', x0, y + 6, name, e['val']))
                    statics.append('M%g,%g V%g M%g,%g V%g' % (x0, y, y + 6, x0, y + 42, y + 52))
                    y += 52
                else:
                    parts.append(('cap_h', x0, y + 18, name, e['val']))
                    statics.append('M%g,%g V%g M%g,%g V%g' % (x0, y, y + 12, x0, y + 24, y + 36))
                    y += 36
            if end_gnd:
                grounds.append((x0, y))
            else:
                parts.append(('dot', x0, y))

        def opamp(cx, oy, t):
            """An op-amp: the triangle, and under it the feedback loop (resistor and capacitor from the output to the - input) with what else hangs from the - input."""
            key = t['key']
            cy = CY + oy
            xr, xl = cx + R + 16, cx - R - OP_BOX
            yc, yf = cy + R + 30, cy + R + 72
            xm = (xl + xr) / 2
            neg = t['grid'] if t.get('inv') else t['cath']
            rf = next((e for e in net.elems if e['type'] == 'R' and {e['a'], e['b']} == {neg, t['plate']}), None)
            cf = next((e for e in net.elems if e['type'] == 'C' and {e['a'], e['b']} == {neg, t['plate']}), None)
            wire(key, 'M%g,%g H%g V%g H%g M%g,%g H%g V%g H%g' % (cx + R, cy, xr, yf, xm + 18, xm - 18, yf, xl, cy + 14, cx - R))
            if cf is not None:
                wire(key, 'M%g,%g V%g H%g M%g,%g H%g' % (xr, yf, yc, xm + 7, xm - 7, yc, xl))
                parts.append(('cap_f', xm, yc, cf['name'], cf['val']))
            parts.append(('res_f', xm - 18, yf, rf['name'] if rf else 'rf%s' % key, rf['val'] if rf else None))
            br = [] if t.get('inv') else branches_of(net, [], net.blocked_for(None) - {t['cath']}, nodes=[t['cath']]).get(t['cath'], [])
            if t.get('inv'):  # the + input is tied to ground
                statics.append('M%g,%g H%g V%g' % (cx - R, cy - 14, cx - R - 14, cy - 4))
                grounds.append((cx - R - 14, cy - 4))
            for j, (seq, gnd, top) in enumerate(br[:2]):
                hang(xl + j * 38, yf, seq, gnd, top, key)
            parts.append(('opamp', cx, cy, t))

        arrow = (0, 0)
        for i, (key, path, br) in enumerate(segs):
            oy = row[i] * ROW_H
            prev = self.chain[i - 1][0] if i > 0 else None
            prev_op = prev is not None and prev.get('opamp')
            prev_cf = prev is not None and prev['plate'] in net.supply and not prev_op
            x0 = tube_x[i - 1] + (R if prev_op else 0) if i > 0 else 0  # where the wire leaves the previous stage
            sx = seg_x[i]
            if i == 0:
                yo = CY
                wire('INPUT', 'M43,%g H%g' % (yo, sx))
            elif wrapped[i]:
                yo = YJ + oy
                oyp = row[i - 1] * ROW_H
                ypo = (CY if prev_op else BASE_BOTTOM + 6 if prev_cf else YJ) + oyp
                wire(key, 'M%g,%g H%g V%g H30 V%g H%g' % (x0, ypo, MAXW + 30, oy + 70, yo, sx))
            else:
                yo = (CY if prev_op else BASE_BOTTOM + 6 if prev_cf else YJ) + oy
                wire(key, 'M%g,%g H%g' % (x0, yo, sx))
            nodes = ([path[0][1]] + [n for _, _, n in path]) if path else []
            xcur = sx
            for k, n in enumerate(nodes):
                nb = br.get(n, [])
                slot = max(16, W_NODE * len(nb))
                wire(key, 'M%g,%g H%g' % (xcur, yo, xcur + slot))
                for j, (seq, gnd, top) in enumerate(nb):
                    hang(xcur + slot / max(1, len(nb)) * (j + 0.5), yo, seq, gnd, top, key)
                xcur += slot
                if k < len(path):
                    e = path[k][0]
                    w = self.edge_width(e)
                    xm = xcur + w / 2
                    info = net.pots.get(e['name'])
                    if net.is_switch(e):  # a closed switch in the path (the amp may open it)
                        wire(key, 'M%g,%g H%g M%g,%g H%g' % (xcur, yo, xm - 14, xm + 14, yo, xcur + w))
                        parts.append(('sw_h', xm, yo, e['name'], 1))
                    elif e['type'] == 'D':
                        wire(key, 'M%g,%g H%g M%g,%g H%g' % (xcur, yo, xm - 8, xm + 8, yo, xcur + w))
                        parts.append(('diode_h', xm, yo, key, e['a'] == path[k][1]))
                    elif net.is_short(e):
                        wire(key, 'M%g,%g H%g' % (xcur, yo, xcur + w))
                    elif info and info[0] and info[1] in ('T', 'V'):
                        param, pid, first = pot_param(e['name'])
                        wire(key, 'M%g,%g H%g M%g,%g H%g' % (xcur, yo, xm - 15, xm + 15, yo, xcur + w))
                        parts.append(('pot', xm, yo, pid, param))
                        if first:
                            parts.append(('lbl', param, xm, yo - 34))
                        pe = next((q for q in net.elems if q['name'] == net.partner.get(e['name'])), None)
                        if pe is not None and (pe['a'] in GND or pe['b'] in GND):
                            statics.append('M%g,%g V%g' % (xm, yo + 15, yo + 26))
                            grounds.append((xm, yo + 26))
                    elif e['type'] == 'R':
                        wire(key, 'M%g,%g H%g M%g,%g H%g' % (xcur, yo, xm - 18, xm + 18, yo, xcur + w))
                        parts.append(('res_h', xm - 18, yo, e['name'], e['val']))
                    else:
                        wire(key, 'M%g,%g H%g M%g,%g H%g' % (xcur, yo, xm - 6, xm + 6, yo, xcur + w))
                        parts.append(('cap_v', xm, yo, e['name'], e['val']))
                    xcur += w
            if i < n_tubes:
                tt = self.chain[i][0]
                cyi = CY + oy + ((14 if tt.get('inv') else -14) if tt.get('opamp') else 0)  # an op-amp takes the signal on its + input, above the middle (an inverting stage on the - input)
                if yo != cyi:
                    wire(key, 'M%g,%g H%g V%g H%g' % (xcur, yo, xcur + 12, cyi, tube_x[i] - R))
                else:
                    wire(key, 'M%g,%g H%g' % (xcur, yo, tube_x[i] - R))
            else:
                wire('OUT', 'M%g,%g H%g' % (xcur, yo, xcur + 60))
                arrow = (xcur + 60, yo)
        # the tubes
        for i, (t, _) in enumerate(self.chain):
            oy = row[i] * ROW_H
            if t.get('opamp'):
                opamp(tube_x[i], oy, t)
                continue
            cx, cf, key = tube_x[i], t['plate'] in net.supply, t['key']
            lst = rails.setdefault(row[i], [])
            lst.append(cx)
            v = self.local(t)
            if cf:
                statics.append('M%g,%g V%g' % (cx, 36 + oy, CY - R + oy))
                wire(key, 'M%g,%g V%g' % (cx, BASE_BOTTOM + oy, BASE_BOTTOM + 6 + oy))
            else:
                statics.append('M%g,%g V%g' % (cx, 36 + oy, YJ - 42 + oy))
                wire(key, 'M%g,%g V%g' % (cx, YJ - 6 + oy, CY - R + oy))
                parts.append(('res_v', cx, YJ - 42 + oy, 'rp%d' % i, v.get('plate'), v.get('plate_n')))
            parts.append(('tube', cx, CY + oy, t))
            yk = BASE_BOTTOM + 8 + oy
            bypass = None if cf else v.get('bypass')
            statics.append('M%g,%g V%g' % (cx, BASE_BOTTOM + (6 if cf else 0) + oy, yk))
            if not bypass:
                parts.append(('res_v', cx, yk, 'rk%d' % i, v.get('cath'), v.get('cath_n')))
                statics.append('M%g,%g V%g' % (cx, yk + 36, yk + 46))
                grounds.append((cx, yk + 46))
            else:
                statics.append('M%g,%g H%g M%g,%g V%g M%g,%g V%g' % (cx - 15, yk, cx + 15, cx - 15, yk, yk + 10, cx + 15, yk, yk + 18))
                statics.append('M%g,%g V%g H%g M%g,%g V%g' % (cx - 15, yk + 46, yk + 58, cx + 15, cx + 15, yk + 30, yk + 58))
                parts.append(('res_v', cx - 15, yk + 10, 'rk%d' % i, v.get('cath'), v.get('cath_n')))
                parts.append(('cap_h', cx + 15, yk + 24, 'ck%d' % i, bypass, v.get('bypass_n')))
                grounds.append((cx, yk + 58))
        # the knobs the netlist did not place: a row of their own under the drawing
        left = [u for u in self.pots_left if u['param'] not in pot_count]
        py = (rows - 1) * ROW_H + 540
        for k, u in enumerate(left):
            parts.append(('pot', 70 + k * min(120, 1000 / max(1, len(left))), py, u['param'], u['param']))
        # draw
        real = {e['name']: e['type'] for e in net.elems}

        def comp(part):
            """The component a value belongs to, for the plugin to edit: "R:R12" (kind and name in the netlist), or None."""
            name = part[5] if len(part) > 5 else part[3]
            return '%s:%s' % (real[name], name) if name in real else None

        for rw, xs in rails.items():
            s.path('rail%d' % rw, 'M%g,%g H%g' % (min(xs) - 40, 36 + rw * ROW_H, max(xs) + 40), '#7a6a4c', 2, extra=' stroke-dasharray="6 5"')
        for key, ds in wires.items():
            s.path('wire_%s' % key, ' '.join(ds))
        s.path('wire_static', ' '.join(statics), '#5a4d3a', 3)
        for gx, gy in grounds:
            ground(s, int(gx), int(gy))
        jack(s, 30, CY)
        for part in parts:
            kind = part[0]
            if kind == 'res_v':
                resistor_v(s, part[1], part[2], part[3])
                if part[4]:
                    s.val('L', part[1] + 11, part[2] + 18, part[4], comp(part))
            elif kind == 'res_h':
                resistor_h(s, part[1], part[2], part[3])
                if part[4]:
                    s.val('C', part[1] + 18, part[2] - 14, part[4], comp(part))
            elif kind == 'res_f':  # the feedback resistor of an op-amp: its value goes under it (the capacitor is above)
                resistor_h(s, part[1], part[2], part[3])
                if part[4]:
                    s.val('C', part[1] + 18, part[2] + 22, part[4], comp(part))
            elif kind == 'cap_v':
                cap(s, part[1], part[2], part[3])
                s.val('C', part[1], part[2] - 26, part[4], comp(part))
            elif kind == 'cap_h':
                cap_h(s, part[1], part[2], part[3])
                s.val('L', part[1] + 15, part[2], part[4], comp(part))
            elif kind == 'pot':
                pot(s, part[1], part[2], part[3], 0.8)
            elif kind == 'lbl':
                anchor(s, part[1], part[2], part[3])
            elif kind == 'diode_v':
                diode(s, part[1], part[2], part[3], part[4], False)
            elif kind == 'diode_h':
                diode(s, part[1], part[2], part[3], part[4], True)
            elif kind == 'sw_v':
                switch(s, part[1], part[2], part[3], part[4], False)
            elif kind == 'sw_h':
                switch(s, part[1], part[2], part[3], part[4], True)
            elif kind == 'dot':
                s.ellipse('open_%d_%d' % (part[1], part[2]), part[1], part[2], 2.5, 2.5, '#7a6a4c')
            elif kind == 'tube':
                _, cx, cyt, t = part
                bulb(s, cx, cyt, 1.0, t['name'], t['key'], label=t['model'])
            elif kind == 'opamp':
                triangle(s, part[1], part[2], part[3])
            elif kind == 'cap_f':
                cap(s, part[1], part[2], part[3])
                if part[4]:
                    s.val('C', part[1], part[2] - 26, part[4], comp(part))
        ax, ay = arrow
        s.path('next_power', 'M%g,%g V%g L%g,%g Z' % (ax, ay - 8, ay + 8, ax + 22, ay), '#8c7a55', 1.5, s.lin(0, ay - 8, 0, ay + 8, [(0, '#d9b866'), (1, '#8a6d2c')]))
