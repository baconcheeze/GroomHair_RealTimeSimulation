"""Frozen-input numerical audit. Standard-library Decimal, no long-double assumptions.
All hex strings are IEEE754 binary64 bit patterns, decoded exactly using from_float.
A: independent Decimal Householder QR. B: partial-pivot Gaussian solve of M64.
C: high-precision rebuild from J64/r64/D64/lambda64, then Householder QR.
No physical residual/Jacobian regeneration is performed by this script.
"""
import decimal as dec, json, math, struct, pathlib, sys, time, hashlib
from fractions import Fraction
from decimal import Decimal as D
ZERO=D(0); ONE=D(1); TWO=D(2)
def dot(a,b):return sum((x*y for x,y in zip(a,b)),ZERO)
def norm(a):return dot(a,a).sqrt()
def sub(a,b):return [x-y for x,y in zip(a,b)]
def mv(a,x):return [dot(row,x) for row in a]
def transpose(a):return list(map(list,zip(*a)))
def gram(a):
 t=transpose(a);return [[dot(x,y) for y in t] for x in t]
def inf(x):return max(map(abs,x),default=ZERO)
def matinf(a):return max((sum(map(abs,row),ZERO) for row in a),default=ZERO)
def qr(a,b):
 a=[row[:] for row in a];b=b[:];m=len(a);n=len(a[0])
 for k in range(n):
  v=[a[i][k] for i in range(k,m)];length=norm(v)
  if not length:raise ArithmeticError('rank-deficient QR: no singular-value truncation permitted')
  alpha=-length if v[0]>=0 else length;v[0]-=alpha;vl=norm(v);v=[x/vl for x in v]
  for j in range(k,n):
   prod=TWO*sum((v[i-k]*a[i][j] for i in range(k,m)),ZERO)
   for i in range(k,m):a[i][j]-=v[i-k]*prod
  prod=TWO*dot(v,b[k:])
  for i in range(k,m):b[i]-=v[i-k]*prod
 x=[ZERO]*n
 for i in range(n-1,-1,-1):x[i]=(b[i]-dot(a[i][i+1:],x[i+1:]))/a[i][i]
 return x

def solve(a,b):
 a=[row[:] for row in a];b=b[:];n=len(b)
 for k in range(n):
  pivot=max(range(k,n),key=lambda i:abs(a[i][k]));a[k],a[pivot]=a[pivot],a[k];b[k],b[pivot]=b[pivot],b[k]
  if not a[k][k]:raise ArithmeticError('singular M64')
  for i in range(k+1,n):
   if not a[i][k]:continue
   f=a[i][k]/a[k][k]
   for j in range(k+1,n):a[i][j]-=f*a[k][j]
   a[i][k]=ZERO;b[i]-=f*b[k]
 x=[ZERO]*n
 for i in range(n-1,-1,-1):x[i]=(b[i]-dot(a[i][i+1:],x[i+1:]))/a[i][i]
 return x

def eigen(a):
 """Cyclic symmetric Jacobi at current Decimal precision, no rank truncation."""
 a=[row[:] for row in a];n=len(a);threshold=D(10)**(-dec.getcontext().prec+15)*max(ONE,matinf(a))
 for sweep in range(150):
  maxoff=ZERO
  for p in range(n):
   for q in range(p+1,n):
    z=a[p][q];maxoff=max(maxoff,abs(z))
    if abs(z)<threshold:continue
    tau=(a[q][q]-a[p][p])/(TWO*z);t=(ONE if tau>=0 else -ONE)/(abs(tau)+(ONE+tau*tau).sqrt());c=ONE/(ONE+t*t).sqrt();s=t*c
    a[p][p]-=t*z;a[q][q]+=t*z;a[p][q]=a[q][p]=ZERO
    for k in range(n):
     if k==p or k==q:continue
     x=a[k][p];y=a[k][q];a[k][p]=a[p][k]=c*x-s*y;a[k][q]=a[q][k]=s*x+c*y
  if maxoff<threshold:return sorted(a[i][i] for i in range(n))
 raise ArithmeticError('Jacobi cap')

def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def mul(a,b):
 v=cross(a[:3],b[:3]);return [a[3]*b[i]+b[3]*a[i]+v[i] for i in range(3)]+[a[3]*b[3]-dot(a[:3],b[:3])]
def conj(q):return [-q[0],-q[1],-q[2],q[3]]
def unit(q):n=norm(q);return [x/n for x in q]
def rotate(q,v):
 t=cross(q[:3],v);t=[t[i]+q[3]*v[i] for i in range(3)];t=cross(q[:3],t);return [v[i]+TWO*t[i] for i in range(3)]
def sincos(x):
 # Physical increments here below one radian. General reduction via doubling.
 halves=0
 while abs(x)>D('.25'):x/=TWO;halves+=1
 ss=term=x;cc=ct=ONE;k=1;eps=D(10)**(-dec.getcontext().prec+4)
 while True:
  term*=-x*x/D((2*k)*(2*k+1));ct*=-x*x/D((2*k-1)*(2*k));ss+=term;cc+=ct;k+=1
  if max(abs(term),abs(ct))<eps:break
 for _ in range(halves):ss,cc=TWO*ss*cc,cc*cc-ss*ss
 return ss,cc

def atan_small(x):
 sign=-ONE if x<0 else ONE;x=abs(x);doublings=0
 # atan(x)=2 atan(x/(1+sqrt(1+x^2))). No pi needed.
 while x>D('.1'):x=x/(ONE+(ONE+x*x).sqrt());doublings+=1
 t=x;value=t;k=1;eps=D(10)**(-dec.getcontext().prec+4)
 while True:
  t*=-x*x;add=t/D(2*k+1);value+=add;k+=1
  if abs(add)<eps:break
 return sign*value*(TWO**doublings)
def angle(a,b):
 rel=unit(mul(unit(a),conj(unit(b))));return TWO*atan_small(norm(rel[:3])/abs(rel[3])) if rel[3] else D('3.1415926535897932384626433832795028841971693993751')
def retract(q,theta):
 n=norm(theta)
 if n:
  s,c=sincos(n/TWO);dq=[s*x/n for x in theta]+[c]
 else:dq=[ZERO,ZERO,ZERO,ONE]
 return unit(mul(dq,q))
def chunks(a,k):return [a[i:i+k] for i in range(0,len(a),k)]
def physical(p,delta):
 x=chunks(p['x'],3);q=chunks(p['q'],4)
 for i,k in enumerate(p['mapX']):
  if k>=0:x[i]=[x[i][j]+delta[k+j] for j in range(3)]
 for i,k in enumerate(p['mapQ']):
  if k>=0:q[i]=retract(q[i],delta[k:k+3])
 y=chunks(p['y'],3);h,ss,bt=p['hSSBT'];f=ZERO
 for i in range(len(x)):f+=p['mass'][i]/(TWO*h*h)*dot(sub(x[i],y[i]),sub(x[i],y[i]))
 for i,L in enumerate(p['L']):
  r=sub([v/L for v in sub(x[i+1],x[i])],rotate(q[i],[ZERO,ZERO,ONE]));f+=ss*L/TWO*dot(r,r)
  if i+1<len(q):
   relative=mul(conj(q[i]),q[i+1]);rest=p['restBT'][4*i:4*i+4];pole=ONE if dot(relative,rest)>=0 else -ONE;r=sub(relative,[pole*v for v in rest]);f+=TWO*bt/L*dot(r,r)
 return x,q,f

def forward(p,u,oracle,nU=None):
 du=sub(u,oracle);dd=[x/d for x,d in zip(du,p['D'])];result={'scaledL2':norm(du),'scaledRelativeL2':norm(du)/max(norm(oracle),D('1e-300')),'position_m':ZERO,'rotationVector_rad':ZERO}
 for key,name in [('mapX','position_m'),('mapQ','rotationVector_rad')]:
  result[name]=max((norm(dd[k:k+3]) for k in p[key] if k>=0),default=ZERO)
 if nU:
  along=dot(du,nU);result.update(nullSigned=along,nullAbs=abs(along),observableL2=norm(sub(du,[along*v for v in nU])))
 return result

def residuals(a,b,m,c,u):
 r=sub(mv(m,u),c);den=matinf(m)*inf(u)+inf(c);component=[]
 for i,v in enumerate(r):
  d=sum((abs(aij)*abs(uj) for aij,uj in zip(m[i],u)),ZERO)+abs(c[i]);component.append(abs(v)/d if d else abs(v))
 rr=sub(mv(a,u),b);g=mv(transpose(a),rr);den2=matinf(transpose(a))*(matinf(a)*inf(u)+inf(b))
 return {'normalBackward':inf(r)/den if den else inf(r),'componentBackward':max(component),'LSstationarityAbs':inf(g),'LSstationarityInputRelative':inf(g)/den2 if den2 else inf(g)}
def decode(path):
 raw=json.loads(path.read_text());p={}
 for k,v in raw.items():
  if isinstance(v,list) and v and isinstance(v[0],str):p[k]=[D.from_float(struct.unpack('>d',bytes.fromhex(x))[0]) for x in v]
  else:p[k]=v
 return p

def audit(p,precision,with_spectra=True):
 dec.getcontext().prec=precision;n=p['n'];a=chunks(p['A'],n);m=chunks(p['M'],n);j=chunks(p['J'],n);r=p['r'];d=p['D'];lam=p['lambda'][0]
 k=[[x/d[c] for c,x in enumerate(row)] for row in j];ac=k+[[lam.sqrt() if row==col else ZERO for col in range(n)] for row in range(n)];bc=[-v for v in r]+[ZERO]*n
 # FIRST oracle inputs are frozen binary64 A/b and M/c. C is built separately.
 ua=qr(a,p['b']);ub=solve(m,p['c']);uc=qr(ac,bc)
 kgram=gram(k);mc=[row[:] for row in kgram]
 for i in range(n):mc[i][i]+=lam
 cc=[-dot(col,r) for col in transpose(k)]
 result={'precisionDigits':precision,'A_solution':ua,'B_solution':ub,'C_solution':uc,'oracles':{},'backends':{},'formation':{'Amax':max(abs(x-y) for x,y in zip(p['A'],sum(ac,[]))),'Mmax':max(abs(x-y) for x,y in zip(p['M'],sum(mc,[]))),'cMax':inf(sub(p['c'],cc))},'LSvsNormal':forward(p,ub,ua),'rebuiltVsLS':forward(p,uc,ua)}
 nU=None
 if len(p['L'])==1 and p['mapQ'][0]>=0:
  tangent=rotate(p['q'],[ZERO,ZERO,ONE]);nd=[ZERO]*n;idx=p['mapQ'][0];nd[idx:idx+3]=tangent;nu=[x*y for x,y in zip(d,nd)];nU=[x/norm(nu) for x in nu]
  result['null']={'JnDeltaL2':norm(mv(j,nd)),'KnUL2':norm(mv(k,nU)),'tangentNorm':norm(tangent),'nU':nU,'oracleProjections':{name:dot(u,nU) for name,u in [('A',ua),('B',ub),('C',uc)]}}
  result['LSvsNormal']=forward(p,ub,ua,nU)
  rot=[[j[6+a][idx+b] for b in range(3)] for a in range(3)]
  exactAxis=[rot[2][1],rot[0][2],rot[1][0]];exactND=[ZERO]*n;exactND[idx:idx+3]=exactAxis
  rationalZero=all(sum((Fraction(x)*Fraction(y) for x,y in zip(row,exactND)),Fraction(0))==0 for row in j)
  exactNU=[x*y for x,y in zip(d,exactND)];exactNU=[x/norm(exactNU) for x in exactNU]
  result['null']['exactJNullRationalProof']=rationalZero
  result['null']['exactJNullVsPhysicalAxis']=norm(sub(unit(exactAxis),unit(tangent)))
  result['null']['exactJNullOracleProjections']={name:dot(u,exactNU) for name,u in [('A',ua),('B',ub),('C',uc)]}
  result['null']['exactJNullKnU']=norm(mv(k,exactNU))
 for name,u in [('A',ua),('B',ub),('C',uc)]:result['oracles'][name]=residuals(a,p['b'],m,p['c'],u)
 for name in ['reference','block','QR']:
  u=p[name+'U'];delta=p[name+'Delta'];expectedDelta=[x/y for x,y in zip(u,d)];x,q,f=physical(p,delta);ax,aq,af=physical(p,[x/y for x,y in zip(ua,d)])
  item={'toA':forward(p,u,ua,nU),'toB':forward(p,u,ub,nU),'toC':forward(p,u,uc,nU),'residuals':residuals(a,p['b'],m,p['c'],u),'unscaleRoundingMax':inf(sub(delta,expectedDelta)),'trialObjectiveHP':f,'trialObjectiveDifferenceToA':f-af,'retractionStateToA_rad':max(angle(v,w) for v,w in zip(q,aq)),'storedTrialRetractionDifference_rad':max(angle(v,w) for v,w in zip(chunks(p[name+'TrialQ'],4),q)),'storedDoubleObjectiveDifference':p[name+'F'][0]-f,'tangentToA':max(norm(sub(rotate(v,[ZERO,ZERO,ONE]),rotate(w,[ZERO,ZERO,ONE]))) for v,w in zip(q,aq))}
  result['backends'][name]=item
 # Audit actual compared increments AND stored retracted states independently.
 result['metric']={'rawPhysicalIncrementDifference':forward(p,[v*d for v,d in zip(p['QRDelta'],d)],[v*d for v,d in zip(p['referenceDelta'],d)],nU),'storedTrialQuaternionDistance':max(angle(v,w) for v,w in zip(chunks(p['referenceTrialQ'],4),chunks(p['QRTrialQ'],4)))}
 result['oracleConsistency']={'A_QR_vs_GramSolve':inf(sub(ua,solve(gram(a),mv(transpose(a),p['b'])))),'C_QR_vs_RebuiltNormalSolve':inf(sub(uc,solve(mc,cc))),'B_exactSystemResidual':inf(sub(mv(m,ub),p['c'])),'A_QR_LSstationarity':inf(mv(transpose(a),sub(mv(a,ua),p['b']))),'C_QR_LSstationarity':inf(mv(transpose(ac),sub(mv(ac,uc),bc)))}
 if with_spectra and n<=30:
  eigK=eigen(kgram);eigK64=eigen(gram(a[:len(j)]));eigM=eigen(m);eigA=eigen(gram(a));sigK=[abs(v).sqrt() for v in eigK];sigA=[v.sqrt() for v in eigA]
  result['spectra']={'KsingularValues':sigK,'K64singularValues':[abs(v).sqrt() for v in eigK64],'K64gramMinSigned':eigK64[0],'K64condition':(abs(eigK64[-1]/eigK64[0])).sqrt() if eigK64[0] else 'infinity','K_primitive_exactNull':result.get('null',{}).get('exactJNullRationalProof',False),'singularValueResolutionWarning':'Gram/Jacobi square-root floor ~10^(-precision/2); exact primitive null verified separately with rational arithmetic','A64singularValues':sigA,'M64eigenvalues':eigM,'conditionK':max(sigK)/min(sigK) if min(sigK) else 'infinity','conditionA':max(sigA)/min(sigA),'conditionM':max(eigM)/min(eigM),'KgramMinSigned':eigK[0]}
 return result

def serialize(obj):
 if isinstance(obj,D):return str(obj)
 if isinstance(obj,list):return [serialize(x) for x in obj]
 if isinstance(obj,dict):return {k:serialize(v) for k,v in obj.items()}
 return obj

def metric_controls():
 out=[]
 for value in ['0','1e-12','1e-9','1e-8']:
  theta=D(value);q=unit([D('.2'),D('-.3'),D('.1'),ONE]);b=retract(q,[theta,ZERO,ZERO]);rel=angle(q,b)
  neg=[-x for x in b];out.append({'theta':theta,'measured':rel,'error':rel-theta,'signEquivalent':angle(b,neg),'self':angle(b,b)})
 return out

def main():
 run=pathlib.Path(sys.argv[1]);out=run/(sys.argv[2] if len(sys.argv)>2 else 'high-precision');out.mkdir(exist_ok=True)
 dec.getcontext().prec=120;(out/'metric-controls.json').write_text(json.dumps(serialize(metric_controls()),indent=2))
 for path in sorted((run/'frozen').glob('*.json')):
  dest=out/(path.stem+'.json')
  if dest.exists():continue
  t=time.monotonic();p=decode(path);results=[]
  for prec in [80,120]:
   results.append(audit(p,prec));print(path.stem,prec,'digits',round(time.monotonic()-t,2),'s',flush=True)
  stability={name:inf(sub(results[0][name+'_solution'],results[1][name+'_solution'])) for name in ['A','B','C']}
  dest.write_text(json.dumps(serialize({'inputSHA256':hashlib.sha256(path.read_bytes()).hexdigest(),'precisionProvider':'Python decimal/libmpdec, context.prec=80 and 120 decimal digits','precisionProbe':{str(digits):str(dec.Context(prec=digits).next_plus(D(1))-D(1)) for digits in [80,120]},'stabilityAbsScaled':stability,'runs':results}),indent=2))
if __name__=='__main__':main()
