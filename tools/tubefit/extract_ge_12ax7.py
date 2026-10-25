import numpy as np, cv2
crop = cv2.imread('ge_plate_crop.png', 0)
dark = (crop < 120).astype(np.uint8)
dist = cv2.distanceTransform(dark, cv2.DIST_L2, 5)
thick = (dist > 3.2).astype(np.uint8)
thick = cv2.dilate(thick, cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(7,7)))
n, lab, stats, _ = cv2.connectedComponentsWithStats(thick, connectivity=8)
X0, X1 = 290.5, 2896.0; Y0, Y1 = 1918.5, 83.5
V = lambda px: (px-X0)/(X1-X0)*500.0
A = lambda py: (Y0-py)/(Y0-Y1)*3.5
big = [i for i in range(1,n) if (stats[i,3] > 100 or stats[i,2] > 200) and not (stats[i,1] < 300 and stats[i,0] > 2000)]
big.sort(key=lambda i: stats[i,0])
def center_rows(mask):
    ys, xs = np.where(mask); rows = {}
    for y,x in zip(ys,xs): rows.setdefault(y, []).append(x)
    return rows
curves = {}
# component 0 = the 0 V and the -0.5 V curves joined at the origin: split by the two runs per row
rows = center_rows(lab == big[0])
c0, c1 = [], []
for y in sorted(rows):
    xs = np.sort(rows[y]); gaps = np.where(np.diff(xs) > 6)[0]
    if len(gaps) == 1:
        a = xs[:gaps[0]+1]; b = xs[gaps[0]+1:]
        c0.append((a.mean(), y)); c1.append((b.mean(), y))
curves[0.0] = c0; curves[-0.5] = c1
for k, i in enumerate(big[1:]):
    rows = center_rows(lab == i)
    curves[-1.0 - 0.5*k] = [(np.mean(rows[y]), y) for y in sorted(rows)]
out = {}
for vg, cl in curves.items():
    pts = np.array([(V(x), A(y)) for x,y in cl])
    out[vg] = pts
    print('Vg %.1f: %d points, Vp %.0f..%.0f V, Ip max %.2f mA; at 250 V: %s' % (vg, len(pts), pts[:,0].min(), pts[:,0].max(), pts[:,1].max(),
          ('%.2f mA' % np.interp(250, pts[np.argsort(pts[:,0]),0], pts[np.argsort(pts[:,0]),1])) if pts[:,0].min()<=250<=pts[:,0].max() else '-'))
np.save('ge_curves_v2.npy', out, allow_pickle=True)
