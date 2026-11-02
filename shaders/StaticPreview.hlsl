cbuffer Camera : register(b0) { row_major float4x4 viewProjection; float4 debugColor; };
StructuredBuffer<float3> CurrentPositions : register(t0);
StructuredBuffer<uint> SegmentStarts : register(t1);
struct Pixel { float4 position : SV_POSITION; float3 normal : NORMAL; };
Pixel HairVS(uint id : SV_VertexID) {
    Pixel o;
    o.position=mul(float4(CurrentPositions[SegmentStarts[id/2]+id%2],1),viewProjection);
    o.normal=0;
    return o;
}
Pixel HeadVS(float3 position : POSITION,float3 normal : NORMAL) {
    Pixel o; o.position=mul(float4(position,1),viewProjection); o.normal=normal; return o;
}
Pixel HullVS(float3 position : POSITION) {
    Pixel o; o.position=mul(float4(position,1),viewProjection); o.normal=0; return o;
}
float4 FlatPS(Pixel p) : SV_TARGET { return debugColor; }
float4 HeadPS(Pixel p) : SV_TARGET {
    float3 n=normalize(p.normal);
    // Fixed scene-space diffuse lighting, neutral gray on both original and selected head.
    float light=.35+.65*saturate(dot(n,normalize(float3(-.3,.6,-.7))));
    return float4(debugColor.rgb*light,1);
}
