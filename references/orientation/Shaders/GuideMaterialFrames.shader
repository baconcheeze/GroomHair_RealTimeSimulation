Shader "HairRest/Guide Material Frames"
{
    SubShader { Tags { "RenderPipeline"="UniversalPipeline" "Queue"="Overlay" }
    Pass { ZWrite Off ZTest Always Cull Off
    HLSLPROGRAM
    #pragma target 4.5
    #pragma vertex Vert
    #pragma fragment Frag
    #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
    #include "GuideQuaternion.hlsl"
    StructuredBuffer<float3> _Position;
    StructuredBuffer<float4> _Orientation;
    StructuredBuffer<uint> _SegmentStarts;
    float4x4 _HairLocalToWorld;
    uint _FrameStride;
    float _AxisLength;
    struct Varying { float4 position:SV_POSITION; float3 color:COLOR; };
    Varying Vert(uint id:SV_VertexID)
    {
        uint s=(id/6)*_FrameStride,axis=(id%6)/2,v=_SegmentStarts[s];
        float3 localAxis=axis==0?float3(1,0,0):axis==1?float3(0,1,0):float3(0,0,1);
        float3 p=0.5*(_Position[v]+_Position[v+1])+QuatRotate(_Orientation[s],localAxis)*_AxisLength*(id&1);
        Varying o;o.position=TransformWorldToHClip(mul(_HairLocalToWorld,float4(p,1)).xyz);o.color=localAxis;return o;
    }
    half4 Frag(Varying i):SV_Target { return half4(i.color,1); }
    ENDHLSL
    }}
}
