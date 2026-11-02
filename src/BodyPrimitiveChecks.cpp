#include <Jolt/Jolt.h>
#include <Jolt/Compute/ComputeSystem.h>
#include <Jolt/Compute/ComputeQueue.h>
#include <Jolt/Shaders/HairBodyStructs.h>
#include <Jolt/Math/Mat44.h>
#include "BodyPrimitiveChecks.h"
#include <vector>
#include <stdexcept>
#include <cmath>
#include <ostream>
#include <algorithm>
void RunBodyPrimitiveChecks(JPH::ComputeSystem *system,JPH::ComputeQueue *queue,std::ostream &report){
 using namespace JPH;static_assert(sizeof(JPH_HairPosition)==28);static_assert(sizeof(JPH_HairCollisionPlane)==20);static_assert(sizeof(Float4)==16);
 auto prepare=system->CreateComputeShader("HairBodyPrepare",64).Get(),fieldShader=system->CreateComputeShader("HairBodyField",64).Get(),query=system->CreateComputeShader("HairBodyQuery",64).Get();
 double maxSkin=0,maxDistance=0;unsigned comparisons=0,checks=0;report<<"{\"cases\":[";bool first=true;
 for(uint count:{1u,31u,32u,33u,63u,64u,65u,127u,128u}){
  std::vector<Float4> input(count*6);std::vector<Vec3> a(count),b(count);std::vector<float> radius(count);
  Mat44 matrices[2]={Mat44::sTranslation(Vec3(.1f,-.05f,.03f)),Mat44::sTranslation(Vec3(-.04f,.02f,.08f))};
  for(uint i=0;i<count;++i){float x=float(i%8)*.3f-1,y=float((i/8)%4)*.35f-.5f,z=float(i/32)*.4f-.6f;radius[i]=.025f+float(i%3)*.01f;
   input[6*i]={x,y,z,radius[i]};input[6*i+1]={x,y+(i%5==0?0:.2f),z,0};input[6*i+2]={0,1,0,0};input[6*i+3]={.75f,.25f,0,0};input[6*i+4]={0,1,0,0};input[6*i+5]={.25f,.75f,0,0};
   a[i]=.75f*(matrices[0]*Vec3(x,y,z))+.25f*(matrices[1]*Vec3(x,y,z));
   float yy=input[6*i+1].y;b[i]=.25f*(matrices[0]*Vec3(x,yy,z))+.75f*(matrices[1]*Vec3(x,yy,z));
  }
  std::vector<JPH_HairPosition> positions(2049);uint seed=731;
  for(uint i=0;i<positions.size();++i){auto rnd=[&](){seed=1664525*seed+1013904223;return float(seed>>8)/16777216.f;};positions[i].mPosition={rnd()*4-2,rnd()*4-2,rnd()*4-2};positions[i].mRotation={0,0,0,1};}
  for(uint i=0;i<count;++i){Vec3 center=(a[i]+b[i])*.5f;center.StoreFloat3(&positions[i].mPosition);}
  // Far out-of-grid points exercise clamped first/last bins without undefined indices.
  positions[2047].mPosition={-8,-8,-8};positions[2048].mPosition={8,8,8};
  std::vector<JPH_HairCollisionPlane> planes(positions.size());for(auto &p:planes){p.mPlane={1,0,0,1.e6f};p.mShapeIndex=0;}
  auto make=[&](ComputeBuffer::EType type,uint n,uint stride,const void *data=nullptr){auto result=system->CreateComputeBuffer(type,n,stride,data);if(result.HasError())throw std::runtime_error("Primitive check allocation failed");return result.Get();};
  auto in=make(ComputeBuffer::EType::Buffer,uint(input.size()),16,input.data());auto joints=make(ComputeBuffer::EType::Buffer,2,64,matrices);
  auto state=make(ComputeBuffer::EType::RWBuffer,count*4,16),wire=make(ComputeBuffer::EType::RWBuffer,count*200,12),field=make(ComputeBuffer::EType::RWBuffer,384,4);
  auto pos=make(ComputeBuffer::EType::Buffer,uint(positions.size()),28,positions.data()),contacts=make(ComputeBuffer::EType::RWBuffer,uint(planes.size()),20,planes.data());
  auto context=make(ComputeBuffer::EType::ConstantBuffer,1,sizeof(JPH_HairUpdateContext)),bc=make(ComputeBuffer::EType::ConstantBuffer,1,sizeof(JPH_BodyContext));
  auto *c=context->Map<JPH_HairUpdateContext>(ComputeBuffer::EMode::Write);*c={};c->cNumVertices=uint(positions.size());for(uint i=0;i<4;++i)Mat44::sIdentity().GetColumn4(i).StoreFloat4(&c->cDeltaTransform[i]);context->Unmap();
  auto *cfg=bc->Map<JPH_BodyContext>(ComputeBuffer::EMode::Write);*cfg={};cfg->cBodyCount=count;cfg->cBodyReset=1;cfg->cBodyInvDt=60;bc->Unmap();
  queue->SetShader(prepare);queue->SetConstantBuffer("gContext",context);queue->SetConstantBuffer("gBodyContext",bc);queue->SetBuffer("gBodyInput",in);queue->SetBuffer("gScalpJointMatrices",joints);queue->SetRWBuffer("gBodyState",state);queue->SetRWBuffer("gBodyWire",wire);queue->Dispatch((count+63)/64);
  queue->SetShader(fieldShader);queue->SetConstantBuffer("gBodyContext",bc);queue->SetBuffer("gBodyState",state);queue->SetRWBuffer("gBodyField",field);queue->Dispatch(6);
  queue->SetShader(query);queue->SetConstantBuffer("gContext",context);queue->SetConstantBuffer("gBodyContext",bc);queue->SetBuffer("gBodyState",state);queue->SetBuffer("gBodyField",field);queue->SetBuffer("gPositions",pos);queue->SetRWBuffer("gCollisionPlanes",contacts);queue->Dispatch((uint(positions.size())+63)/64);
  auto sr=state->CreateReadBackBuffer().Get(),fr=field->CreateReadBackBuffer().Get(),cr=contacts->CreateReadBackBuffer().Get();queue->ScheduleReadback(sr,state);queue->ScheduleReadback(fr,field);queue->ScheduleReadback(cr,contacts);queue->ExecuteAndWait();
  auto *states=sr->Map<Float4>(ComputeBuffer::EMode::Read);auto *masks=fr->Map<uint>(ComputeBuffer::EMode::Read);auto *results=cr->Map<JPH_HairCollisionPlane>(ComputeBuffer::EMode::Read);
  for(uint i=0;i<count;++i){maxSkin=std::max(maxSkin,double((Vec3(states[4*i].x,states[4*i].y,states[4*i].z)-a[i]).Length()));maxSkin=std::max(maxSkin,double((Vec3(states[4*i+1].x,states[4*i+1].y,states[4*i+1].z)-b[i]).Length()));}
  auto bin=[](float p){return uint(std::clamp(std::floor((p+4)*4),0.f,31.f));};
  for(uint v=0;v<positions.size();++v){Vec3 p(positions[v].mPosition);float nearest=1e6f;
   for(uint i=0;i<count;++i){Vec3 axis=b[i]-a[i];float den=axis.LengthSq(),t=den>1e-12f?std::clamp((p-a[i]).Dot(axis)/den,0.f,1.f):0;float distance=(p-a[i]-t*axis).Length()-radius[i];nearest=std::min(nearest,distance);
    if(distance<=.24999f){uint word=i/32,bit=1u<<(i%32);uint mask=masks[bin(p.GetX())*4+word]&masks[128+bin(p.GetY())*4+word]&masks[256+bin(p.GetZ())*4+word];if(!(mask&bit))throw std::runtime_error("Body bitmask false negative");++checks;}
   }
   if(nearest<=.24999f){auto pl=results[v].mPlane;float distance=Vec3(pl.x,pl.y,pl.z).Dot(p)+pl.w;double err=std::abs(double(distance)-nearest);maxDistance=std::max(maxDistance,err);++comparisons;if(!std::isfinite(err)||err>2.e-5||!(results[v].mShapeIndex&0x80000000u))throw std::runtime_error("Primitive narrow phase mismatch");}
  }
  for(uint axis=0;axis<3;++axis)for(uint binId=0;binId<32;++binId)for(uint word=0;word<4;++word){uint expected=0;for(uint j=0;j<32;++j){uint id=word*32+j;if(id>=count)break;float lo=std::min(a[id][axis],b[id][axis])-radius[id]-.25f,hi=std::max(a[id][axis],b[id][axis])+radius[id]+.25f;if(binId>=bin(lo)&&binId<=bin(hi))expected|=1u<<j;}if(masks[axis*128+binId*4+word]!=expected)throw std::runtime_error("Body field bit mismatch");}
  sr->Unmap();fr->Unmap();cr->Unmap();
  if(count==65){
   Mat44 moved[2]={matrices[0],matrices[1]};for(auto &m:moved)m.SetTranslation(m.GetTranslation()+Vec3(.02f,.01f,0));
   auto movingJoints=make(ComputeBuffer::EType::Buffer,2,64,moved);
   auto *cc=context->Map<JPH_HairUpdateContext>(ComputeBuffer::EMode::Write);*cc={};for(uint k=0;k<4;++k)Mat44::sTranslation(Vec3(-.03f,0,0)).GetColumn4(k).StoreFloat4(&cc->cDeltaTransform[k]);context->Unmap();
   auto *config=bc->Map<JPH_BodyContext>(ComputeBuffer::EMode::Write);config->cBodyCount=count;config->cBodyInvDt=60;config->cBodyReset=0;config->cBodyReserved=0;bc->Unmap();
   queue->SetShader(prepare);queue->SetConstantBuffer("gContext",context);queue->SetConstantBuffer("gBodyContext",bc);queue->SetBuffer("gBodyInput",in);queue->SetBuffer("gScalpJointMatrices",movingJoints);queue->SetRWBuffer("gBodyState",state);queue->SetRWBuffer("gBodyWire",wire);queue->Dispatch((count+63)/64);queue->ScheduleReadback(sr,state);queue->ExecuteAndWait();
   auto *moving=sr->Map<Float4>(ComputeBuffer::EMode::Read);for(uint id=0;id<count;++id){auto v=moving[id*4+2],w=moving[id*4+3];if((Vec3(v.x,v.y,v.z)-Vec3(3,.6f,0)).Length()>1e-4f||Vec3(w.x,w.y,w.z).Length()>1e-3f)throw std::runtime_error("Primitive world contact velocity/history mismatch");}sr->Unmap();
  }
  if(!first)report<<',';first=false;report<<"{\"count\":"<<count<<",\"pass\":true}";
 }
 report<<"],\"maxSkinErrorMeters\":"<<maxSkin<<",\"maxDistanceErrorMeters\":"<<maxDistance<<",\"narrowComparisons\":"<<comparisons<<",\"conservativeCandidateChecks\":"<<checks<<",\"pass\":true}";
 if(maxSkin>1.e-5)throw std::runtime_error("Body GPU skin mismatch");
}
