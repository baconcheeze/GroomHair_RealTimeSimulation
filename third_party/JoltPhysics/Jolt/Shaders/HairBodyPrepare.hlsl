#include "HairBodyPrepareBindings.h"
#include "HairCommon.h"
float3 SkinBodyPoint(float3 p,float4 joints,float4 weights){
 float3 result=0;for(uint k=0;k<4;++k)if(weights[k]>0)result+=weights[k]*JPH_Mat44Mul3x4Vec3(gScalpJointMatrices[(uint)joints[k]],p);return result;
}
JPH_SHADER_FUNCTION_BEGIN(void, main, cHairPerVertexBatch, 1, 1)
 JPH_SHADER_PARAM_THREAD_ID(tid)
JPH_SHADER_FUNCTION_END
{
 uint id=tid.x;if(id>=cBodyCount)return;
 uint base=id*6, outBase=id*4;
 float4 ba=gBodyInput[base],bb=gBodyInput[base+1];
 float3 a=SkinBodyPoint(ba.xyz,gBodyInput[base+2],gBodyInput[base+3]);
 float3 b=SkinBodyPoint(bb.xyz,gBodyInput[base+4],gBodyInput[base+5]);
 float3 va=0,vb=0;
 if(cBodyReset==0){
  va=(a-JPH_Mat44Mul3x4Vec3(cDeltaTransform,gBodyState[outBase].xyz))*cBodyInvDt;
  vb=(b-JPH_Mat44Mul3x4Vec3(cDeltaTransform,gBodyState[outBase+1].xyz))*cBodyInvDt;
 }
 float3 axis=b-a;float lengthSq=dot(axis,axis);
 float3 angular=lengthSq>1e-12?cross(axis,vb-va)/lengthSq:float3(0,0,0);
 gBodyState[outBase]=float4(a,ba.w);gBodyState[outBase+1]=float4(b,ba.w);
 gBodyState[outBase+2]=float4((va+vb)*.5,0);gBodyState[outBase+3]=float4(angular,0);
 // Exact capsule silhouette meridians and two equator rings; 200 line vertices.
 float3 n=lengthSq>1e-12?axis*rsqrt(lengthSq):float3(0,1,0);
 float3 u=normalize(cross(n,abs(n.y)<.9?float3(0,1,0):float3(1,0,0))),v=cross(n,u);
 uint w=id*200;
 for(uint ring=0;ring<2;++ring)for(uint s=0;s<24;++s){
  float t0=6.28318530718*s/24,t1=6.28318530718*(s+1)/24;float3 center=ring==0?a:b;
  gBodyWire[w++]=center+ba.w*(cos(t0)*u+sin(t0)*v);gBodyWire[w++]=center+ba.w*(cos(t1)*u+sin(t1)*v);
 }
 for(uint m=0;m<2;++m)for(uint s=0;s<24;++s){
  float t0=6.28318530718*s/24,t1=6.28318530718*(s+1)/24;float3 radial=m==0?u:v;
  gBodyWire[w++]=(cos(t0)<0?a:b)+ba.w*(cos(t0)*n+sin(t0)*radial);
  gBodyWire[w++]=(cos(t1)<0?a:b)+ba.w*(cos(t1)*n+sin(t1)*radial);
 }
 for(uint k=0;k<4;++k){float3 offset=(k==0?u:k==1?-u:k==2?v:-v)*ba.w;gBodyWire[w++]=a+offset;gBodyWire[w++]=b+offset;}
}
