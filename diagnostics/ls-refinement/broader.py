import pathlib,json,sys,struct,decimal,subprocess,time,hashlib
from core import Host,run,readn,doubles,packd
from frozen import load,fl,error
sys.path.insert(0,str(pathlib.Path(__file__).parent.parent/'qr-audit'));import audit
D=decimal.Decimal;decimal.getcontext().prec=120

def suite(source,dest,hostpath):
 dest.mkdir(parents=True,exist_ok=True);host=Host(hostpath);ok=True;count=0
 try:
  for path in sorted(source.glob('*.json')):
   p=audit.decode(path);raw=load(path);n=p['n'];sizes=[3*(p['mapX'][i]>=0)+3*(i<len(p['mapQ']) and p['mapQ'][i]>=0) for i in range(len(p['mapX']))]
   curves,cost=run(host,n,sizes,fl(p['M']),fl(p['c']),fl(p['D']),audit.chunks(fl(p['A']),n),fl(p['b']))
   assert [struct.pack('>d',v).hex() for v in curves[0]['u']]==raw['blockU'],'Initial solution changed'
   for point in curves:
    point['toDoubleQR']=error(p,point['u'],point['delta'],p['QRU'],p['QRDelta'])
   last=curves[-1];valid=bool(last['toDoubleQR']['strictPass']);count+=1
   last['diagnostics']=audit.residuals(audit.chunks(p['A'],n),p['b'],audit.chunks(p['M'],n),p['c'],list(map(D,last['u'])))
   _,q,_=audit.physical(p,list(map(D,last['delta'])));_,qr,_=audit.physical(p,p['QRDelta']);last['retractedAngleToQR_rad']=max(audit.angle(a,b) for a,b in zip(q,qr));valid &= D(last['diagnostics']['LSstationarityInputRelative'])<=D('5e-14');ok &= valid
   result={'id':p['id'],'target':'exact frozen A64/b64 LS; M64 used only as preconditioner','inputSHA256':hashlib.sha256(path.read_bytes()).hexdigest(),'originalPass':p['originalPass'],'candidatePass':valid,'cost':cost,'curves':curves};(dest/path.name).write_text(json.dumps(audit.serialize(result),indent=2))
   print(p['id'],valid,flush=True)
 finally:host.close()
 assert count==83,count
 (dest/'STATUS.json').write_text(json.dumps({'linearComparisons':count,'candidatePass':ok,'oldStrictPass':74,'oldStrictFailure':9,'comparison':'validated binary64 QR on matching original augmented LS; frozen HP controls separately'},indent=2));return ok

def nonlinear(exe,inputs,dest,hostpath):
 dest.mkdir(parents=True,exist_ok=True);child=subprocess.Popen([str(exe),str(inputs),str(dest)],stdin=subprocess.PIPE,stdout=subprocess.PIPE);host=Host(hostpath);records=[];rawInputs=[];t=time.perf_counter()
 try:
  while True:
   cmd=struct.unpack('<I',readn(child.stdout,4))[0]
   if not cmd:break
   if cmd!=1:raise RuntimeError('Unknown nonlinear worker command')
   n,count=struct.unpack('<II',readn(child.stdout,8));sizes=list(struct.unpack('<'+str(count)+'I',readn(child.stdout,4*count)));rows=struct.unpack('<I',readn(child.stdout,4))[0];m=doubles(child.stdout,n*n);c=doubles(child.stdout,n);d=doubles(child.stdout,n);a=doubles(child.stdout,rows*n);b=doubles(child.stdout,rows)
   curves,cost=run(host,n,sizes,m,c,d,audit.chunks(a,n),b);delta=curves[-1]['delta'];child.stdin.write(struct.pack('<I',1));child.stdin.write(packd(delta));child.stdin.flush();records.append({'linearSolve':len(records),'n':n,'rows':rows,'cost':cost,'curves':curves});rawInputs.append(struct.pack('<III',n,count,rows)+struct.pack('<'+str(count)+'I',*sizes)+packd(m+c+d+a+b))
  child.stdin.close();child.wait(timeout=15)
  if child.returncode:raise RuntimeError('Nonlinear adapter failed')
 finally:
  host.close()
  if child.poll() is None:child.terminate();child.wait(timeout=10)
 wall=1000*(time.perf_counter()-t);(dest/'linear-inputs').mkdir(exist_ok=True)
 for i,raw in enumerate(rawInputs):(dest/'linear-inputs'/f'{i:04}.bin').write_bytes(raw)
 (dest/'worker.json').write_text(json.dumps({'wallMsIncludingIPC':wall,'linearSolves':len(records),'records':records},indent=2));results=[load(p) for p in dest.glob('*/result.json')];return len(results)==12 and all(r['qualified'] and not r['factorFailure'] for r in results)
if __name__=='__main__':
 mode=sys.argv[1]
 if mode=='suite':ok=suite(pathlib.Path(sys.argv[2]),pathlib.Path(sys.argv[3]),pathlib.Path(sys.argv[4]).resolve())
 elif mode=='nonlinear':ok=nonlinear(pathlib.Path(sys.argv[2]).resolve(),pathlib.Path(sys.argv[3]).resolve(),pathlib.Path(sys.argv[4]).resolve(),pathlib.Path(sys.argv[5]).resolve())
 else:raise ValueError(mode)
 sys.exit(0 if ok else 1)
