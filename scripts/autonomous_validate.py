"""Bounded explicit autonomous finalists. Never builds or changes reference expectations.
Phases are manually invoked once; no self-relaunch. JSON metrics are retained on gate failure.
"""
import argparse,json,pathlib,subprocess,time
p=argparse.ArgumentParser();p.add_argument('--phase',choices=['stress','references','timing','analytic'],required=True);p.add_argument('--config',choices=['Debug','Release'],default='Release');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];run=(root/'build/autonomous-run.txt').read_text().strip();out=root/'artifacts/autonomous'/run/'cycle-12'/a.config;out.mkdir(parents=True,exist_ok=True);exe=root/f'build/vs2022/bin/{a.config}/AutonomousBench.exe';records=[]
def go(name,args,timeout=300):
 start=time.monotonic();r=subprocess.run([str(exe),*map(str,args),'--output',str(out/f'{name}.json')],cwd=root,capture_output=True,text=True,timeout=timeout)
 (out/f'{name}.log').write_text(r.stdout+'\n'+r.stderr);records.append({'name':name,'command':[str(exe),*map(str,args),'--output',str(out/f'{name}.json')],'exit':r.returncode,'wallSecondsNotGPU':time.monotonic()-start});(out/f'{a.phase}-execution.json').write_text(json.dumps(records,indent=2));print(name,r.returncode,r.stderr[-180:],flush=True)
 return r.returncode
if a.phase=='stress':
 go('all-rest-600',['--selection','all','--initializer','3','--steps','600'],600)
 for seed in [31871,912773]:go(f'all-kick-{seed}-600',['--selection','all','--initializer','3','--steps','600','--kick','--seed',seed],600)
 go('all-zero-gravity-120',['--selection','all','--initializer','3','--steps','120','--zero-gravity'],300)
 go('inactive-1-of-100',['--selection','100','--participating','1','--initializer','3','--steps','120'],300)
 for n in ['a','b']:go('reset-replay-'+n,['--selection','heldout','--initializer','3','--steps','60','--kick','--seed','912773','--trajectory',out/f'replay-{n}.bin'])
 assert (out/'replay-a.bin').read_bytes()==(out/'replay-b.bin').read_bytes(),'Replay differs'
elif a.phase=='references':
 refs=root/'artifacts/autonomous'/run/'cycle-02'
 for selection in ['train','heldout']:
  for seed in [31871,912773]:
   name=f'{selection}-kick-{seed}';common=['--selection',selection,'--steps','60','--kick','--seed',seed]
   good=go('reference-'+name,['--suite','reference',*common,'--trajectory',out/f'reference-{name}.bin'])
   tight=go('reference-refined-'+name,['--suite','reference',*common,'--ref-tolerance','.1','--trajectory',out/f'reference-refined-{name}.bin'])
   if good==0 and tight==0:
    go('candidate-'+name,[*common,'--initializer','3','--reference',out/f'reference-refined-{name}.bin'])
    import struct,math
    def values(path):data=path.read_bytes();return struct.unpack('<'+'f'*((len(data)-16)//4),data[16:])
    x=values(out/f'reference-{name}.bin');y=values(out/f'reference-refined-{name}.bin');errors=[sum((x[i+k]-y[i+k])**2 for k in range(3))**.5 for i in range(0,len(x),3)];j={'trajectoryRefinementMax':max(errors),'trajectoryRefinementRMS':(sum(e*e for e in errors)/len(errors))**.5};j['qualified']=j['trajectoryRefinementMax']<5e-5 and j['trajectoryRefinementRMS']<1e-5;(out/f'qualification-{name}.json').write_text(json.dumps(j,indent=2))
  go('candidate-'+selection,['--selection',selection,'--initializer','3','--steps','60','--reference',refs/f'reference-{selection}-refined.bin'])
elif a.phase=='timing':
 for n in ['10','100','all']:
  for guess in ['0','3']:go(f'timing-{n}-{guess}',['--suite','timing','--selection',n,'--initializer',guess,'--sweeps','32'])
elif a.phase=='analytic':
 for vertices in ['2','5','32']:
  for zero in [False,True]:go(f'analytic-{vertices}-{zero}',['--suite','analytic','--initializer','3','--analytic-vertices',vertices,*(['--zero-gravity'] if zero else [])])
 for n in [1,4,8,16,32]:go(f'budget-{n}',['--selection','heldout','--initializer','3','--sweeps',n,'--steps','60','--reference',root/'artifacts/autonomous'/run/'cycle-02/reference-heldout-refined.bin'])
 go('h240',['--selection','heldout','--initializer','3','--steps','120','--h',str(1/240)])
if any(r['exit'] for r in records):raise SystemExit(1)
