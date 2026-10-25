import numpy as np, cv2, pymupdf
d = pymupdf.open('mullard_12at7.pdf'); p = d[2]
dr = p.get_drawings(); items = dr[111]['items']
subs=[]; cur=[]
for it in items:
    a,b = it[1], it[2]
    if cur and (abs(cur[-1][1].x-a.x)>0.01 or abs(cur[-1][1].y-a.y)>0.01): subs.append(cur); cur=[]
    cur.append((a,b))
subs.append(cur)
subs = subs[:8]
IP = lambda x: (x-98.4)/28.333*2.0
VP = lambda y: (y-348.5)/28.333*20.0
S = 40
curves = {}
for k, sub in enumerate(subs):
    pts = np.array([[q[0].x, q[0].y] for q in sub] + [[sub[-1][1].x, sub[-1][1].y]])
    img = np.zeros((int(800*S), int(500*S)), np.uint8)
    cv2.fillPoly(img, [np.round(pts*S).astype(np.int32)], 255)
    ys, xs = np.where(img>0)
    rows = {}
    for y,x in zip(ys,xs): rows.setdefault(y, []).append(x)
    ysort = sorted(rows)[::S//2]
    c = np.array([[VP(y/S), IP(np.mean(rows[y])/S)] for y in ysort])
    vg = -3.5 + 0.5*k
    curves[vg] = c
    print('Vg %.1f: %d pts, Vp %.0f..%.0f, Ip %.2f..%.2f' % (vg, len(c), c[:,0].min(), c[:,0].max(), c[:,1].min(), c[:,1].max()))
np.save('mullard_12at7_curves.npy', curves, allow_pickle=True)
for vg,c in curves.items():
    c = c[np.argsort(c[:,0])]
    print('Vg %.1f: Ip at 100/200/250/300 V = %s' % (vg, ['%.2f'%np.interp(v, c[:,0], c[:,1]) if c[0,0]<=v<=c[-1,0] else '-' for v in (100,200,250,300)]))
