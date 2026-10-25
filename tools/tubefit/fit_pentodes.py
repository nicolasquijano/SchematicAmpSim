import numpy as np
from fit_pentode import *
from fit_triode import lm
# (name, curves, Ug2 of the chart, mu from the data sheet, drop, typical (ug1, ua, ug2, Ia mA, gm mA/V))
cases = [('EL34', 'el34_curves.npy', 250.0, 11.0, [], (-13.5, 250.0, 265.0, 100.0, 11.0)),
         ('KT88', 'kt88_curves.npy', 300.0, 8.0, [-35], (-15.0, 250.0, 250.0, 140.0, 11.5))]
for name, f, ug2c, mu, drop, typ in cases:
    curves = np.load(f, allow_pickle=True).item()
    data = samples(curves, ug2c, drop=drop)
    ug1, ua, I = data.T
    t1, ta, tg2, tI, tgm = typ
    for ex in (1.2, 1.35, 1.5):
        def res(q):
            p = np.array([mu, ex, q[0], q[1], q[2]])
            r = (ip_mA(p, ug1, ua, ug2c) - I)/(I + 10.0)
            ti = (float(ip_mA(p, t1, ta, tg2)) - tI)/tI*6.0              # typical point, heavy weight
            h = 0.01; gm = (float(ip_mA(p, t1+h, ta, tg2)) - float(ip_mA(p, t1-h, ta, tg2)))/(2*h)
            tg = (gm - tgm)/tgm*3.0
            return np.concatenate([r, [ti, tg]])
        best = None
        for kg in (200, 500, 1000):
            for kp0 in (20, 50, 100):
                for kvb0 in (10, 40):
                    q, c = lm(res, np.array([kg, kp0, kvb0], float), np.array([0.0, 0.0, 0.0]))
                    if best is None or c < best[1]: best = (q, c)
        q = best[0]; p = np.array([mu, ex, q[0], q[1], q[2]])
        rel = float(np.sqrt((((ip_mA(p, ug1, ua, ug2c) - I)/(I + 10.0))**2).mean()))
        h = 0.01; gm = (float(ip_mA(p, t1+h, ta, tg2)) - float(ip_mA(p, t1-h, ta, tg2)))/(2*h)
        gp = (float(ip_mA(p, t1, ta+0.1, tg2)) - float(ip_mA(p, t1, ta-0.1, tg2)))/0.2
        E = e1_of(p, t1, tg2)
        print('%s ex %.2f: rel.rms %.3f  mu %.1f kg1 %.0f kp %.1f kvb %.1f | typical: Ia %.1f mA (%.0f) gm %.1f (%.1f) ri %.1fk E1^ex %.3f' % (name, ex, rel, mu, q[0], q[1], q[2], float(ip_mA(p, t1, ta, tg2)), tI, gm, tgm, 1.0/gp, E**ex))
