import bpy,bmesh,struct,json,sys
from pathlib import Path
from mathutils import Vector
R=Path(__file__).resolve().parents[1];sys.path.insert(0,str(R/'scripts'))
from groom_surface import ScalpSurface
b=(R/'assets/moca/character.bin').read_bytes();head,nv=struct.unpack_from('<2I',b);o=8+12*nv
nt=struct.unpack_from('<I',b,o)[0];o+=4+12*nt;nj=struct.unpack_from('<I',b,o)[0];o+=4+64*nj
nw=struct.unpack_from('<I',b,o)[0];o+=4+8*nv*nw;nf=struct.unpack_from('<I',b,o)[0];o+=4+24*nj*nf
nh=struct.unpack_from('<I',b,o)[0];o+=4;out=[]
for i in range(nh):
 joint,n=struct.unpack_from('<2I',b,o);o+=8;ps=[Vector(struct.unpack_from('<3f',b,o+12*j)) for j in range(n)];o+=12*n
 out.append({'hull':i,'joint':joint,'points':n,'bounds':[[min(p[k] for p in ps) for k in range(3)],[max(p[k] for p in ps) for k in range(3)]],
             'pointsInAuricularBulge':sum(abs(p.x)>.060 and 1.20<p.y<1.28 and .007<p.z<.06 for p in ps)})
(R/'artifacts/hair-salon-finish/collider-audit.json').write_text(json.dumps(out,indent=2));print(out)
