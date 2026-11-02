"""Read-only local Alembic inventory through the installed Blender importer."""
import bpy, json, hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/metahuman';OUT.mkdir(parents=True,exist_ok=True)
path=ROOT/'assets/MetaHuman/long_wavy_twin_ponytail_hair/Long_Wavy_Twin_Ponytail_Hair.abc'
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.alembic_import(filepath=str(path),as_background_job=False,set_frame_range=False)
report={'source':str(path.relative_to(ROOT)),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'blender':bpy.app.version_string,'objects':[]}
for o in bpy.data.objects:
    item={'name':o.name,'type':o.type,'matrixWorld':[list(v) for v in o.matrix_world],
          'properties':{k:str(o[k]) for k in o.keys()},'dataProperties':{k:str(o.data[k]) for k in o.data.keys()} if o.data else {}}
    if o.type=='CURVE':
        counts=[len(s.points) if s.type!='BEZIER' else len(s.bezier_points) for s in o.data.splines]
        points=[o.matrix_world@p.co.to_3d() for s in o.data.splines for p in s.points]
        roots=[o.matrix_world@s.points[0].co.to_3d() for s in o.data.splines if len(s.points)]
        item.update(strands=len(counts),points=sum(counts),minPoints=min(counts),maxPoints=max(counts),splineTypes=sorted(set(s.type for s in o.data.splines)))
        if points:item['bounds']=[[min(p[k] for p in points) for k in range(3)],[max(p[k] for p in points) for k in range(3)]]
        if roots:item['rootBounds']=[[min(p[k] for p in roots) for k in range(3)],[max(p[k] for p in roots) for k in range(3)]]
        item['firstPoints']=[list(p) for p in points[:5]]
    elif o.type=='MESH':item.update(vertices=len(o.data.vertices),polygons=len(o.data.polygons))
    elif o.type=='CURVES':
        from array import array
        from mathutils import Vector
        co=array('f',[0.])*(3*len(o.data.points));o.data.attributes['position'].data.foreach_get('vector',co)
        points=[o.matrix_world@Vector(co[i:i+3]) for i in range(0,len(co),3)]
        offsets=[c.first_point_index for c in o.data.curves]+[len(points)]
        roots=[points[i] for i in offsets[:-1]]
        item.update(points=len(o.data.points),curves=len(o.data.curves),attributes=[a.name for a in o.data.attributes],
            counts=sorted(set(b-a for a,b in zip(offsets,offsets[1:]))),
            bounds=[[min(p[k] for p in points) for k in range(3)],[max(p[k] for p in points) for k in range(3)]],
            rootBounds=[[min(p[k] for p in roots) for k in range(3)],[max(p[k] for p in roots) for k in range(3)]],
            firstPoints=[list(p) for p in points[:5]],firstTip=list(points[offsets[1]-1]),
            curveTypes=sorted(set(v.value for v in o.data.attributes['curve_type'].data)))
    report['objects'].append(item)
(OUT/'inspect.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
