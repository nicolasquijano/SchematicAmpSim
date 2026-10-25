"""Fit the Koren pentode plate current to digitised curves at one screen voltage.
Ip = 2/kg1 * E1^ex * atan(Ua/kvb), E1 = Ug2/kp * ln(1 + exp(kp*(1/mu + Ug1/Ug2))); Ig2 = E1^ex/kg2."""
import numpy as np
from fit_triode import lm

def ip_mA(p, ug1, ua, ug2):
    mu, ex, kg1, kp, kvb = p
    x = kp*(1.0/mu + ug1/ug2)
    sp = np.where(x > 30, x, np.log1p(np.exp(np.minimum(x, 30))))
    e1 = ug2/kp*sp
    return np.where(e1 > 0, 1000.0*2.0/kg1*np.power(np.maximum(e1, 1e-12), ex)*np.arctan(ua/kvb), 0.0)

def e1_of(p, ug1, ug2):
    mu, ex, kg1, kp, kvb = p
    x = kp*(1.0/mu + ug1/ug2); sp = np.log1p(np.exp(x)) if x < 30 else x
    return ug2/kp*sp

def samples(curves, ug2, step=15.0, floor_ua=0.0, drop=()):
    pts = []
    for ug1, c in curves.items():
        if ug1 in drop: continue
        c = c[np.argsort(c[:, 0])]
        for ua in np.arange(max(c[0, 0], floor_ua) + 10, c[-1, 0] - 5, step):
            pts.append((ug1, ua, float(np.interp(ua, c[:, 0], c[:, 1]))))
    return np.array(pts)

def fit(data, ug2, ex, floor=10.0, start=None):
    ug1, ua, I = data.T; w = 1.0/(I + floor)
    best = None
    for mu0 in (5, 8, 11, 15):
        for kg in (300, 800, 1500):
            for kp0 in (20, 60, 150):
                for kvb0 in (10, 40, 100):
                    full = lambda q: np.array([q[0], ex, q[1], q[2], q[3]])
                    res = lambda q: (ip_mA(full(q), ug1, ua, ug2) - I)*w
                    q, c = lm(res, np.array([mu0, kg, kp0, kvb0], float), np.array([1.5, 0.0, 0.0, 0.0]))
                    if best is None or c < best[1]: best = (full(q), c)
    return best
