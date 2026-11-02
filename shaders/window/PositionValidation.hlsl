#include "PositionLocal.hlsl"
StructuredBuffer<float4> Inputs:register(t0);
RWStructuredBuffer<float4> Outputs:register(u0);
cbuffer Parameters:register(b0){uint Count;uint3 Padding;};
[numthreads(64,1,1)]void PositionValidation(uint3 id:SV_DispatchThreadID){
    uint i=id.x;if(i>=Count)return;uint b=i*6;
    float3 result;float diagonal;
    bool ok=SolvePositionLocal(Inputs[b].xyz,Inputs[b+1].xyz,Inputs[b].w,
        Inputs[b+2].xyz,Inputs[b+1].w,Inputs[b+4].xyz,
        Inputs[b+3].xyz,Inputs[b+2].w,Inputs[b+5].xyz,Inputs[b+3].w!=0,result,diagonal);
    Outputs[i*2]=float4(result,diagonal);Outputs[i*2+1]=float4(ok?1:0,0,0,0);
}
