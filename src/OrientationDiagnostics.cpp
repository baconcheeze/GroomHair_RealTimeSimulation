#include "OrientationDiagnostics.h"
#include "OrientationFixtures.h"
#include <iomanip>
#include <sstream>
#include <cstring>
OrientationSnapshot readOrientation(guidegpu::State& state,OrientationSolver& solver,ID3D12Device* device,const Submit& submit){
    OrientationSnapshot s;Microsoft::WRL::ComPtr<ID3D12Resource> q,e,stats;submit([&](auto* list){q=state.orientation.readback(device,list);e=solver.errors.readback(device,list);stats=solver.stats.readback(device,list);});s.q.resize(state.cpu->orientations.size());s.errors.resize(state.cpu->ranges.size());s.stats.resize(s.errors.size());
    auto copy=[](auto& values,ID3D12Resource* r){auto raw=guidegpu::mappedBytes(r,values.size()*sizeof(values[0]));if(!raw.empty())std::memcpy(values.data(),raw.data(),raw.size());};copy(s.q,q.Get());copy(s.errors,e.Get());copy(s.stats,stats.Get());return s;
}
OrientationDiagnostic measureOrientation(const GuideCpuState& cpu,const OrientationSolver& solver,const OrientationSnapshot& snapshot,const std::vector<hair::F>* before){
    using namespace hair;OrientationDiagnostic d;if(before)d.maxBatch=0;if(solver.hasRun)d.maxLast=0;const auto& q=snapshot.q;unsigned participating=solver.participating();
    for(size_t i=0;i<snapshot.errors.size();++i)require(snapshot.errors[i]==0,"Sticky orientation GPU failure strand="+std::to_string(i)+" code="+std::to_string(snapshot.errors[i]));
    for(unsigned strand=0;strand<cpu.ranges.size();++strand){auto r=cpu.ranges[strand];if(strand<participating&&solver.hasRun){auto c=snapshot.stats[strand];d.ordinary+=c.regular;d.fallback+=c.fallback;d.hard+=c.hard;d.rejected+=c.rejected;d.failed+=c.failures;d.maxLast=std::max(d.maxLast,4*std::asin(std::min(1.,double(c.lastSweep)*.5)));}
        for(unsigned j=0;j<r.segmentCount;++j){unsigned s=r.firstSegment+j,vtx=r.firstVertex+j;D current(q[s]);double n=current.norm();require(std::isfinite(n)&&n>1e-20,"Invalid current orientation");d.norm=std::max(d.norm,std::abs(n-1));if(strand>=participating){require(std::memcmp(&q[s],&cpu.orientations[s],16)==0,"Inactive strand changed");continue;}
            if(before)d.maxBatch=std::max(d.maxBatch,angle(D((*before)[s]),current));bool pinned=j==0&&solver.settings.pinRoot;if(pinned){d.root=std::max(d.root,angle(D(cpu.orientations[s]),current));require(std::memcmp(&q[s],&cpu.orientations[s],16)==0,"Pinned root orientation changed");}
            auto a=cpu.source->positions[vtx],z=cpu.source->positions[vtx+1];D edge{double(z.x)-a.x,double(z.y)-a.y,double(z.z)-a.z};double length=cpu.lengths[s],kss=solver.settings.baseSS*length;D css=edge/length-rotate(current,D{0,0,1});d.ss+=.5*kss*css.dot(css);
            if(j+1<r.segmentCount){D rel=current.conj()*D(q[s+1]),rest(cpu.bends[r.firstBend+j]);D c=rel-(rel.dot(rest)>=0?1.:-1.)*rest;d.bt+=.5*(4*solver.settings.baseBT/length)*c.dot(c);}
            if(pinned)continue;
            D coeff=(-2*double(solver.settings.baseSS))*edge,b;
            auto neighbor=[&](D nbor,D rest,double weight,bool right){D rel=right?current.unit().conj()*nbor:nbor.conj()*current.unit();double pole=rel.dot(rest);if(std::abs(pole)<=1e-6)++d.ties;D target=right?nbor*rest.conj():nbor*rest;b=b+(weight*(pole>=0?1.:-1.))*target;};
            if(j)neighbor(D(q[s-1]),D(cpu.bends[r.firstBend+j-1]),4*solver.settings.baseBT/cpu.lengths[s-1],false);
            if(j+1<r.segmentCount)neighbor(D(q[s+1]),D(cpu.bends[r.firstBend+j]),4*solver.settings.baseBT/length,true);
            double violation,res=residual(coeff,b,current,violation),scale=coeff.norm()+b.norm();d.stationarity=std::max(d.stationarity,res);d.spectral=std::max(d.spectral,violation);if(scale){d.normalizedStationarity=std::max(d.normalizedStationarity,res/scale);d.normalizedSpectral=std::max(d.normalizedSpectral,violation/scale);}
        }
    }require(d.norm<=2e-5&&d.root<=1e-6&&d.failed==0,"Orientation safety diagnostic failed");return d;
}
std::string OrientationDiagnostic::json() const {std::ostringstream s;s<<std::setprecision(12)<<"{\"E_ss\":"<<ss<<",\"E_bt\":"<<bt<<",\"E_total\":"<<energy()<<",\"MaxBatchUpdate\":";if(std::isnan(maxBatch))s<<"null";else s<<maxBatch;s<<",\"MaxLastSweepUpdate\":";if(std::isnan(maxLast))s<<"null";else s<<maxLast;
    s<<",\"stationarity\":"<<stationarity<<",\"normalizedStationarity\":"<<normalizedStationarity<<",\"spectralViolation\":"<<spectral<<",\"normalizedSpectralViolation\":"<<normalizedSpectral<<",\"nearPoleTies\":"<<ties<<",\"ordinary\":"<<ordinary<<",\"fallback\":"<<fallback<<",\"hard\":"<<hard<<",\"rejected\":"<<rejected<<",\"failed\":"<<failed<<",\"maxNormError\":"<<norm<<",\"rootError\":"<<root<<",\"positionError\":0,\"velocity\":0}";return s.str();}
