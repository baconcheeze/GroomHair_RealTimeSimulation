"""Compare native diagnostics to retained Unity diagnostics and verify provenance."""
import hashlib, json, pathlib, re
root=pathlib.Path(__file__).resolve().parents[1]
manifest=json.loads((root/'assets/manifest.json').read_text(encoding='utf-8'))
checked=[]
for f in manifest['files']:
    assert hashlib.sha256(pathlib.Path(f['source']).read_bytes()).hexdigest()==f['sourceSHA256'],f['source']
    if 'staged' in f:
        assert hashlib.sha256((root/f['staged']).read_bytes()).hexdigest()==f['stagedSHA256'],f['staged']
    checked.append(f['source'])
text=(root/'assets/reference/Unity-validation.log').read_text(encoding='utf-8-sig')
report=json.loads((root/'artifacts/Debug/front.json').read_text())
comparisons={}
for key,label in [('guide','Guide'),('render','Render')]:
    match=re.search(label+r': strands=(\d+), vertices=(\d+), segments=(\d+), vertices/strand min=(\d+) max=(\d+)\s+Unity neck-local AABB min=\(([^)]+)\) max=\(([^)]+)\)\s+First strand: source ID=(\d+), root=\(([^)]+)\), tip=\(([^)]+)\), total rest length=([0-9.Ee+-]+) m',text)
    assert match, label
    values=match.groups()
    actual=report[key]
    for field,value in zip(['strands','vertices','segments','minVertices','maxVertices'],values[:5]): assert actual[field]==int(value)
    deltas={}
    for field,value in [('boundsMin',values[5]),('boundsMax',values[6]),('firstRoot',values[8]),('firstTip',values[9])]:
        expected=[float(x) for x in value.split(',')]
        delta=max(abs(a-b) for a,b in zip(actual[field],expected)); assert delta<1e-6
        deltas[field]=delta
    assert actual['firstSourceID']==int(values[7])
    delta=abs(actual['firstLengthMeters']-float(values[10])); assert delta<1e-6
    deltas['firstLengthMeters']=delta
    comparisons[key]=deltas
result={'sourceFilesUnchanged':True,'checkedSourceFiles':len(checked),'stagedHashesMatch':True,'unityCountComparison':'exact','unityFloatToleranceMeters':1e-6,'maxAbsDifferences':comparisons,'guideSubsetExact':report['guideSubsetExact'],'gpuInitialExact':report['gpuInitialExact'],'gpuFinalExact':report['gpuFinalExact']}
(root/'artifacts/migration-check.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
