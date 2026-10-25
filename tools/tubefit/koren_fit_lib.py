import numpy as np
cur = np.load('ge_curves.npy', allow_pickle=True)
def triode(p, vg, vp):
    mu, ex, kg1, kp, kvb = p
    s = np.sqrt(kvb + vp*vp)
    x = kp*(1.0/mu + vg/s)
    sp = np.where(x>30, x, np.log1p(np.exp(np.minimum(x,30))))
    e1 = vp/kp*sp
    return np.where(e1>0, 1000.0*np.power(np.maximum(e1,1e-12), ex)/kg1, 0.0)  # mA
def samples(label0, step=-0.5):
    pts=[]
    for k,c in enumerate(cur):
        c = np.array(c, dtype=float)
        v, i = c[:,0], c[:,1]
        order = np.argsort(i); v=v[order]; i=i[order]
        vg = label0 + step*k
        for I in np.arange(0.1, i.max()-0.05, 0.1):
            pts.append((vg, float(np.interp(I, i, v)), I))
    return np.array(pts)
def fit(data, p0):
    vg, vp, I = data.T
    w = 1.0/(I+0.1)
    def res(p):
        return (triode(p, vg, vp) - I)*w
    p = np.array(p0, float); lam=1e-2
    r = res(p); cost = (r*r).sum()
    for it in range(300):
        J = np.zeros((len(r), len(p)))
        for j in range(len(p)):
            d = np.zeros(len(p)); d[j] = 1e-5*max(1.0, abs(p[j]))
            J[:,j] = (res(p+d)-r)/d[j]
        A = J.T@J; g = J.T@r
        improved=False
        for _ in range(12):
            dp = np.linalg.solve(A + lam*np.diag(np.diag(A)+1e-12), -g)
            pn = p + dp
            if pn[0]<=1 or pn[1]<=0.5 or pn[2]<=0 or pn[3]<=0 or pn[4]<0: lam*=10; continue
            rn = res(pn); cn=(rn*rn).sum()
            if cn < cost: p, r, cost = pn, rn, cn; lam=max(lam/3,1e-9); improved=True; break
            lam*=5
        if not improved: break
    return p, cost
cur_params = [100.0, 1.4, 1060.0, 600.0, 300.0]
for name, l0 in (('A: first curve = 0 V', 0.0), ('B: first curve = -0.5 V', -0.5)):
    d = samples(l0)
    vg, vp, I = d.T
    cost0 = (((triode(cur_params, vg, vp)-I)/(I+0.1))**2).sum()
    best=None
    for mu0 in (60,100,130):
        for kg in (500,1060,2000):
            p, c = fit(d, [mu0, 1.4, kg, 600, 300])
            if best is None or c<best[1]: best=(p,c)
    p,c = best
    rms = np.sqrt(c/len(d)); rms0 = np.sqrt(cost0/len(d))
    print(name, 'n=%d'%len(d), 'rel.rms current params %.3f -> fitted %.3f'%(rms0, rms), 'p=', np.round(p,3))
    print('   Ip(250V,-2V) fitted %.3f mA, current params %.3f mA'%(triode(p,-2.0,250.0), triode(cur_params,-2.0,250.0)))


