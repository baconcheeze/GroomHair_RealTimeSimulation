#include "RestData.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
namespace {
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
struct Reader {
    std::vector<char> bytes; size_t at{};
    explicit Reader(const std::filesystem::path& p) {
        std::ifstream s(p,std::ios::binary|std::ios::ate); if(!s) throw std::runtime_error("Missing asset: "+p.string());
        auto size=s.tellg(); require(size>0 && size<1024ll*1024*1024,"Asset size unsupported"); bytes.resize(static_cast<size_t>(size)); s.seekg(0); require(bool(s.read(bytes.data(),size)),"Asset read failed");
    }
    template<class T> T read() { require(sizeof(T)<=bytes.size()-at,"Truncated asset"); T t; std::memcpy(&t,bytes.data()+at,sizeof(T)); at+=sizeof(T); return t; }
    uint32_t count() { auto v=read<uint32_t>(); require(v<=bytes.size()/4 && v<=INT_MAX,"Unsupported count"); return v; }
    float scalar() { float f=read<float>(); require(std::isfinite(f),"Nonfinite asset value"); return f; }
    Float3 point() { float x=scalar(),y=scalar(),z=scalar(); return {x,y,z}; }
    std::array<float,16> matrix() {
        std::array<float,16> m; for(auto& f:m) f=scalar();
        require(std::abs(m[3])+std::abs(m[7])+std::abs(m[11])<1e-5f && std::abs(m[15]-1)<1e-5f,"Non-affine matrix");
        DirectX::XMFLOAT4X4 row; std::memcpy(&row,m.data(),64); float det=DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(DirectX::XMLoadFloat4x4(&row))); require(std::abs(det)>1e-10f,"Singular matrix"); return m;
    }
    void end() { require(at==bytes.size(),"Trailing asset data"); }
};
Float3 transform(const std::array<float,16>& column,Float3 p) {
    return {column[0]*p.x+column[4]*p.y+column[8]*p.z+column[12],column[1]*p.x+column[5]*p.y+column[9]*p.z+column[13],column[2]*p.x+column[6]*p.y+column[10]*p.z+column[14]};
}
float distance(Float3 a,Float3 b) { return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z)); }
void vectorJson(std::ostream& s,Float3 p) { s<<'['<<p.x<<','<<p.y<<','<<p.z<<']'; }
void normals(MeshData& mesh) {
    using namespace DirectX;
    for(auto& v:mesh.vertices) v.normal={0,0,0};
    for(size_t i=0;i<mesh.indices.size();i+=3) {
        auto& a=mesh.vertices[mesh.indices[i]]; auto& b=mesh.vertices[mesh.indices[i+1]]; auto& c=mesh.vertices[mesh.indices[i+2]];
        auto n=XMVector3Cross(XMLoadFloat3(&b.position)-XMLoadFloat3(&a.position),XMLoadFloat3(&c.position)-XMLoadFloat3(&a.position));
        for(auto* v:{&a,&b,&c}) XMStoreFloat3(&v->normal,XMLoadFloat3(&v->normal)+n);
    }
    for(auto& v:mesh.vertices) if(XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(&v.normal)))>1e-20f) XMStoreFloat3(&v.normal,XMVector3Normalize(XMLoadFloat3(&v.normal)));
}
}
void Bounds::add(Float3 p) { min={std::min(min.x,p.x),std::min(min.y,p.y),std::min(min.z,p.z)}; max={std::max(max.x,p.x),std::max(max.y,p.y),std::max(max.z,p.z)}; }
Float3 Bounds::center() const { return {(min.x+max.x)*.5f,(min.y+max.y)*.5f,(min.z+max.z)*.5f}; }
float Bounds::radius() const { return distance(min,max)*.5f; }
void Strands::derive() {
    require(!positions.empty() && offsets.size()>=2 && offsets.front()==0 && offsets.back()==positions.size(),"Invalid strand bounds");
    require(sourceIds.size()+1==offsets.size(),"Invalid source ID count");
    require(positions.size()<=UINT_MAX/2,"Hair draw arithmetic overflow");
    for(auto p:positions) { require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z),"Nonfinite position"); bounds.add(p); }
    for(size_t s=0;s<sourceIds.size();++s) {
        require(offsets[s+1]<=positions.size() && uint64_t(offsets[s])+2<=offsets[s+1],"Strand requires >=2 vertices and monotonic offsets");
        for(uint32_t v=offsets[s];v+1<offsets[s+1];++v) segments.push_back(v);
    }
}
std::string Strands::diagnostics() const {
    uint32_t lo=UINT_MAX,hi=0; for(size_t i=0;i<sourceIds.size();++i) { auto n=offsets[i+1]-offsets[i]; lo=std::min(lo,n); hi=std::max(hi,n); }
    double length=0; for(uint32_t i=1;i<offsets[1];++i) length+=distance(positions[i-1],positions[i]);
    std::ostringstream s; s<<std::setprecision(9)<<"{\"strands\":"<<sourceIds.size()<<",\"vertices\":"<<positions.size()<<",\"segments\":"<<segments.size()<<",\"bends\":"<<positions.size()-2*sourceIds.size()<<",\"minVertices\":"<<lo<<",\"maxVertices\":"<<hi<<",\"boundsMin\":"; vectorJson(s,bounds.min); s<<",\"boundsMax\":"; vectorJson(s,bounds.max);
    s<<",\"firstSourceID\":"<<sourceIds[0]<<",\"firstRoot\":"; vectorJson(s,positions[0]); s<<",\"firstTip\":"; vectorJson(s,positions[offsets[1]-1]); s<<",\"firstLengthMeters\":"<<length<<",\"firstVertices\":[";
    for(uint32_t i=0;i<std::min(4u,offsets[1]);++i) { if(i) s<<','; vectorJson(s,positions[i]); } s<<"]}"; return s.str();
}
void RestData::load(const std::filesystem::path& assets) {
    std::ifstream name(assets/"dataset.txt"); if(!std::getline(name,dataset)) throw std::runtime_error("Missing asset: "+(assets/"dataset.txt").string());
    Reader r(assets/"hair/SelectedRest.bytes"); require(r.read<uint32_t>()==0x5452484a && r.read<uint32_t>()==1,"Expected JHRT v1");
    auto ns=r.count(),nv=r.count(),ng=r.count(); require(ns && ng && ng<=ns && uint64_t(nv)>=2ull*ns,"Invalid hair counts");
    require(r.bytes.size()==20ull+4ull*(ns+1ull)+12ull*nv+4ull*ng,"Incorrect JHRT file length");
    render.offsets.resize(size_t(ns)+1); for(auto& o:render.offsets) o=r.count();
    render.positions.resize(nv); for(auto& p:render.positions) p=joltToNative(r.point());
    render.sourceIds.resize(ns); std::iota(render.sourceIds.begin(),render.sourceIds.end(),0); render.derive();
    guide.offsets.push_back(0);
    for(uint32_t i=0;i<ng;++i) {
        auto id=r.count(); require(id<ns && (i==0 || id>guide.sourceIds.back()),"Guide selection IDs must be sorted unique and in range");
        guide.sourceIds.push_back(id); guide.positions.insert(guide.positions.end(),render.positions.begin()+render.offsets[id],render.positions.begin()+render.offsets[id+1]);
        require(guide.positions.size()<=UINT_MAX,"Guide offset overflow"); guide.offsets.push_back(static_cast<uint32_t>(guide.positions.size()));
    }
    r.end(); guide.derive();
    for(size_t s=0;s<guide.sourceIds.size();++s) {
        auto id=guide.sourceIds[s]; auto count=guide.offsets[s+1]-guide.offsets[s];
        require(count==render.offsets[id+1]-render.offsets[id] && std::memcmp(guide.positions.data()+guide.offsets[s],render.positions.data()+render.offsets[id],size_t(count)*12)==0,"Guide/source mismatch");
    }
    Reader h(assets/"head/SelectedHead.nhm"); require(h.read<uint32_t>()==0x314d484e && h.read<uint32_t>()==1,"Expected NHM1 v1");
    auto hn=h.count(),hi=h.count(); require(hn && hi && hi%3==0 && h.bytes.size()==16ull+24ull*hn+4ull*hi,"Invalid NHM1 length/counts");
    selectedHead.vertices.resize(hn); for(auto& v:selectedHead.vertices) { v.position=h.point(); v.normal=h.point(); selectedHead.bounds.add(v.position); }
    selectedHead.indices.resize(hi); for(auto& i:selectedHead.indices) { i=h.count(); require(i<hn,"Head index out of range"); } h.end();
    Reader j(assets/"head/JoltHeadRest.bytes"); require(j.read<uint32_t>()==0x4448484a && j.read<uint32_t>()==1,"Expected JHHD v1");
    auto headJoint=j.count(),jv=j.count(),jt=j.count(),nj=j.count(),weights=j.count(),frames=j.count(),nh=j.count();
    require(jv && jt && nj && weights && frames && headJoint<nj,"Invalid JHHD counts");
    require(36ull+192ull+192ull*nj+8ull*jv*weights+36ull*jv+12ull*jt<=j.bytes.size(),"Invalid JHHD length");
    auto inverseBind=j.matrix(); auto bind=j.matrix(); auto neck=j.matrix(); (void)inverseBind; (void)bind;
    // Serialized values are COLUMN-major column-vector semantics. Construct the
    // row-vector transpose explicitly; H on both bases changes handedness.
    DirectX::XMFLOAT4X4 row;
    for(unsigned a=0;a<4;++a) for(unsigned b=0;b<4;++b) row.m[a][b]=neck[a*4+b]*(a==2?-1.f:1.f)*(b==2?-1.f:1.f);
    auto converted=DirectX::XMLoadFloat4x4(&row); DirectX::XMStoreFloat4x4(&cameraBasis,DirectX::XMMatrixInverse(nullptr,converted));
    hairLocalToWorld=row;
    auto rotation=converted;rotation.r[3]=DirectX::XMVectorSet(0,0,0,1);
    for(unsigned a=0;a<3;++a){
        require(std::abs(DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(rotation.r[a]))-1)<1e-4f,"Hair frame must be a rigid rotation");
        for(unsigned b=a+1;b<3;++b)require(std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(rotation.r[a],rotation.r[b])))<1e-4f,"Nonorthogonal hair frame");
    }
    require(std::abs(DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(rotation))-1)<1e-4f,"Invalid hair rotation handedness");
    DirectX::XMStoreFloat4x4(&worldToHairRotation,DirectX::XMMatrixTranspose(rotation));
    for(auto world:{Float3{0,-9.81f,0},Float3{1.3f,-2.1f,.7f}}){
        auto local=worldVectorToHair(world,worldToHairRotation);Float3 back;
        DirectX::XMStoreFloat3(&back,DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&local),rotation));
        require(distance(world,back)<3e-6f,"World/hair direction roundtrip failed");
    }
    for(auto p:{Float3{0,0,0},Float3{1,2,3},Float3{-.13f,.21f,-.07f}}) {
        Float3 actual; auto reflected=joltToNative(p); DirectX::XMStoreFloat3(&actual,DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&reflected),converted));
        require(distance(actual,joltToNative(transform(neck,p)))<1e-6f,"Matrix semantic conversion test failed");
    }
    for(uint32_t i=0;i<nj;++i) { j.matrix(); j.matrix(); j.matrix(); }
    for(uint64_t i=0;i<uint64_t(jv)*weights;++i) { auto joint=j.read<uint32_t>(); float w=j.scalar(); require(w>=0 && (w==0 || joint<nj),"Invalid skin weight"); }
    originalHead.vertices.resize(jv);
    for(auto& v:originalHead.vertices) { j.point(); j.point(); v.position=joltToNative(j.point()); originalHead.bounds.add(v.position); }
    originalHead.indices.resize(size_t(jt)*3);
    for(size_t i=0;i<originalHead.indices.size();i+=3) { auto a=j.count(),b=j.count(),c=j.count(); require(a<jv&&b<jv&&c<jv,"Original scalp index out of range"); originalHead.indices[i]=a; originalHead.indices[i+1]=c; originalHead.indices[i+2]=b; }
    normals(originalHead);
    for(uint32_t i=0;i<nh;++i) {
        HullData hull; hull.id=i; hull.joint=j.read<int32_t>(); auto count=j.count(),indices=j.count(),planes=j.count(); require(hull.joint>=-1 && hull.joint<int32_t(nj) && count>=4 && indices && indices%3==0,"Invalid hull metadata");
        require(128ull+36ull*count+4ull*indices+16ull*planes<=j.bytes.size()-j.at,"Truncated hull");
        hull.sourceWorld=j.matrix(); hull.sourceNeck=j.matrix();
        auto start=static_cast<uint32_t>(hullPositions.size());
        for(uint32_t v=0;v<count;++v) {
            hull.sourcePoints.push_back(j.point()); hull.sourceLocal.push_back(j.point()); auto neckP=j.point();
            require(distance(transform(hull.sourceNeck,hull.sourceLocal.back()),neckP)<2e-5f,"Hull local-to-neck transform mismatch");
            hull.neckPositions.push_back(joltToNative(neckP)); hullPositions.push_back(hull.neckPositions.back());
        }
        std::set<std::pair<uint32_t,uint32_t>> edges;
        for(uint32_t t=0;t<indices;t+=3) {
            auto a=j.count(),b=j.count(),c=j.count(); require(a<count&&b<count&&c<count,"Hull index out of range"); hull.triangles.insert(hull.triangles.end(),{a,c,b});
            for(auto e:{std::pair{a,b},std::pair{b,c},std::pair{c,a}}) edges.insert(std::minmax(e.first,e.second));
        }
        for(auto [a,b]:edges) { hullEdges.push_back(start+a); hullEdges.push_back(start+b); }
        for(uint32_t p=0;p<planes;++p) { auto n=j.point(); auto d=j.scalar(); require(std::abs(distance(n,{0,0,0})-1)<.001f,"Invalid hull plane"); hull.planes.push_back({n.x,n.y,n.z,d}); }
        hulls.push_back(std::move(hull));
    }
    j.end(); combined=render.bounds; combined.add(guide.bounds.min); combined.add(guide.bounds.max); combined.add(selectedHead.bounds.min); combined.add(selectedHead.bounds.max);
}
void RestData::report(const std::filesystem::path& path,unsigned frames,bool initial,bool final) const {
    if(path.has_parent_path()) std::filesystem::create_directories(path.parent_path()); std::ofstream s(path); require(bool(s),"Cannot open validation report");
    s<<std::setprecision(9)<<"{\n\"dataset\":\""<<dataset<<"\",\n\"space\":\"meters, LH Unity neck-local; scene transform identity\",\n\"guide\":"<<guide.diagnostics()<<",\n\"render\":"<<render.diagnostics()<<",\n\"guideSubsetExact\":true,\n\"matrixPointTestsPassed\":true,\n\"headVertices\":"<<selectedHead.vertices.size()<<",\"headTriangles\":"<<selectedHead.indices.size()/3<<",\"originalHeadVertices\":"<<originalHead.vertices.size()<<",\"originalHeadTriangles\":"<<originalHead.indices.size()/3<<",\n\"hulls\":[";
    for(size_t i=0;i<hulls.size();++i) { if(i) s<<','; const auto& h=hulls[i]; s<<"{\"id\":"<<h.id<<",\"attachedJoint\":"<<h.joint<<",\"vertices\":"<<h.sourceLocal.size()<<",\"triangles\":"<<h.triangles.size()/3<<",\"sourceNeckColumnMajor\":["; for(size_t k=0;k<16;++k) { if(k) s<<','; s<<h.sourceNeck[k]; } s<<"]}"; }
    s<<"],\n\"gpuInitialExact\":"<<(initial?"true":"false")<<",\"gpuFinalExact\":"<<(final?"true":"false")<<",\"renderedFrames\":"<<frames<<",\"simulationDispatches\":0\n}\n"; s.flush(); require(bool(s),"Report write failed");
}
