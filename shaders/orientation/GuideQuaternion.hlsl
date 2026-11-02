#ifndef GUIDE_QUATERNION_INCLUDED
#define GUIDE_QUATERNION_INCLUDED
// Unity/HLSL xyzw, Hamilton product, local +Z tangent.
float4 QuatConjugate(float4 q) { return float4(-q.xyz,q.w); }
float4 QuatMultiply(float4 a,float4 b) { return float4(a.w*b.xyz+b.w*a.xyz+cross(a.xyz,b.xyz),a.w*b.w-dot(a.xyz,b.xyz)); }
float QuatDot(float4 a,float4 b) { return dot(a,b); }
float4 QuatNegate(float4 q) { return -q; }
bool QuatValid(float4 q) { return all(isfinite(q)) && dot(q,q)>1e-20; }
// Callers MUST reject invalid/zero input before normalization.
float4 QuatNormalize(float4 q) { return q*rsqrt(dot(q,q)); }
float3 QuatRotate(float4 q,float3 v) { return v+2*cross(q.xyz,cross(q.xyz,v)+q.w*v); }
// Sign-invariant chord; CPU explicit diagnostics maps it to 4*asin(chord/2) radians.
// Avoids acos cancellation near zero and GPU atan approximation for large angles.
float QuatRotationChord(float4 a,float4 b)
{
    a=QuatNormalize(a);b=QuatNormalize(b);if(dot(a,b)<0)b=-b;
    return length(a-b);
}
#endif
