"""Final explicit bounded checks. No tuning, rebuild, or reference regeneration."""
from pathlib import Path
import subprocess,json
root=Path(__file__).resolve().parents[1];run=(root/'build/autonomous-run.txt').read_text().strip();r=root/'artifacts/autonomous'/run;out=r/'cycle-12/final-checks';out.mkdir(parents=True,exist_ok=True);results=[]
def go(name,config,args,bench=True,exe=None,cwd=root,expected=0):
 program=exe
 if program is None:program=root/f'build/vs2022/bin/{config}'/('AutonomousBench.exe' if bench else 'HairLabDX12.exe')
 args=list(map(str,args));args+=['--output',str(out/f'{name}.json')] if bench else ['--report',str(out/f'{name}.json'),'--log',str(out/f'{name}.log')]
 p=subprocess.run([str(program),*args],cwd=cwd,capture_output=True,text=True,timeout=300)
 if bench:(out/f'{name}.log').write_text(p.stdout+'\n'+p.stderr)
 results.append({'name':name,'exit':p.returncode,'expected':expected,'command':[str(program),*args],'cwd':str(cwd)});(out/'execution.json').write_text(json.dumps(results,indent=2));print(name,p.returncode,p.stderr[-200:],flush=True)
 return p.returncode==expected

go('baseline-full-all-600','Release',['--selection','all','--steps','600'])
go('baseline-full-heldout','Release',['--selection','heldout','--steps','60','--reference',r/'cycle-02/reference-heldout-refined.bin'])
for mode in [0,3]:
 for cache in ['cached','uncached']:
  go(f'frozen-{mode}-{cache}','Release',['--suite','frozen','--frozen',r/'cycle-01/frozen.bin','--initializer',mode,'--trajectory',out/f'frozen-{mode}-{cache}.bin',*(['--uncached'] if cache=='uncached' else [])])
 assert (out/f'frozen-{mode}-cached.bin').read_bytes()==(out/f'frozen-{mode}-uncached.bin').read_bytes()
for vertices in [2,5,32]:
 for zero in [False,True]:go(f'debug-analytic-{vertices}-{zero}','Debug',['--suite','analytic','--initializer','3','--analytic-vertices',vertices,*(['--zero-gravity'] if zero else [])])
go('debug-heldout','Debug',['--selection','heldout','--initializer','3','--steps','60','--reference',r/'cycle-02/reference-heldout-refined.bin'])
go('debug-heldout-kick','Debug',['--selection','heldout','--initializer','3','--steps','60','--kick','--seed','31871','--reference',r/'cycle-12/Release/reference-refined-heldout-kick-31871.bin'])
go('debug-all-kick-600','Debug',['--selection','all','--initializer','3','--steps','600','--kick','--seed','31871'])
common=['--scene','hair','--view','front','--width','1024','--height','1024','--draw','guides','--solver-mode','dynamics','--participating','all','--sim-initializer','guarded','--sweeps-per-step','32']
def cap(name,step,extra=[],frames=1,config='Release',exe=None,cwd=root):return go(name,config,[*common,'--sim-steps',step,'--frames',frames,'--capture',out/f'{name}.png',*extra],False,exe,cwd)
cap('guarded-rest',0)
cap('guarded-16',16,['--sim-diagnostics'])
cap('guarded-60',60,['--sim-diagnostics'])
cap('guarded-kick-60',60,['--sim-velocity','0.01,0.02,0.005','--sim-diagnostics'])
cap('guarded-600',600)
cap('guarded-reset',60,['--sim-reset-after'])
cap('guarded-60-idle',60,frames=130)
cap('guarded-debug-60',60,config='Debug')
cap('guarded-isolated-60',60,exe=root/'build/dynamics-isolated-Release/HairLabDX12.exe',cwd=out)
assert (out/'guarded-rest.png').read_bytes()==(out/'guarded-reset.png').read_bytes(),'Reset image differs'
assert (out/'guarded-60.png').read_bytes()==(out/'guarded-60-idle.png').read_bytes(),'Idle render frames changed dynamics'
assert (out/'guarded-60.png').read_bytes()==(out/'guarded-isolated-60.png').read_bytes(),'Isolated candidate differs'
(out/'image-invariants.json').write_text(json.dumps({'resetRestExact':True,'idleExact':True,'isolatedExact':True,'imageContentsInspected':False},indent=2))
if any(j['exit']!=j['expected'] for j in results):raise SystemExit(1)
