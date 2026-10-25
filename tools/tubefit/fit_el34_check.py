import numpy as np, pymupdf
from pdfcurves import *
from fit_pentode import ip_mA, e1_of, samples
from fit_triode import lm
p = pymupdf.open('jj_el34.pdf')[0]; dr = p.get_drawings(); subs = subpaths(dr[568])
IA = lambda x: (381.1-x)*1.6
UG = lambda y: -(y-367.4)*0.3125
conds = {2: (250.0, 250.0), 1: (350.0, 375.0), 0: (400.0, 425.0)}   # subpath -> (Ua, Ug2)
tr = []
for k, (ua, ug2) in conds.items():
    pl = polyline(subs[k]); ug = UG(pl[:,1]); ia = IA(pl[:,0]); o = np.argsort(ug); ug, ia = ug[o], ia[o]
    for u in np.arange(ug.min()+1.5, ug.max()-1.5, 3.0):
        I = float(np.interp(u, ug, ia))
        if 3.0 < I < 170.0: tr.append((u, ua, ug2, I))
tr = np.array(tr); print(len(tr), 'transfer points')
curves = np.load('el34_curves.npy', allow_pickle=True).item()
pc = samples(curves, 250.0, drop=[0.0]); pc = np.column_stack([pc[:,0], pc[:,1], np.full(len(pc), 250.0), pc[:,2]])
data = np.vstack([pc, tr]); ug1, ua, ug2, I = data.T
typ = (-13.5, 250.0, 265.0, 100.0, 11.0)
def run(ex, mu_fixed):
    full = lambda q: np.array([mu_fixed if mu_fixed else q[0], ex, q[-3], q[-2], q[-1]])
    def res(q):
        pp = full(q); r = (ip_mA(pp, ug1, ua, ug2) - I)/(I + 8.0)
        t1, ta, tg2, tI, tgm = typ
        ti = (float(ip_mA(pp, t1, ta, tg2)) - tI)/tI*6.0
        h = 0.01; gm = (float(ip_mA(pp, t1+h, ta, tg2)) - float(ip_mA(pp, t1-h, ta, tg2)))/(2*h)
        return np.concatenate([r, [ti, (gm - tgm)/tgm*3.0]])
    best = None
    for mu0 in (8, 11, 14):
        for kg in (300, 800, 1500):
            for kp0 in (20, 60, 150):
                for kvb0 in (10, 40):
                    q0 = np.array([kg, kp0, kvb0], float) if mu_fixed else np.array([mu0, kg, kp0, kvb0], float)
                    q, c = lm(res, q0, np.zeros(3) if mu_fixed else np.array([1.5, 0, 0, 0]))
                    if best is None or c < best[1]: best = (q, c)
    return full(best[0])
for mu_fixed in (11.0, None):
    for ex in (1.2, 1.35, 1.5):
        pp = run(ex, mu_fixed)
        rp = np.sqrt((((ip_mA(pp, pc[:,0], pc[:,1], pc[:,2]) - pc[:,3])/(pc[:,3]+8))**2).mean())
        rt = np.sqrt((((ip_mA(pp, tr[:,0], tr[:,1], tr[:,2]) - tr[:,3])/(tr[:,3]+8))**2).mean())
        t1, ta, tg2, tI, tgm = typ; E = float(e1_of(pp, t1, tg2))**ex
        print('mu %s ex %.2f: plate rms %.3f, transfer(Ug2 250/375/425) rms %.3f | mu %.2f kg1 %.0f kp %.1f kvb %.1f | typical %.1f mA | kg2 %.0f' % ('fixed' if mu_fixed else 'free ', ex, rp, rt, pp[0], pp[2], pp[3], pp[4], float(ip_mA(pp, t1, ta, tg2)), E/14.9e-3))
stock = np.array([11.0, 1.35, 845.0, 2600.0, 60.0, 24.0])[[0,1,2,4,5]]
print('stock: transfer rms %.3f, plate rms %.3f' % (np.sqrt((((ip_mA(stock, tr[:,0], tr[:,1], tr[:,2]) - tr[:,3])/(tr[:,3]+8))**2).mean()), np.sqrt((((ip_mA(stock, pc[:,0], pc[:,1], pc[:,2]) - pc[:,3])/(pc[:,3]+8))**2).mean())))
np.save('el34_transfer_points.npy', tr)
