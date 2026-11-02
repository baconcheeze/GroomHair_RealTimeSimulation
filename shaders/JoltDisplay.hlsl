// Renderer boundary only: Jolt right-handed world -> display left-handed (reflect Z).
// EyePosition.xyz is the camera; .w is the display-only body visibility flag.
cbuffer Draw : register(b0) {row_major float4x4 ViewProjection;row_major float4x4 World;float4 ColorStride;float4 EyePosition;float4 HairStyle;};
ByteAddressBuffer Positions:register(t0);StructuredBuffer<uint> Indices:register(t1);
StructuredBuffer<float4> CharacterColors:register(t2);
struct Surface{float2 uv;uint textureIndex;uint flags;float4 normal;uint4 joints;float4 weights;};
StructuredBuffer<Surface> CharacterSurface:register(t3);
struct Pose{row_major float4x4 transform;};StructuredBuffer<Pose> SkinWorld:register(t4);
Texture2DArray CharacterTextures:register(t5);SamplerState CharacterSampler:register(s0);
StructuredBuffer<uint> RenderMaterials:register(t6);
// Temporary appearance preview: retain the authored curves, hide ear wisps.
static const bool HideSideburns = true;
struct Output{float4 position:SV_POSITION;float3 world:WORLD;float3 color:COLOR;float material:MATERIAL;float3 normal:NORMAL;float2 uv:TEXCOORD0;nointerpolation uint tex:TEXCOORD1;float3 local:TEXCOORD2;nointerpolation uint flags:TEXCOORD3;float coverage:TEXCOORD4;};
float3 Display(float3 x){x=max(x,0);x=saturate((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14));return pow(x,1/2.2);}
Output GroundVS(uint id:SV_VertexID){
    static const float2 corners[6]={float2(-1,-1),float2(-1,1),float2(1,1),float2(-1,-1),float2(1,1),float2(1,-1)};
    Output o=(Output)0;float3 p=float3(corners[id].x*40+ColorStride.x,0,corners[id].y*40+ColorStride.z);
    o.world=p;o.position=mul(float4(p,1),ViewProjection);return o;
}
float GridLine(float2 p){float2 w=max(fwidth(p),.00001);float2 d=abs(frac(p+.5)-.5)/w;return 1-saturate(min(d.x,d.y));}
float4 GroundPS(Output i):SV_TARGET{
    float2 p=i.world.xz;
    float checker=frac((floor(p.x*2)+floor(p.y*2))*.5)*2;
    float3 tile=lerp(float3(.82,.835,.85),float3(.89,.90,.91),checker);
    tile=lerp(tile,float3(.55,.59,.64),GridLine(p*2)*.45);
    tile=lerp(tile,float3(.40,.46,.54),GridLine(p/2.5)*.55);
    float2 relative=p-ColorStride.xz;
    tile*=1-.12*exp(-dot(relative*float2(2.8,2.0),relative*float2(2.8,2.0)));
    float fade=smoothstep(12,32,length(i.world-EyePosition.xyz));
    return float4(lerp(tile,float3(.985,.985,.985),fade),1);
}
Output Transform(float3 p){Output o=(Output)0;float4 w=mul(float4(p,1),World);w.z=-w.z;o.position=mul(w,ViewProjection);o.world=w.xyz;o.local=p;o.color=ColorStride.xyz;return o;}
Output HairVS(uint id:SV_VertexID){
    uint index=ColorStride.w<0?id:Indices[id];
    float3 p=asfloat(Positions.Load3(index*uint(abs(ColorStride.w))));Output o=Transform(p);o.coverage=1;
    if(ColorStride.w==12){
        // Native Jolt assets are resampled to 32 points/strand. Shade along
        // actual CURRENT render-strand tangents; no geometry readback.
        uint within=index%32;
        uint left=within>0?index-1:index,right=within<31?index+1:index;
        float3 d=asfloat(Positions.Load3(right*12))-asfloat(Positions.Load3(left*12));
        float3 tangent=mul(float4(d,0),World).xyz;tangent.z=-tangent.z;
        tangent*=rsqrt(max(dot(tangent,tangent),1e-16));
        float3 light=normalize(float3(-.32,.65,.72));
        float3 view=normalize(EyePosition.xyz-o.world);
        float3 halfVector=normalize(light+view);
        float fiber=sqrt(saturate(1-pow(dot(tangent,light),2)));
        float sinHalf=sqrt(saturate(1-pow(dot(tangent,halfVector),2)));
        float sheen=pow(sinHalf,56),broad=pow(sinHalf,12);
        float strandHash=frac((index/32)*.6180339);
        float strandTone=HairStyle.x>0?.82+.36*strandHash:.95+.05*strandHash;
        float3 radial=normalize(float3(p.x,0,p.z+.01));radial=mul(float4(radial,0),World).xyz;radial.z=-radial.z;
        float rim=pow(saturate(1-abs(dot(radial,view))),5);
        float3 sheenColor=float3(.040,.042,.046);
        float3 broadColor=float3(.009,.010,.012);
        o.color=ColorStride.xyz*(.42+.75*fiber)*strandTone+sheenColor*sheen+broadColor*broad+float3(.009,.012,.018)*rim;
        o.material=within/31.;
        if(HairStyle.x>0) {
            uint region=RenderMaterials[index/32];
            float bangCoverage=HairStyle.y*lerp(1.45,1,smoothstep(.55,.95,o.material));
            o.coverage=region==3?bangCoverage:region==4?(HideSideburns?0:HairStyle.z):1;
        }
    }
    return o;
}
Output HeadVS(uint id:SV_VertexID){Output o=Transform(asfloat(Positions.Load3(id*12)));float4 c=CharacterColors[id];o.color=c.rgb;o.material=c.a;Surface s=CharacterSurface[id];o.uv=s.uv;o.tex=s.textureIndex;o.flags=s.flags;float3 n=0;[unroll]for(uint j=0;j<4;j++)n+=s.weights[j]*mul(float4(s.normal.xyz,0),SkinWorld[s.joints[j]].transform).xyz;o.normal=n;o.normal.z=-o.normal.z;return o;}
Output JacketVS(uint id:SV_VertexID){
    Surface s=CharacterSurface[id];float3 rest=asfloat(Positions.Load3(id*12));
    float3 p=0,n=0;
    [unroll]for(uint k=0;k<4;k++){
        p+=s.weights[k]*mul(float4(rest,1),SkinWorld[s.joints[k]].transform).xyz;
        n+=s.weights[k]*mul(float4(s.normal.xyz,0),SkinWorld[s.joints[k]].transform).xyz;
    }
    // SkinWorld yields world coordinates. Do not apply the hair's neck transform.
    Output o=(Output)0;p.z=-p.z;n.z=-n.z;
    o.world=p;o.position=mul(float4(p,1),ViewProjection);o.local=rest;
    o.normal=n;o.uv=s.uv;o.color=CharacterColors[id].rgb;o.flags=s.flags;return o;
}
float4 JacketPS(Output i,bool front:SV_IsFrontFace):SV_TARGET{
    float3 n=normalize(i.normal)*(front?1:-1),v=normalize(EyePosition.xyz-i.world);
    float3 key=normalize(float3(-.32,.65,.72)),fill=normalize(float3(.55,-.18,.82));
    float3 base=pow(saturate(i.color),2.2);
    bool metal=(i.flags&4u)!=0,thread=(i.flags&2u)!=0;
    // Small filtered twill modulation, without a bitmap dependency or shimmer.
    float2 uv=i.uv*float2(260,210);float footprint=max(length(ddx(uv)),length(ddy(uv)));
    float weave=sin((uv.x+uv.y)*6.2831853)*sin(uv.x*6.2831853);
    float grain=1+(.055*weave)*(1-smoothstep(.35,1.5,footprint));
    if(!metal&&!thread){
        float wear=pow(abs(i.uv.x*2-1),18)*.10+pow(abs(i.uv.y*2-1),20)*.08;
        float wash=.96+.04*sin(i.uv.x*11+1.2)*sin(i.uv.y*7);
        base=lerp(base*grain*wash,base*1.55,wear);
    }
    float light=.30+.80*saturate(dot(n,key))+.20*saturate(dot(n,fill));
    float highlight=pow(saturate(dot(n,normalize(v+key))),metal?70:18)*(metal?.22:.008);
    return float4(Display(base*light+highlight),1);
}
float4 PS(Output i):SV_TARGET{float alpha=(1-smoothstep(.992,1,i.material))*i.coverage;clip(alpha-.04);return float4(Display(i.color),alpha);}

float4 HeadPS(Output i):SV_TARGET{
    // Display-only visibility: preserve geometry/rig, hide inner bra and tail.
    if ((i.flags & 1u) != 0) discard;
    if (EyePosition.w<.5 && (i.flags & 2u)!=0) discard;
    float weight=saturate(i.material);float4 tex=weight>=.999?float4(1,1,1,1):CharacterTextures.Sample(CharacterSampler,float3(i.uv,i.tex));
    tex=lerp(tex,float4(1,1,1,1),weight);clip(tex.a-.25);
    float3 painted=tex.rgb*i.color;
    float saturation=max(tex.r,max(tex.g,tex.b))-min(tex.r,min(tex.g,tex.b));
    float forehead=(i.tex==5?1:0)*(1-smoothstep(.015,.05,saturation))*smoothstep(.065,.085,i.local.y);
    painted=lerp(painted,float3(.97,.88,.87),forehead*.85);
    // Pigment the existing closed scalp; no offset overlay and no z-fighting.
    // Positions are in the current Head frame. Keep the forehead and ears clear.
    float cap=0;
    if(HairStyle.w>.5 && (i.flags&8u)!=0){
        float frontCap=1-smoothstep(-.025,.025,i.local.z);
        float hairline=lerp(-.015,.085,frontCap);
        float earSide=smoothstep(.047,.060,abs(i.local.x))*(1-smoothstep(.030,.050,i.local.z));
        hairline=max(hairline,lerp(-.015,.047,earSide));
        cap=smoothstep(hairline-.002,hairline+.002,i.local.y);
    }
    float3 base=lerp(pow(saturate(painted),2.2),float3(.006,.006,.007),cap);
    float3 n=normalize(i.normal),v=normalize(EyePosition.xyz-i.world);
    float3 key=normalize(float3(-.32,.65,.72)),fill=normalize(float3(.55,-.18,.82));
    float illumination=.27+.78*saturate(dot(n,key))+.22*saturate(dot(n,fill));
    float front=1-smoothstep(-.06,-.035,i.local.z);
    float cheek=exp(-pow((abs(i.local.x)-.032)/.018,2)-pow((i.local.y-.038)/.025,2));
    float tzone=exp(-pow(i.local.x/.012,2))*smoothstep(.0,.03,i.local.y)*(1-smoothstep(.09,.14,i.local.y));
    float lip=exp(-pow(i.local.x/.018,2)-pow((i.local.y-.004)/.009,2));
    float eye=saturate((tex.b-tex.r)*5)*(i.tex==5?1:0);
    float gloss=(i.tex==5?(.035+.065*cheek+.10*tzone+.16*lip)*front:.025);
    float spec=pow(saturate(dot(n,normalize(key+v))),30)*gloss;
    spec+=pow(saturate(dot(n,normalize(fill+v))),100)*(.012+.22*eye);
    spec=lerp(spec,pow(saturate(dot(n,normalize(key+v))),24)*.008,cap);
    float rim=pow(saturate(1-dot(n,v)),4)*.025;
    return float4(Display(base*illumination+spec+rim),1);
}

