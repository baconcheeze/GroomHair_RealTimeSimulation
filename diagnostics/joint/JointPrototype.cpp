#include "JointMath.h"
#include <iostream>
using namespace jointcpu;
static double rms(const State& a,const State& b){double v=0;for(unsigned j=0;j<a.x.size();j++)v+=(a.x[j]-b.x[j]).dot(a.x[j]-b.x[j]);return std::sqrt(v/a.x.size());}
static double maximum(const State& a,const State& b){double v=0;for(unsigned j=0;j<a.x.size();j++)v=std::max(v,(a.x[j]-b.x[j]).norm());return v;}
static void qualify(Problem& p,bool reuse){
    autonomous::Layout l(p.cpu,p.set);
    if(reuse){auto s=autonomous::evaluate(p.cpu,p.reference,p.dy,p.set,l,true);auto r=autonomous::diagnose(p.cpu,p.reference,p.set,l,s);p.refCorrection=r.positionCorrection;p.refStationarity=r.stationarity;p.qualified=r.positionCorrection<1e-12&&r.stationarity<1e-10&&r.spectral<1e-10;return;}
    p.reference=p.initial;auto a=autonomous::minimize(p.cpu,p.reference,p.dy,p.set,1e-11,1e-9);auto before=p.reference;auto b=autonomous::minimize(p.cpu,p.reference,p.dy,p.set,1e-13,1e-11);p.refinement=maximum(before,p.reference);p.refCorrection=b.positionCorrection;p.refStationarity=b.stationarity;p.qualified=a.qualified&&b.qualified&&p.refinement<1e-6;
}
static State exactPositions(const Problem& p,const State& start){std::vector<double>a,c;std::vector<D>t;for(auto m:p.cpu.masses)a.push_back(double(m)/(double(p.set.h)*p.set.h));for(unsigned s=0;s<p.cpu.lengths.size();s++){double l=p.cpu.lengths[s];c.push_back(double(p.set.ss)/l);t.push_back(l*hair::rotate(start.q[s],D{0,0,1}));}auto r=start;r.x=windowmath::tridiagonal(p.dy,a,c,t,p.set.pinX,start.x[0]);return r;}
static void baseline(const Problem& p,const fs::path& out){auto cpu=p.initial;for(unsigned k=0;k<32;k++)windowmath::uncachedSweep(p.cpu,cpu,p.y,p.set);auto polished=exactPositions(p,p.gpu);double a=rms(cpu,p.gpu),b=rms(p.gpu,p.reference),c=rms(polished,p.reference);
    require(std::abs(a-0.0733671e-6)<1e-9&&std::abs(b-116.713e-6)<5e-8&&std::abs(c-115.280e-6)<5e-8,"Original baseline reproduction mismatch");require(p.qualified,"Stored reference residual qualification failed");std::ostringstream s;s.precision(17);s<<"{\"gpuVsSameOrderDoubleRMS\":"<<a<<",\"gpuVsReferenceRMS\":"<<b<<",\"exactPositionsAtGpuQRMS\":"<<c<<",\"storedReferenceQualified\":true,\"referencePositionCorrection\":"<<p.refCorrection<<",\"referenceQStationarity\":"<<p.refStationarity<<"}";text(out/"baseline-reproduced.json",s.str());}
static void prepare(const fs::path& root,const fs::path& out){
    fs::create_directories(out);auto first=primary(root/"artifacts/diagnosis/20261030T000839Z");qualify(*first,true);baseline(*first,out);saveProblem(out/"primary.jwp",*first);
    RestData data;data.load(root/"assets");auto old=root/"artifacts/autonomous/20261029T173940Z";std::ifstream f(old/"cycle-01/frozen.bin",std::ios::binary);auto h=getn<uint32_t>(f,5);require(h[0]==0x31465a41&&h[1]==128&&h[2]==124&&h[3]==4&&h[4]==8,"Persisted frozen batch layout");auto oldX=getn<Float3>(f,h[1]);getn<Float3>(f,h[1]);getn<Float3>(f,h[1]);getn<F>(f,h[2]);auto y=getn<Float3>(f,h[1]);require(f.peek()==EOF,"Frozen input trailing bytes");
    std::ifstream init(old/"cycle-12/final-checks/frozen-3-cached.bin",std::ios::binary);auto x=getn<Float3>(init,h[1]);auto q=getn<F>(init,h[2]);
    for(unsigned slot:{0u,2u}){unsigned guide=slot==0?0:5;auto p=std::make_unique<Problem>();std::vector<Float3> rest(data.guide.positions.begin()+data.guide.offsets[guide],data.guide.positions.begin()+data.guide.offsets[guide+1]);finishRest(*p,rest,data.guide.sourceIds[guide]);p->step=9;p->iterate=1;
        for(unsigned j=0;j<32;j++){p->initial.x.push_back(vec(x[slot*32+j]));p->y.push_back(y[slot*32+j]);p->preX[j]=vec(oldX[slot*32+j]);}for(unsigned j=0;j<31;j++)p->initial.q.emplace_back(q[slot*31+j]);p->bind();qualify(*p,false);saveProblem(out/("heldout-slot"+std::to_string(slot)+".jwp"),*p);
    }
    for(unsigned n:{2u,5u})for(unsigned pins=0;pins<4;pins++){
        auto p=std::make_unique<Problem>();p->set.pinX=(pins&1)!=0;p->set.pinQ=(pins&2)!=0;std::vector<Float3> rest;float z=0;const float lengths[]={.008f,.011f,.009f,.013f};for(unsigned j=0;j<n;j++){rest.push_back({0,0,z});if(j+1<n)z+=lengths[j];}finishRest(*p,rest,90000+n*10+pins);p->initial=windowmath::rest(p->cpu);
        for(unsigned j=0;j<n;j++){double u=double(j)/(n-1);p->initial.x[j]=p->initial.x[j]+D{.0002*std::sin(3.141592653589793*u),-.0001*u,.00005*u*u};auto y=vec(rest[j])+D{.0003*u,-.0002*u*u,0};p->y.push_back({float(y.x),float(y.y),float(y.z)});}if(p->set.pinX)p->initial.x[0]=vec(rest[0]);
        for(unsigned j=0;j+1<n;j++){auto q=hair::axisAngle(D{1,.3,.7},.2*std::sin(3.141592653589793*(j+.5)/(n-1)))*p->initial.q[j];p->initial.q[j]=j%2?-q:q;}if(p->set.pinQ)p->initial.q[0]=D(p->cpu.orientations[0]);p->bind();qualify(*p,false);saveProblem(out/("synthetic-"+std::to_string(n)+"-pins"+std::to_string(pins)+".jwp"),*p);
    }
    auto tie=loadProblem(out/"synthetic-5-pins3.jwp");tie->initial.q[1]=D(tie->cpu.orientations[1]);tie->initial.q[2]=hair::axisAngle(D{1,0,0},3.141592653589793)*D(tie->cpu.orientations[2]);tie->qualified=false;saveProblem(out/"tie-jacobian-only.jwp",*tie);
}
static std::string metrics(const Problem& p,const State& s,const Work& w,unsigned macro){
    auto e=windowmath::measure(p.cpu,s,p.y,p.set);auto ref=windowmath::measure(p.cpu,p.reference,p.y,p.set);double maxAngle=0,angle2=0,ss=0,strain=0;
    for(unsigned j=0;j<s.q.size();j++){double a=hair::angle(s.q[j],p.reference.q[j]);maxAngle=std::max(maxAngle,a);angle2+=a*a;auto edge=s.x[j+1]-s.x[j];strain=std::max(strain,std::abs(edge.norm()/p.cpu.lengths[j]-1));ss=std::max(ss,(edge/double(p.cpu.lengths[j])-hair::rotate(s.q[j],D{0,0,1})).norm());}
    if(p.set.pinX)require((s.x[0]-p.initial.x[0]).norm()==0,"Pinned position changed");if(p.set.pinQ)require((s.q[0]-p.initial.q[0]).norm()==0,"Pinned orientation changed");
    std::ostringstream o;o.precision(17);o<<"{\"macroSweeps\":"<<macro<<",\"physical\":"<<e.json()<<",\"objectiveGapJ\":"<<e.total()-ref.total()<<",\"positionRMS\":"<<rms(s,p.reference)<<",\"positionMax\":"<<maximum(s,p.reference)<<",\"tipDisplacement\":"<<vectorJSON(s.x.back()-p.preX.back())<<",\"tipError\":"<<vectorJSON(s.x.back()-p.reference.x.back())<<",\"maxQuaternionRadians\":"<<maxAngle<<",\"rmsQuaternionRadians\":"<<std::sqrt(angle2/s.q.size())<<",\"maxAxialStrain\":"<<strain<<",\"maxSSVectorError\":"<<ss<<",\"work\":"<<w.json()<<'}';return o.str();
}
static void verify(const fs::path& inputs,const fs::path& out){
    fs::create_directories(out);std::ostringstream result;result.precision(17);result<<"[";bool first=true;
    for(auto& file:fs::directory_iterator(inputs))if(file.path().extension()==".jwp"){auto p=loadProblem(file.path());for(unsigned width:{2u,4u,8u}){coverage(*p,width);unsigned count=0,crossing=0;double error=jacobianTest(*p,p->initial,width,count,crossing);if(!first)result<<',';first=false;result<<"{\"case\":\""<<file.path().stem().string()<<"\",\"width\":"<<width<<",\"samples\":"<<count<<",\"maxNormalizedJacobianError\":"<<error<<",\"crossingBTTermsChecked\":"<<crossing<<",\"coveragePassed\":true}";}}
    result<<"]";text(out/"jacobian-and-coverage.json",result.str());
    // Nonsmooth-pole safeguard probe only, not a candidate/convergence case.
    auto tied=loadProblem(inputs/"tie-jacobian-only.jwp");
    for(bool ablate:{false,true}){State s=tied->initial;Work work;std::ofstream log(out/(ablate?"tie-ablation.csv":"tie-joint.csv"));log.precision(17);auto schedule=blocks(*tied,2);double before=windowmath::measure(tied->cpu,s,tied->y,tied->set).total();
        for(unsigned j=0;j<schedule.size();j++)visit(*tied,s,schedule[j],ablate,work,log,1,j);
        double after=windowmath::measure(tied->cpu,s,tied->y,tied->set).total();require(after<=before,"Tie safeguard increased actual closest-pole objective");
        std::ostringstream x;x.precision(17);x<<"{\"Fbefore\":"<<before<<",\"Fafter\":"<<after<<",\"notAReferenceQualifiedCase\":true,\"work\":"<<work.json()<<'}';text(out/(ablate?"tie-ablation.json":"tie-joint.json"),x.str());}
}
static void run(const fs::path& input,const fs::path& out,unsigned width,const std::string& method){
    fs::create_directories(out);auto p=loadProblem(input);coverage(*p,width);State state=p->initial;Work w;auto schedule=blocks(*p,width);std::ofstream log(out/"trials.csv");log.precision(17);log<<"macro,block,activeDOFs,lambda,alpha,Fbefore,Ftrial,result,poleChanges\n";
    std::ostringstream rows;rows<<'[';std::set<unsigned> checkpoints{1,2,4,8,16,32};double last=windowmath::measure(p->cpu,state,p->y,p->set).total();
    for(unsigned macro=1;macro<=32;macro++){auto start=std::chrono::steady_clock::now();
        if(method=="sequential"){windowmath::uncachedSweep(p->cpu,state,p->y,p->set);unsigned S=unsigned(state.q.size());w.pSolves+=(2*S+1)-(p->set.pinX?1:0);w.qSolves+=(2*S-1)-(p->set.pinQ?1:0);w.activeDOFs=3*(w.pSolves+w.qSolves);}
        else{for(unsigned j=0;j<schedule.size();j++)visit(*p,state,schedule[j],method=="ablation",w,log,macro,j);}
        w.milliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();double now=windowmath::measure(p->cpu,state,p->y,p->set).total();
        // This is a check, not an acceptance slack; each prototype trial is strict.
        require(now<=last+1e-20*std::max(1.,std::abs(last)),"Macro objective grew");last=now;
        if(checkpoints.count(macro)){if(macro!=1)rows<<',';rows<<metrics(*p,state,w,macro);writeState(out/("state-"+std::to_string(macro)+".json"),state);}
    }rows<<']';std::ostringstream o;o.precision(17);o<<"{\"method\":\""<<method<<"\",\"width\":"<<width<<",\"sourceId\":"<<p->id<<",\"referenceQualified\":"<<(p->qualified?"true":"false")<<",\"referencePositionCorrection\":"<<p->refCorrection<<",\"referenceQStationarity\":"<<p->refStationarity<<",\"referenceRefinementMaxPosition\":"<<p->refinement<<",\"initialObjective\":"<<windowmath::measure(p->cpu,p->initial,p->y,p->set).total()<<",\"screeningRMSMeters\":0.0000116713,\"checkpoints\":"<<rows.str()<<'}';text(out/"curve.json",o.str());
}
int main(int argc,char** argv){try{std::string mode,method="joint";fs::path root=fs::current_path(),out,input;unsigned width=2;for(int j=1;j<argc;j++){std::string a=argv[j];require(j+1<argc,"Missing option value");std::string v=argv[++j];if(a=="--mode")mode=v;else if(a=="--root")root=v;else if(a=="--out")out=v;else if(a=="--input")input=v;else if(a=="--width")width=unsigned(std::stoul(v));else if(a=="--method")method=v;else throw std::runtime_error("Unknown option");}require(!out.empty(),"--out required");
    if(mode=="prepare")prepare(root,out);else if(mode=="verify")verify(input,out);else if(mode=="run"){require(width==2||width==4||width==8,"Allowed widths:2/4/8");require(method=="sequential"||method=="joint"||method=="ablation","Allowed methods");run(input,out,width,method);}else throw std::runtime_error("Unknown mode");std::cout<<"PASS CPU "<<mode<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
