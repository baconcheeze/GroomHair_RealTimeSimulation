"""Mechanical adapters for unchanged reference equations/nonlinear policy."""
from pathlib import Path
import hashlib,json
base=Path(__file__).resolve().parent;source=base.parents[1]/'src/AutonomousReference.cpp'
s=source.read_text(encoding='utf-8')
def function(name):
    start=s.index(name);brace=s.index('{',start);depth=1;end=brace+1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:brace],s[brace:end]
sig,emit=function('System evaluate(')
emit=emit.replace('System system(layout.size,jac);','').replace('return system;','')
sig=sig.replace('System evaluate(','template<class Acc> void emitPhysics(Acc& system,')
dsig,diag=function('ReferenceResult diagnose(')
dsig=dsig.replace('ReferenceResult diagnose(','template<class Acc> ReferenceResult diagnoseBands(').replace('const System& sys','const Acc& sys')
msig,loop=function('ReferenceResult minimize(')
msig=msig.replace('ReferenceResult minimize(','template<class Backend> ReferenceResult minimizeTraced(').replace('double qtol)','double qtol,Trace& trace)')
loop=loop.replace('auto system=evaluate(cpu,state,y,set,layout,true);','auto system=Backend::assemble(cpu,state,y,set,layout,trace.work);')
loop=loop.replace('result=diagnose(cpu,state,set,layout,system);','result=Backend::diagnose(cpu,state,set,layout,system);')
loop=loop.replace('result.iterations=iteration;','result.iterations=iteration;trace.observe(state,result,iteration,lambda);')
a=loop.index('unsigned n=layout.size;constexpr unsigned band=12;')
b=loop.index('for(unsigned line=0;line<16;line++)',a)
loop=loop[:a]+'''bool accepted=false;
        for(unsigned retry=0;retry<16;retry++){
            std::vector<double> step;
            if(!Backend::solve(system,lambda,step,trace.work)){trace.factorFailure=true;return result;}
            '''+loop[b:]
loop=loop.replace('if(std::isfinite(e)&&e<system.energy){','trace.trial(iteration,retry,line,lambda,system.energy,e,std::isfinite(e)&&e<system.energy);if(std::isfinite(e)&&e<system.energy){')
loop=loop.replace('accepted=true;break;','accepted=true;trace.accepted++;break;')
loop=loop.replace('if(!accepted)return result;','if(!accepted){trace.noAcceptedTrial=true;return result;}')
(base/'Equations.generated.h').write_text('// Generated from src/AutonomousReference.cpp. Do not hand edit.\n'+sig+emit+'\n'+dsig+diag+'\n',encoding='utf-8')
(base/'Nonlinear.generated.h').write_text('// Same nonlinear policy; only backend calls and observation hooks adapted.\n'+msig+loop+'\n',encoding='utf-8')
(base/'generation.json').write_text(json.dumps({'referenceSHA256':hashlib.sha256(source.read_bytes()).hexdigest(),'equations':'exact evaluate/diagnose bodies; accumulator/type interface only','nonlinear':'exact normalize/lambda/retry/line/convergence/cap policy; linear section delegated; failed factorization reported instead of throwing; observations added'},indent=2))
