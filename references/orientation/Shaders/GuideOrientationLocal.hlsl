#ifndef GUIDE_ORIENTATION_LOCAL_INCLUDED
#define GUIDE_ORIENTATION_LOCAL_INCLUDED
#include "GuideQuaternion.hlsl"

// 프로젝트 전용 수치 확장. 재료/에너지는 그대로이며 공식 코드에서 복사한 fallback이 아니다.
// xyzw Hamilton: A(q)=v*q*e3, A^T=A, A^2=|v|^2 I.
// 고정 pole 분기: E=-0.5 dot(q,A(q))-dot(b,q)+const, |q|=1.
float4 OrientationA(float3 v,float4 q) { return QuatMultiply(QuatMultiply(float4(v,0),q),float4(0,0,1,0)); }
float OrientationObjective(float3 v,float4 b,float4 q) { return -.5*dot(q,OrientationA(v,q))-dot(b,q); }
float MaxComponent(float4 q) { return max(max(abs(q.x),abs(q.y)),max(abs(q.z),abs(q.w))); }
static const float LocalTolerance=4e-6;

// 공통 계수 스케일로 나눈 잔차/최소 가지/에너지 검사. normalize 성공만으로 승인하지 않는다.
bool OrientationAccept(float3 v,float4 b,float4 old,float4 q)
{
    if(!QuatValid(q) || abs(dot(q,q)-1)>2e-6) return false;
    float alpha=length(v),scale=alpha+length(b);
    if(scale==0) return true;
    float4 s=OrientationA(v,q)+b;float lambdaHat=dot(q,s);
    return length(s-lambdaHat*q)<=LocalTolerance*scale &&
        lambdaHat>=alpha-LocalTolerance*scale &&
        OrientationObjective(v,b,q)<=OrientationObjective(v,b,old)+2e-6*scale;
}
float4 ProjectPlus(float3 unitV,float4 z) { return .5*(z+OrientationA(unitV,z)); }

// hard case의 비유일한 +alpha 성분: old의 투영을 우선 사용한다.
// 투영이 반올림 크기이면 xyzw 표준기저 중 투영 길이가 최대인 첫 축을 선택한다.
float4 HardDirection(float3 unitV,float4 old)
{
    float4 p=ProjectPlus(unitV,old);
    if(dot(p,p)>1e-12) return normalize(p);
    float4 best=0;float best2=-1;
    [unroll] for(uint axis=0;axis<4;axis++)
    {
        float4 basis=0;basis[axis]=1;float4 t=ProjectPlus(unitV,basis);
        float n=dot(t,t);if(n>best2){best=t;best2=n;}
    }
    return normalize(best);
}

// kind: 0 기존/SS 전용 경로, 1 regular secular fallback, 2 hard boundary fallback.
// 실패하면 false와 old를 돌려준다. 호출자는 별도 오류를 기록해야 하며 수렴으로 취급 금지.
bool SolveOrientationLocal(float4 old,float3 inputV,float4 inputB,out float4 result,out uint kind)
{
    result=old;kind=0;
    if(!all(isfinite(inputV)) || !all(isfinite(inputB)) || !QuatValid(old)) return false;
    float common=max(MaxComponent(float4(inputV,0)),MaxComponent(inputB));
    if(common==0) return true; // 정확히 상수인 목적함수. geometry 길이 검증과는 별개.
    // v,b를 같은 값으로 나누면 최소해 불변. 내부 lambda/잔차는 common으로 스케일됨.
    float3 v=inputV/common;float4 b=inputB/common;
    float alpha=length(v),bl=length(b);float4 candidate=old;
    if(alpha==0) {result=normalize(b);return OrientationAccept(v,b,old,result);}
    if(bl==0)
    {
        // 기존 SS-only 최소 회전과 free twist 유지. 정반대는 현재 material d1을 회전축으로.
        float3 target=-v/alpha,tangent=QuatRotate(old,float3(0,0,1));float d=dot(tangent,target);
        float4 rot=d<-.999999?float4(QuatRotate(old,float3(1,0,0)),0):float4(cross(tangent,target),1+d);
        if(QuatValid(rot))candidate=normalize(QuatMultiply(normalize(rot),old));
        if(OrientationAccept(v,b,old,candidate)){result=candidate;return true;}
        // 극단적으로 작은 정반대 오차도 아래 eigenspace 경로로 해결한다.
    }
    else
    {
        // 논문 (18),(22),(25)의 기존 ordinary 경로. 보정 횟수/정지 조건 유지.
        float4 a=OrientationA(v,b);float lambda=alpha+bl;
        for(uint step=0;step<32;step++)
        {
            float next=sqrt(length(a+lambda*b)+alpha*alpha);
            if(abs(next-lambda)<=1e-7*max(lambda,1e-20)){lambda=next;break;}
            lambda=next;
        }
        float4 numerator=a+lambda*b;
        float cancellationScale=length(a)+abs(lambda)*bl;
        // numerator는 상태 quaternion이 아니다. 고정 절대 QuatValid 임계값을 사용하지 않는다.
        bool meaningful=all(isfinite(numerator)) && length(numerator)>1e-5*cancellationScale &&
            lambda-alpha>1e-5*max(alpha,bl);
        if(meaningful)
        {
            candidate=normalize(numerator);
            if(OrientationAccept(v,b,old,candidate)){result=candidate;return true;}
        }
    }
    kind=1;
    float3 unitV=v/alpha;
    float4 bp=ProjectPlus(unitV,b),bm=.5*(b-OrientationA(unitV,b));
    float p=length(bp),m=length(bm);
    // 4 float eps * |b|: projector subtraction의 반올림 규모만 hard로 취급한다.
    // 작은 절대 계수라는 이유로 BT를 제거하지 않는다. 최종 잔차/에너지 인증은 필수이다.
    if(p<=4.7683716e-7*bl && m<=2*alpha)
    {
        kind=2;
        float4 qm=bm/(2*alpha);
        candidate=qm+sqrt(max(0,1-dot(qm,qm)))*HardDirection(unitV,old);
    }
    else
    {
        // delta=lambda-alpha를 직접 유지한다. alpha+delta-alpha로 작은 차를 잃지 않는다.
        // secular norm은 delta에 대해 단조 감소. [|b+|, |b|]가 안전한 bracket.
        float lo=p,hi=bl,delta=hi;
        if(p==0) delta=max(0,m-2*alpha); // nonsingular minus-only endpoint, m>2alpha
        else
        {
            // geometric midpoint accelerates extreme near-hard brackets; ordinary bisection thereafter.
            for(uint root=0;root<96;root++)
            {
                delta=hi>4*lo?sqrt(lo)*sqrt(hi):.5*(lo+hi);
                float plusRatio=p/delta,minusRatio=m/(delta+2*alpha);
                float norm2=plusRatio*plusRatio+minusRatio*minusRatio;
                if(abs(norm2-1)<=2e-7 || hi-lo<=2e-7*hi)break;
                if(norm2>1)lo=delta;else hi=delta;
            }
        }
        if(!(delta>0) || !isfinite(delta))return false;
        candidate=bp/delta+bm/(delta+2*alpha);
    }
    if(!QuatValid(candidate))return false;
    candidate=normalize(candidate);
    if(!OrientationAccept(v,b,old,candidate))return false;
    result=candidate;return true;
}
#endif
