// Diagnostic executable only. Reuse the exact existing offscreen harness/helpers.
// No production source or shader is modified or replaced.
#define main UnusedOriginalBenchmarkEntry
#include "../src/AutonomousBench.cpp"
#undef main

namespace diagnosis {
struct Header {
    uint32_t magic=0x31444648,version=1,sourceId=9852,guideIndex=1023,seed=912773,step=1;
    uint32_t vertices=32,segments=31,bends=30,initializer=3,sweeps=32,pinX=1,pinQ=1;
    float h=1.f/120,ss=1,bt=1e-5f,gx{},gy{},gz{};
};
static_assert(sizeof(Header)==76);
struct Input {
    Header h;
    DirectX::XMFLOAT4X4 localToWorld{},worldToLocal{};
    Float3 rootX{};F rootQ{};
    std::vector<Float3> x,v,history,y,initialX,restX,historicalGPU,historicalReference;
    std::vector<F> q,initialQ,restQ,restBend;
    std::vector<float> length,mass,radius;
};
template<class T> void scalar(std::ofstream& f,const T& v){f.write(reinterpret_cast<const char*>(&v),sizeof(v));}
template<class T> T scalar(std::ifstream& f){T v{};require(bool(f.read(reinterpret_cast<char*>(&v),sizeof(v))),"Truncated fixture");return v;}
void save(const fs::path& p,const Input& i){
    std::ofstream f(p,std::ios::binary);scalar(f,i.h);scalar(f,i.localToWorld);scalar(f,i.worldToLocal);scalar(f,i.rootX);scalar(f,i.rootQ);
    for(auto* a:{&i.x,&i.v,&i.history,&i.y,&i.initialX,&i.restX,&i.historicalGPU,&i.historicalReference})saveArray(f,*a);
    for(auto* a:{&i.q,&i.initialQ,&i.restQ,&i.restBend})saveArray(f,*a);
    for(auto* a:{&i.length,&i.mass,&i.radius})saveArray(f,*a);
    f.flush();require(bool(f),"Fixture write failure");
}
Input load(const fs::path& p){
    std::ifstream f(p,std::ios::binary);Input i;i.h=scalar<Header>(f);auto h=i.h;
    require(h.magic==0x31444648&&h.version==1&&h.vertices>=2&&h.vertices<=1024&&h.segments==h.vertices-1&&h.bends==h.vertices-2,"Fixture header");
    i.localToWorld=scalar<DirectX::XMFLOAT4X4>(f);i.worldToLocal=scalar<DirectX::XMFLOAT4X4>(f);i.rootX=scalar<Float3>(f);i.rootQ=scalar<F>(f);
    for(auto* a:{&i.x,&i.v,&i.history,&i.y,&i.initialX,&i.restX,&i.historicalGPU,&i.historicalReference})*a=loadArray<Float3>(f,h.vertices);
    for(auto* a:{&i.q,&i.initialQ,&i.restQ})*a=loadArray<F>(f,h.segments);i.restBend=loadArray<F>(f,h.bends);
    i.length=loadArray<float>(f,h.segments);i.mass=loadArray<float>(f,h.vertices);i.radius=loadArray<float>(f,h.segments);
    require(f.peek()==EOF,"Fixture trailing bytes");return i;
}
template<class T> std::vector<T> slice(const std::vector<T>& a,unsigned first,unsigned n){return {a.begin()+first,a.begin()+first+n};}
std::vector<Float3> historical(const fs::path& p,unsigned slot){
    std::ifstream f(p,std::ios::binary);auto h=loadArray<uint32_t>(f,4);require(h[0]==0x31545a41&&h[1]==128&&h[2]==60,"Historical trajectory header");
    auto v=loadArray<Float3>(f,2*h[1]);return slice(v,h[1]+slot*32,32);
}
Input create(const fs::path& assets,const fs::path& old){
    RestData data;data.load(assets);Strands batch;batch.offsets={0};
    for(unsigned n:{17u,127u,511u,1023u}){batch.sourceIds.push_back(data.guide.sourceIds[n]);auto v=slice(data.guide.positions,data.guide.offsets[n],data.guide.offsets[n+1]-data.guide.offsets[n]);batch.positions.insert(batch.positions.end(),v.begin(),v.end());batch.offsets.push_back(unsigned(batch.positions.size()));}
    batch.derive();GuideCpuState cpu;cpu.initialize(batch);auto r=cpu.ranges.at(3);Input i;require(batch.sourceIds[3]==i.h.sourceId,"Wrong failing source strand");
    i.localToWorld=data.hairLocalToWorld;i.worldToLocal=data.worldToHairRotation;auto g=worldVectorToHair({0,-9.81f,0},i.worldToLocal);i.h.gx=g.x;i.h.gy=g.y;i.h.gz=g.z;
    i.restX=slice(batch.positions,r.firstVertex,r.vertexCount);i.x=i.restX;i.restQ=slice(cpu.orientations,r.firstSegment,r.segmentCount);i.q=i.restQ;i.history.resize(r.vertexCount);
    i.v=slice(kickFixture(cpu,i.h.seed+17*i.h.step),r.firstVertex,r.vertexCount);i.length=slice(cpu.lengths,r.firstSegment,r.segmentCount);i.radius=slice(cpu.radii,r.firstSegment,r.segmentCount);i.mass=slice(cpu.masses,r.firstVertex,r.vertexCount);i.restBend=slice(cpu.bends,r.firstBend,r.bendCount);
    i.rootX=i.restX[0];i.rootQ=i.restQ[0];i.historicalGPU=historical(old/"cycle-12/Release/replay-a.bin",3);i.historicalReference=historical(old/"cycle-12/Release/reference-refined-heldout-kick-912773.bin",3);return i;
}
struct One {
    Strands source;GuideCpuState cpu;guidegpu::State gpu;WindowSolver window;DynamicsSolver dyn;
    One(Context& c,const Input& i,const fs::path& shaders){
        auto h=i.h;source.positions=i.restX;source.offsets={0,h.vertices};source.sourceIds={h.sourceId};source.derive();
        cpu.source=&source;cpu.ranges={{0,h.vertices,0,h.segments,0,h.bends}};cpu.orientations=i.restQ;cpu.bends=i.restBend;cpu.lengths=i.length;cpu.masses=i.mass;cpu.radii=i.radius;cpu.velocities.resize(h.vertices);
        UploadList keep;c.submit([&](auto* l){gpu.initialize(c.device.Get(),l,cpu,nullptr,nullptr,keep);window.initialize(c.device.Get(),l,gpu,shaders/"window",keep);dyn.initialize(c.device.Get(),l,gpu,window,shaders/"dynamics",keep);});
        dyn.settings={1,h.h,h.ss,h.bt,h.pinX!=0,h.pinQ!=0};dyn.gravity={h.gx,h.gy,h.gz};dyn.initializer=h.initializer;dyn.sweeps=h.sweeps;
        c.submit([&](auto* l){dyn.reset(c.device.Get(),l,keep);gpu.position.upload(c.device.Get(),l,i.x.data(),i.x.size()*12,keep,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);gpu.reset(c.device.Get(),l,i.q,keep);gpu.velocity.upload(c.device.Get(),l,i.v.data(),i.v.size()*12,keep,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);dyn.accelerationBuffer().upload(c.device.Get(),l,i.history.data(),i.history.size()*12,keep,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);});
    }
    WindowSnapshot solve(Context& c,const windowmath::State& initial,const std::vector<Float3>& y,unsigned n,unsigned flags=4){
        UploadList keep;auto x=packedX(initial);std::vector<F> q;for(auto v:initial.q)q.emplace_back(v);
        c.submit([&](auto* l){gpu.position.upload(c.device.Get(),l,x.data(),x.size()*12,keep,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);gpu.reset(c.device.Get(),l,q,keep);window.settings=dyn.settings;window.prepare(c.device.Get(),l,y,"diagnosis",keep);window.run(l,n,flags);});
        auto s=window.read(c.device.Get(),c.submission());for(auto f:s.faults)require(f.code==0,"Frozen GPU failure");return s;
    }
};
struct Difference {double rms{},maximum{},q{};};
Difference difference(const windowmath::State& a,const windowmath::State& b){Difference d;for(unsigned j=0;j<a.x.size();j++){double e=(a.x[j]-b.x[j]).norm();d.rms+=e*e;d.maximum=std::max(d.maximum,e);}d.rms=std::sqrt(d.rms/a.x.size());for(unsigned j=0;j<a.q.size();j++)d.q=std::max(d.q,hair::angle(a.q[j],b.q[j]));return d;}
std::string json(Difference d,bool qAvailable=true){std::ostringstream s;s.precision(17);s<<"{\"rmsMeters\":"<<d.rms<<",\"maxMeters\":"<<d.maximum<<",\"qRadians\":";if(qAvailable)s<<d.q;else s<<"null";s<<"}";return s.str();}
std::string referenceJson(autonomous::ReferenceResult r){std::ostringstream s;s.precision(17);s<<"{\"qualified\":"<<(r.qualified?"true":"false")<<",\"iterationsNotQualification\":"<<r.iterations<<",\"F\":"<<r.objective<<",\"positionResidualCorrection\":"<<r.positionCorrection<<",\"qStationarity\":"<<r.stationarity<<",\"qSpectral\":"<<r.spectral<<"}";return s.str();}
void stateJSON(const fs::path& path,const windowmath::State& s){std::ostringstream o;o.precision(17);o<<"{\"x\":[";for(unsigned j=0;j<s.x.size();j++){if(j)o<<',';auto p=s.x[j];o<<'['<<p.x<<','<<p.y<<','<<p.z<<']';}o<<"],\"q\":[";for(unsigned j=0;j<s.q.size();j++){if(j)o<<',';auto q=s.q[j];o<<'['<<q.x<<','<<q.y<<','<<q.z<<','<<q.w<<']';}o<<"]}";write(path,o.str());}
windowmath::State positionOracle(const GuideCpuState& cpu,const windowmath::State& start,const std::vector<Float3>& y,const windowmath::Settings& set){
    std::vector<D> target;std::vector<double> a,c;std::vector<D> t;for(unsigned v=0;v<y.size();v++){target.push_back({y[v].x,y[v].y,y[v].z});a.push_back(double(cpu.masses[v])/(double(set.h)*set.h));}
    for(unsigned s=0;s<start.q.size();s++){double l=cpu.lengths[s];c.push_back(double(set.ss)/l);t.push_back(l*hair::rotate(start.q[s],D{0,0,1}));}
    auto result=start;result.x=windowmath::tridiagonal(target,a,c,t,set.pinX,start.x[0]);return result;
}
} // namespace diagnosis

int main(int argc,char** argv){using namespace diagnosis;try{
    fs::path out,fixture,old;for(int k=1;k<argc;k++){std::string a=argv[k];require(k+1<argc,"Missing argument");fs::path v=argv[++k];if(a=="--out")out=v;else if(a=="--fixture")fixture=v;else if(a=="--prior-run")old=v;else throw std::runtime_error("Unknown option");}
    require(!out.empty(),"--out required");fs::create_directories(out);wchar_t module[32768];GetModuleFileNameW(nullptr,module,32768);auto base=fs::path(module).parent_path();
    Input i=fixture.empty()?create(base/"assets",old):load(fixture);Context c;One one(c,i,base/"shaders");
    auto pre=one.dyn.read(c.device.Get(),c.submission());c.submit([&](auto* l){one.dyn.begin(l);});auto initial=one.dyn.read(c.device.Get(),c.submission());
    if(!i.y.empty()){require(std::memcmp(i.y.data(),initial.target.data(),i.y.size()*12)==0,"Replayed y mismatch");auto p=packedX(initial.guide.state);require(std::memcmp(i.initialX.data(),p.data(),p.size()*12)==0,"Replayed initializer mismatch");}
    i.y=initial.target;i.initialX=packedX(initial.guide.state);i.initialQ.clear();for(auto q:initial.guide.state.q)i.initialQ.emplace_back(q);save(out/"fixture.hfd",i);
    stateJSON(out/"initial.json",initial.guide.state);c.submit([&](auto* l){one.dyn.solve(l);one.dyn.end(l);});auto gpu=one.dyn.read(c.device.Get(),c.submission());stateJSON(out/"gpu32.json",gpu.guide.state);
    auto historic=gpu.guide.state,historicReference=gpu.guide.state;for(unsigned v=0;v<i.x.size();v++){historic.x[v]={i.historicalGPU[v].x,i.historicalGPU[v].y,i.historicalGPU[v].z};historicReference.x[v]={i.historicalReference[v].x,i.historicalReference[v].y,i.historicalReference[v].z};}
    require(difference(gpu.guide.state,historic).maximum==0,"Single-strand result differs from existing four-strand trajectory");
    std::ostringstream o;o.precision(17);o<<"{\"sourceId\":"<<i.h.sourceId<<",\"step\":1,\"historicalGPUExact\":true,\"historicalReferenceDifference\":"<<json(difference(gpu.guide.state,historicReference),false)<<",\"gravitySolver\":["<<i.h.gx<<','<<i.h.gy<<','<<i.h.gz<<"],\"initialEnergy\":"<<windowmath::measure(one.cpu,initial.guide.state,i.y,one.dyn.settings).json()<<",\"gpu32Energy\":"<<windowmath::measure(one.cpu,gpu.guide.state,i.y,one.dyn.settings).json();
    double predictionDouble=0,reconstruction=0;for(unsigned v=1;v<i.x.size();v++){D oldX{i.x[v].x,i.x[v].y,i.x[v].z},vel{i.v[v].x,i.v[v].y,i.v[v].z},gravity{i.h.gx,i.h.gy,i.h.gz};D y=oldX+double(i.h.h)*vel+(double(i.h.h)*i.h.h)*gravity;predictionDouble=std::max(predictionDouble,(y-D{i.y[v].x,i.y[v].y,i.y[v].z}).norm());reconstruction=std::max(reconstruction,(D{gpu.velocity[v].x,gpu.velocity[v].y,gpu.velocity[v].z}-(gpu.guide.state.x[v]-oldX)/double(i.h.h)).norm());}
    o<<",\"predictorVsDoubleMeters\":"<<predictionDouble<<",\"velocityReconstructionError\":"<<reconstruction<<",\"A\":[";auto cpu=initial.guide.state;unsigned done=0;
    for(unsigned n:{1u,2u,4u,8u,16u,32u}){for(;done<n;done++)windowmath::uncachedSweep(one.cpu,cpu,i.y,one.dyn.settings);auto g=one.solve(c,initial.guide.state,i.y,n);if(n!=1)o<<',';o<<"{\"sweeps\":"<<n<<",\"gpuVsDouble\":"<<json(difference(g.state,cpu))<<",\"doubleEnergy\":"<<windowmath::measure(one.cpu,cpu,i.y,one.dyn.settings).json()<<'}';stateJSON(out/("cpu-"+std::to_string(n)+".json"),cpu);}
    auto uncached=one.solve(c,initial.guide.state,i.y,32,5);o<<"],\"cachedVsUncached\":"<<json(difference(gpu.guide.state,uncached.state))<<",\"B\":[";
    for(unsigned kind=0;kind<2;kind++){
        auto start=kind?gpu.guide.state:initial.guide.state;auto oracle=positionOracle(one.cpu,start,i.y,one.dyn.settings);double oracleF=windowmath::measure(one.cpu,oracle,i.y,one.dyn.settings).total();
        if(kind)o<<',';o<<"{\"qFrom\":\""<<(kind?"gpu32":"initial")<<"\",\"oracleEnergy\":"<<windowmath::measure(one.cpu,oracle,i.y,one.dyn.settings).json()<<",\"startDifference\":"<<json(difference(start,oracle))<<",\"startConditionalGapJ\":"<<windowmath::measure(one.cpu,start,i.y,one.dyn.settings).total()-oracleF<<",\"iterations\":[";auto s=start;unsigned k=0;
        stateJSON(out/("oracle-"+std::to_string(kind)+".json"),oracle);
        for(unsigned n:{32u,4096u}){for(;k<n;k++)windowmath::uncachedSweep(one.cpu,s,i.y,one.dyn.settings,0,true);auto g=one.solve(c,start,i.y,n,6);if(n!=32)o<<',';o<<"{\"sweeps\":"<<n<<",\"gpuVsOracle\":"<<json(difference(g.state,oracle))<<",\"doubleVsOracle\":"<<json(difference(s,oracle))<<",\"gpuConditionalGapJ\":"<<windowmath::measure(one.cpu,g.state,i.y,one.dyn.settings).total()-oracleF<<",\"doubleConditionalGapJ\":"<<windowmath::measure(one.cpu,s,i.y,one.dyn.settings).total()-oracleF<<'}';}
        o<<"]}";
    }
    o<<"],\"C\":[";windowmath::State reference;std::vector<D> doubleTarget;for(auto p:i.y)doubleTarget.push_back({p.x,p.y,p.z});
    for(unsigned startKind=0;startKind<2;startKind++){auto s=startKind?gpu.guide.state:initial.guide.state;auto a=autonomous::minimize(one.cpu,s,doubleTarget,one.dyn.settings,1e-11,1e-9);auto coarse=s;auto b=autonomous::minimize(one.cpu,s,doubleTarget,one.dyn.settings,1e-12,1e-10);auto refined=s;auto d=autonomous::minimize(one.cpu,s,doubleTarget,one.dyn.settings,1e-14,1e-13);if(startKind)o<<',';o<<"{\"start\":\""<<(startKind?"gpu32":"initial")<<"\",\"solve\":"<<referenceJson(a)<<",\"refine\":"<<referenceJson(b)<<",\"additionalRefine\":"<<referenceJson(d)<<",\"firstRefinement\":"<<json(difference(coarse,refined))<<",\"additionalRefinement\":"<<json(difference(refined,s))<<",\"gpuVsReference\":"<<json(difference(gpu.guide.state,s))<<",\"cpuDouble32VsReference\":"<<json(difference(cpu,s))<<",\"gpuObjectiveGapJ\":"<<windowmath::measure(one.cpu,gpu.guide.state,i.y,one.dyn.settings).total()-windowmath::measure(one.cpu,s,i.y,one.dyn.settings).total()<<'}';stateJSON(out/("coupled-reference-"+std::to_string(startKind)+".json"),s);if(startKind==0)reference=s;else o<<"],\"referenceStartAgreement\":"<<json(difference(reference,s));}
    o<<'}';write(out/"results.json",o.str());c.audit();std::cout<<"Diagnostic complete; no production behavior changed. "<<out<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
