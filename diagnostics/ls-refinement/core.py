import decimal,struct,subprocess,time
D=decimal.Decimal
def readn(s,n):
 out=bytearray()
 while len(out)<n:
  b=s.read(n-len(out))
  if not b:raise RuntimeError('Factor host stopped')
  out.extend(b)
 return bytes(out)
def doubles(s,n):return list(struct.unpack('<'+str(n)+'d',readn(s,8*n)))
def packd(v):return struct.pack('<'+str(len(v))+'d',*v)
class Host:
 def __init__(self,path):self.proc=subprocess.Popen([str(path)],stdin=subprocess.PIPE,stdout=subprocess.PIPE)
 def init(self,n,sizes,m,c,d):
  t=time.perf_counter();self.n=n;f=self.proc.stdin;f.write(struct.pack('<III',1,n,len(sizes)));f.write(struct.pack('<'+str(len(sizes))+'I',*sizes));f.write(packd(m));f.write(packd(c));f.write(packd(d));f.flush();valid,fm,mem=struct.unpack('<IdQ',readn(self.proc.stdout,20))
  if not valid:raise ArithmeticError('Block factorization failed')
  sm=struct.unpack('<d',readn(self.proc.stdout,8))[0];u=doubles(self.proc.stdout,n);delta=doubles(self.proc.stdout,n)
  return u,delta,{'factorMs':fm,'initialSolveMs':sm,'factorBytes':mem,'initRoundtripMs':1000*(time.perf_counter()-t)}
 def step(self,rhs):
  t=time.perf_counter();f=self.proc.stdin;f.write(struct.pack('<I',2));f.write(packd(rhs));f.flush();sm,lost=struct.unpack('<dI',readn(self.proc.stdout,12));du=doubles(self.proc.stdout,self.n);u=doubles(self.proc.stdout,self.n);delta=doubles(self.proc.stdout,self.n)
  return u,delta,du,{'solveMs':sm,'lostComponentUpdates':lost,'roundtripMs':1000*(time.perf_counter()-t)}
 def close(self):
  self.proc.stdin.write(struct.pack('<I',0));self.proc.stdin.flush();self.proc.stdin.close();self.proc.wait(timeout=10)
  if self.proc.returncode:raise RuntimeError('Factor host failed')
class Residual:
 def __init__(self,a,b,m,c,n):
  self.n=n;self.a=[[(j,D.from_float(v)) for j,v in enumerate(row) if v!=0.] for row in a];self.b=[D.from_float(v) for v in b];self.m=[[(j,D.from_float(v)) for j,v in enumerate(row) if v!=0.] for row in m];self.c=[D.from_float(v) for v in c];self.nonzeros=sum(map(len,self.a))
 def rhs(self,u,kind,precision):
  with decimal.localcontext() as ctx:
   ctx.prec=precision;v=[D.from_float(x) for x in u]
   if kind=='normal':s=[c-sum((a*v[j] for j,a in row),D(0)) for c,row in zip(self.c,self.m)]
   elif kind=='LS':
    r=[b-sum((a*v[j] for j,a in row),D(0)) for b,row in zip(self.b,self.a)];s=[D(0)]*self.n
    for row,rv in zip(self.a,r):
     for j,a in row:s[j]+=a*rv
   else:raise ValueError(kind)
   return [float(x) for x in s]
def run(host,n,sizes,m,c,d,a,b,kind='LS',precision=80):
 begin=time.perf_counter();u,delta,stats=host.init(n,sizes,m,c,d);curves=[{'iteration':0,'u':u,'delta':delta,'du':[0.]*n}];t=time.perf_counter();residual=Residual(a,b,[m[i:i+n] for i in range(0,len(m),n)],c,n);stats['residualSetupMs']=1000*(time.perf_counter()-t);stats.update(residualMs=0.,triangularCorrectionMs=0.,correctionIPCroundtripMs=0.,factorizations=1,initialSolves=1,corrections=5,extraTriangularSolves=5,precision=precision,LSrowNonzeros=residual.nonzeros,denseATransportBytes=8*len(a)*n,denseMTransportBytes=8*n*n,sparseANumericPayloadBytes=residual.nonzeros*12+8*len(a))
 for k in range(1,6):
  t=time.perf_counter();rhs=residual.rhs(u,kind,precision);rm=1000*(time.perf_counter()-t);old=u;u,delta,du,timing=host.step(rhs);stats['residualMs']+=rm;stats['triangularCorrectionMs']+=timing['solveMs'];stats['correctionIPCroundtripMs']+=timing['roundtripMs'];curves.append({'iteration':k,'u':u,'delta':delta,'du':du,'residualMs':rm,**timing,'stagnated':old==u})
 stats['mechanismWallMs']=1000*(time.perf_counter()-begin)
 return curves,stats
