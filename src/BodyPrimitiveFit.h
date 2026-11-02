#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Math/Mat44.h>
#include <Jolt/Physics/Hair/HairSettings.h>
#include <limits>
#include <cmath>
#include <algorithm>
#include <stdexcept>
namespace hairlab {
// Initialization-only principal direction; deterministic covariance power iteration.
inline JPH::Vec3 BodyPrincipalAxis(const JPH::Array<JPH::Vec3>& points){
 using namespace JPH;Vec3 mean=Vec3::sZero(),lo=Vec3::sReplicate(1.e30f),hi=-lo;
 for(auto p:points){mean+=p;lo=Vec3::sMin(lo,p);hi=Vec3::sMax(hi,p);}mean/=float(points.size());
 Vec3 e=hi-lo;uint k=e.GetX()>e.GetY()?0:1;if(e.GetZ()>e[k])k=2;Vec3 n=Vec3::sZero();n.SetComponent(k,1);
 for(uint it=0;it<24;++it){Vec3 next=Vec3::sZero();for(auto p:points){Vec3 d=p-mean;next+=d*d.Dot(n);}if(next.LengthSq()<1.e-20f)break;n=next.Normalized();}
 return n;
}
// Initialization only. Enclose one partition with a capsule. Other regions
// retain the original axis-aligned fit; refined shoulder pieces use PCA.
inline void FitBodyCapsule(const JPH::Array<JPH::Vec3>& points,JPH::uint32 joint,
 const JPH::Array<JPH::uint32>& skinVertices,const JPH::Array<JPH::Float3>& vertices,
 const JPH::Array<JPH::HairSettings::SkinWeight>& weights,JPH::uint32 numWeights,
 const JPH::Array<JPH::Mat44>& inverseBind,JPH::Array<JPH::Float4>& output,bool principal=false){
 using namespace JPH;
 Vec3 lo=Vec3::sReplicate(std::numeric_limits<float>::max()),hi=-lo;
 for(auto p:points){lo=Vec3::sMin(lo,p);hi=Vec3::sMax(hi,p);}
 Vec3 extent=hi-lo,center=(lo+hi)*.5f;uint axis=extent.GetX()>extent.GetY()?0:1;if(extent.GetZ()>extent[axis])axis=2;
 Vec3 n=Vec3::sZero();n.SetComponent(axis,1);
 if(principal){
  n=BodyPrincipalAxis(points);Vec3 u=n.Cross(std::abs(n.GetY())<.9f?Vec3::sAxisY():Vec3::sAxisX()).Normalized(),v=n.Cross(u);
  Vec3 minProj=Vec3::sReplicate(1.e30f),maxProj=-minProj;
  for(auto p:points){Vec3 q(p.Dot(n),p.Dot(u),p.Dot(v));minProj=Vec3::sMin(minProj,q);maxProj=Vec3::sMax(maxProj,q);}
  Vec3 mid=(minProj+maxProj)*.5f;center=n*mid.GetX()+u*mid.GetY()+v*mid.GetZ();
 }
 float radiusSq=0,tMin=1.e30f,tMax=-1.e30f;
 for(auto p:points){Vec3 d=p-center;float t=d.Dot(n);radiusSq=std::max(radiusSq,(d-t*n).LengthSq());tMin=std::min(tMin,t);tMax=std::max(tMax,t);}
 float radius=std::max(std::sqrt(radiusSq),.001f),a=tMax,b=tMin;
 for(auto p:points){Vec3 d=p-center;float t=d.Dot(n);float cap=std::sqrt(std::max(0.f,radius*radius-(d-t*n).LengthSq()));a=std::min(a,t+cap);b=std::max(b,t-cap);}
 if(a>b)a=b=(a+b)*.5f;
 Vec3 endpoints[2]={center+a*n,center+b*n};
 // Verify the initialization fit against every supplied partition point.
 for(auto p:points){Vec3 axisVector=endpoints[1]-endpoints[0];float den=axisVector.LengthSq();float t=den>1e-12f?std::clamp((p-endpoints[0]).Dot(axisVector)/den,0.f,1.f):0;
  if((p-endpoints[0]-t*axisVector).Length()>radius+1.e-5f)throw std::runtime_error("Primitive does not enclose bind partition");
 }
 Mat44 bind=inverseBind[joint].Inversed();Float4 packed[6];
 for(uint e=0;e<2;++e){Vec3 point=bind*endpoints[e];Float3 p;point.StoreFloat3(&p);packed[e]=Float4(p.x,p.y,p.z,e==0?radius:0);
  float js[4]={float(joint),0,0,0},ws[4]={1,0,0,0};
  if(!skinVertices.empty()){
   float best=std::numeric_limits<float>::max();uint nearest=skinVertices[0];
   for(uint v:skinVertices){float dist=(Vec3(vertices[v])-point).LengthSq();if(dist<best){best=dist;nearest=v;}}
   for(uint k=0;k<4;++k){if(k<numWeights){auto sw=weights[nearest*numWeights+k];js[k]=float(sw.mJointIdx);ws[k]=sw.mWeight;}else{js[k]=0;ws[k]=0;}}
   float sum=ws[0]+ws[1]+ws[2]+ws[3];if(!(sum>0))throw std::runtime_error("Invalid primitive skin weights");for(float &w:ws)w/=sum;
  }
  packed[2+e*2]=Float4(js[0],js[1],js[2],js[3]);packed[3+e*2]=Float4(ws[0],ws[1],ws[2],ws[3]);
 }
 for(auto p:packed)output.push_back(p);
}
inline void FitBodyCapsuleCover(const JPH::Array<JPH::Vec3>& points,JPH::uint32 joint,
 const JPH::Array<JPH::uint32>& skinVertices,const JPH::Array<JPH::Float3>& vertices,
 const JPH::Array<JPH::HairSettings::SkinWeight>& weights,JPH::uint32 numWeights,
 const JPH::Array<JPH::Mat44>& inverseBind,JPH::Array<JPH::Float4>& output,unsigned pieces){
 using namespace JPH;
 if(pieces<=1||points.size()<12){FitBodyCapsule(points,joint,skinVertices,vertices,weights,numWeights,inverseBind,output,true);return;}
 Vec3 axis=BodyPrincipalAxis(points);Array<float> projection;projection.reserve(points.size());
 for(auto p:points)projection.push_back(p.Dot(axis));std::sort(projection.begin(),projection.end());
 unsigned leftPieces=pieces/2;float split=projection[std::min(projection.size()-1,projection.size()*leftPieces/pieces)];
 float overlap=.06f*(projection.back()-projection.front());Array<Vec3> left,right;
 for(auto p:points){float t=p.Dot(axis);if(t<=split+overlap)left.push_back(p);if(t>=split-overlap)right.push_back(p);}
 if(left.empty()||right.empty()||left.size()==points.size()||right.size()==points.size()){
  FitBodyCapsule(points,joint,skinVertices,vertices,weights,numWeights,inverseBind,output,true);return;
 }
 FitBodyCapsuleCover(left,joint,skinVertices,vertices,weights,numWeights,inverseBind,output,leftPieces);
 FitBodyCapsuleCover(right,joint,skinVertices,vertices,weights,numWeights,inverseBind,output,pieces-leftPieces);
}

}
