#include "RestData.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <cstring>
namespace fs=std::filesystem;
template<class T> void append(std::vector<char>& bytes,const T& value) { auto* p=reinterpret_cast<const char*>(&value); bytes.insert(bytes.end(),p,p+sizeof(T)); }
template<class T> void replace(std::vector<char>& bytes,size_t at,T value) { std::memcpy(bytes.data()+at,&value,sizeof(T)); }
int main(int argc,char** argv) {
    try {
        if(argc!=3) throw std::runtime_error("Expected staged assets and scratch paths");
        fs::path source=argv[1],scratch=argv[2]; fs::create_directories(scratch/"hair"); fs::create_directories(scratch/"head");
        for(auto name:{"SelectedHead.nhm","JoltHeadRest.bytes"}) fs::copy_file(source/"head"/name,scratch/"head"/name,fs::copy_options::overwrite_existing);
        std::ofstream(scratch/"dataset.txt")<<"variable-length-test\n";
        std::vector<char> good;
        for(uint32_t n:{0x5452484au,1u,3u,9u,2u,0u,2u,5u,9u}) append(good,n);
        for(unsigned i=0;i<9;++i) append(good,Float3{float(i)*.001f,.1f,-.02f});
        append(good,0u); append(good,2u);
        auto load=[&](const std::vector<char>& b) { std::ofstream f(scratch/"hair/SelectedRest.bytes",std::ios::binary); f.write(b.data(),b.size()); f.close(); RestData d; d.load(scratch); return d; };
        auto data=load(good);
        Float3 world{0,-9.81f,0};auto gravity=worldVectorToHair(world,data.worldToHairRotation);
        auto& pose=data.hairLocalToWorld;
        std::array<float,3> components{gravity.x,gravity.y,gravity.z};
        for(unsigned axis=0;axis<3;axis++)if(std::abs(components[axis]-(-9.81f*pose.m[axis][1]))>2e-6f)throw std::runtime_error("Gravity inverse rotation convention failed");
        Float3 roundtrip;DirectX::XMStoreFloat3(&roundtrip,DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&gravity),DirectX::XMLoadFloat4x4(&pose)));
        if(std::abs(roundtrip.x)>3e-6f||std::abs(roundtrip.y+9.81f)>3e-6f||std::abs(roundtrip.z)>3e-6f)throw std::runtime_error("World gravity roundtrip failed");
        auto shifted=data.worldToHairRotation;shifted._41=123;shifted._42=-456;shifted._43=789;
        auto withoutTranslation=worldVectorToHair(world,shifted);
        if(std::memcmp(&gravity,&withoutTranslation,12)!=0)throw std::runtime_error("Translation affected gravity");
        DirectX::XMStoreFloat4x4(&data.cameraBasis,DirectX::XMMatrixRotationRollPitchYaw(.7f,1.2f,-.4f));
        auto withoutCamera=worldVectorToHair(world,data.worldToHairRotation);
        if(std::memcmp(&gravity,&withoutCamera,12)!=0)throw std::runtime_error("Camera affected gravity");
        std::cout<<"PASS fixed-pose gravity rotation, roundtrip, translation/camera independence\n";
        if(data.render.offsets!=std::vector<uint32_t>{0,2,5,9} || data.render.segments!=std::vector<uint32_t>{0,2,3,5,6,7} || data.guide.sourceIds!=std::vector<uint32_t>{0,2} || data.guide.positions.size()!=6) throw std::runtime_error("Variable strand boundaries/source IDs failed");
        unsigned rejected=0;
        auto invalid=[&](std::vector<char> bad) { try { load(bad); } catch(const std::exception&) { ++rejected; return; } throw std::runtime_error("Malformed hair accepted"); };
        auto bad=good; bad.pop_back(); invalid(bad);
        bad=good; bad.push_back(0); invalid(bad);
        bad=good; replace(bad,4,2u); invalid(bad);
        bad=good; replace(bad,8,UINT32_MAX); invalid(bad);
        bad=good; replace(bad,24,1u); invalid(bad); // one-vertex first strand
        bad=good; replace(bad,28,1u); invalid(bad); // decreasing offset
        bad=good; replace(bad,36,std::numeric_limits<float>::quiet_NaN()); invalid(bad);
        bad=good; replace(bad,bad.size()-4,3u); invalid(bad); // guide out of range
        bad=good; replace(bad,bad.size()-4,0u); invalid(bad); // duplicate guide
        if(rejected!=9) throw std::runtime_error("Missing negative tests");
        std::cout<<"PASS variable lengths 2/3/4, no cross-strand segments, exact guide subset; 9 malformed snapshots rejected\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
