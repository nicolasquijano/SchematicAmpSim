import numpy as np, pymupdf

def sample_item(it, n=24):
    if it[0] == 'l':
        a, b = it[1], it[2]
        return [(a.x, a.y), (b.x, b.y)]
    a, c1, c2, b = it[1], it[2], it[3], it[4]
    out = []
    for t in np.linspace(0, 1, n):
        u = 1 - t
        out.append((u**3*a.x + 3*u*u*t*c1.x + 3*u*t*t*c2.x + t**3*b.x,
                    u**3*a.y + 3*u*u*t*c1.y + 3*u*t*t*c2.y + t**3*b.y))
    return out

def item_end(it):
    return it[2] if it[0] == 'l' else it[4]

def subpaths(drawing):
    subs = []; cur = []
    for it in drawing['items']:
        a = it[1]
        if cur and (abs(item_end(cur[-1]).x - a.x) > 0.05 or abs(item_end(cur[-1]).y - a.y) > 0.05): subs.append(cur); cur = []
        cur.append(it)
    subs.append(cur)
    return subs

def polyline(sub):
    pts = []
    for it in sub: pts += sample_item(it)
    return np.array(pts)
