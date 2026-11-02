from pathlib import Path
s=Path('diagnostics/native-ls/NativeLS.cpp').read_text(encoding='utf-8-sig');a=s.index('Result minimize(');b=s.rfind('} // namespace nativels');body=s[a:b].replace('Result minimize(', 'Result hybridMinimize(').replace('w.solve(count, mode)','gpuLinear(w, count)')
prefix='''#include "Hybrid.h"
namespace gpuls {Engine* hybridEngine=nullptr;std::vector<HybridRecord> hybridRecords;}
namespace nativels {
#include "../native-ls/LocalEquations.generated.h"
bool gpuLinear(Workspace& w,unsigned count){require(gpuls::hybridEngine!=nullptr,"No diagnostic GPU backend");gpuls::Pack pack(1);pack.system(0,w);auto& engine=*gpuls::hybridEngine;engine.prepare(pack);auto r=engine.run(count);gpuls::Status status;std::memcpy(&status,r.bytes.data()+pack.systems[0].statusOffset,sizeof(status));gpuls::hybridRecords.push_back({r.total,engine.uploadWall,r.wall,w.n,count,status.code});if(status.code||status.done!=1||status.corrections!=count)return false;w.timing.factors++;w.timing.solves+=count+1;w.timing.refinements+=count;std::memcpy(w.u.data(),r.bytes.data()+pack.systems[0].outputOffset,w.n*8);for(unsigned j=0;j<w.n;j++)w.delta[j]=w.u[j]/w.scale[j];return true;}
'''
Path('diagnostics/gpu-structured/Hybrid.generated.cpp').write_text(prefix+body+'\n}\n',encoding='utf-8')
# Reuse only the existing native CPU dynamics wrapper, not its long-run driver.
s=Path('diagnostics/native-ls/Bridge.cpp').read_text(encoding='utf-8-sig');a=s.index('struct DynamicPoint');b=s.index('int dynamics(');body=s[a:b]
body=body.replace('allocationCount = 0;','').replace('measureAllocations = true;','').replace('measureAllocations = false;','').replace('point.allocations = allocationCount;','point.allocations = 0;')
body=body.replace('nativels::minimize(', 'nativels::hybridMinimize(')
body=body.replace('<< ",\\\"allocations\\\":" << p.allocations','<< ",\\\"allocationCountingPerformed\\\":false"')
header='''#include "Hybrid.h"
#include "../joint/Problem.h"
#include <iomanip>
#include <cstring>
namespace fs=std::filesystem;using hair::D;using hair::require;using State=windowmath::State;
std::string vec(D v){std::ostringstream o;o<<std::setprecision(17)<<'['<<v.x<<','<<v.y<<','<<v.z<<']';return o.str();}
void write(const fs::path&p,const std::string&s){gpuls::text(p,s);}
'''
Path('diagnostics/gpu-structured/HybridBridge.generated.cpp').write_text(header+body+'\n#include "DynamicsEntry.inl"\n',encoding='utf-8')
