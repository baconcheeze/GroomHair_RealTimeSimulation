#include "HairBodyQueryBindings.h"
#include "HairCommon.h"
JPH_SHADER_FUNCTION_BEGIN(void, main, cHairPerVertexBatch, 1, 1)
 JPH_SHADER_PARAM_THREAD_ID(tid)
JPH_SHADER_FUNCTION_END
{
 uint vertex=tid.x+cNumStrands;if(vertex>=cNumVertices)return;
 float3 p=gPositions[vertex].mPosition;
 uint3 bin=(uint3)clamp(floor((p+4)*4),0,31);
 JPH_HairCollisionPlane contact=gCollisionPlanes[vertex];
 float best=-JPH_PlaneSignedDistance(contact.mPlane,p);
 for(uint word=0;word<(cBodyCount+31)/32;++word){
  uint mask=gBodyField[bin.x*4+word]&gBodyField[128+bin.y*4+word]&gBodyField[256+bin.z*4+word];
  while(mask!=0){uint bit=firstbitlow(mask);mask&=mask-1;uint id=word*32+bit;
   float4 a=gBodyState[id*4],b=gBodyState[id*4+1];float3 axis=b.xyz-a.xyz;
   float denominator=dot(axis,axis),t=denominator>1e-12?saturate(dot(p-a.xyz,axis)/denominator):0;
   float3 center=lerp(a.xyz,b.xyz,t),delta=p-center;float len=length(delta),penetration=a.w-len;
   if(penetration>best){float3 fallback=denominator>1e-12?normalize(cross(axis,abs(axis.y)<.9*sqrt(denominator)?float3(0,1,0):float3(1,0,0))):float3(1,0,0);float3 normal=len>1e-8?delta/len:fallback;
    contact.mPlane=JPH_PlaneFromPointAndNormal(center+a.w*normal,normal);contact.mShapeIndex=0x80000000u|id;best=penetration;}
  }
 }
 gCollisionPlanes[vertex]=contact;
}
