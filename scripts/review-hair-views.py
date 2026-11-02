"""Bounded actual-app all-angle review, with exact executable/asset provenance."""
import hashlib,json,subprocess,sys
from pathlib import Path
R=Path(__file__).resolve().parents[1]
name=sys.argv[1] if len(sys.argv)>1 else 'review'
O=R/'artifacts/hair-salon-finish'/name;O.mkdir(parents=True,exist_ok=True)
exe=R/'build/jolt-only/bin/Release/HairLabDX12.exe'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
views=['audit-front','audit-left','audit-right','audit-back','audit-top','audit-low-front','audit-low-left','audit-low-right','audit-low-back','bust','bust-back']
records=[]
for view in views:
    out=O/view;out.mkdir(parents=True,exist_ok=True)
    args=['--view',view,'--frames','1','--paused']
    if view.startswith('audit-'):args+=['--hide-body']
    command=[str(exe),*args,'--capture-dir',str(out),'--log',str(out/'app.log')]
    process=subprocess.run(command,cwd=R.parent,timeout=120)
    assert process.returncode==0,(view,process.returncode)
    state=json.loads((out/'step-0000.json').read_text());assert state['finite'] and state['maxRootAttachmentErrorMeters']<1e-5
    records.append({'view':view,'command':command,'exit':process.returncode,'pngSha256':sha(out/'step-0000.png')})
    (O/'views.json').write_text(json.dumps({'exeSha256':sha(exe),'inputs':{n:sha(R/'assets/moca'/n) for n in ('metahuman.hair','metahuman.materials','metahuman.ears','metahuman.skull')},'views':records,'qualification':'Actual app captures; visual inspection is recorded separately, not inferred from exit codes.'},indent=2))
    print(view,'PASS',flush=True)
