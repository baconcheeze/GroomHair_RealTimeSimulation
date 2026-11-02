#include "CharacterLocomotion.h"
#include <Jolt/Math/Quat.h>
#include <Jolt/Core/Memory.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace JPH;
static void require(bool x,const char* reason){if(!x)throw std::runtime_error(reason);}
int main(int argc,char** argv){try{
    RegisterDefaultAllocator();
    require(argc==2,"asset directory required");std::filesystem::path dir=argv[1];
    std::ifstream in(dir/"character.bin",std::ios::binary);auto u=[&](){uint32_t n=0;in.read(reinterpret_cast<char*>(&n),4);return n;};
    u();auto nv=u();in.seekg(nv*12,std::ios::cur);auto nt=u();in.seekg(nt*12,std::ios::cur);
    auto nj=u();in.seekg(nj*64,std::ios::cur);auto nw=u();in.seekg(nv*nw*8,std::ios::cur);require(u()>0,"no pose");
    Array<Mat44> base(nj);
    for(auto& p:base){float f[6];in.read(reinterpret_cast<char*>(f),24);float w=std::sqrt(std::max(0.f,1-f[3]*f[3]-f[4]*f[4]-f[5]*f[5]));p=Mat44::sRotationTranslation(Quat(f[3],f[4],f[5],w),Vec3(f[0],f[1],f[2]));}
    require(bool(in),"truncated pose");
    std::ifstream rig(dir/"walk-rig.bin",std::ios::binary);rig.seekg(16);uint32_t j[9];rig.read(reinterpret_cast<char*>(j),sizeof(j));
    hairlab::CharacterLocomotion walk;walk.Load(dir);
    auto initial=walk.Pose(base,true);float maxBoneError=0;float behind=0;unsigned count=0;
    require(walk.clipFrames==39 && std::abs(walk.clipDuration-38.f/60)<1e-6f,"FBX timing mismatch");
    require(std::abs(walk.speed-walk.clipStride/walk.clipDuration)<1e-6f,"FBX root-motion rate mismatch");
    std::ifstream sk(dir/"skeleton.bin",std::ios::binary);uint32_t n;sk.read(reinterpret_cast<char*>(&n),4);Array<int32_t> parents(n);sk.read(reinterpret_cast<char*>(parents.data()),n*4);
    for(unsigned step=0;step<360;++step){
        walk.Advance(Vec3(0,0,-1),1.f/60);auto pose=walk.Pose(base,true);
        for(unsigned i=0;i<nj;++i)if(parents[i]>=0){
            auto parent=parents[i];float actual=(pose[i].GetTranslation()-pose[parent].GetTranslation()).Length();
            float rest=(base[i].GetTranslation()-base[parent].GetTranslation()).Length();
            maxBoneError=std::max(maxBoneError,std::abs(actual-rest));
        }
        if(step>120){behind+=.5f*(pose[153].GetTranslation().GetZ()+pose[173].GetTranslation().GetZ())-pose[j[0]].GetTranslation().GetZ();++count;}
    }
    require(maxBoneError<2e-5f,"retarget altered bone lengths");
    require(behind/count>.03f,"imported arms are not behind hips");
    require(walk.velocity.Length()<=walk.speed+.0001f,"clip speed cap failed");
    unsigned brakeSteps=unsigned(std::ceil(walk.speed/walk.braking*60))+4;
    for(unsigned i=0;i<brakeSteps;++i)walk.Advance(Vec3::sZero(),1.f/60);
    require(walk.velocity.Length()<1e-6f,"brake failed");
    walk.Reset();require(walk.position.Length()==0&&walk.velocity.Length()==0&&walk.phase==0,"reset failed");
    auto reset=walk.Pose(base,true);for(unsigned i=0;i<nj;++i)require((reset[i].GetTranslation()-initial[i].GetTranslation()).Length()<1e-7f,"reset pose mismatch");
    hairlab::CharacterLocomotion diagonal;diagonal.Load(dir);
    for(unsigned i=0;i<120;++i){walk.Advance(Vec3(0,0,-1),1.f/60);diagonal.Advance(Vec3(1,0,-1),1.f/60);}
    require(std::abs(walk.distance-diagonal.distance)<1e-5f,"diagonal speed boost");
    std::cout<<"PASS imported NarutoRun: frames="<<walk.clipFrames<<", duration="<<walk.clipDuration<<" s, speed="<<walk.speed<<" m/s, mean hands-behind-hips="<<behind/count<<" m, max bone length error="<<maxBoneError<<" m\n";
    hairlab::CharacterLocomotion dance;dance.Load(dir,true);
    require(dance.clipFrames==1010 && std::abs(dance.clipDuration-1009.f/60)<2e-6f,"Dance timing mismatch");
    auto danceInitial=dance.Pose(base,true);float danceMotion=0,danceBoneError=0;
    for(unsigned step=0;step<1100;++step){
        dance.Advance(Vec3(1,0,0),1.f/60);auto pose=dance.Pose(base,true);
        require(dance.position.Length()==0 && dance.velocity.Length()==0,"Dance must stay at its scene origin");
        for(unsigned i=0;i<nj;++i){
            auto p=pose[i].GetTranslation();require(std::isfinite(p.LengthSq()),"Nonfinite dance pose");
            danceMotion=std::max(danceMotion,(p-danceInitial[i].GetTranslation()).Length());
            if(parents[i]>=0) danceBoneError=std::max(danceBoneError,std::abs((p-pose[parents[i]].GetTranslation()).Length()-(base[i].GetTranslation()-base[parents[i]].GetTranslation()).Length()));
        }
    }
    require(danceMotion>.1f && danceBoneError<2e-5f,"Dance retarget motion or bone lengths failed");
    dance.Reset();auto danceReset=dance.Pose(base,true);
    for(unsigned i=0;i<nj;++i)require((danceReset[i].GetTranslation()-danceInitial[i].GetTranslation()).Length()<1e-7f,"Dance reset mismatch");
    std::cout<<"PASS Wave Hip Hop Dance: frames="<<dance.clipFrames<<", max joint motion="<<danceMotion<<", bone error="<<danceBoneError<<" m; full clip/loop/reset checked\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
