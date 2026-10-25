"""Fit the Koren triode model to digitised plate curves ({grid volts: array of (plate volts, mA)}).
usage: python fit_triode.py curves.npy [floor_mA] [typical_vp typical_vg]"""
import sys
import numpy as np

def triode_mA(p, vg, vp):
    mu, ex, kg1, kp, kvb = p
    s = np.sqrt(kvb + vp*vp)
    x = kp*(1.0/mu + vg/s)
    sp = np.where(x > 30, x, np.log1p(np.exp(np.minimum(x, 30))))
    e1 = vp/kp*sp
    return np.where(e1 > 0, 1000.0*np.power(np.maximum(e1, 1e-12), ex)/kg1, 0.0)

def samples(curves, step, floor):
    pts = []
    for vg, c in curves.items():
        c = c[np.argsort(c[:, 1])]
        for I in np.arange(floor, c[:, 1].max() - 0.5*step, step):
            pts.append((vg, float(np.interp(I, c[:, 1], c[:, 0])), I))
    return np.array(pts)

def lm(res, q, lo, iters=400):
    lam = 1e-2
    r = res(q); cost = (r*r).sum()
    for _ in range(iters):
        J = np.zeros((len(r), len(q)))
        for j in range(len(q)):
            d = np.zeros(len(q)); d[j] = 1e-5*max(1.0, abs(q[j])); J[:, j] = (res(q + d) - r)/d[j]
        A = J.T@J; g = J.T@r; ok = False
        for _ in range(12):
            qn = q + np.linalg.solve(A + lam*np.diag(np.diag(A) + 1e-12), -g)
            if np.any(qn <= lo): lam *= 10; continue
            rn = res(qn); cn = (rn*rn).sum()
            if cn < cost: q, r, cost = qn, rn, cn; lam = max(lam/3, 1e-9); ok = True; break
            lam *= 5
        if not ok: break
    return q, cost

def fit_fixed_ex(data, ex, floor):
    vg, vp, I = data.T; w = 1.0/(I + floor)
    best = None
    for mu0 in (40, 60, 80, 100, 120):
        for kg in (200, 400, 800):
            for kvb0 in (30, 300, 3000):
                full = lambda q: np.array([q[0], ex, q[1], q[2], q[3]])
                res = lambda q: (triode_mA(full(q), vg, vp) - I)*w
                q, c = lm(res, np.array([mu0, kg, 600.0, kvb0]), np.array([5.0, 0.0, 0.0, -1.0]))
                if best is None or c < best[1]: best = (full(q), c)
    return best

def report(p, typ_vp, typ_vg):
    ip = float(triode_mA(p, typ_vg, typ_vp)); h = 1e-3
    gm = (float(triode_mA(p, typ_vg + h, typ_vp)) - float(triode_mA(p, typ_vg - h, typ_vp)))/(2*h)
    gp = (float(triode_mA(p, typ_vg, typ_vp + 0.01)) - float(triode_mA(p, typ_vg, typ_vp - 0.01)))/0.02
    return ip, gm, 1.0/gp, gm/gp

if __name__ == '__main__':
    curves = np.load(sys.argv[1], allow_pickle=True).item()
    floor = float(sys.argv[2]) if len(sys.argv) > 2 else 0.1
    tvp = float(sys.argv[3]) if len(sys.argv) > 3 else 250.0
    tvg = float(sys.argv[4]) if len(sys.argv) > 4 else -2.0
    data = samples(curves, 0.05 if floor < 0.2 else 0.25, floor)
    vg, vp, I = data.T
    print('%d points' % len(data))
    for ex in (1.2, 1.3, 1.4, 1.5):
        p, c = fit_fixed_ex(data, ex, floor)
        rel = np.sqrt((((triode_mA(p, vg, vp) - I)/(I + floor))**2).mean())
        ip, gm, ra, mu = report(p, tvp, tvg)
        print('ex=%.1f: rel.rms %.3f  mu %.1f kg1 %.0f kp %.0f kvb %.0f | at %g V / %g V: Ip %.2f mA gm %.2f mA/V ra %.1fk mu %.0f' % (ex, rel, p[0], p[2], p[3], p[4], tvp, tvg, ip, gm, ra/1.0, mu))
