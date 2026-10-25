import numpy as np
cols = np.load('tad_cols.npy', allow_pickle=True).item()
UA = lambda x: 230.0 + (x - 57.0)/2.03
IA = lambda y: (363.0 - y)/4.5
names = [-35, -40, -45, -50, -55, -60]
curves = {n: [] for n in names}
for x, ys in cols.items():
    ua = UA(x); I = sorted([IA(y) for y in ys], reverse=True)
    if ua < 232 or ua > 470: continue
    # which curves are still drawn at this Ua
    first = 0 if ua <= 428 else (1 if ua <= 458 else 2)
    avail = names[first:]
    if len(I) > len(avail): continue            # label pixels or noise: skip the column
    # curves that start late (small current) are missing from the bottom of the list
    for n, v in zip(avail, I):
        if v > 0.4: curves[n].append((ua, v))
out = {}
for n in names:
    a = np.array(curves[n]); a = a[np.argsort(a[:,0])]
    out[float(n)] = a
    print('Ug1 %d: %d points, Ua %.0f..%.0f, Ia %.1f..%.1f; at 300/350/400/450: %s' % (n, len(a), a[0,0], a[-1,0], a[:,1].min(), a[:,1].max(), ['%.1f' % np.interp(v, a[:,0], a[:,1]) if a[0,0] <= v <= a[-1,0] else '-' for v in (300, 350, 400, 450)]))
np.save('tad_6l6_curves.npy', out, allow_pickle=True)
