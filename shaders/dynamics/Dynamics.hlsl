#include "../orientation/GuideQuaternion.hlsl"
struct Range {uint firstVertex,vertexCount,firstSegment,segmentCount,firstBend,bendCount;};
struct Fault {uint code,sweep,window,operation,index,strand,pad0,pad1;};
struct Stats {uint attemptedP,executedP,attemptedQ,executedQ,ordinary,fallback,hard,rejected,failures,completedSweeps;float batchX,batchQ,lastX,lastQ;};
struct Status {uint code,substep,strand,operation,index,completed,ordinary,fallback,hard,failures,beginCount,endCount;};
cbuffer Parameters:register(b0){uint Strands,Participating,PinX,PinQ;float H,Gx,Gy,Gz;uint Substep,Initializer,Padding1,Padding2;float KickX,KickY,KickZ,BaseSS;};
StructuredBuffer<Range> Ranges:register(t0);
StructuredBuffer<float3> RestPosition:register(t1);
StructuredBuffer<float4> RestOrientation:register(t2);
StructuredBuffer<float> Mass:register(t3);
StructuredBuffer<float> RestLength:register(t4);
RWStructuredBuffer<float3> Position:register(u0);
RWStructuredBuffer<float4> Orientation:register(u1);
RWStructuredBuffer<float3> Velocity:register(u2);
RWStructuredBuffer<float3> Previous:register(u3);
RWStructuredBuffer<float3> Target:register(u4);
RWStructuredBuffer<Fault> Faults:register(u5);
RWStructuredBuffer<Stats> Statistics:register(u6);
RWStructuredBuffer<Status> State:register(u7);
RWStructuredBuffer<float3> Acceleration:register(u8);
void RecordFailure(uint strand,uint code,uint operation,uint index){
    Status s=State[strand];if(s.code!=0)return;
    s.code=code;s.substep=Substep;s.strand=strand;s.operation=operation;s.index=index;s.failures++;State[strand]=s;
    Fault f=Faults[strand];if(f.code==0){f=(Fault)0;f.code=code;f.operation=operation;f.index=index;f.strand=strand;Faults[strand]=f;}
}
float3 GuessPosition(Range r,uint j,uint mode,uint completed){
    uint v=r.firstVertex+j;if(j==0&&PinX!=0)return RestPosition[v];
    float3 inertial=Position[v]+H*Velocity[v];
    if(mode==1)return inertial;
    if(mode==2){if(completed>0)return inertial+(H*H)*Acceleration[v];if(PinX!=0)return inertial;}
    return inertial+(H*H)*float3(Gx,Gy,Gz);
}
float GuessEnergy(Range r,uint mode,uint completed){
    float total=0;
    for(uint j=0;j<r.vertexCount;j++){
        uint v=r.firstVertex+j;float3 y=j==0&&PinX!=0?RestPosition[v]:Position[v]+H*Velocity[v]+(H*H)*float3(Gx,Gy,Gz);
        float3 d=GuessPosition(r,j,mode,completed)-y;total+=.5*(Mass[v]/(H*H))*dot(d,d);
        if(j<r.segmentCount){float l=RestLength[r.firstSegment+j];float4 q=j==0&&PinQ!=0?RestOrientation[r.firstSegment]:Orientation[r.firstSegment+j];
            float3 css=(GuessPosition(r,j+1,mode,completed)-GuessPosition(r,j,mode,completed))/l-QuatRotate(q,float3(0,0,1));total+=.5*(BaseSS*l)*dot(css,css);}
    }return total;
}
[numthreads(64,1,1)] void BeginSubstep(uint3 id:SV_DispatchThreadID){
    uint strand=id.x;if(strand>=Strands||strand>=Participating||State[strand].code!=0||Faults[strand].code!=0)return;
    Range r=Ranges[strand];float3 gravity=float3(Gx,Gy,Gz);
    // Preflight the entire strand before writing prediction. No cross-strand writes.
    for(uint j=0;j<r.vertexCount;j++){
        uint v=r.firstVertex+j;float3 x=Position[v],velocity=Velocity[v];
        float3 y=x+H*velocity+(H*H)*gravity;
        if(!all(isfinite(x))||!all(isfinite(velocity))||!all(isfinite(y))){RecordFailure(strand,8,3,j);return;}
        if(Initializer==2&&State[strand].completed>0&&!all(isfinite(x+H*velocity+(H*H)*Acceleration[v]))){RecordFailure(strand,8,3,j);return;}
    }
    uint guessMode=Initializer;
    if(Initializer==3){float best=GuessEnergy(r,0,State[strand].completed);guessMode=0;
        for(uint mode=1;mode<2;mode++){float candidate=GuessEnergy(r,mode,State[strand].completed);if(isfinite(candidate)&&candidate<best){best=candidate;guessMode=mode;}}}
    for(uint j=0;j<r.vertexCount;j++){
        uint v=r.firstVertex+j;float3 old=Position[v];
        if(j==0&&PinX!=0){Previous[v]=RestPosition[v];Target[v]=RestPosition[v];Position[v]=RestPosition[v];Velocity[v]=0;}
        else{Previous[v]=old;float3 inertial=old+H*Velocity[v];float3 y=inertial+(H*H)*gravity;Target[v]=y;
            float3 guess=GuessPosition(r,j,guessMode,State[strand].completed);
            if(!all(isfinite(guess))){RecordFailure(strand,8,3,j);return;}
            Position[v]=guess;}
    }
    if(PinQ!=0)Orientation[r.firstSegment]=RestOrientation[r.firstSegment];
    Status s=State[strand];s.beginCount++;State[strand]=s;
}
[numthreads(64,1,1)] void EndSubstep(uint3 id:SV_DispatchThreadID){
    uint strand=id.x;if(strand>=Strands||strand>=Participating||State[strand].code!=0)return;
    Fault f=Faults[strand];if(f.code!=0){RecordFailure(strand,f.code,f.operation,f.index);return;}
    Range r=Ranges[strand];
    for(uint j=0;j<r.vertexCount;j++){
        uint v=r.firstVertex+j;float3 velocity=(Position[v]-Previous[v])/H;
        if(!all(isfinite(velocity))||!all(isfinite((velocity-Velocity[v])/H))){RecordFailure(strand,9,4,j);return;}
    }
    for(uint j=0;j<r.vertexCount;j++){
        uint v=r.firstVertex+j;float3 next=j==0&&PinX!=0?float3(0,0,0):(Position[v]-Previous[v])/H;
        Acceleration[v]=j==0&&PinX!=0?float3(0,0,0):(next-Velocity[v])/H;Velocity[v]=next;
    }
    Status s=State[strand];Stats stats=Statistics[strand];s.completed++;s.endCount++;
    s.ordinary+=stats.ordinary;s.fallback+=stats.fallback;s.hard+=stats.hard;State[strand]=s;
}
[numthreads(64,1,1)] void KickVelocity(uint3 id:SV_DispatchThreadID){
    uint strand=id.x;if(strand>=Strands||strand>=Participating||State[strand].code!=0||Faults[strand].code!=0)return;
    Range r=Ranges[strand];
    for(uint j=0;j<r.vertexCount;j++){
        if(j==0&&PinX!=0)continue;
        float u=(float)j/r.segmentCount;Velocity[r.firstVertex+j]+=sin(1.57079632679*u)*float3(KickX,KickY,KickZ);
    }
}
