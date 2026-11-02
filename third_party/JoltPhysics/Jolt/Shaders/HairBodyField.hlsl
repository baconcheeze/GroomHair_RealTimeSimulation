#include "HairBodyFieldBindings.h"
#include "HairCommon.h"
JPH_SHADER_FUNCTION_BEGIN(void, main, cHairPerVertexBatch, 1, 1)
 JPH_SHADER_PARAM_THREAD_ID(tid)
JPH_SHADER_FUNCTION_END
{
 uint slot=tid.x;if(slot>=384)return;
 uint word=slot%4,bin=(slot/4)%32,axis=slot/128;uint mask=0;
 for(uint j=0;j<32;++j){uint id=word*32+j;if(id>=cBodyCount)break;
  float4 a=gBodyState[id*4],b=gBodyState[id*4+1];
  float lo=min(a[axis],b[axis])-a.w-.25,hi=max(a[axis],b[axis])+a.w+.25;
  uint first=(uint)clamp(floor((lo+4)*4),0,31),last=(uint)clamp(floor((hi+4)*4),0,31);
  if(bin>=first&&bin<=last)mask|=1u<<j;
 }
 gBodyField[slot]=mask;
}
