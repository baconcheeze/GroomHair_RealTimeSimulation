#include "GuideState.h"
#include <iomanip>
#include <sstream>
#include <climits>
namespace {
using hair::F;
F point(Float3 p) {return {p.x,p.y,p.z};}
// Columns are the PHYSICAL d1,d2,d3 basis. No renderer matrix conversion here.
F look(F t,F up) {
    F r=hair::cross(up,t).unit(),u=hair::cross(t,r);
    float m[3][3]={{r.x,u.x,t.x},{r.y,u.y,t.y},{r.z,u.z,t.z}}; F q;
    float trace=m[0][0]+m[1][1]+m[2][2];
    if(trace>0) {float s=std::sqrt(trace+1)*2;q={ (m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,s*.25f};}
    else {int i=m[1][1]>m[0][0]?1:0;if(m[2][2]>m[i][i])i=2;int j=(i+1)%3,k=(j+1)%3;float s=std::sqrt(1+m[i][i]-m[j][j]-m[k][k])*2;q[i]=s*.25f;q[j]=(m[j][i]+m[i][j])/s;q[k]=(m[k][i]+m[i][k])/s;q.w=(m[k][j]-m[j][k])/s;}
    return q.unit();
}
F first(F t) {F reference=std::abs(t.x)<.8f?F{1,0,0}:F{0,1,0};F d1=(reference-reference.dot(t)*t).unit();return look(t,hair::cross(t,d1));}
F transport(F previous,F t) {
    F old=hair::rotate(previous,F{0,0,1});float dot=std::clamp(old.dot(t),-1.f,1.f);F rotation;
    if(dot<-.999999f)rotation=hair::axisAngle(hair::rotate(previous,F{1,0,0}),3.14159265358979323846f);
    else {auto c=hair::cross(old,t);rotation=F{c.x,c.y,c.z,1+dot}.unit();}
    F transported=(rotation*previous).unit(),d1=hair::rotate(transported,F{1,0,0});d1=(d1-d1.dot(t)*t).unit();F q=look(t,hair::cross(t,d1));return previous.dot(q)<0?-q:q;
}
void require(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
}
std::string GuideInitialization::json() const {std::ostringstream s;s<<std::setprecision(12)<<"{\"minLength\":"<<minLength<<",\"maxLength\":"<<maxLength<<",\"averageLength\":"<<averageLength<<",\"maxNormError\":"<<maxNorm<<",\"maxTangentRadians\":"<<maxTangent<<",\"maxRestSS\":"<<maxSS<<",\"maxRelativeReconstructionRadians\":"<<maxBend<<",\"minMass\":"<<minMass<<",\"maxMass\":"<<maxMass<<",\"totalMass\":"<<totalMass<<"}";return s.str();}
void GuideCpuState::initialize(const Strands& strands) {
    require(!strands.positions.empty()&&strands.offsets.size()>=2,"Empty guide state");source=&strands;
    uint64_t segments=0,bendCount=0;
    for(size_t i=0;i+1<strands.offsets.size();++i){uint64_t begin=strands.offsets[i],end=strands.offsets[i+1];require(end<=strands.positions.size()&&end>=begin+2,"Guide range requires >=2 vertices");uint64_t count=end-begin;require(count<=UINT_MAX&&segments+count-1<=UINT_MAX&&bendCount+count-2<=UINT_MAX,"Guide topology overflow");ranges.push_back({uint32_t(begin),uint32_t(count),uint32_t(segments),uint32_t(count-1),uint32_t(bendCount),uint32_t(count-2)});segments+=count-1;bendCount+=count-2;}
    orientations.resize(size_t(segments));lengths.resize(size_t(segments));radii.resize(size_t(segments),Radius);bends.resize(size_t(bendCount));masses.resize(strands.positions.size());velocities.resize(strands.positions.size());
    for(auto r:ranges) {F q{0,0,0,1};for(unsigned j=0;j<r.segmentCount;++j){unsigned v=r.firstVertex+j,s=r.firstSegment+j;F edge=point(strands.positions[v+1])-point(strands.positions[v]);float length=edge.norm();require(std::isfinite(length)&&length>LengthEpsilon,"Invalid rest geometry: degenerate segment");lengths[s]=length;q=j==0?first(edge/length):transport(q,edge/length);orientations[s]=q;
        float halfMass=.5f*Density*3.14159265358979323846f*Radius*Radius*length;masses[v]+=halfMass;masses[v+1]+=halfMass;
        if(j){F relative=(orientations[s-1].conj()*q).unit();bends[r.firstBend+j-1]=relative.w<0?-relative:relative;require(orientations[s-1].dot(q)>=-1e-6f,"Rest sign continuity failure");}
        F tangent=edge/length,material=hair::rotate(q,F{0,0,1});double tangentAngle=std::atan2(hair::cross(tangent.unit(),material.unit()).norm(),tangent.unit().dot(material.unit()));
        diagnostics.maxTangent=std::max(diagnostics.maxTangent,tangentAngle);diagnostics.maxNorm=std::max(diagnostics.maxNorm,double(std::abs(q.norm()-1)));diagnostics.maxSS=std::max(diagnostics.maxSS,double((tangent-material).norm()));
        diagnostics.minLength=std::min(diagnostics.minLength,double(length));diagnostics.maxLength=std::max(diagnostics.maxLength,double(length));diagnostics.averageLength+=length;
        if(j){auto target=(orientations[s-1]*bends[r.firstBend+j-1]).unit();diagnostics.maxBend=std::max(diagnostics.maxBend,double(hair::angle(target,q)));}
    }}
    diagnostics.averageLength/=double(segments);
    for(float m:masses){require(std::isfinite(m)&&m>0,"Nonpositive physical mass");diagnostics.minMass=std::min(diagnostics.minMass,double(m));diagnostics.maxMass=std::max(diagnostics.maxMass,double(m));diagnostics.totalMass+=m;}
    require(diagnostics.maxTangent<2e-5&&diagnostics.maxNorm<2e-6&&diagnostics.maxSS<2e-5&&diagnostics.maxBend<1e-5,"Guide rest frame tolerance failure");
}
std::vector<hair::F> GuideCpuState::perturb(const std::string& mode,unsigned participating,bool pinned,int seed,float degrees) const {
    auto q=orientations;hair::CsRandom random(seed);constexpr float rad=3.14159265358979323846f/180;
    for(unsigned strand=0;strand<std::min(participating,unsigned(ranges.size()));++strand){auto r=ranges[strand];int middle=int(r.segmentCount)/2;for(unsigned j=0;j<r.segmentCount;++j){if(j==0&&pinned)continue;F axis=mode=="twist"?F{0,0,1}:F{1,0,0};float a=int(j)==middle?20.f:0.f;
        if(mode=="smooth")a=20*std::max(0.f,1-std::abs(int(j)-middle)/4.f);
        if(mode=="random"){float x=float(random.sample())*2-1,y=float(random.sample())*2-1,z=float(random.sample())*2-1;axis={x,y,z};if(axis.dot(axis)<1e-12f)axis={1,0,0};a=float(random.sample()*2-1)*degrees;}
        if(mode!="rest")q[r.firstSegment+j]=(q[r.firstSegment+j]*hair::axisAngle(axis.unit(),a*rad)).unit();
    }}return q;
}
