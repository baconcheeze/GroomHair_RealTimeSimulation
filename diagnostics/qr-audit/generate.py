from pathlib import Path
import json,hashlib,datetime
old=Path('diagnostics/strand-block');new=Path('diagnostics/qr-audit')
for name in ['Backend.h','Verify.h','Equations.generated.h','Nonlinear.generated.h','StrandBlockExperiment.cpp']:
 s=(old/name).read_text(encoding='utf-8-sig')
 if name=='Backend.h':
  s=s.replace('namespace strandblock {','namespace strandblock {\ninline std::vector<double> frozenRefU,frozenBlockU,frozenQRU,frozenM,frozenC,frozenA,frozenB,frozenD;\ninline std::string fixtureName; ',1)
  s=s.replace('for(unsigned i=0;i<n;i++)step[i]/=scale[i];work.factorMs','frozenRefU=step;for(unsigned i=0;i<n;i++)step[i]/=scale[i];work.factorMs')
  s=s.replace('for(unsigned j=0;j<s.n;j++)step[j]/=scale[j];work.factorMs','frozenBlockU=step;for(unsigned j=0;j<s.n;j++)step[j]/=scale[j];work.factorMs')
  s=s.replace('for(unsigned i=0;i<n;i++)for(unsigned j=i>band?', 'frozenM.assign(size_t(n)*n,0);frozenC=rhs;frozenD=scale;for(unsigned i=0;i<n;i++)for(unsigned j=i>band?',1)
  s=s.replace('unsigned first=std::max(i>band?', 'frozenM[size_t(i)*n+j]=value;frozenM[size_t(j)*n+i]=value;unsigned first=std::max(i>band?',1)
 if name=='Verify.h':
  s=s.replace('for(unsigned k=0;k<n;k++){double norm=0;', 'frozenA=a;frozenB=b;for(unsigned k=0;k<n;k++){double norm=0;',1)
  s=s.replace('for(unsigned j=0;j<n;j++)step[j]/=scale[j];return true;','frozenQRU=step;for(unsigned j=0;j<n;j++)step[j]/=scale[j];return true;')
  s=s.replace('inline LinearRecord verifyLinear(', '#include "Freeze.h"\ninline LinearRecord verifyLinear(')
  s=s.replace('    return r;\n}', '    freeze(p,state,l,dense,rows,scale,lambda,da,db,dq,r);\n    return r;\n}')
 if name=='StrandBlockExperiment.cpp':s=s.replace('auto p=loadProblem(file);bool primary', 'fixtureName=name;auto p=loadProblem(file);bool primary')
 (new/('QRFreeze.cpp' if name=='StrandBlockExperiment.cpp' else name)).write_text(s,encoding='utf-8')
(new/'CMakeLists.txt').write_text((old/'CMakeLists.txt').read_text().replace('HairLabStrandBlockCPU','HairLabQRAuditCPU').replace('StrandBlockExperiment','QRFreeze'))
print('Diagnostic copy only; original experiment unchanged')


with (new/"CMakeLists.txt").open("a") as f:
 f.write("\nadd_executable(QRMetricProbe MetricProbe.cpp)\ntarget_compile_features(QRMetricProbe PRIVATE cxx_std_20)\ntarget_compile_options(QRMetricProbe PRIVATE /W4 /fp:precise)\nset_target_properties(QRMetricProbe PROPERTIES RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/bin\")\n")
