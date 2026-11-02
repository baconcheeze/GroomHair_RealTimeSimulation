"""Run with installed Blender in background. Read-only FBX -> native Moca clip.
No model import/export or source edits. Source physical world rotation deltas
are mapped into the original target bind skeleton, then baked as local quats.
"""
import bpy,math,json,struct,hashlib
from mathutils import Matrix,Vector,Quaternion
from pathlib import Path
R=Path(__file__).resolve().parents[1];A=R/'assets/moca';O=R/'artifacts/naruto-run';O.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(R/'assets/NarutoRun.fbx'))
arm=next(o for o in bpy.data.objects if o.type=='ARMATURE');scene=bpy.context.scene
action=arm.animation_data.action;start,end=map(float,action.frame_range);fps=scene.render.fps/scene.render.fps_base
count=round(end-start)+1;duration=(end-start)/fps;assert count>2 and duration>0
rig=json.loads((A/'rig.json').read_text(encoding='utf-8'));bones=rig['bones'];nj=len(bones);parents=[b['parent'] for b in bones]
node_index={b['node']:b['index'] for b in bones};human={b['bone']:node_index[b['node']] for b in rig['humanoid']['humanBones']}
byname={b['name']:b['index'] for b in bones}
rest=[];local=[]
for b in bones:
    n=b['bindLocal'];q=n.get('rotation',[0,0,0,1]);m=Matrix.LocRotScale(Vector(n.get('translation',[0,0,0])),Quaternion((q[3],*q[:3])),Vector(n.get('scale',[1,1,1])))
    local.append(m);rest.append(rest[b['parent']]@m if b['parent']>=0 else m)
mapping={'hips':'Hips','spine':'Spine','chest':'Spine2','neck':'Neck','head':'Head'}
for side in ('left','right'):
    cap=side.title()
    for key,src in [('Shoulder','Shoulder'),('UpperArm','Arm'),('LowerArm','ForeArm'),('Hand','Hand'),('UpperLeg','UpLeg'),('LowerLeg','Leg'),('Foot','Foot')]:mapping[side+key]=cap+src
    for finger in ('Thumb','Index','Middle','Ring','Little'):
        for k,part in enumerate(('Proximal','Intermediate','Distal')):mapping[side+finger+part]=cap+'Hand'+('Pinky' if finger=='Little' else finger)+str(k+1)
mapped={human[k]:'mixamorig:'+v for k,v in mapping.items() if k in human and 'mixamorig:'+v in arm.data.bones}
for side,suffix in [('Left','.L'),('Right','.R')]:
    for name in ('Toes'+suffix,'Toe'+suffix):
        if name in byname:mapped[byname[name]]='mixamorig:'+side+'ToeBase';break
C=Quaternion(Vector((0,1,0)),math.pi);Ci=C.conjugated()
srcHip=arm.data.bones['mixamorig:Hips'].head_local
srcLeg=sum((arm.data.bones['mixamorig:'+a].head_local-arm.data.bones['mixamorig:'+b].head_local).length for a,b in [('LeftUpLeg','LeftLeg'),('LeftLeg','LeftFoot')])
tgtLeg=sum((rest[human[a]].translation-rest[human[b]].translation).length for a,b in [('leftUpperLeg','leftLowerLeg'),('leftLowerLeg','leftFoot')])
ratio=tgtLeg/srcLeg
scene.frame_set(round(start));p0=arm.pose.bones['mixamorig:Hips'].matrix.translation.copy()
scene.frame_set(round(end));p1=arm.pose.bones['mixamorig:Hips'].matrix.translation.copy()
stride=Vector(((p1-p0).x,0,(p1-p0).z)).length*ratio;referenceSpeed=stride/duration
assert .1<stride<5 and .3<referenceSpeed<10
frames=[];worlds=[];maxBoneError=0
for frame in range(count):
    scene.frame_set(round(start)+frame);u=frame/(count-1);srcP=arm.pose.bones['mixamorig:Hips'].matrix.translation
    drift=p0.lerp(p1,u);rootOffset=C@Vector((srcP.x-drift.x,srcP.y-srcHip.y,srcP.z-drift.z))*ratio
    out=[];quats=[]
    for j,b in enumerate(bones):
        p=parents[j];restLocalR=local[j].to_quaternion()
        if j in mapped:
            name=mapped[j];delta=arm.pose.bones[name].matrix.to_quaternion()@arm.data.bones[name].matrix_local.to_quaternion().conjugated()
            desired=(C@delta@Ci)@rest[j].to_quaternion()
            q=out[p].to_quaternion().conjugated()@desired if p>=0 else desired
        else:q=restLocalR
        q.normalize();pos=local[j].translation.copy()
        if j==human['hips']:pos+=rootOffset
        m=Matrix.LocRotScale(pos,q,Vector((1,1,1)));out.append(out[p]@m if p>=0 else m)
        if p>=0:maxBoneError=max(maxBoneError,abs((out[j].translation-out[p].translation).length-(rest[j].translation-rest[p].translation).length))
        quats.append(q)
    frames.append([rootOffset,quats]);worlds.append(out)
# One constant floor alignment from actual skinned foot vertices, not a gait
# generator or per-frame projection. Preserve the authored vertical trajectory.
b=(A/'character.bin').read_bytes();nv=struct.unpack_from('<I',b,4)[0];nt=struct.unpack_from('<I',b,8+nv*12)[0]
off=12+nv*12+nt*12;nj2=struct.unpack_from('<I',b,off)[0];assert nj2==nj;off+=4
inverse=[]
for j in range(nj):
    f=struct.unpack_from('<16f',b,off+j*64);inverse.append(Matrix([f[k:k+4] for k in range(0,16,4)]).transposed())
off+=nj*64;nw=struct.unpack_from('<I',b,off)[0];off+=4;assert nw==4
foot=[]
for i in range(nv):
    p=Vector(struct.unpack_from('<3f',b,8+i*12))
    if p.y<.21:foot.append((p,struct.unpack_from('<IfIfIfIf',b,off+i*32)))
floorBind=min(p.y for p,w in foot);minima=[]
for out in worlds:
    mats=[out[j]@inverse[j] for j in range(nj)];lo=1e10
    for p,w in foot:
        v=sum(((mats[w[k]]@p)*w[k+1] for k in range(0,8,2)),Vector((0,0,0)))
        lo=min(lo,v.y)
    minima.append(lo)
floorCorrection=floorBind-min(minima)
for f in frames:f[0].y+=floorCorrection
# Normalize signs only; do not alter rotations, key timing, or loop endpoints.
for j in range(nj):
    for f in range(1,count):
        if frames[f-1][1][j].dot(frames[f][1][j])<0:frames[f][1][j].negate()
seam=max(2*math.acos(min(1,abs(frames[0][1][j].dot(frames[-1][1][j])))) for j in range(nj))
with (A/'naruto-run.bin').open('wb') as f:
    f.write(struct.pack('<4sIIIIfff',b'RUNA',1,nj,count,60,fps,duration,stride))
    for offset,qs in frames:
        f.write(struct.pack('<3f',*offset))
        for q in qs:f.write(struct.pack('<4f',q.x,q.y,q.z,q.w))
report={'source':'assets/NarutoRun.fbx','sourceSha256':hashlib.sha256((R/'assets/NarutoRun.fbx').read_bytes()).hexdigest(),
        'action':action.name,'frameRange':[start,end],'fps':fps,'durationSeconds':duration,'frames':count,'joints':nj,
        'mappedBones':{bones[j]['name']:s for j,s in mapped.items()},'sourceCentimetresToTargetMetres':ratio,
        'rootStrideMetres':stride,'referenceSpeedMetresPerSecond':referenceSpeed,'rootXZ':'Removed linear trajectory only; preserve within-cycle sway. WASD supplies world translation.',
        'orientation':'Mixamo +Z forward -> Moca -Z forward via Y 180 degrees; world rotation deltas -> target local quaternions',
        'floorCorrectionMetres':floorCorrection,'skinnedFootMinimumHeightRangeBeforeAlignment': [min(minima),max(minima)],
        'maxBoneLengthErrorMetres':maxBoneError,'loopQuaternionSeamMaxRadians':seam,
        'blender':bpy.app.version_string,'runtimeFormat':'RUNA v1; header32, frame root float3 + 315 local xyzw float4',
        'licence':'User-provided FBX; no licence supplied, not relabelled CC0',
        'outputSha256':hashlib.sha256((A/'naruto-run.bin').read_bytes()).hexdigest()}
(A/'naruto-run.json').write_text(json.dumps(report,indent=2));(O/'retarget.json').write_text(json.dumps(report,indent=2))
m=json.loads((A/'manifest.json').read_text(encoding='utf-8'))
for n in ('naruto-run.bin','naruto-run.json'):m['files'][n]=hashlib.sha256((A/n).read_bytes()).hexdigest()
(A/'manifest.json').write_text(json.dumps(m,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='mappedBones'},indent=2))
