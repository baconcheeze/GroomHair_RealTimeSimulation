"""Bounded guide-only capture of the best tested, still incomplete research candidate."""
import argparse,datetime,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('--config',choices=['Debug','Release'],default='Release');p.add_argument('--steps',type=int,default=60);p.add_argument('--interactive',action='store_true');a=p.parse_args()
if not 0<=a.steps<=10000:p.error('steps must be 0..10000')
root=pathlib.Path(__file__).resolve().parents[1];exe=root/f'build/vs2022/bin/{a.config}/HairLabDX12.exe'
marker=root/'build/autonomous-run.txt';run=marker.read_text().strip() if marker.exists() else 'manual'
stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ');out=root/'artifacts/autonomous'/run/'manual'/stamp;out.mkdir(parents=True)
args=['--scene','hair','--draw','guides','--view','front','--solver-mode','dynamics','--participating','all','--sim-initializer','guarded','--sim-h',str(1/120),'--sweeps-per-step','32','--log',str(out/'run.log')]
print('Research candidate: strain and trajectory gates remain unmet; see REPORT.md.',flush=True)
if not a.interactive:args+=['--sim-steps',str(a.steps),'--frames','1','--capture',str(out/'guides.png'),'--report',str(out/'report.json')]
r=subprocess.run([str(exe),*args],cwd=root,timeout=None if a.interactive else 300);print(out);raise SystemExit(r.returncode)
