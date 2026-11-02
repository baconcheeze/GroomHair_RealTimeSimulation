#include "../orientation/GuideOrientationLocal.hlsl"
#include "PositionLocal.hlsl"
cbuffer Parameters:register(b0){
    uint Strands,Vertices,Segments,Bends;
    uint Participating,Iterations,PinX,PinQ;
    float BaseSS,BaseBT,InvH2;uint FirstChunk;
    uint BaseSweep,OperationLimit,ReferenceFlags,PerturbMode;
};
struct Range{uint firstVertex,vertexCount,firstSegment,segmentCount,firstBend,bendCount;};
struct Fault{uint code,sweep,window,operation,index,strand,pad0,pad1;};
struct Stats{uint attemptedP,executedP,attemptedQ,executedQ,ordinary,fallback,hard,rejected,failures,completedSweeps;float batchX,batchQ,lastX,lastQ;};
StructuredBuffer<Range> Ranges:register(t0);
StructuredBuffer<float> RestLength:register(t1);
StructuredBuffer<float4> RestBend:register(t2);
StructuredBuffer<float4> RestOrientation:register(t3);
StructuredBuffer<float> Mass:register(t4);
StructuredBuffer<float3> Target:register(t5);
StructuredBuffer<float3> RestPosition:register(t6);
RWStructuredBuffer<float3> Position:register(u0);
RWStructuredBuffer<float4> Orientation:register(u1);
RWStructuredBuffer<Fault> Faults:register(u2);
RWStructuredBuffer<Stats> Statistics:register(u3);
RWStructuredBuffer<float3> BatchX:register(u4);
RWStructuredBuffer<float4> BatchQ:register(u5);
RWStructuredBuffer<float3> SweepX:register(u6);
RWStructuredBuffer<float4> SweepQ:register(u7);
struct Cache{float3 x0,x1,x2;float4 left,q0,q1,right;};
float WindowPhysicalLocalEnergy(float4 q,float3 derivative,float kss,float4 leftTarget,float kl,float4 rightTarget,float kr){
    float scale=max(kss,max(kl,kr));float3 css=derivative-QuatRotate(q,float3(0,0,1));
    float4 cl=q-(dot(q,leftTarget)>=0?1:-1)*leftTarget;
    float4 cr=q-(dot(q,rightTarget)>=0?1:-1)*rightTarget;
    return .5*((kss/scale)*dot(css,css)+(kl/scale)*dot(cl,cl)+(kr/scale)*dot(cr,cr));
}
void Fail(inout Fault f,inout Stats stats,uint code,uint sweep,uint window,uint operation,uint index,uint strand){
    if(f.code==0){f.code=code;f.sweep=sweep;f.window=window;f.operation=operation;f.index=index;f.strand=strand;stats.failures++;}
}
bool P(Range r,uint j,float3 left,float3 right,float4 ql,float4 qr,inout float3 x,inout Stats stats){
    stats.attemptedP++;if(j==0&&PinX!=0)return true;
    uint v=r.firstVertex+j;float cl=0,cr=0;float3 tl=0,tr=0;
    if(j>0){float l=RestLength[r.firstSegment+j-1];cl=BaseSS/l;tl=l*QuatRotate(ql,float3(0,0,1));}
    if(j<r.segmentCount){float l=RestLength[r.firstSegment+j];cr=BaseSS/l;tr=l*QuatRotate(qr,float3(0,0,1));}
    float3 candidate;float diagonal;
    if(!SolvePositionLocal(x,Target[v],Mass[v]*InvH2,left,cl,tl,right,cr,tr,false,candidate,diagonal))return false;
    x=candidate;stats.executedP++;return true;
}
bool Q(Range r,uint j,float3 xa,float3 xb,float4 left,float4 right,inout float4 q,inout Stats stats){
    stats.attemptedQ++;if((j==0&&PinQ!=0)||(ReferenceFlags&2)!=0)return true;
    uint s=r.firstSegment+j;float l=RestLength[s],kss=BaseSS*l;float3 v=(-2*kss/l)*(xb-xa);
    float4 b=0,lt=0,rt=0;float kl=0,kr=0;
    if(j>0){float4 rest=RestBend[r.firstBend+j-1];float phi=dot(QuatMultiply(QuatConjugate(left),q),rest)>=0?1:-1;kl=4*BaseBT/RestLength[s-1];lt=QuatMultiply(left,rest);b+=kl*phi*lt;}
    if(j+1<r.segmentCount){float4 rest=RestBend[r.firstBend+j];float phi=dot(QuatMultiply(QuatConjugate(q),right),rest)>=0?1:-1;kr=4*BaseBT/l;rt=QuatMultiply(right,QuatConjugate(rest));b+=kr*phi*rt;}
    float4 result;uint kind;bool accepted=SolveOrientationLocal(q,v,b,result,kind);
    if(kind==0)stats.ordinary++;else{stats.fallback++;stats.rejected++;if(kind==2)stats.hard++;}
    float before=WindowPhysicalLocalEnergy(q,(xb-xa)/l,kss,lt,kl,rt,kr);
    float after=WindowPhysicalLocalEnergy(result,(xb-xa)/l,kss,lt,kl,rt,kr);
    if(!accepted||!isfinite(after)||after>before+6e-6)return false;
    if(dot(result,q)<0)result=-result;q=result;stats.executedQ++;return true;
}
void SaveCache(Range r,uint i,Cache c){
    Position[r.firstVertex+i]=c.x0;Position[r.firstVertex+i+1]=c.x1;Position[r.firstVertex+i+2]=c.x2;
    Orientation[r.firstSegment+i]=c.q0;Orientation[r.firstSegment+i+1]=c.q1;
}
// OperationLimit is a diagnostic prefix stop only. Production always passes zero.
bool Stop(uint operations){return OperationLimit!=0&&operations>=OperationLimit;}
#define CP(J,LEFT,RIGHT,QL,QR,X) { bool ok=P(r,J,LEFT,RIGHT,QL,QR,X,stats);operations++;if(!ok){Fail(f,stats,7,sweep,i,1,J,strand);SaveCache(r,i,c);return false;}if(Stop(operations)){SaveCache(r,i,c);return false;} }
#define CQ(J,XA,XB,LEFT,RIGHT,VALUE) { bool ok=Q(r,J,XA,XB,LEFT,RIGHT,VALUE,stats);operations++;if(!ok){Fail(f,stats,6,sweep,i,2,J,strand);SaveCache(r,i,c);return false;}if(Stop(operations)){SaveCache(r,i,c);return false;} }
bool CachedSweep(Range r,uint strand,uint sweep,inout Stats stats,inout Fault f){
    uint operations=0;
    if(r.segmentCount==1){
        float3 a=Position[r.firstVertex],b=Position[r.firstVertex+1];float4 q=Orientation[r.firstSegment];
        bool ok=P(r,0,0,b,0,q,a,stats);operations++;if(!ok)Fail(f,stats,7,sweep,0xfffffffe,1,0,strand);
        if(ok&&!Stop(operations)){ok=P(r,1,a,0,q,0,b,stats);operations++;if(!ok)Fail(f,stats,7,sweep,0xffffffff,1,1,strand);}
        if(ok&&!Stop(operations)){ok=Q(r,0,a,b,0,0,q,stats);operations++;if(!ok)Fail(f,stats,6,sweep,0xffffffff,2,0,strand);}
        if(ok&&!Stop(operations)){ok=P(r,1,a,0,q,0,b,stats);operations++;if(!ok)Fail(f,stats,7,sweep,0xffffffff,1,1,strand);}
        Position[r.firstVertex]=a;Position[r.firstVertex+1]=b;Orientation[r.firstSegment]=q;
        return ok&&(OperationLimit==0||OperationLimit>=4);
    }
    Cache c;c.x0=Position[r.firstVertex];c.x1=Position[r.firstVertex+1];c.x2=Position[r.firstVertex+2];
    c.q0=Orientation[r.firstSegment];c.q1=Orientation[r.firstSegment+1];c.left=0;c.right=r.segmentCount>2?Orientation[r.firstSegment+2]:0;
    uint i=0;
    // Prologue P(0), including the unpinned root.
    CP(0,0,c.x1,0,c.q0,c.x0)
    for(i=0;i+1<r.segmentCount;i++){
        CP(i+1,c.x0,c.x2,c.q0,c.q1,c.x1)
        CQ(i+1,c.x1,c.x2,c.q0,c.right,c.q1)
        CQ(i,c.x0,c.x1,c.left,c.q1,c.q0)
        CP(i+1,c.x0,c.x2,c.q0,c.q1,c.x1)
        if(i+2<r.segmentCount){
            // Retire only outgoing modified values. q0 becomes the next left halo.
            Position[r.firstVertex+i]=c.x0;Orientation[r.firstSegment+i]=c.q0;
            c.x0=c.x1;c.x1=c.x2;c.x2=Position[r.firstVertex+i+3];
            c.left=c.q0;c.q0=c.q1;c.q1=c.right;
            c.right=i+3<r.segmentCount?Orientation[r.firstSegment+i+3]:0;
        }
    }
    i=r.segmentCount-2;
    CP(r.segmentCount,c.x1,0,c.q1,0,c.x2)
    CQ(r.segmentCount-1,c.x1,c.x2,c.q0,0,c.q1)
    CP(r.segmentCount,c.x1,0,c.q1,0,c.x2)
    SaveCache(r,i,c);return true;
}
#undef CP
#undef CQ
bool ImmediateP(Range r,uint j,inout Stats stats){uint v=r.firstVertex+j;float3 x=Position[v];bool ok=P(r,j,j>0?Position[v-1]:0,j<r.segmentCount?Position[v+1]:0,j>0?Orientation[r.firstSegment+j-1]:0,j<r.segmentCount?Orientation[r.firstSegment+j]:0,x,stats);if(ok)Position[v]=x;return ok;}
bool ImmediateQ(Range r,uint j,inout Stats stats){uint s=r.firstSegment+j;float4 q=Orientation[s];bool ok=Q(r,j,Position[r.firstVertex+j],Position[r.firstVertex+j+1],j>0?Orientation[s-1]:0,j+1<r.segmentCount?Orientation[s+1]:0,q,stats);if(ok)Orientation[s]=q;return ok;}
#define IP(J) {bool ok=ImmediateP(r,J,stats);operations++;if(!ok){Fail(f,stats,7,sweep,window,1,J,strand);return false;}if(Stop(operations))return false;}
#define IQ(J) {bool ok=ImmediateQ(r,J,stats);operations++;if(!ok){Fail(f,stats,6,sweep,window,2,J,strand);return false;}if(Stop(operations))return false;}
bool UncachedSweep(Range r,uint strand,uint sweep,inout Stats stats,inout Fault f){
    uint operations=0,window=0xfffffffe;IP(0)
    if(r.segmentCount==1){window=0xffffffff;IP(1)IQ(0)IP(1)}
    else{for(uint i=0;i+1<r.segmentCount;i++){window=i;IP(i+1)IQ(i+1)IQ(i)IP(i+1)}window=0xffffffff;IP(r.segmentCount)IQ(r.segmentCount-1)IP(r.segmentCount)}return true;
}
#undef IP
#undef IQ
// Reverse ORDER only. Physical root-to-tip indices and material +Z are unchanged.
#define RP(J,LEFT,RIGHT,QL,QR,X) {bool ok=P(r,J,LEFT,RIGHT,QL,QR,X,stats);if(!ok){Fail(f,stats,7,sweep,i,1,J,strand);SaveCache(r,i,c);return false;}}
#define RQ(J,XA,XB,LEFT,RIGHT,VALUE) {bool ok=Q(r,J,XA,XB,LEFT,RIGHT,VALUE,stats);if(!ok){Fail(f,stats,6,sweep,i,2,J,strand);SaveCache(r,i,c);return false;}}
bool ReverseCachedSweep(Range r,uint strand,uint sweep,inout Stats stats,inout Fault f){
    if(r.segmentCount==1){
        float3 a=Position[r.firstVertex],b=Position[r.firstVertex+1];float4 q=Orientation[r.firstSegment];
        bool ok=P(r,1,a,0,q,0,b,stats);if(!ok)Fail(f,stats,7,sweep,0xffffffff,1,1,strand);
        if(ok){ok=P(r,0,0,b,0,q,a,stats);if(!ok)Fail(f,stats,7,sweep,0xffffffff,1,0,strand);}
        if(ok){ok=Q(r,0,a,b,0,0,q,stats);if(!ok)Fail(f,stats,6,sweep,0xffffffff,2,0,strand);}
        if(ok){ok=P(r,0,0,b,0,q,a,stats);if(!ok)Fail(f,stats,7,sweep,0xffffffff,1,0,strand);}
        Position[r.firstVertex]=a;Position[r.firstVertex+1]=b;Orientation[r.firstSegment]=q;return ok;
    }
    uint i=r.segmentCount-2;Cache c;c.x0=Position[r.firstVertex+i];c.x1=Position[r.firstVertex+i+1];c.x2=Position[r.firstVertex+i+2];
    c.q0=Orientation[r.firstSegment+i];c.q1=Orientation[r.firstSegment+i+1];c.left=i>0?Orientation[r.firstSegment+i-1]:0;c.right=0;
    RP(r.segmentCount,c.x1,0,c.q1,0,c.x2)
    for(int index=(int)r.segmentCount-2;index>=0;index--){i=(uint)index;
        RP(i+1,c.x0,c.x2,c.q0,c.q1,c.x1)
        RQ(i,c.x0,c.x1,c.left,c.q1,c.q0)
        RQ(i+1,c.x1,c.x2,c.q0,c.right,c.q1)
        RP(i+1,c.x0,c.x2,c.q0,c.q1,c.x1)
        if(i>0){Position[r.firstVertex+i+2]=c.x2;Orientation[r.firstSegment+i+1]=c.q1;
            c.x2=c.x1;c.x1=c.x0;c.x0=Position[r.firstVertex+i-1];
            c.right=c.q1;c.q1=c.q0;c.q0=c.left;c.left=i>1?Orientation[r.firstSegment+i-2]:0;}
    }
    i=0;RP(0,0,c.x1,0,c.q0,c.x0) RQ(0,c.x0,c.x1,0,c.q1,c.q0) RP(0,0,c.x1,0,c.q0,c.x0)
    SaveCache(r,0,c);return true;
}
#undef RP
#undef RQ
#define BP(J) {if(!ImmediateP(r,J,stats)){Fail(f,stats,7,sweep,window,1,J,strand);return false;}}
#define BQ(J) {if(!ImmediateQ(r,J,stats)){Fail(f,stats,6,sweep,window,2,J,strand);return false;}}
bool ReverseUncachedSweep(Range r,uint strand,uint sweep,inout Stats stats,inout Fault f){
    uint window=0xfffffffe;BP(r.segmentCount)
    if(r.segmentCount==1){window=0xffffffff;BP(0)BQ(0)BP(0)}
    else{for(int i=(int)r.segmentCount-2;i>=0;i--){window=(uint)i;BP((uint)i+1)BQ((uint)i)BQ((uint)i+1)BP((uint)i+1)}window=0xffffffff;BP(0)BQ(0)BP(0)}return true;
}
#undef BP
#undef BQ
bool Valid(Range r){return r.vertexCount>=2&&r.vertexCount<=Vertices&&r.firstVertex<=Vertices-r.vertexCount&&r.segmentCount==r.vertexCount-1&&r.segmentCount<=Segments&&r.firstSegment<=Segments-r.segmentCount&&r.bendCount==r.vertexCount-2&&r.bendCount<=Bends&&r.firstBend<=Bends-r.bendCount;}
[numthreads(64,1,1)]void SolveWindow(uint3 id:SV_DispatchThreadID){
    uint strand=id.x;if(strand>=Participating||strand>=Strands)return;
    Fault fault=Faults[strand];if(fault.code!=0)return;
    Stats stats=Statistics[strand];if(FirstChunk!=0)stats=(Stats)0;
    Range r=Ranges[strand];
    if(!Valid(r)){Fail(fault,stats,1,BaseSweep,0,0,0,strand);Faults[strand]=fault;Statistics[strand]=stats;return;}
    for(uint j=0;j<r.vertexCount;j++){uint v=r.firstVertex+j;if(!all(isfinite(Position[v]))||!all(isfinite(Target[v]))||!isfinite(Mass[v])||Mass[v]<=0){Fail(fault,stats,3,BaseSweep,0,1,j,strand);break;}}
    for(uint j=0;j<r.segmentCount&&fault.code==0;j++){uint s=r.firstSegment+j;if(!QuatValid(Orientation[s])||!QuatValid(RestOrientation[s]))Fail(fault,stats,2,BaseSweep,0,2,j,strand);else if(!isfinite(RestLength[s])||RestLength[s]<=1e-7)Fail(fault,stats,3,BaseSweep,0,0,j,strand);else if(j<r.bendCount&&!QuatValid(RestBend[r.firstBend+j]))Fail(fault,stats,4,BaseSweep,0,2,j,strand);}
    if(fault.code==0&&PinX!=0&&any(Position[r.firstVertex]!=RestPosition[r.firstVertex]))Fail(fault,stats,5,BaseSweep,0,1,0,strand);
    if(fault.code==0&&PinQ!=0&&QuatRotationChord(Orientation[r.firstSegment],RestOrientation[r.firstSegment])>1e-5)Fail(fault,stats,5,BaseSweep,0,2,0,strand);
    if(fault.code!=0){Faults[strand]=fault;Statistics[strand]=stats;return;}
    if(FirstChunk!=0&&(ReferenceFlags&4)==0){for(uint j=0;j<r.vertexCount;j++)BatchX[r.firstVertex+j]=Position[r.firstVertex+j];for(uint j=0;j<r.segmentCount;j++)BatchQ[r.firstSegment+j]=Orientation[r.firstSegment+j];}
    for(uint sweep=0;sweep<Iterations;sweep++){
        // Global diagnostic baselines, NOT a full-strand thread-local cache.
        if((ReferenceFlags&4)==0){
        for(uint j=0;j<r.vertexCount;j++)SweepX[r.firstVertex+j]=Position[r.firstVertex+j];
        for(uint j=0;j<r.segmentCount;j++)SweepQ[r.firstSegment+j]=Orientation[r.firstSegment+j];
        }
        bool reverse=(ReferenceFlags&16)!=0||((ReferenceFlags&8)!=0&&((BaseSweep+sweep)&1)!=0);
        bool complete=reverse?((ReferenceFlags&1)!=0?ReverseUncachedSweep(r,strand,BaseSweep+sweep,stats,fault):ReverseCachedSweep(r,strand,BaseSweep+sweep,stats,fault)):
            ((ReferenceFlags&1)!=0?UncachedSweep(r,strand,BaseSweep+sweep,stats,fault):CachedSweep(r,strand,BaseSweep+sweep,stats,fault));
        if(!complete)break;
        if((ReferenceFlags&4)==0){
        stats.lastX=0;stats.lastQ=0;stats.batchX=0;stats.batchQ=0;
        for(uint j=0;j<r.vertexCount;j++){uint v=r.firstVertex+j;stats.lastX=max(stats.lastX,length(Position[v]-SweepX[v]));stats.batchX=max(stats.batchX,length(Position[v]-BatchX[v]));}
        for(uint j=0;j<r.segmentCount;j++){uint s=r.firstSegment+j;stats.lastQ=max(stats.lastQ,QuatRotationChord(Orientation[s],SweepQ[s]));stats.batchQ=max(stats.batchQ,QuatRotationChord(Orientation[s],BatchQ[s]));}
        }
        stats.completedSweeps++;
    }
    Faults[strand]=fault;Statistics[strand]=stats;
}
[numthreads(64,1,1)]void PerturbWindow(uint3 id:SV_DispatchThreadID){
    uint strand=id.x;if(strand>=Participating||strand>=Strands||Faults[strand].code!=0)return;Range r=Ranges[strand];if(!Valid(r))return;
    if((PerturbMode&1)!=0)for(uint j=0;j<r.vertexCount;j++){if(j==0&&PinX!=0)continue;float u=(float)j/r.segmentCount;float s=sin(3.14159265359*u*.5);Position[r.firstVertex+j]+=float3(.004*s*s,-.002*u*u,.003*sin(3.14159265359*u));}
    if((PerturbMode&2)!=0)for(uint j=0;j<r.segmentCount;j++){if(j==0&&PinQ!=0)continue;float u=(j+.5)/r.segmentCount;float angle=.349065850399*sin(3.14159265359*u);float4 rot=float4(sin(angle*.5),0,0,cos(angle*.5));uint s=r.firstSegment+j;Orientation[s]=QuatNormalize(QuatMultiply(Orientation[s],rot));}
}
