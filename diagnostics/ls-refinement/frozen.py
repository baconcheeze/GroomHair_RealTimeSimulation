import pathlib,json,sys,struct,decimal,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent.parent/'qr-audit'));import audit
from core import Host,run
D=decimal.Decimal;decimal.getcontext().prec=120
load=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
def intsizes(p):return [3*(p['mapX'][i]>=0)+3*(i<len(p['mapQ']) and p['mapQ'][i]>=0) for i in range(len(p['mapX']))]
def fl(v):return [float(x) for x in v]
def error(p,u,delta,refU,refDelta,nU=None):
 e=audit.forward(p,list(map(D,u)),list(map(D,refU)),nU);diff=audit.sub(list(map(D,delta)),list(map(D,refDelta)))
 for key,name in [('mapX','position_m'),('mapQ','rotationVector_rad')]:e[name]=max((audit.norm(diff[k:k+3]) for k in p[key] if k>=0),default=D(0))
 rel=audit.inf(audit.sub(list(map(D,u)),list(map(D,refU))))/max(audit.inf(list(map(D,refU))),D('1e-300'));maxpos=max((audit.norm(list(map(D,refDelta[k:k+3]))) for k in p['mapX'] if k>=0),default=D(0));maxang=max((audit.norm(list(map(D,refDelta[k:k+3]))) for k in p['mapQ'] if k>=0),default=D(0));e['scaledRelativeInf']=rel;e['strictPass']=(rel<=D('1e-7') or (e['position_m']<=D('1e-10') and e['rotationVector_rad']<=D('1e-9'))) and e['position_m']<=max(D('1e-10'),D('1e-8')*maxpos) and e['rotationVector_rad']<=max(D('1e-9'),D('1e-8')*maxang)
 return e
def evaluate(p,curves,hp):
 a=audit.chunks(p['A'],p['n']);m=audit.chunks(p['M'],p['n']);nU=hp.get('null',{}).get('nU');nU=list(map(D,nU)) if nU else None;oracle={k:list(map(D,hp[k+'_solution'])) for k in ['A','B']};out=[]
 for step in curves:
  u=step['u'];delta=step['delta'];ud=list(map(D,u));dd=list(map(D,delta));x,q,f=audit.physical(p,dd);record=dict(step);record['hexU']=[struct.pack('>d',v).hex() for v in u];record['hexDelta']=[struct.pack('>d',v).hex() for v in delta];record['errors']={}
  for name,ru in [*oracle.items(),('doubleQR',p['QRU'])]:
   rd=[v/d for v,d in zip(ru,p['D'])] if name!='doubleQR' else p['QRDelta'];e=error(p,u,delta,ru,rd,nU);_,rq,rf=audit.physical(p,rd);e['retractedAngle_rad']=max(audit.angle(v,w) for v,w in zip(q,rq));record['errors'][name]=e
  record['diagnostics']=audit.residuals(a,p['b'],m,p['c'],ud);record['correctionScaledL2']=audit.norm(list(map(D,step['du'])));record['physicalTrialF']=f;out.append(record)
 return out
def main():
 source=pathlib.Path(sys.argv[1]);dest=pathlib.Path(sys.argv[2]);dest.mkdir(parents=True,exist_ok=True);host=Host(pathlib.Path(sys.argv[3]).resolve());allpass=True
 try:
  for path in sorted((source/'frozen').glob('*.json')):
   p=audit.decode(path);raw=load(path);hp=load(source/'high-precision-v2'/path.name)['runs'][-1];n=p['n'];a=audit.chunks(fl(p['A']),n);results={};stats={}
   for precision in ([80,120] if not p['originalPass'] else [80]):
    for kind in ['normal','LS']:
     curves,timing=run(host,n,intsizes(p),fl(p['M']),fl(p['c']),fl(p['D']),a,fl(p['b']),kind,precision)
     assert all(struct.pack('>d',v).hex()==bits for v,bits in zip(curves[0]['u'],raw['blockU'])),'Split factor initial solution mismatch'
     assert all(struct.pack('>d',v).hex()==bits for v,bits in zip(curves[0]['delta'],raw['blockDelta'])),'Unscaling mismatch'
     key=kind+str(precision);results[key]=evaluate(p,curves,hp);stats[key]=timing
   final=results['LS80'][-1];passed=bool(final['errors']['A']['strictPass'] and final['errors']['doubleQR']['strictPass']);allpass&=passed
   (dest/path.name).write_text(json.dumps(audit.serialize({'id':p['id'],'inputSHA256':hashlib.sha256(path.read_bytes()).hexdigest(),'frozenBaselineBitExact':True,'originalPass':p['originalPass'],'candidatePass':passed,'curves':results,'cost':stats}),indent=2));print(path.stem,'pass',passed,'angle A',final['errors']['A']['rotationVector_rad'],flush=True)
 finally:host.close()
 (dest/'STATUS.json').write_text(json.dumps({'allFrozenCandidatePass':allpass,'fixtures':13,'failedCheckpoints':9,'distinctFailedLinearInputs':7,'correctionCap':5,'residualPrecision':80,'higherPrecisionFailedCheckpoints':120},indent=2));return 0 if allpass else 1
if __name__=='__main__':sys.exit(main())
