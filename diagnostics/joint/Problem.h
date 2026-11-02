#pragma once
#include "../../src/AutonomousReference.h"
#include "../../src/OrientationFixtures.h"
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <memory>
namespace jointcpu {
namespace fs=std::filesystem;
using hair::D;using hair::F;using hair::require;
static_assert(sizeof(D)==32);
using State=windowmath::State;
inline D vec(Float3 v){return {v.x,v.y,v.z};}
inline void text(const fs::path& path,const std::string& s){std::ofstream f(path);f<<s;f.flush();require(bool(f),"Output write");}
template<class T>T get(std::ifstream& f){T v{};require(bool(f.read(reinterpret_cast<char*>(&v),sizeof(v))),"Truncated input");return v;}
template<class T>std::vector<T> getn(std::ifstream& f,unsigned n){std::vector<T> v(n);require(bool(f.read(reinterpret_cast<char*>(v.data()),n*sizeof(T))),"Truncated array");return v;}
template<class T>void put(std::ofstream& f,const T& v){f.write(reinterpret_cast<const char*>(&v),sizeof(v));}
template<class T>void putn(std::ofstream& f,const std::vector<T>& v){f.write(reinterpret_cast<const char*>(v.data()),v.size()*sizeof(T));}
inline std::string vectorJSON(D v){std::ostringstream s;s.precision(17);s<<'['<<v.x<<','<<v.y<<','<<v.z<<']';return s.str();}
inline State readState(const fs::path& p,unsigned n){
    std::ifstream f(p);std::string s((std::istreambuf_iterator<char>(f)),{});std::vector<double> a;
    for(const char* c=s.c_str();*c;){if((*c>='0'&&*c<='9')||*c=='-'||*c=='+'){char* end{};double x=std::strtod(c,&end);require(end!=c,"State parse");a.push_back(x);c=end;}else ++c;}
    require(a.size()==3*n+4*(n-1),"Expected persisted x/q state only");State r;unsigned k=0;for(unsigned j=0;j<n;j++){r.x.push_back({a[k],a[k+1],a[k+2]});k+=3;}for(unsigned j=0;j+1<n;j++){r.q.push_back({a[k],a[k+1],a[k+2],a[k+3]});k+=4;}return r;
}
inline void writeState(const fs::path& path,const State& s){std::ostringstream o;o.precision(17);o<<"{\"x\":[";for(unsigned j=0;j<s.x.size();j++){if(j)o<<',';o<<vectorJSON(s.x[j]);}o<<"],\"q\":[";for(unsigned j=0;j<s.q.size();j++){if(j)o<<',';auto q=s.q[j];o<<'['<<q.x<<','<<q.y<<','<<q.z<<','<<q.w<<']';}o<<"]}";text(path,o.str());}
struct Problem {
    Strands source;GuideCpuState cpu;State initial,reference,gpu;
    std::vector<Float3> y;std::vector<D> dy,preX;
    windowmath::Settings set{1,1.f/120,1,1e-5f,true,true};
    unsigned id=0,step=1,iterate=0;bool hasGPU=false,qualified=false;
    double refCorrection=0,refStationarity=0,refinement=0;
    void bind(){cpu.source=&source;dy.clear();for(auto p:y)dy.push_back(vec(p));}
};
inline void finishRest(Problem& p,const std::vector<Float3>& x,unsigned id){p.id=id;p.source.positions=x;p.source.offsets={0,unsigned(x.size())};p.source.sourceIds={id};p.source.derive();p.cpu.initialize(p.source);p.preX=windowmath::rest(p.cpu).x;}
inline std::unique_ptr<Problem> primary(const fs::path& dir){
    std::ifstream f(dir/"frozen/fixture.hfd",std::ios::binary);auto h=getn<uint32_t>(f,13);require(h[0]==0x31444648&&h[2]==9852&&h[6]==32,"Original HFD1 fixture required");auto constants=getn<float>(f,6);getn<float>(f,32+3+4);
    std::vector<std::vector<Float3>> v;for(unsigned j=0;j<8;j++)v.push_back(getn<Float3>(f,h[6]));
    auto q=getn<F>(f,h[7]),iq=getn<F>(f,h[7]),rq=getn<F>(f,h[7]),bend=getn<F>(f,h[8]);auto length=getn<float>(f,h[7]),mass=getn<float>(f,h[6]),radius=getn<float>(f,h[7]);require(f.peek()==EOF,"HFD trailing data");
    auto p=std::make_unique<Problem>();p->id=h[2];p->set={1,constants[0],constants[1],constants[2],h[11]!=0,h[12]!=0};p->source.positions=v[5];p->source.offsets={0,h[6]};p->source.sourceIds={h[2]};p->source.derive();
    p->cpu.ranges={{0,h[6],0,h[7],0,h[8]}};p->cpu.orientations=rq;p->cpu.bends=bend;p->cpu.lengths=length;p->cpu.masses=mass;p->cpu.radii=radius;p->cpu.velocities.resize(h[6]);
    for(auto x:v[4])p->initial.x.push_back(vec(x));for(auto x:iq)p->initial.q.emplace_back(x);for(auto x:v[0])p->preX.push_back(vec(x));p->y=v[3];p->gpu=readState(dir/"matched/gpu32.json",h[6]);p->reference=readState(dir/"matched/coupled-reference-0.json",h[6]);p->hasGPU=true;p->qualified=true;p->bind();return p;
}
inline void saveProblem(const fs::path& file,const Problem& p){
    std::ofstream f(file,std::ios::binary);for(unsigned x:{0x3150574au,unsigned(p.initial.x.size()),p.id,p.step,p.iterate,unsigned(p.set.pinX),unsigned(p.set.pinQ),unsigned(p.hasGPU),unsigned(p.qualified)})put(f,x);
    for(float x:{p.set.h,p.set.ss,p.set.bt})put(f,x);for(double x:{p.refCorrection,p.refStationarity,p.refinement})put(f,x);
    for(auto* s:{&p.initial,&p.reference,&p.gpu}){auto x=s->x,q=s->q;if(x.empty())x.resize(p.initial.x.size());if(q.empty())q.resize(p.initial.q.size());putn(f,x);putn(f,q);}
    putn(f,p.preX);putn(f,p.source.positions);putn(f,p.cpu.orientations);putn(f,p.cpu.bends);putn(f,p.cpu.lengths);putn(f,p.cpu.masses);putn(f,p.cpu.radii);putn(f,p.y);f.flush();require(bool(f),"JWP write");
}
inline std::unique_ptr<Problem> loadProblem(const fs::path& path){
    std::ifstream f(path,std::ios::binary);auto h=getn<uint32_t>(f,9);require(h[0]==0x3150574a&&h[1]>=2&&h[1]<=128,"JWP1 input");auto n=h[1];auto a=getn<float>(f,3);auto r=getn<double>(f,3);auto p=std::make_unique<Problem>();p->id=h[2];p->step=h[3];p->iterate=h[4];p->set={1,a[0],a[1],a[2],h[5]!=0,h[6]!=0};p->hasGPU=h[7]!=0;p->qualified=h[8]!=0;p->refCorrection=r[0];p->refStationarity=r[1];p->refinement=r[2];
    for(auto* s:{&p->initial,&p->reference,&p->gpu}){s->x=getn<D>(f,n);s->q=getn<D>(f,n-1);}p->preX=getn<D>(f,n);p->source.positions=getn<Float3>(f,n);p->source.offsets={0,n};p->source.sourceIds={p->id};p->source.derive();p->cpu.orientations=getn<F>(f,n-1);p->cpu.bends=getn<F>(f,n-2);p->cpu.lengths=getn<float>(f,n-1);p->cpu.masses=getn<float>(f,n);p->cpu.radii=getn<float>(f,n-1);p->cpu.velocities.resize(n);p->cpu.ranges={{0,n,0,n-1,0,n-2}};p->y=getn<Float3>(f,n);require(f.peek()==EOF,"JWP trailing data");p->bind();return p;
}
} // namespace jointcpu
