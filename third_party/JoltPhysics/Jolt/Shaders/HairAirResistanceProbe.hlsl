#include "HairAirResistanceProbeBindings.h"
#include "HairAirResistance.h"
JPH_SHADER_FUNCTION_BEGIN(void, main, 64, 1, 1)
    JPH_SHADER_PARAM_THREAD_ID(tid)
JPH_SHADER_FUNCTION_END
{
    if(tid.x>=12)return;
    uint i=tid.x*4;
    float3 transport=HairAirTransportVelocity(gInput[i+1].xyz,gInput[i+2].xyz,gInput[i+1].w,gInput[i+2].w);
    float3 velocity=gInput[i].xyz;
    uint iterations=(uint)gInput[i+3].x;
    for(uint j=0;j<iterations;++j)velocity=HairApplyAirResistance(velocity,transport,gInput[i].w);
    gOutput[tid.x*2]=float4(velocity,0);
    gOutput[tid.x*2+1]=float4(transport,0);
}
