"""Profile least-squares fits of a hyperbola with up to three smooth steps."""
import math
import random


def fit_steps(points, terms):
    if len(points) <= 2+3*terms:
        return dict(m=terms, status='insufficient_data', parameters=2+3*terms)
    ns, ys = zip(*points)
    lo, hi = math.log(min(ns)), math.log(max(ns))
    bounds = [(lo, hi), (math.log(.03), math.log(3))]*terms

    def solve(q):
        columns = [[1.0]*len(ns), [min(ns)/n for n in ns]]
        for t,v in zip(q[::2],q[1::2]):
            columns.append([1/(1+math.exp(max(-700,min(700,-(math.log(n)-t)/math.exp(v))))) for n in ns])
        # Reorthogonalized QR avoids normal-equation conditioning problems.
        orthogonal=[]; r=[[0.0]*len(columns) for _ in columns]
        for j,column in enumerate(columns):
            z=column[:]
            for _ in range(2):
                for i,u in enumerate(orthogonal):
                    coefficient=sum(a*b for a,b in zip(u,z)); r[i][j]+=coefficient
                    z=[a-coefficient*b for a,b in zip(z,u)]
            norm=math.sqrt(sum(a*a for a in z))
            if norm<1e-9: return float('inf'), []
            r[j][j]=norm;orthogonal.append([a/norm for a in z])
        beta=[sum(a*b for a,b in zip(u,ys)) for u in orthogonal]
        for j in reversed(range(len(beta))):
            beta[j]=(beta[j]-sum(r[j][k]*beta[k] for k in range(j+1,len(beta))))/r[j][j]
        loss=sum((y-sum(b*col[i] for b,col in zip(beta,columns)))**2 for i,y in enumerate(ys))
        return loss,beta

    rng=random.Random(741+terms)
    starts=[[lo+(hi-lo)*(j+1)/(terms+1) if i%2==0 else math.log(.3)
             for j in range(terms) for i in (0,1)]]
    starts += [[rng.uniform(a,b) for a,b in bounds] for _ in range(19)]
    best=(float('inf'), [], [])
    for q in starts:
        loss,beta=solve(q)
        steps=[(b-a)/8 for a,b in bounds]
        for _ in range(300):
            improved=False
            for i,(a,b) in enumerate(bounds):
                for direction in (-1,1):
                    trial=q[:];trial[i]=max(a,min(b,q[i]+direction*steps[i]))
                    cost,coeff=solve(trial)
                    if cost<loss:
                        loss,beta,q=cost,coeff,trial;improved=True
            if not improved:steps=[s*.5 for s in steps]
            if max(steps,default=0)<1e-6:break
        if loss<best[0]:best=loss,q,beta
    loss,q,beta=best
    if not math.isfinite(loss):return dict(m=terms,status='singular')
    transitions=sorted([dict(N=math.exp(t),w=math.exp(v),d=d)
                        for t,v,d in zip(q[::2],q[1::2],beta[2:])],key=lambda s:s['N'])
    mean=sum(ys)/len(ys);sst=sum((y-mean)**2 for y in ys)
    return dict(m=terms,status='ok',parameters=2+3*terms,a=beta[1]*min(ns),c=beta[0],
                transitions=transitions,asymptote=beta[0]+sum(beta[2:]),sse=loss,
                rmse=math.sqrt(loss/len(ns)),r_squared=1-loss/sst if sst else None,
                at_search_boundary=any(min(abs(x-a),abs(x-b))<1e-5 for x,(a,b) in zip(q,bounds)),
                bounds=dict(N=[min(ns),max(ns)],w=[.03,3]),
                method='profile QR least squares; 20 deterministic starts, bounded coordinate search')
