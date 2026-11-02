#include "DenimJacket.h"
#include <fstream>
#include <stdexcept>
#include <cmath>
DenimJacketAsset DenimJacketAsset::Load(const std::filesystem::path& path,uint32_t jointCount) {
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in)throw std::runtime_error("Missing denim jacket asset");
    auto size=in.tellg();in.seekg(0);
    uint32_t h[4]{};in.read(reinterpret_cast<char*>(h),sizeof(h));
    if(h[0]!=0x4a4d4e44 || h[1]!=1 || !h[2] || h[2]>1000000 || !h[3] || h[3]>6000000 || h[3]%3)
        throw std::runtime_error("Invalid denim jacket header");
    if(uint64_t(size)!=16ull+uint64_t(h[2])*92+uint64_t(h[3])*4)
        throw std::runtime_error("Invalid denim jacket length");
    DenimJacketAsset a;a.positions.resize(h[2]);a.colors.resize(h[2]);a.surfaces.resize(h[2]);a.indices.resize(h[3]);
    auto read=[&](auto& v){in.read(reinterpret_cast<char*>(v.data()),v.size()*sizeof(v[0]));};
    read(a.positions);read(a.colors);read(a.surfaces);read(a.indices);
    if(!in)throw std::runtime_error("Truncated denim jacket");
    for(size_t i=0;i<a.positions.size();++i){
        auto p=a.positions[i];auto& s=a.surfaces[i];float sum=0;
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))throw std::runtime_error("Nonfinite jacket vertex");
        auto c=a.colors[i];
        if(!std::isfinite(c.r)||!std::isfinite(c.g)||!std::isfinite(c.b)||!std::isfinite(c.a)
           || !std::isfinite(s.uv[0]) || !std::isfinite(s.uv[1]))throw std::runtime_error("Nonfinite jacket surface");
        float n2=s.normal[0]*s.normal[0]+s.normal[1]*s.normal[1]+s.normal[2]*s.normal[2];
        if(!std::isfinite(n2)||n2<.9f||n2>1.1f)throw std::runtime_error("Invalid jacket normal");
        for(unsigned k=0;k<4;++k){
            if(s.joints[k]>=jointCount || !std::isfinite(s.weights[k]) || s.weights[k]<0)throw std::runtime_error("Invalid jacket skin");
            sum+=s.weights[k];
        }
        if(std::abs(sum-1)>1e-4f)throw std::runtime_error("Invalid jacket weight sum");
    }
    for(auto i:a.indices)if(i>=h[2])throw std::runtime_error("Invalid jacket index");
    return a;
}
