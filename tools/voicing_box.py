#!/usr/bin/env python3
"""Shows the preamp parts a preamp voicing changes when the amp's circuit drawing has no value label for them.

    py tools/voicing_box.py            every amp
    py tools/voicing_box.py <amp id>   one amp

A row of the missing parts (the part and its value, bound to the netlist so the page can edit it and light it up) is added under each preamp drawing of the
amp (circuit.svg, circuit_1.svg ...). Running it again replaces the row, so a drawing that is later completed by hand (or regenerated) loses nothing: the
parts it then shows are left out of the row. The power drawings are not touched: gen_circuits.py / circuits_hand.py draw every part their voicings change.
"""
import json
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'amps')
ROW_H = 78


def preamp_parts(amp_id, d):
    """The parts the preamp voicings change: name -> (kind, value text), read from the preamp's netlists."""
    names = {n for v in d.get('preamp_voicings', []) for pos in v['positions'] for n in pos.get('factors', {})}
    files = [d['preamp']['netlist']] + [st['netlist'] for st in d['preamp'].get('stages', [])]
    out = {}
    for f in files:
        with open(os.path.join(ROOT, amp_id, f), encoding='utf-8') as fh:
            for ln in fh:
                t = ln.split()
                if len(t) >= 5 and t[0] in ('R', 'C') and t[1] in names and not ln.lstrip().startswith('*') and t[1] not in out:
                    out[t[1]] = (t[0], t[4])
    return out


def bound_names(text):
    return {m.group(1) for m in re.finditer(r'id="val[LRC]_\d+_[^"]*@[RC]:([^"]+)"', text)}


def part_svg(kind, name, value, cx, cy):
    """A resistor (a rounded bar with bands) or a capacitor (two plates) with its value under it, as a value label the page can bind."""
    if kind == 'R':
        body = ('<rect id="vb_%s" x="%g" y="%g" width="36" height="14" rx="3" fill="#cfae6e" stroke="#5a4630" stroke-width="1"/>'
                '<path d="M%g,%g V%g M%g,%g V%g M%g,%g V%g" stroke="#7a3b1f" stroke-width="3" fill="none"/>'
                % (name, cx - 18, cy - 7, cx - 10, cy - 7, cy + 7, cx - 4, cy - 7, cy + 7, cx + 2, cy - 7, cy + 7))
        wires = '<path d="M%g,%g H%g M%g,%g H%g" stroke="#6b5c44" stroke-width="3" stroke-linecap="round" fill="none"/>' % (cx - 34, cy, cx - 18, cx + 18, cy, cx + 34)
    else:
        body = ('<rect id="vb_%sa" x="%g" y="%g" width="4" height="30" rx="1" fill="#d9b866" stroke="#5a4630" stroke-width="0.8"/>'
                '<rect id="vb_%sb" x="%g" y="%g" width="4" height="30" rx="1" fill="#d9b866" stroke="#5a4630" stroke-width="0.8"/>'
                % (name, cx - 6, cy - 15, name, cx + 2, cy - 15))
        wires = '<path d="M%g,%g H%g M%g,%g H%g" stroke="#6b5c44" stroke-width="3" stroke-linecap="round" fill="none"/>' % (cx - 34, cy, cx - 6, cx + 6, cy, cx + 34)
    label = '<ellipse id="valC_%s_%s@%s:%s" cx="%g" cy="%g" rx="1" ry="1" fill="none" stroke="none"/>' % (name, value, kind, name, cx, cy + 26)
    return wires + body + label


def add_row(path, parts):
    with open(path, encoding='utf-8', newline='') as f:
        text = f.read()
    m = re.search(r'<g id="voicing_box" data-orig-h="(\d+)">.*?</g>\s*', text, flags=re.S)
    height_attr = re.search(r'<svg[^>]*\bheight="(\d+)"', text)
    base_h = int(m.group(1)) if m else int(height_attr.group(1))
    if m:
        text = text.replace(m.group(0), '')
    have = bound_names(text)
    missing = [(n, k, v) for n, (k, v) in parts.items() if n not in have]
    width = int(re.search(r'<svg[^>]*\bwidth="(\d+)"', text).group(1))
    if missing:
        y = base_h + 30
        step = min(130, (width - 80) / max(1, len(missing)))
        items = ''.join(part_svg(k, n, v, 60 + step * (i + 0.5), y) for i, (n, k, v) in enumerate(missing))
        box = '<g id="voicing_box" data-orig-h="%d"><path d="M20,%d H%d" stroke="#5a4d3a" stroke-width="1" stroke-dasharray="4 4" fill="none"/>%s</g>\n' % (base_h, base_h + 6, width - 20, items)
        text = text.replace('</svg>', box + '</svg>')
        new_h = base_h + ROW_H
    else:
        new_h = base_h
    text = re.sub(r'(<svg[^>]*\bheight=")\d+(")', r'\g<1>%d\2' % new_h, text, count=1)
    text = re.sub(r'(<svg[^>]*\bviewBox="0 0 \d+ )\d+(")', r'\g<1>%d\2' % new_h, text, count=1)
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(text)
    return [n for n, _, _ in missing]


def process(amp_id):
    with open(os.path.join(ROOT, amp_id, 'amp.json'), encoding='utf-8') as f:
        d = json.load(f)
    if not d.get('preamp_voicings') or d.get('nam'):
        return None
    parts = preamp_parts(amp_id, d)
    face = os.path.join(ROOT, amp_id, 'face')
    shown = []
    for fn in sorted(os.listdir(face)) if os.path.isdir(face) else []:
        if re.match(r'circuit(_\d+)?\.svg$', fn):
            shown += add_row(os.path.join(face, fn), parts)
    return sorted(set(shown))


def main():
    ids = sys.argv[1:] or sorted(a for a in os.listdir(ROOT) if os.path.exists(os.path.join(ROOT, a, 'amp.json')))
    for amp_id in ids:
        r = process(amp_id)
        if r:
            print('%-18s %s' % (amp_id, ', '.join(r)))


if __name__ == '__main__':
    main()
