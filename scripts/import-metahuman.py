"""Offline user Alembic -> fitted native Jolt groom. Run in Blender 5.1.
Original purchased files are read-only; no Unreal/Blender dependency at runtime.
"""
import bpy, json, math, struct, hashlib, sys
from collections import Counter
from array import array
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
from groom_surface import ScalpSurface
from ear_surface import EarEnvelope
from groom_drape import ScalpDrape
A=ROOT/'assets/moca';O=ROOT/'artifacts/hair-ear-wrap';O.mkdir(parents=True,exist_ok=True)
source=ROOT/'assets/MetaHuman/long_wavy_twin_ponytail_hair/Long_Wavy_Twin_Ponytail_Hair.abc'
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.alembic_import(filepath=str(source),as_background_job=False,set_frame_range=False)
surface=ScalpSurface(A/'character.bin')
def triangles(node):
    if node[3] is None:return node[2]
    return triangles(node[2])+triangles(node[3])
bvh=BVHTree.FromPolygons(surface.v,triangles(surface.tree),all_triangles=True)
ears=[EarEnvelope(surface.v,side) for side in (-1,1)]
finish=ScalpDrape(surface,bvh,ears,A/'metahuman-wisps.shape')
groups=[('D_MAIN_HAIR_002',0),('D_FRONT_00',1),('D_MAIN_HAIR',0),('D_FRONT_01',3),('D_PONY_TAIL',2),('D_OTHER_01',7),('D_SIDE_01',4),('D_SIDE_02',4)]
# Blender evaluates the imported NURBS, then samples true curve arc length.
tree=bpy.data.node_groups.new('OfflineJoltCurveSampling','GeometryNodeTree')
tree.interface.new_socket(name='Geometry',in_out='INPUT',socket_type='NodeSocketGeometry')
tree.interface.new_socket(name='Geometry',in_out='OUTPUT',socket_type='NodeSocketGeometry')
ni=tree.nodes.new('NodeGroupInput');no=tree.nodes.new('NodeGroupOutput');res=tree.nodes.new('GeometryNodeResampleCurve');res.inputs['Mode'].default_value='Count';res.inputs['Count'].default_value=32
tree.links.new(ni.outputs['Geometry'],res.inputs['Curve']);tree.links.new(res.outputs['Curve'],no.inputs['Geometry'])
points=[];materials=[];records=[];rootCorrections=[];assigned=set();repairDepths=[];protectedErrors=[]
earRepairDepths=[];rearGapCorrections=[]
def fit(p):
    # Imported file is Z-up, front -Y, centimetre-scale. Explicit artist fit
    # to Moca: right=X, up=Y, front=-Z. This is not a renderer conversion.
    return Vector((p.x*.010,1.270+(p.z-160.56)*.0091,.022+(p.y-.5)*.0085))
# Shared tie deformation prevents independently translated cap tips from
# forming spikes or separating from the ponytail root volume.
ties={-1:[],1:[]};anchors={-1:[],1:[]};bundle_samples={-1:[],1:[]}
pony=next(o for o in bpy.data.objects if o.type=='CURVES' and ':D_PONY_TAIL_splineDescriptionShape' in o.name)
probe=pony.modifiers.new('TieRootProbe','NODES');probe.node_group=tree;bpy.context.view_layer.update()
probe_data=pony.evaluated_get(bpy.context.evaluated_depsgraph_get()).data
probe_co=array('f',[0.])*(3*len(probe_data.points));probe_data.attributes['position'].data.foreach_get('vector',probe_co)
for curve in probe_data.curves:
    first=curve.first_point_index
    p=fit(pony.matrix_world@Vector(probe_co[first*3:first*3+3]))
    next_p=fit(pony.matrix_world@Vector(probe_co[(first+1)*3:(first+1)*3+3]))
    side=-1 if p.x<0 else 1;anchor=finish.root(p,2,record=False)
    ties[side].append(anchor-p);bundle_samples[side].append(p.lerp(next_p,.35))
pony.modifiers.remove(probe)
finish.set_ties(ties)
for side,values in bundle_samples.items():
    anchors[side]=[finish.protect(p+finish.tie[side],.002) for p in values]
finish.set_tie_anchors(anchors)
for key,region in groups:
    obj=next(o for o in bpy.data.objects if o.type=='CURVES' and ':'+key+'_splineDescriptionShape' in o.name)
    assert obj.name not in assigned;assigned.add(obj.name)
    sourceCount=len(obj.data.curves);sourcePoints=len(obj.data.points)
    modifier=obj.modifiers.new('Offline32PointSampling','NODES');modifier.node_group=tree
    bpy.context.view_layer.update();evaluated=obj.evaluated_get(bpy.context.evaluated_depsgraph_get());data=evaluated.data
    co=array('f',[0.])*(3*len(data.points));data.attributes['position'].data.foreach_get('vector',co)
    offsets=[c.first_point_index for c in data.curves]+[len(data.points)]
    assert len(offsets)-1==sourceCount
    first=len(materials)
    trimmedCount=0;dropped=[]
    for strandIndex,(start,end) in enumerate(zip(offsets,offsets[1:])):
        assert end-start==32
        original=[fit(obj.matrix_world@Vector(co[i*3:i*3+3])) for i in range(start,end)]
        strand=finish.apply(original,region,len(materials),key)
        correction=strand[0]-original[0];rootCorrections.append(correction.length)
        if region in (1,3,4,7):
            protectedErrors.extend((a-b-correction).length for a,b in zip(strand,original))
        assert all(math.isfinite(v) for p in strand for v in p)
        assert min((b-a).length for a,b in zip(strand,strand[1:]))>1e-7
        contact=False
        if region in (0,2):
            sweep_margin=.018 if region==0 else .008
            for p in strand:
                for ear in ears:
                    lo,hi=ear.bounds
                    if all(lo[k]-sweep_margin<=p[k]<=hi[k]+sweep_margin for k in range(3)) and ear.separation(p)[0]<sweep_margin:
                        contact=True;break
                if contact:break
        # Keep physical values identical; this Jolt material split only gives
        # contact-critical curves direct simulation instead of offset parenting.
        material=(5 if region==0 else 6) if contact else region
        points.extend(tuple(p) for p in strand);materials.append(material)
    records.append({'sourceObject':obj.name,'region':region,'firstStrand':first,'sourceStrandCount':sourceCount,'strandCount':len(materials)-first,'originalControlPoints':sourcePoints,'stagedPointsPerStrand':32,'removed':False,'trimmedStrands':trimmedCount,'removedSourceStrandIndices':dropped})
    print('Fitted',key,'strands',len(materials)-first,flush=True)
assert len(assigned)==len([o for o in bpy.data.objects if o.type=='CURVES'])
header=bytearray((A/'studio.hair').read_bytes()[:128]);struct.pack_into('<IIII',header,4,len(materials),len(points),2,31)
header[40:128]=b'User purchased Alembic; private conversion; see metahuman.NOTICE.txt'.ljust(88,b'\0')
with (A/'metahuman.hair').open('wb') as f:
    f.write(header)
    for p in points:f.write(struct.pack('<3f',*p))
with (A/'metahuman.materials').open('wb') as f:
    f.write(struct.pack('<I',len(materials)))
    for m in materials:f.write(struct.pack('<I',m))
with (A/'metahuman.ears').open('wb') as f:
    f.write(struct.pack('<4sII',b'EARH',1,len(ears)))
    for ear in ears:
        f.write(struct.pack('<I',len(ear.points)))
        for p in ear.points:f.write(struct.pack('<3f',*p))
finish.write_skull(A/'metahuman.skull')
report={'name':'Ear-rim side coverage / twin ponytail','profile':'ear-wrap-v9','source':str(source.relative_to(ROOT)),
    'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'blender':bpy.app.version_string,
    'curveEvaluation':'Imported NURBS evaluated at 32 points; caps continuously draped and faired, then arc-length sampled once. Other curls retain source shape with smooth contact displacements. Strand/root-to-tip order retained.',
    'guides':'All original curves retained. Jolt selects 5% of non-contact cap/tails; all face-framing locks, 117 ear wisps and curves in the ear sweep envelope simulate directly. No original Unreal guide flags are exposed by Blender.',
    'earSweepEnvelopeMetres':{'cap':.018,'ponytail':.008},
    'guideFractions':{'0':.05,'1':.05,'2':.05,'3':1.0,'4':1.0,'5':1.0,'6':1.0,'7':1.0},
    'materialRegionCounts':dict(Counter(materials)),
    'fit':{'x':'.010*x','y':'1.270 + .0091*(z-160.56)','z':'.022 + .0085*(y-.5)','units':'native metres, +Y up, -Z forward','rootProjection':'Continuous anatomical scalp hairline, shared ponytail endpoints and measured smooth skull drape. Bangs/face/nape curls retain source shape; contact corrections are spread along the curve. No deletion or trimming.'},
    'groups':records,'renderStrands':len(materials),'vertices':len(points),'bounds':[[min(p[k] for p in points) for k in range(3)],[max(p[k] for p in points) for k in range(3)]],
    'rootCorrectionMetres':{'max':max(rootCorrections),'mean':sum(rootCorrections)/len(rootCorrections)},
    'originalSourceStrands':sum(g['sourceStrandCount'] for g in records),'removedStrands':sum(g['sourceStrandCount']-g['strandCount'] for g in records),
    'referencePhotos':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((ROOT/'assets/MetaHuman').glob('*.jpg'))},
    'protectedFeatures':['centre part and swept bangs','cheek/jaw-length face framing','fine ear-front sideburns','low ponytail junctions','nape coverage','long wavy twin tails'],
    'localClearanceRepairs':{'points':len(repairDepths),'maxMetres':max(repairDepths,default=0)},
    'protectedCurveNonrigidChange':{'points':len(protectedErrors),'maxMetres':max(protectedErrors,default=0),'rmsMetres':math.sqrt(sum(e*e for e in protectedErrors)/len(protectedErrors))},
    'earFit':{'geometry':[e.record() for e in ears],'pointClearanceMetres':.0022,'segmentClearanceMetres':.0015,'runtimeColliders':'Measured skull excluding auricles, separate Head-attached ears, existing neck/chest; upstream Jolt collision.'},
    'authoringControls':{'file':'metahuman-wisps.shape','curves':117,'sha256':hashlib.sha256((A/'metahuman-wisps.shape').read_bytes()).hexdigest(),'purpose':'Preserve fitted original ear curls while redraping the unrelated cap.'},
    'sideFinish':finish.record(),
    'license':'User-provided purchased asset. No redistribution license granted by this conversion. Keep package private; original author/license applies.',
    'appearance':'Native black strand material. Unreal shader graphs and RootUV pink/blue texture are not reproduced.',
    'stagedSha256':{n:hashlib.sha256((A/n).read_bytes()).hexdigest() for n in ['metahuman.hair','metahuman.materials','metahuman.ears','metahuman.skull']}}
(A/'metahuman.json').write_text(json.dumps(report,indent=2));(O/'import.json').write_text(json.dumps(report,indent=2))
manifest=json.loads((A/'manifest.json').read_text(encoding='utf-8'))
for n in ['metahuman.hair','metahuman.materials','metahuman.ears','metahuman.skull','metahuman.json','metahuman.NOTICE.txt','metahuman-wisps.shape','metahuman-wisps.json']:manifest['files'][n]=hashlib.sha256((A/n).read_bytes()).hexdigest()
(A/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
