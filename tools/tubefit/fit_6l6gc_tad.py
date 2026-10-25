import numpy as np
from fit_pentode import ip_mA
from fit_triode import lm
curves = np.load('tad_6l6_curves.npy', allow_pickle=True).item()
pts = []
for ug1, c in curves.items():
    c = c[np.argsort(c[:,0])]
    for ua in np.arange(max(c[0,0], 240)+5, 420, 10.0):
        if c[0,0] <= ua <= c[-1,0]:
            I = float(np.interp(ua, c[:,0], c[:,1]))
            if I > 0.8: pts.append((ug1, ua, ua - 3.0, I))
data = np.array(pts); ug1, ua, ug2, I = data.T
typ = (-18.0, 350.0, 250.0, 54.0, 5.4)
def run(ex, floor, mu_fixed=None):
    def full(q): return np.array([mu_fixed if mu_fixed else q[0], ex, q[-3], q[-2], q[-1]])
    def res(q):
        p = full(q)
        r = (ip_mA(p, ug1, ua, ug2) - I)/(I + floor)
        t1, ta, tg2, tI, tgm = typ
        ti = (float(ip_mA(p, t1, ta, tg2)) - tI)/tI*5.0
        h = 0.01; gm = (float(ip_mA(p, t1+h, ta, tg2)) - float(ip_mA(p, t1-h, ta, tg2)))/(2*h)
        return np.concatenate([r, [ti, (gm - tgm)/tgm*3.0]])
    best = None
    starts = [(mu0, kg, kp0, kvb0) for mu0 in (6, 10) for kg in (300, 1000, 2500) for kp0 in (20, 80) for kvb0 in (5, 40)]
    for mu0, kg, kp0, kvb0 in starts:
        q0 = np.array([kg, kp0, kvb0], float) if mu_fixed else np.array([mu0, kg, kp0, kvb0], float)
        lo = np.zeros(3) if mu_fixed else np.array([1.5, 0, 0, 0])
        q, c = lm(res, q0, lo)
        if best is None or c < best[1]: best = (q, c)
    return full(best[0])
for floor in (3.0, 8.0, 15.0):
    for ex in (1.2, 1.35, 1.5):
        p = run(ex, floor)
        pred = ip_mA(p, ug1, ua, ug2); zone = (I > 15) & (I < 70)
        t1, ta, tg2, tI, tgm = typ
        print('floor %4.1f ex %.2f: mu %.2f kg1 %.0f kp %.1f kvb %.1f | rms err in 15..70 mA zone %.1f%%, all points %.1f%% | typical Ia %.1f' % (floor, ex, p[0], p[2], p[3], p[4], 100*np.sqrt((((pred-I)/I)[zone]**2).mean()), 100*np.sqrt((((pred-I)/(I+3))**2).mean()), float(ip_mA(p, t1, ta, tg2))))
