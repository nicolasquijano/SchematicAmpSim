import numpy as np
exec(open('ge_fit.py').read().split("cur_params")[0].replace("cur = np.load('ge_curves.npy', allow_pickle=True)", ""))
curves = np.load('ge_curves_v2.npy', allow_pickle=True).item()
pts = []
for vg, c in curves.items():
    c = c[np.argsort(c[:,1])]
    for I in np.arange(0.05, c[:,1].max()-0.03, 0.05):
        pts.append((vg, float(np.interp(I, c[:,1], c[:,0])), I))
data = np.array(pts); vg, vp, I = data.T
def relrms(p): return float(np.sqrt((((triode(p, vg, vp)-I)/(I+0.1))**2).mean()))
def fit_fixed_ex(ex):
    best=None
    for mu0 in (80,100,120):
        for kg in (300,600,1000):
            for kvb0 in (30, 300, 3000):
                # optimise the four free parameters; ex is held
                def wrap(q): return triode([q[0], ex, q[1], q[2], q[3]], vg, vp)
                import types
                # reuse LM by adapting: parameters vector p = [mu, ex, kg1, kp, kvb] with ex frozen through large penalty? simpler: project
                p, c = fit(data, [mu0, ex, kg, 600, kvb0])
                p[1] = ex
                p2, c2 = fit_proj(data, p, ex)
                if best is None or c2<best[1]: best=(p2,c2)
    return best
def fit_proj(data, p0, ex):
    vg, vp, I = data.T; w = 1.0/(I+0.1)
    full = lambda q: np.array([q[0], ex, q[1], q[2], q[3]])
    res = lambda q: (triode(full(q), vg, vp)-I)*w
    q = np.array([p0[0], p0[2], p0[3], p0[4]], float); lam=1e-2
    r = res(q); cost=(r*r).sum()
    for it in range(400):
        J = np.zeros((len(r), 4))
        for j in range(4):
            d = np.zeros(4); d[j] = 1e-5*max(1.0, abs(q[j])); J[:,j] = (res(q+d)-r)/d[j]
        Am = J.T@J; g = J.T@r; ok=False
        for _ in range(12):
            dq = np.linalg.solve(Am + lam*np.diag(np.diag(Am)+1e-12), -g); qn = q+dq
            if qn[0]<=5 or qn[1]<=0 or qn[2]<=0 or qn[3]<0: lam*=10; continue
            rn = res(qn); cn=(rn*rn).sum()
            if cn<cost: q, r, cost = qn, rn, cn; lam=max(lam/3,1e-9); ok=True; break
            lam*=5
        if not ok: break
    return full(q), cost
for ex in (1.2, 1.3, 1.4, 1.5, 1.6):
    p, c = fit_fixed_ex(ex)
    ip = float(triode(p,-2.0,250.0)); h=1e-3
    gm = (float(triode(p,-2.0+h,250.0))-float(triode(p,-2.0-h,250.0)))/(2*h)
    gp = (float(triode(p,-2.0,250.01))-float(triode(p,-2.0,249.99)))/0.02
    print('ex=%.1f: rel.rms %.3f  mu %.1f kg1 %.0f kp %.0f kvb %.0f | Ip %.2f gm %.2f ra %.1fk mu %.0f' % (ex, relrms(p), p[0], p[2], p[3], p[4], ip, gm, 1/gp, gm/gp))
