import numpy as np, pymupdf
from pdfcurves import *
def curves_from(pdf, idx, ia_of, ua_of, ugs, order):
    p = pymupdf.open(pdf)[0]; dr = p.get_drawings()
    subs = subpaths(dr[idx]); out = {}
    for k, ug in zip(order, ugs):
        pl = polyline(subs[k])
        out[ug] = np.column_stack([ua_of(pl[:,1]), ia_of(pl[:,0])])
    return out
# EL34: Ug2 = 250 V; subpath 0 is the 25 W hyperbola
el34 = curves_from('jj_el34.pdf', 475, lambda x: (381.1-x)*1.6, lambda y: (350.9-y)/51.7*100.0, [0,-5,-10,-15,-20,-25], [1,2,3,4,5,6])
# KT88: Ug2 = 300 V
kt88 = curves_from('jj_kt88.pdf', 487, lambda x: (364.8-x)*400.0/(364.8-142.2), lambda y: (314.0-y)*500.0/(314.0-81.1), [0,-5,-10,-15,-20,-25,-30,-35], [7,6,5,4,3,2,1,0])
np.save('el34_curves.npy', el34, allow_pickle=True); np.save('kt88_curves.npy', kt88, allow_pickle=True)
for name, cv in (('EL34', el34), ('KT88', kt88)):
    print(name)
    for ug, c in cv.items():
        c = c[np.argsort(c[:,0])]
        print('  Ug1 %4d: Ua %.0f..%.0f, Ia at 100/250/400 V: %s' % (ug, c[0,0], c[-1,0], ['%.0f'%np.interp(v, c[:,0], c[:,1]) if c[0,0]<=v<=c[-1,0] else '-' for v in (100,250,400)]))
