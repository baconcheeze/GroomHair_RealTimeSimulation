"""Regression for rejected/accepted solver command-line combinations (no Unity access)."""
import argparse,json,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('--config',choices=['Debug','Release'],default='Debug');p.add_argument('--build-dir',default='build/vs2022');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1]
exe=root/a.build_dir/f'bin/{a.config}/HairLabDX12.exe'
out=root/f'artifacts/window/{a.config}/review';out.mkdir(parents=True,exist_ok=True)
rejected=[
    ['--scene','bootstrap','--window-target','rest','--window-sweeps','1'],
    ['--scene','bootstrap','--solver-mode','window'],
    ['--scene','bootstrap','--orientation-sweeps','1'],
    ['--scene','bootstrap','--material-frames'],
    ['--solver-mode','static','--window-target','rest'],
    ['--window-target','rest','--solver-mode','static'],
    ['--solver-mode','orientation','--window-unpin-position'],
    ['--solver-mode','window','--orientation-sweeps','0'],
    ['--window-h','.01','--unpin-root'],
    ['--test','window-solver','--window-target','rest'],
    ['--test','orientation-local','--orientation-auto'],
    ['--window-sweeps','1'],
]
accepted=[
    ['--scene','bootstrap'],
    ['--solver-mode','static'],
    ['--window-unpin-position'],
    ['--solver-mode','window','--window-target','rest','--window-perturb','joint','--window-sweeps','2'],
    ['--orientation-perturb','bend','--orientation-sweeps','2'],
]
results=[]
for should_pass,cases in [(False,rejected),(True,accepted)]:
    for args in cases:
        result=subprocess.run([str(exe),*args,'--frames','1','--log',str(out/'cli-smoke.log')],cwd=out,timeout=30,capture_output=True)
        assert (result.returncode==0)==should_pass,(args,result.returncode)
        results.append({'args':args,'expectedSuccess':should_pass,'exitCode':result.returncode})
(out/'cli.json').write_text(json.dumps({'passed':True,'rejected':len(rejected),'accepted':len(accepted),'cases':results},indent=2)+'\n')
print(f'PASS CLI: {len(rejected)} rejected / {len(accepted)} accepted')
