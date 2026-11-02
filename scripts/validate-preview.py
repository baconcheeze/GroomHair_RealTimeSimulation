"""Bounded application-owned captures; no desktop/window automation required."""
import argparse, pathlib, shutil, subprocess
p=argparse.ArgumentParser()
p.add_argument('--config',choices=['Debug','Release'],default='Debug')
args=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1]
exe=root/f'build/vs2022/bin/{args.config}/HairLabDX12.exe'
out=root/f'artifacts/{args.config}'
out.mkdir(parents=True,exist_ok=True)
def run(name, options):
    subprocess.run([str(exe),*options,'--log',str(out/f'{name}.log')],cwd=root,check=True,timeout=45)
    print('PASS',name,flush=True)
def capture(name,frames=120,**options):
    cmd=['--scene','hair','--view','front','--width','1024','--height','1024','--frames',str(frames),'--capture',str(out/f'{name}.png')]
    for key,value in options.items(): cmd += ['--'+key,str(value)]
    run(name,cmd)
for view in ['front','side','back']:
    run(view,['--scene','hair','--view',view,'--width','1024','--height','1024','--frames','120','--capture',str(out/f'{view}.png'),'--validate-rest','--report',str(out/f'{view}.json')])
for draw in ['guides','render','both','head','hulls']: capture(draw,draw=draw)
capture('original-head',head='original',draw='head')
capture('bootstrap',scene='bootstrap')
capture('padded-pitch',1,width=1023,height=769)
capture('before',1)
assert (out/'before.png').read_bytes()==(out/'front.png').read_bytes(),'Static before/after PNG differs'
print('PASS byte-identical PNG frames 1 and 120',flush=True)
package=root/f'build/isolated-{args.config}'
package.mkdir(parents=True,exist_ok=True)
shutil.copy2(exe,package/exe.name)
for folder in ['assets','shaders']: shutil.copytree(exe.parent/folder,package/folder,dirs_exist_ok=True)
subprocess.run([str(package/exe.name),'--scene','hair','--frames','120','--validate-rest','--report',str(out/'isolated.json'),'--capture',str(out/'isolated.png'),'--log',str(out/'isolated.log')],cwd=out,check=True,timeout=45)
print('PASS executable-relative isolated assets, different working directory',flush=True)
missing=root/f'build/missing-assets-{args.config}'
missing.mkdir(parents=True,exist_ok=True)
shutil.copy2(exe,missing/exe.name)
shutil.copytree(exe.parent/'shaders',missing/'shaders',dirs_exist_ok=True)
result=subprocess.run([str(missing/exe.name),'--scene','hair','--frames','1','--log',str(out/'missing-assets.log')],cwd=out,timeout=45)
assert result.returncode!=0 and 'Missing asset:' in (out/'missing-assets.log').read_text(),'Missing hair assets must fail'
print('PASS requested hair asset missing returns nonzero; no cube fallback',flush=True)
