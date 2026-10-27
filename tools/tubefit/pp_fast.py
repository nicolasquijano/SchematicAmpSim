import numpy as np
from pp_bench import e1, tube, PARAMS

def pair_fast(p, bplus, vg2s, rg2, vbias, vpk, raa, rk=None, n=256):
    """Vectorised version. rk: cathode resistor (bypassed): bias follows the average current. Returns po, ia, ig2, thd, bias."""
    t = np.arange(n) / n
    s = vpk * np.sin(2 * np.pi * t)
    a = np.zeros(n); b = np.zeros(n); vg2 = np.full(n, vg2s); bias = vbias
    for outer in range(30 if rk else 1):
        for it in range(400):
            v1 = bplus - (a - b) * raa / 4.0; v2 = bplus + (a - b) * raa / 4.0
            ia, ga = tube(p, -bias + s, v1, vg2); ib, gb = tube(p, -bias - s, v2, vg2)
            vg2n = vg2s - rg2 * (ga + gb)
            na = 0.5 * a + 0.5 * ia; nb = 0.5 * b + 0.5 * ib
            err = np.abs(na - a).max() + np.abs(nb - b).max()
            a, b, vg2 = na, nb, 0.5 * vg2 + 0.5 * vg2n
            if err < 1e-8: break
        if rk:
            newb = rk * np.mean(a + b + ga + gb)
            if abs(newb - bias) < 1e-4: bias = newb; break
            bias = 0.5 * bias + 0.5 * newb
    vfull = (a - b) * raa / 2.0
    po = np.mean(vfull ** 2) / raa
    f = np.fft.rfft(vfull) / n * 2
    thd = np.sqrt(np.sum(np.abs(f[2:12]) ** 2)) / max(np.abs(f[1]), 1e-9) * 100
    return dict(po=po, ia=1000 * np.mean(a + b), ig2=1000 * np.mean(ga + gb), thd=thd, bias=bias)

if __name__ == '__main__':
    import time; t0 = time.time()
    p = PARAMS['6L6GC (current)']
    print(pair_fast(p, 450, 400, 0, 37.0, 35.0, 5600), time.time() - t0)
    p = PARAMS['EL84 (current)']
    z = pair_fast(p, 300, 300, 0, 0, 0.0, 8000, rk=130); m = pair_fast(p, 300, 300, 0, 0, 12.0, 8000, rk=130)
    print('EL84 zero', z); print('EL84 12V', m)
    for vpk in (8, 10, 12, 14, 16): print(vpk, pair_fast(p, 300, 300, 0, 0, vpk, 8000, rk=130))
