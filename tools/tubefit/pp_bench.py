"""Quasi-static push-pull pair with an ideal output transformer, to compare the pentode models with the data sheets'
typical-operation tables (power, zero-signal and maximum-signal currents)."""
import numpy as np

def e1(p, vg1, vg2):
    mu, ex, kg1, kg2, kp, kvb = p
    x = kp * (1.0 / mu + vg1 / np.maximum(vg2, 1e-6))
    sp = np.where(x > 30, x, np.log1p(np.exp(np.minimum(x, 30))))
    return vg2 / kp * sp

def tube(p, vg1, vp, vg2):
    """returns (ip, ig2) in amps; vg1 negative; grid current ignored."""
    mu, ex, kg1, kg2, kp, kvb = p
    E = e1(p, vg1, vg2)
    ip = 2.0 / kg1 * np.power(np.maximum(E, 1e-12), ex) * np.arctan(np.maximum(vp, 0.0) / kvb)
    ig = np.power(np.maximum(E, 1e-12), ex) / kg2
    return ip, ig

def pair(p, bplus, vg2_supply, rg2, vbias, vpk, raa, n=2000, freq=1.0):
    """Drives the pair with a sine of peak vpk on each grid (opposite phase). Returns dict of results."""
    t = np.linspace(0, 1, n, endpoint=False)
    s = vpk * np.sin(2 * np.pi * t)
    i1 = np.zeros(n); i2 = np.zeros(n); g1c = np.zeros(n); g2c = np.zeros(n)
    # screens through a common resistor: vg2 = supply - rg2 * (ig2_1 + ig2_2) (common, same for both)
    for k in range(n):
        a = 0.0; b = 0.0; vg2 = vg2_supply
        for it in range(60):
            v1 = bplus - (a - b) * raa / 4.0; v2 = bplus + (a - b) * raa / 4.0
            ia, ga = tube(p, -vbias + s[k], v1, vg2)
            ib, gb = tube(p, -vbias - s[k], v2, vg2)
            vg2n = vg2_supply - rg2 * (ga + gb)
            na = 0.5 * a + 0.5 * ia; nb = 0.5 * b + 0.5 * ib
            if abs(na - a) + abs(nb - b) < 1e-9: a, b, vg2 = na, nb, vg2n; break
            a, b, vg2 = na, nb, 0.5 * vg2 + 0.5 * vg2n
        i1[k], i2[k], g1c[k], g2c[k] = a, b, ga, gb
    vfull = (i1 - i2) * raa / 2.0
    po = np.mean(vfull ** 2) / raa
    # fundamental component of the output voltage for distortion
    f = np.fft.rfft(vfull) / n * 2
    thd = np.sqrt(np.sum(np.abs(f[2:12]) ** 2)) / np.abs(f[1]) * 100 if np.abs(f[1]) > 0 else 0
    return dict(po=po, ia=1000 * np.mean(i1 + i2), ig2=1000 * np.mean(g1c + g2c), thd=thd)

PARAMS = {  # mu, ex, kg1, kg2, kp, kvb  (devices.h)
    'EL34 (current)': (11.0, 1.35, 845.0, 2600.0, 60.0, 24.0),
    '6L6GC (current)': (8.7, 1.35, 1460.0, 4500.0, 48.0, 12.0),
    'EL84 (current)': (19.56, 1.35, 578.6, 1833.0, 152.4, 49.0),
    '6550DS (current)': (8.0, 1.35, 977.0, 8500.0, 37.3, 19.8),
    '6V6GT (current)': (9.15, 1.35, 2334.1, 10300.0, 76.1, 48.9),
}
# data-sheet operating tables (two tubes): (label, tube, B+, Vg2, Rg2, bias, drive pk per grid, Raa, expected)
TABLES = [
    ('EL34 AB1 400/400 -36V 3.5k', 'EL34 (current)', 400, 400, 800, 36.0, 35.4, 3500, dict(po=54, ia0=60, ia=221, ig0=9, ig=46)),
    ('EL34 AB 450/450 6.5k (Rk 232)', 'EL34 (current)', 450, 450, 1000, 32.5, 38.2, 6500, dict(po=40, ia0=120, ia=143, ig0=20, ig=44)),
    ('EL34 AB 375/375 3.5k (Rk 130)', 'EL34 (current)', 375, 375, 470, 28.0, 28.3, 3500, dict(po=35, ia0=150, ia=188, ig0=25, ig=39)),
    ('6L6GC AB1 450/400 -37V 5.6k', '6L6GC (current)', 450, 400, 0, 37.0, 35.0, 5600, dict(po=55, ia0=116, ia=210, ig0=5.6, ig=22)),
]
if __name__ == '__main__':
    for lab, name, bp, vg2, rg2, vb, vpk, raa, exp in TABLES:
        p = PARAMS[name]
        z = pair(p, bp, vg2, rg2, vb, 0.0, raa)
        m = pair(p, bp, vg2, rg2, vb, vpk, raa)
        print('%-34s zero: Ia %6.1f (%4.0f) Ig2 %5.1f (%4.1f) | max: Ia %6.1f (%4.0f) Ig2 %5.1f (%4.1f) Po %5.1f (%4.0f) THD %.1f' % (
            lab, z['ia'], exp['ia0'], z['ig2'], exp['ig0'], m['ia'], exp['ia'], m['ig2'], exp['ig'], m['po'], exp['po'], m['thd']))
