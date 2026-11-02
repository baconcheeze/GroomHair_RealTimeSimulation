#include "DoubleMath.hlsli"
#ifndef GROUP_SIZE
#define GROUP_SIZE 1
#endif
ByteAddressBuffer Input:register(t0);
RWByteAddressBuffer Work:register(u0);
cbuffer Parameters:register(b0){uint SystemCount,SystemBase,UseFormation,TileSystemCount;};
struct Meta{uint n,blocks,rows,rowOffset,blockOffset,diagOffset,lowerOffset,rhsOffset,scaleOffset,workOffset,statusOffset,outputOffset,frameBytes,mapOffset,pad0,pad1;};
Meta Metadata(uint i){uint p=i*64;Meta m;uint4 a=Input.Load4(p),b=Input.Load4(p+16),c=Input.Load4(p+32),d=Input.Load4(p+48);m.n=a.x;m.blocks=a.y;m.rows=a.z;m.rowOffset=a.w;m.blockOffset=b.x;m.diagOffset=b.y;m.lowerOffset=b.z;m.rhsOffset=b.w;m.scaleOffset=c.x;m.workOffset=c.y;m.statusOffset=c.z;m.outputOffset=c.w;m.frameBytes=d.x;m.mapOffset=d.y;m.pad0=d.z;m.pad1=d.w;return m;}
struct Addresses{uint L,C,u,y,du,s,steps,status,formedDiag,formedLower,formedRhs;};
Addresses AddressesOf(Meta m){Addresses a;a.L=m.workOffset;a.C=a.L+m.blocks*288;a.u=a.C+m.blocks*288;a.y=a.u+m.n*8;a.du=a.y+m.n*8;a.s=a.du+m.n*8;a.steps=a.s+m.n*8;a.status=a.steps+40;a.formedDiag=a.status+32;a.formedLower=a.formedDiag+m.blocks*288;a.formedRhs=a.formedLower+m.blocks*288;return a;}
double InD(uint p){uint2 a=Input.Load2(p);return asdouble(a.x,a.y);}
double WD(uint p){uint2 a=Work.Load2(p);return asdouble(a.x,a.y);}
void SD(uint p,double v){uint lo,hi;asuint(v,lo,hi);Work.Store2(p,uint2(lo,hi));}
uint2 Block(Meta m,uint i){return Input.Load2(m.blockOffset+8*i);}// size,start
#ifdef MIXED
 typedef float FactorReal;
 FactorReal FLoad(uint p){return asfloat(Work.Load(p));}
 void FStore(uint p,FactorReal x){Work.Store(p,asuint(x));}
 bool FFinite(FactorReal x){return (asuint(x)&0x7f800000u)!=0x7f800000u;}
 FactorReal FSqrt(FactorReal x){return sqrt(x);}
#else
 typedef double FactorReal;
 FactorReal FLoad(uint p){return WD(p);}
 void FStore(uint p,FactorReal x){SD(p,x);}
 bool FFinite(FactorReal x){return DFinite(x);}
 FactorReal FSqrt(FactorReal x){return DSqrt(x);}
#endif
void Failure(Addresses a,uint code,uint stage,uint index){if(Work.Load(a.status)==0){Work.Store(a.status,code);Work.Store(a.status+4,stage);Work.Store(a.status+8,index);}}
double Matrix(Meta m,Addresses a,uint block,uint row,uint col,bool lower){uint p=block*288+(row*6+col)*8;if(UseFormation!=0)return WD((lower?a.formedLower:a.formedDiag)+p);return InD((lower?m.lowerOffset:m.diagOffset)+p);}
[numthreads(GROUP_SIZE,1,1)]void Form(uint3 tid:SV_DispatchThreadID){if(tid.x>=TileSystemCount)return;uint id=SystemBase+tid.x;if(id>=SystemCount)return;Meta m=Metadata(id);Addresses a=AddressesOf(m);for(uint i=0;i<m.blocks*36;i++){SD(a.formedDiag+8*i,0.0);SD(a.formedLower+8*i,0.0);}for(uint j=0;j<m.n;j++)SD(a.formedRhs+8*j,0.0);
 for(uint r=0;r<m.rows;r++){uint p=m.rowOffset+r*88,count=Input.Load(p);double b=InD(p+32);for(uint j=0;j<count;j++){uint column=Input.Load(p+8+4*j),map=m.mapOffset+16*column;uint4 jm=Input.Load4(map);double x=InD(p+40+8*j);SD(a.formedRhs+8*column,WD(a.formedRhs+8*column)+x*b);for(uint k=0;k<count;k++){uint other=Input.Load(p+8+4*k);uint4 km=Input.Load4(m.mapOffset+16*other);double y=InD(p+40+8*k);if(jm.x==km.x){uint address=a.formedDiag+jm.x*288+(jm.y*6+km.y)*8;SD(address,WD(address)+x*y);}else if(jm.x==km.x+1){uint address=a.formedLower+jm.x*288+(jm.y*6+km.y)*8;SD(address,WD(address)+x*y);}}}}
}
[numthreads(GROUP_SIZE,1,1)]void Factor(uint3 tid:SV_DispatchThreadID){if(tid.x>=TileSystemCount)return;uint id=SystemBase+tid.x;if(id>=SystemCount)return;Meta m=Metadata(id);Addresses w=AddressesOf(m);
#ifdef OWNERSHIP_CHECK
for(uint t=0;t<7;t++)Work.Store(w.status+4*t,0);uint prior;Work.InterlockedAdd(w.status+28,1u,prior);
#else
for(uint t=0;t<8;t++)Work.Store(w.status+4*t,0);
#endif
for(uint t=0;t<5;t++)SD(w.steps+8*t,0.0);for(uint t=0;t<m.blocks*36;t++){FStore(w.L+8*t,FactorReal(0));FStore(w.C+8*t,FactorReal(0));}
 for(uint i=0;i<m.blocks;i++){uint2 b=Block(m,i);uint ni=b.x,np=i?Block(m,i-1).x:0;
  for(uint a=0;a<ni;a++)for(uint k=0;k<np;k++){precise FactorReal v=FactorReal(Matrix(m,w,i,a,k,true));for(uint j=0;j<k;j++)v=v-FLoad(w.L+(i-1)*288+(k*6+j)*8)*FLoad(w.C+i*288+(a*6+j)*8);v=v/FLoad(w.L+(i-1)*288+(k*6+k)*8);if(!FFinite(v)){Failure(w,2,1,i*6+a);return;}FStore(w.C+i*288+(a*6+k)*8,v);}
  for(uint a=0;a<ni;a++)for(uint b=0;b<=a;b++){precise FactorReal v=FactorReal(Matrix(m,w,i,a,b,false));for(uint k=0;k<np;k++)v=v-FLoad(w.C+i*288+(a*6+k)*8)*FLoad(w.C+i*288+(b*6+k)*8);for(uint k=0;k<b;k++)v=v-FLoad(w.L+i*288+(a*6+k)*8)*FLoad(w.L+i*288+(b*6+k)*8);
   if(a==b){if(!(v>FactorReal(0))||!FFinite(v)){Failure(w,1,1,i*6+a);return;}v=FSqrt(v);}else v=v/FLoad(w.L+i*288+(b*6+b)*8);if(!FFinite(v)){Failure(w,2,1,i*6+a);return;}FStore(w.L+i*288+(a*6+b)*8,v);
  }
 }
}
bool Solve(Meta m,Addresses w,bool correction,uint stage){for(uint i=0;i<m.blocks;i++){uint2 b=Block(m,i);uint ni=b.x,si=b.y;uint2 prev=i?Block(m,i-1):uint2(0,0);for(uint a=0;a<ni;a++){double r=correction?WD(w.s+(si+a)*8):(UseFormation!=0?WD(w.formedRhs+(si+a)*8):InD(m.rhsOffset+(si+a)*8));precise FactorReal v=FactorReal(r);for(uint k=0;k<prev.x;k++)v=v-FLoad(w.C+i*288+(a*6+k)*8)*FLoad(w.y+(prev.y+k)*8);for(uint k=0;k<a;k++)v=v-FLoad(w.L+i*288+(a*6+k)*8)*FLoad(w.y+(si+k)*8);v=v/FLoad(w.L+i*288+(a*6+a)*8);if(!FFinite(v)){Failure(w,3,stage,si+a);return false;}FStore(w.y+(si+a)*8,v);}}
 for(uint count=m.blocks;count>0;count--){uint i=count-1;uint2 b=Block(m,i);uint2 next=i+1<m.blocks?Block(m,i+1):uint2(0,0);for(uint countA=b.x;countA>0;countA--){uint a=countA-1;precise FactorReal v=FLoad(w.y+(b.y+a)*8);for(uint k=0;k<next.x;k++)v=v-FLoad(w.C+(i+1)*288+(k*6+a)*8)*FLoad(w.du+(next.y+k)*8);for(uint k=a+1;k<b.x;k++)v=v-FLoad(w.L+i*288+(k*6+a)*8)*FLoad(w.du+(b.y+k)*8);v=v/FLoad(w.L+i*288+(a*6+a)*8);if(!FFinite(v)){Failure(w,3,stage,b.y+a);return false;}FStore(w.du+(b.y+a)*8,v);}}
 return true;
}
[numthreads(GROUP_SIZE,1,1)]void Initial(uint3 tid:SV_DispatchThreadID){if(tid.x>=TileSystemCount)return;uint id=SystemBase+tid.x;if(id>=SystemCount)return;Meta m=Metadata(id);Addresses w=AddressesOf(m);if(Work.Load(w.status)!=0)return;
#ifdef OWNERSHIP_CHECK
uint prior;Work.InterlockedAdd(w.status+28,256u,prior);
#endif
if(!Solve(m,w,false,2))return;for(uint j=0;j<m.n;j++)SD(w.u+8*j,double(FLoad(w.du+8*j)));Work.Store(w.status+24,1);}
[numthreads(GROUP_SIZE,1,1)]void Correct(uint3 tid:SV_DispatchThreadID){if(tid.x>=TileSystemCount)return;uint id=SystemBase+tid.x;if(id>=SystemCount)return;Meta m=Metadata(id);Addresses w=AddressesOf(m);if(Work.Load(w.status)!=0)return;
#ifdef OWNERSHIP_CHECK
uint prior;Work.InterlockedAdd(w.status+28,65536u,prior);
#endif
for(uint j=0;j<m.n;j++)SD(w.s+8*j,0.0);
 for(uint r=0;r<m.rows;r++){uint p=m.rowOffset+r*88,count=Input.Load(p);precise double rho=InD(p+32);for(uint k=0;k<count;k++){uint j=Input.Load(p+8+4*k);rho=rho-InD(p+40+8*k)*WD(w.u+8*j);}if(!DFinite(rho)){Failure(w,4,3,r);return;}for(uint k=0;k<count;k++){uint j=Input.Load(p+8+4*k);precise double value=WD(w.s+8*j)+InD(p+40+8*k)*rho;if(!DFinite(value)){Failure(w,4,3,j);return;}SD(w.s+8*j,value);}}
 if(!Solve(m,w,true,3))return;double maximum=0.0;uint changes=0,lost=0;for(uint j=0;j<m.n;j++){double old=WD(w.u+8*j),du=double(FLoad(w.du+8*j));precise double next=old+du;if(!DFinite(next)){Failure(w,5,3,j);return;}double magnitude=du<0.0?-du:du;if(magnitude>maximum)maximum=magnitude;if(next!=old)changes++;else if(du!=0.0)lost++;SD(w.u+8*j,next);}uint k=Work.Load(w.status+12);if(k<5)SD(w.steps+8*k,maximum);Work.Store(w.status+12,k+1);Work.Store(w.status+16,Work.Load(w.status+16)+lost);if(changes==0)Work.Store(w.status+20,Work.Load(w.status+20)+1);
}
