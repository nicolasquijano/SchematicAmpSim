import numpy as np
from pp_bench import tube
from pp_fast import pair_fast
from fit_triode import lm

tr = np.load('p8_transfer.npy', allow_pickle=True).item()
pts = []
for ec2, a in tr.items():
    for v in np.arange(-62.0, -0.5, 2.0):
        if a[0, 0] <= v <= a[-1, 0]:
            I = float(np.interp(v, a[:, 0], a[:, 1]))
            if I > 1.0: pts.append((v, 400.0, ec2, I))
T = np.array(pts)
CA = [(-14.0, 250.0, 250.0, 72.0, 6.0, 22.5), (-12.5, 300.0, 200.0, 48.0, 5.3, 35.0), (-18.0, 350.0, 250.0, 54.0, 5.2, 33.0)]
EX = 1.35

def full(q): return (q[0], EX, q[1], q[4], q[2], q[3])   # q = mu kg1 kp kvb kg2
def deriv(p, u1, ua, u2):
    h = 0.01
    gm = (tube(p, u1 + h, ua, u2)[0] - tube(p, u1 - h, ua, u2)[0]) / (2 * h) * 1000
    gp = (tube(p, u1, ua + 0.1, u2)[0] - tube(p, u1, ua - 0.1, u2)[0]) / 0.2 * 1000
    return float(gm), float(1 / gp)
def table(p):
    z = pair_fast(p, 450, 400, 0, 37.0, 0.0, 5600); m = pair_fast(p, 450, 400, 0, 37.0, 35.0, 5600)
    return z, m
def residuals(q, wt=1.0):
    p = full(q)
    r = list((tube(p, T[:, 0], T[:, 1], T[:, 2])[0] * 1000 - T[:, 3]) / (T[:, 3] + 8.0))
    for u1, ua, u2, ib, gm, ra in CA:
        m = float(tube(p, u1, ua, u2)[0]) * 1000; g, rr = deriv(p, u1, ua, u2)
        r += [(m - ib) / ib * 2.0, (g - gm) / gm * 1.5, (rr - ra) / ra * 0.7]
    z, m = table(p)
    r += [wt * 3 * (z['ia'] - 116) / 116, wt * 3 * (m['ia'] - 210) / 210, wt * 4 * (m['po'] - 55) / 55,
          wt * 1.5 * (z['ig2'] - 5.6) / 5.6, wt * 1.5 * (m['ig2'] - 22) / 22]
    return np.array(r)
def report(name, q):
    p = full(q); z, m = table(p)
    tr_rms = np.sqrt(np.mean(((tube(p, T[:, 0], T[:, 1], T[:, 2])[0] * 1000 - T[:, 3]) / (T[:, 3] + 8.0)) ** 2))
    print('%s: transfer rms %.3f | table: Ia0 %.0f (116) Imax %.0f (210) Po %.1f (55) Ig2 %.1f/%.1f (5.6/22) THD %.1f' % (name, tr_rms, z['ia'], m['ia'], m['po'], z['ig2'], m['ig2'], m['thd']))
    for u1, ua, u2, ib, gm, ra in CA:
        g, rr = deriv(p, u1, ua, u2); print('    A %.0f/%.0f/%.1f Ib %.1f (%.0f) gm %.2f (%.1f) ra %.0fk (%.1f)' % (ua, u2, u1, float(tube(p, u1, ua, u2)[0]) * 1000, ib, g, gm, rr, ra))
cur = np.array([8.7, 1460.0, 48.0, 12.0, 4500.0])
report('current', cur)
import os, time
WT = float(os.environ['WT'])
q0 = np.array([46.05, 264.74, 23.17, 77.23, 1412.7])   # the weight-1 optimum
t0 = time.time()
q, c = lm(lambda x: residuals(x, WT), q0, np.array([2.0, 0, 0, 0, 0]), iters=25)
print('WT', WT, 'q (mu kg1 kp kvb kg2):', np.round(q, 2), 'cost %.3f' % c, 'time %.0fs' % (time.time() - t0))
report('fitted', q)
np.save('fit6l6_w%g.npy' % WT, q)
