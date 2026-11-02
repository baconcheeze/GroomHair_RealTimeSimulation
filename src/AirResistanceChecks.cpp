#include <Jolt/Jolt.h>
#include <Jolt/Compute/ComputeSystem.h>
#include <Jolt/Compute/ComputeQueue.h>
#include <Jolt/Math/Float4.h>
#include "AirResistanceChecks.h"
#include <array>
#include <cmath>
#include <ostream>
#include <stdexcept>
void RunAirResistanceChecks(JPH::ComputeSystem *system,JPH::ComputeQueue *queue,std::ostream &report) {
 using namespace JPH;
 static_assert(sizeof(Float4)==16);
 std::array<Float4,48> input{};
 std::array<std::array<float,3>,12> expected{},transport{};
 const char *names[]={"stationary-frame","translation-follow","inertial-no-double-count","partial-follow","rotation-follow","moving-particle","disabled-exact","stationary-frame-clear","60-substeps","120-substeps","360-substeps","zero-world-speed"};
 auto set=[&](unsigned i,Float4 v,Float4 old,Float4 mapped,unsigned n,std::array<float,3> out,std::array<float,3> carry){input[4*i]=v;input[4*i+1]=old;input[4*i+2]=mapped;input[4*i+3]=Float4(float(n),0,0,0);expected[i]=out;transport[i]=carry;};
 set(0,{2,0,0,.1f},{0,0,0,0},{0,0,0,100},1,{1.8f,0,0},{0,0,0});
 set(1,{0,0,0,.1f},{1,0,0,0},{.95f,0,0,100},1,{-.5f,0,0},{5,0,0});
 set(2,{0,0,0,.1f},{1,0,0,1},{.95f,0,0,100},1,{0,0,0},{0,0,0});
 set(3,{0,0,0,.1f},{1,0,0,.25f},{.95f,0,0,100},1,{-.375f,0,0},{3.75f,0,0});
 set(4,{0,0,0,.1f},{1,0,0,0},{0,0,1,1},1,{-.1f,0,.1f},{1,0,-1});
 set(5,{2,0,0,.1f},{1,0,0,0},{.95f,0,0,100},1,{1.3f,0,0},{5,0,0});
 set(6,{2,3,4,0},{1,0,0,0},{-999,0,0,100},1,{2,3,4},{100000,0,0});
 set(7,{0,1,2,.1f},{1,2,3,0},{25,2,3,0},1,{0,.9f,1.8f},{0,0,0});
 for(unsigned i=8;i<=10;++i){unsigned n=i==8?60:i==9?120:360;const float gain=float(-std::expm1(-1.0/double(n)));set(i,{0,0,0,gain},{1,0,0,0},{.95f,0,0,100},n,{float(-5*(1-std::exp(-1.0))),0,0},{5,0,0});}
 set(11,{-5,0,0,.1f},{1,0,0,0},{.95f,0,0,100},1,{-5,0,0},{5,0,0});
 auto shader=system->CreateComputeShader("HairAirResistanceProbe",64);
 auto in=system->CreateComputeBuffer(ComputeBuffer::EType::Buffer,input.size(),sizeof(Float4),input.data());
 auto out=system->CreateComputeBuffer(ComputeBuffer::EType::RWBuffer,24,sizeof(Float4));
 if(shader.HasError()||in.HasError()||out.HasError())throw std::runtime_error("Air probe GPU allocation failed");
 auto readResult=out.Get()->CreateReadBackBuffer();
 if(readResult.HasError())throw std::runtime_error("Air probe readback allocation failed");
 auto read=readResult.Get();
 queue->SetShader(shader.Get());queue->SetBuffer("gInput",in.Get());queue->SetRWBuffer("gOutput",out.Get());queue->Dispatch(1,1,1);
 queue->ScheduleReadback(read,out.Get());queue->ExecuteAndWait();
 const auto *values=read->Map<Float4>(ComputeBuffer::EMode::Read);double maxError=0;
 report<<"{\"cases\":[";
 for(unsigned i=0;i<12;++i){double error=0;
  for(unsigned k=0;k<3;++k){
   const float a=(&values[2*i].x)[k],b=(&values[2*i+1].x)[k];
   if(!std::isfinite(a)||!std::isfinite(b)){error=INFINITY;break;}
   error=std::max(error,std::abs(double(a)-expected[i][k]));error=std::max(error,std::abs(double(b)-transport[i][k]));
  }
  if(i)report<<',';report<<"{\"name\":\""<<names[i]<<"\",\"maxError\":"<<error<<'}';maxError=std::max(maxError,error);
 }
 read->Unmap();
 report<<"],\"maxError\":"<<maxError<<",\"tolerance\":0.00005,\"pass\":"<<(maxError<5.e-5?"true":"false")<<'}';
 if(!std::isfinite(maxError)||maxError>=5.e-5)throw std::runtime_error("World air resistance GPU checks failed");
}
