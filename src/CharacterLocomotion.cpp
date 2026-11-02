#include "CharacterLocomotion.h"
#include <Jolt/Math/Quat.h>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cmath>
using namespace JPH;
namespace hairlab {
void CharacterLocomotion::Load(const std::filesystem::path& dir,bool dance){
    danceClip=dance;
    std::ifstream meta(dir/"walk-rig.bin",std::ios::binary);uint32_t h[3]{};
    meta.read(reinterpret_cast<char*>(h),12);meta.read(reinterpret_cast<char*>(&floorBindY),4);
    meta.read(reinterpret_cast<char*>(joints),sizeof(joints));
    if(!meta||h[0]!=0x4b4c4157||h[1]!=1||h[2]>4096||!h[2]||!std::isfinite(floorBindY))throw std::runtime_error("Invalid walking rig");
    std::ifstream sk(dir/"skeleton.bin",std::ios::binary);uint32_t n=0;sk.read(reinterpret_cast<char*>(&n),4);
    if(n!=h[2])throw std::runtime_error("Walking skeleton count mismatch");
    parents.resize(n);sk.read(reinterpret_cast<char*>(parents.data()),n*4);
    if(!sk)throw std::runtime_error("Truncated walking skeleton");
    for(auto j:joints)if(j>=n)throw std::runtime_error("Invalid walking joint");
    for(uint32_t j=0;j<n;++j){int k=int(j);unsigned hops=0;while(k>=0){if(k>=int(n)||++hops>n)throw std::runtime_error("Invalid walking hierarchy");k=parents[k];}}
    std::ifstream clip(dir/(danceClip?"wave-hip-hop.bin":"naruto-run.bin"),std::ios::binary|std::ios::ate);
    if(!clip)throw std::runtime_error("Missing selected native animation");
    auto bytes=clip.tellg();clip.seekg(0);uint32_t ch[5]{};float params[3]{};
    clip.read(reinterpret_cast<char*>(ch),20);clip.read(reinterpret_cast<char*>(params),12);
    if(ch[0]!=0x414e5552||ch[1]!=1||ch[2]!=n||ch[3]<2||ch[3]>10000
       ||uint64_t(bytes)!=32ull+uint64_t(ch[3])*(12+uint64_t(n)*16))throw std::runtime_error("Invalid selected clip animation header");
    clipFrames=ch[3];clipFps=params[0];clipDuration=params[1];clipStride=params[2];
    if(!std::isfinite(clipFps)||!std::isfinite(clipDuration)||!std::isfinite(clipStride)||clipFps<=0||clipDuration<=0||clipStride<=0)
        throw std::runtime_error("Invalid selected clip timing");
    speed=clipStride/clipDuration;clipRotations.resize(size_t(clipFrames)*n);clipRoots.resize(clipFrames);
    for(uint32_t f=0;f<clipFrames;++f){
        float p[3];clip.read(reinterpret_cast<char*>(p),12);
        if(!std::isfinite(p[0])||!std::isfinite(p[1])||!std::isfinite(p[2]))throw std::runtime_error("Nonfinite animation root");
        clipRoots[f]=Vec3(p[0],p[1],p[2]);
        for(uint32_t j=0;j<n;++j){
            float q[4];clip.read(reinterpret_cast<char*>(q),16);Quat rotation(q[0],q[1],q[2],q[3]);
            if(!std::isfinite(rotation.LengthSq())||std::abs(rotation.LengthSq()-1)>1e-3f)throw std::runtime_error("Invalid animation quaternion");
            clipRotations[size_t(f)*n+j]=rotation.Normalized();
        }
    }
    if(!clip)throw std::runtime_error("Truncated selected clip animation");
    for(uint32_t j=0;j<n;++j)if(parents[j]>=int32_t(j))throw std::runtime_error("Animation requires parent-first target skeleton");
    Reset();
}
void CharacterLocomotion::Reset(){position=velocity=Vec3::sZero();yaw=phase=distance=0;walkBlend=danceClip?1.f:0.f;}
void CharacterLocomotion::Advance(Vec3Arg input,float dt){
    if(!(dt>0&&std::isfinite(dt)))throw std::runtime_error("Invalid walking dt");
    if(danceClip){phase=std::fmod(phase+dt*(2*JPH_PI/clipDuration),2*JPH_PI);return;}
    Vec3 direction(input.GetX(),0,input.GetZ());float len=direction.Length();
    if(len>1)direction/=len;
    Vec3 target=direction*speed,change=target-velocity;
    float limit=(len>.01f?acceleration:braking)*dt;
    if(change.Length()>limit)change=change.Normalized()*limit;
    velocity+=change;position+=velocity*dt;
    float v=velocity.Length();distance+=v*dt;
    phase=std::fmod(phase+v*dt*(2*JPH_PI/clipStride),2*JPH_PI);
    float goal=std::min(1.f,v/.35f);
    walkBlend+=(goal-walkBlend)*(1-std::exp(-12*dt));
    if(v>.02f){
        float targetYaw=std::atan2(-velocity.GetX(),-velocity.GetZ());
        float angle=std::remainder(targetYaw-yaw,2*JPH_PI);
        yaw+=std::clamp(angle,-3.8f*dt,3.8f*dt);
    }
}
Array<Mat44> CharacterLocomotion::Pose(const Array<Mat44>& base,bool enabled) const{
    Array<Mat44> pose=base;if(!enabled)return pose;
    if(pose.size()!=parents.size())throw std::runtime_error("Walking pose count mismatch");
    if(walkBlend>1e-6f){
        float sample=phase/(2*JPH_PI)*float(clipFrames-1);
        uint32_t first=std::min(uint32_t(sample),clipFrames-2),next=first+1;float t=sample-first;
        Vec3 root=clipRoots[first]*(1-t)+clipRoots[next]*t;
        for(uint32_t j=0;j<pose.size();++j){
            int parent=parents[j];Mat44 local=parent>=0?base[parent].InversedRotationTranslation()*base[j]:base[j];
            Quat a=clipRotations[size_t(first)*pose.size()+j],b=clipRotations[size_t(next)*pose.size()+j];
            Quat rotation=local.GetQuaternion().SLERP(a.SLERP(b,t),walkBlend).Normalized();
            Vec3 translation=local.GetTranslation();if(j==joints[0])translation+=root*walkBlend;
            Mat44 result=Mat44::sRotationTranslation(rotation,translation);
            pose[j]=parent>=0?pose[parent]*result:result;
        }
    }
    Mat44 world=Mat44::sRotationTranslation(Quat::sRotation(Vec3::sAxisY(),yaw),position+Vec3(0,-floorBindY,0));
    for(auto& p:pose)p=world*p;
    return pose;
}
}
