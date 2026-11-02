// Same nonlinear policy; only backend calls and observation hooks adapted.
template<class Backend> ReferenceResult minimizeTraced(const GuideCpuState& cpu,windowmath::State& state,const std::vector<D>& y,const windowmath::Settings& set,double ptol,double qtol,Trace& trace){Layout layout(cpu,set);for(unsigned s=0;s<layout.q.size();s++)if(layout.q[s]>=0)state.q[s]=state.q[s].unit();double lambda=1e-8;ReferenceResult result;
    for(unsigned iteration=0;iteration<1500;iteration++){auto system=Backend::assemble(cpu,state,y,set,layout,trace.work);result=Backend::diagnose(cpu,state,set,layout,system);result.iterations=iteration;trace.observe(state,result,iteration,lambda);require(std::isfinite(result.objective),"Double reference nonfinite objective");if(result.positionCorrection<ptol&&result.stationarity<qtol&&result.spectral<qtol){result.qualified=true;return result;}
        bool accepted=false;
        for(unsigned retry=0;retry<16;retry++){
            std::vector<double> step;
            if(!Backend::solve(system,lambda,step,trace.work)){trace.factorFailure=true;return result;}
            for(unsigned line=0;line<16;line++){auto candidate=apply(state,layout,step,std::ldexp(1.,-int(line)));double e=evaluate(cpu,candidate,y,set,layout,false).energy;trace.trial(iteration,retry,line,lambda,system.energy,e,std::isfinite(e)&&e<system.energy);if(std::isfinite(e)&&e<system.energy){state=std::move(candidate);accepted=true;trace.accepted++;break;}}
            if(accepted){lambda=std::max(1e-12,lambda*.2);break;}lambda=std::min(1e12,lambda*10);
        }if(!accepted){trace.noAcceptedTrial=true;return result;}
    }return result;
}
