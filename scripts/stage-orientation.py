"""Read-only orientation source staging; never writes into Unity."""
import argparse, hashlib, json, pathlib
p=argparse.ArgumentParser(); p.add_argument('--source',type=pathlib.Path,required=True); args=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1]; base=args.source.resolve()
ref=root/'references/orientation'; ref.mkdir(parents=True,exist_ok=True)
files=['Runtime/GuideHairResources.cs','Runtime/GuideHairFrames.cs','Runtime/GuideHairOrientationSolver.cs','Runtime/GuideHairRuntime.cs','Shaders/GuideQuaternion.hlsl','Shaders/GuideOrientationLocal.hlsl','Shaders/GuideMaterialFrames.shader','Resources/StableCosseratOrientation.compute','ORIENTATION_SOLVER.md','Editor/OrientationLocalReference.cs','Editor/OrientationRobustnessValidation.cs','Editor/OrientationRobustnessMathValidation.cs','Editor/OrientationRobustnessGpuValidation.cs','Editor/OrientationLocalValidation.compute','Editor/GuideHairOrientationValidation.cs']
records=[]
def copy(path,target):
    raw=path.read_bytes(); target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(raw)
    records.append({'source':str(path),'copied':str(target.relative_to(root)),'sha256':hashlib.sha256(raw).hexdigest()})
for name in files: copy(base/'Assets/HairRest'/name,ref/name)
for name in ['REPORT.md','local-cases.csv','local-gpu.txt','cpu-math.txt','Regression/iterations.csv','Regression/summary.txt']:
    copy(base/'Tools/HairRest/Validation/OrientationRobustness'/name,ref/'historical'/name)
copy(base/'Tools/HairRest/References/StableCosserat-CPU-LICENSE.txt',ref/'StableCosserat-CPU-LICENSE.txt')
copy(base/'Tools/HairRest/Validation/GuideState/current.txt',ref/'historical/guide-state.txt')
shader=root/'shaders/orientation'; shader.mkdir(parents=True,exist_ok=True)
for name in ['GuideQuaternion.hlsl','GuideOrientationLocal.hlsl']:
    raw=(ref/'Shaders'/name).read_bytes()
    if name=='GuideOrientationLocal.hlsl':
        assert all(s in raw for s in [b'HardDirection',b'4.7683716e-7',b'OrientationAccept',b'root<96'])
    (shader/name).write_bytes(raw)
text=(ref/'Resources/StableCosseratOrientation.compute').read_text(encoding='utf-8-sig')
text=text.replace('#pragma kernel SolveOrientation','// Native DXC entry: SolveOrientation; numerical body unchanged.')
text=text.replace('../Shaders/GuideOrientationLocal.hlsl','GuideOrientationLocal.hlsl')
for i,(kind,name) in enumerate([('Range','_Ranges'),('float3','_Position'),('float','_RestLength'),('float4','_RestBendTwist'),('float4','_RestOrientation')]):
    text=text.replace(f'StructuredBuffer<{kind}> {name};',f'StructuredBuffer<{kind}> {name} : register(t{i});')
for i,(kind,name) in enumerate([('float4','_Orientation'),('uint','_Error'),('SolveStats','_Stats')]):
    text=text.replace(f'RWStructuredBuffer<{kind}> {name};',f'RWStructuredBuffer<{kind}> {name} : register(u{i});')
for decl in ['uint _FirstChunk;','uint _Strands,_Vertices,_Segments,_Bends,_Participating,_Iterations,_PinRoot;','float _BaseSS,_BaseBT;']: text=text.replace(decl,'')
text='cbuffer Parameters : register(b0) {\n uint _Strands,_Vertices,_Segments,_Bends;\n uint _Participating,_Iterations,_PinRoot,_FirstChunk;\n float _BaseSS,_BaseBT; uint2 _ParameterPadding;\n};\n'+text
(shader/'GuideSolve.hlsl').write_text(text,encoding='utf-8')
text=(ref/'Editor/OrientationLocalValidation.compute').read_text(encoding='utf-8-sig')
text=text.replace('#pragma kernel LegacyLocal','').replace('#pragma kernel RobustLocal','').replace('../Shaders/','')
text=text.replace('StructuredBuffer<float4> _Inputs;','StructuredBuffer<float4> _Inputs : register(t0);').replace('RWStructuredBuffer<float4> _Outputs;','RWStructuredBuffer<float4> _Outputs : register(u0);').replace('uint _Count;','cbuffer Parameters : register(b0) { uint _Count; uint3 _Padding; };')
(shader/'LocalValidation.hlsl').write_text(text,encoding='utf-8')
for f in sorted(shader.glob('*')): records.append({'native':str(f.relative_to(root)),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()})
(ref/'manifest.json').write_text(json.dumps({'version':1,'records':records},indent=2)+'\n')
print('Staged',len(records),'source/native records; robust numerical helpers copied byte-for-byte')
