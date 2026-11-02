#include "GuideQuaternion.hlsl"
cbuffer Camera : register(b0) { row_major float4x4 viewProjection; uint frameStride; float axisLength; uint2 padding; };
StructuredBuffer<float3> Position : register(t0);
StructuredBuffer<float4> Orientation : register(t1);
StructuredBuffer<uint> SegmentStarts : register(t2);
struct Pixel {float4 position:SV_POSITION;float3 color:COLOR;};
Pixel VSMain(uint id:SV_VertexID) {
    uint s=(id/6)*frameStride,axis=(id%6)/2,v=SegmentStarts[s];
    float3 localAxis=axis==0?float3(1,0,0):axis==1?float3(0,1,0):float3(0,0,1);
    float3 p=.5*(Position[v]+Position[v+1])+QuatRotate(Orientation[s],localAxis)*axisLength*(id&1);
    Pixel o;o.position=mul(float4(p,1),viewProjection);o.color=localAxis;return o;
}
float4 PSMain(Pixel p):SV_TARGET {return float4(p.color,1);}
