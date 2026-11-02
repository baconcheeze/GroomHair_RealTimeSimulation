cbuffer Camera : register(b0) { row_major float4x4 viewProjection; };
struct Vertex { float3 position : POSITION; float3 color : COLOR; };
struct Pixel { float4 position : SV_POSITION; float3 color : COLOR; };
Pixel VSMain(Vertex v) {
    Pixel p;
    p.position = mul(float4(v.position, 1), viewProjection);
    p.color = v.color;
    return p;
}
float4 PSMain(Pixel p) : SV_TARGET { return float4(p.color, 1); }
