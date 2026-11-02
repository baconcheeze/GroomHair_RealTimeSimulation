#ifndef WINDOW_POSITION_LOCAL_INCLUDED
#define WINDOW_POSITION_LOCAL_INCLUDED
// Exact scalar-diagonal minimizer of inertia plus ALL incident SS terms.
// Missing endpoints have c=0. No current-edge normalization or projection.
bool SolvePositionLocal(float3 x,float3 y,float a,float3 left,float cl,float3 tl,
                        float3 right,float cr,float3 tr,bool pinned,
                        out float3 result,out float diagonal)
{
    result=x;diagonal=a+cl+cr;
    if(!all(isfinite(x))||!all(isfinite(y))||!isfinite(a)||!(a>0)||
       !isfinite(cl)||!isfinite(cr)||cl<0||cr<0||!isfinite(diagonal)||!(diagonal>0))return false;
    if((cl>0&&(!all(isfinite(left))||!all(isfinite(tl))))||
       (cr>0&&(!all(isfinite(right))||!all(isfinite(tr)))))return false;
    if(pinned)return true;
    float3 gradient=a*(x-y);
    if(cl>0)gradient+=cl*(x-left-tl);
    if(cr>0)gradient+=cr*(x-right+tr);
    result=x-gradient/diagonal;
    return all(isfinite(result));
}
#endif
