#include "../strand-block/Verify.h"
#include <io.h>
#include <fcntl.h>
#include <cstdio>
#include <set>
using namespace strandblock;
template<class T>void send(const T* p,size_t n=1){require(fwrite(p,sizeof(T),n,stdout)==n,"Protocol write");}
template<class T>void recv(T* p,size_t n=1){require(fread(p,sizeof(T),n,stdin)==n,"Protocol read");}
struct LiveSystem: Bands {Rows rows;LiveSystem(const GuideCpuState& c,const State& s,const std::vector<D>& y,const windowmath::Settings& settings,const Layout& l):Bands(Spec(c,l)),rows(l.size){emitPhysics(*this,c,s,y,settings,l,true);emitPhysics(rows,c,s,y,settings,l,true);}};
struct RefinedBackend {
 static LiveSystem assemble(const GuideCpuState& c,const State& s,const std::vector<D>& y,const windowmath::Settings& settings,const Layout& l,Work& w){auto t=Clock::now();LiveSystem out(c,s,y,settings,l);w.assemblies++;w.assemblyMs+=ms(t);return out;}
 static ReferenceResult diagnose(const GuideCpuState& c,const State& s,const windowmath::Settings& settings,const Layout& l,const LiveSystem& sys){return diagnoseBands(c,s,settings,l,sys);}
 static bool solve(const LiveSystem& sys,double lambda,std::vector<double>& delta,Work& w){
  // Original scaled/regularized normal preconditioner and matching binary64
  // augmented LS. The Python worker returns a binary64 refined u, not an oracle.
  auto d=scales(sys);unsigned n=sys.n;std::vector<double>m(size_t(n)*n),c(n),a;auto count=unsigned(sys.spec.size.size());
  for(unsigned i=0;i<n;i++){c[i]=-sys.gradient[i]/d[i];for(unsigned j=0;j<n;j++)m[size_t(i)*n+j]=sys.get(i,j)/(d[i]*d[j])+(i==j?lambda:0);}
  unsigned rows=unsigned(sys.rows.r.size())+n;a.resize(size_t(rows)*n);std::vector<double>b(rows);
  for(unsigned i=0;i<sys.rows.r.size();i++){b[i]=-sys.rows.r[i];for(unsigned j=0;j<n;j++)a[size_t(i)*n+j]=sys.rows.j[i][j]/d[j];}
  for(unsigned j=0;j<n;j++)a[size_t(sys.rows.r.size()+j)*n+j]=std::sqrt(lambda);
  uint32_t cmd=1;send(&cmd);send(&n);send(&count);send(sys.spec.size.data(),count);send(&rows);send(m.data(),m.size());send(c.data(),n);send(d.data(),n);send(a.data(),a.size());send(b.data(),rows);fflush(stdout);
  uint32_t valid;recv(&valid);if(!valid)return false;delta.resize(n);recv(delta.data(),n);w.factors++;return true;
 }
};
int main(int argc,char**argv){_setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);try{require(argc==3,"input-directory output-directory");fs::path input=argv[1],out=argv[2];fs::create_directories(out);std::vector<fs::path>files;for(auto f:fs::directory_iterator(input))if(f.path().extension()==".jwp")files.push_back(f.path());std::sort(files.begin(),files.end());
 for(auto file:files){auto p=loadProblem(file);State state=p->initial;Trace trace;auto name=file.stem().string();auto path=out/name;fs::create_directories(path);bool qualified=true;unsigned phase=0;std::vector<std::pair<double,double>>tol=name=="primary"?std::vector<std::pair<double,double>>{{1e-11,1e-9},{1e-12,1e-10},{1e-14,1e-13}}:std::vector<std::pair<double,double>>{{1e-11,1e-9},{1e-13,1e-11}};
  auto begin=Clock::now();for(auto [pt,qt]:tol){trace.phase=phase++;auto r=minimizeTraced<RefinedBackend>(p->cpu,state,p->dy,p->set,pt,qt,trace);qualified&=r.qualified;if(!r.qualified)break;}double wall=ms(begin);writeState(path/"final.json",state);
  auto metrics=windowmath::measure(p->cpu,state,p->y,p->set);std::ostringstream o;o.precision(17);o<<"{\"qualified\":"<<(qualified?"true":"false")<<",\"accepted\":"<<trace.accepted<<",\"trials\":"<<trace.trials<<",\"rejected\":"<<trace.rejected<<",\"factorFailure\":"<<(trace.factorFailure?"true":"false")<<",\"wallMsIncludingIPC\":"<<wall<<",\"F\":"<<metrics.total()<<",\"positionCorrection\":"<<metrics.correction<<",\"qStationarity\":"<<metrics.stationarity<<",\"spectral\":"<<metrics.spectral<<",\"tipDisplacement\":"<<vectorJSON(state.x.back()-p->preX.back())<<",\"trace\":[";
  bool first=true;unsigned k=0;for(auto& point:trace.points){if(!first)o<<',';first=false;auto e=windowmath::measure(p->cpu,point.state,p->y,p->set);o<<"{\"phase\":"<<point.phase<<",\"accepted\":"<<point.total<<",\"lambda\":"<<point.lambda<<",\"F\":"<<e.total()<<",\"positionCorrection\":"<<e.correction<<",\"qStationarity\":"<<e.stationarity<<'}';writeState(path/("point-"+std::to_string(k++)+".json"),point.state);}o<<"]}";text(path/"result.json",o.str());std::ofstream f(path/"trials.csv");f.precision(17);f<<"phase,iteration,retry,line,lambda,before,after,accepted\n";for(auto t:trace.trialLog)f<<t.phase<<','<<t.iteration<<','<<t.retry<<','<<t.line<<','<<t.lambda<<','<<t.before<<','<<t.after<<','<<t.accepted<<'\n';
 }uint32_t done=0;send(&done);fflush(stdout);return 0;}catch(const std::exception&e){fprintf(stderr,"%s\n",e.what());return 2;}}
