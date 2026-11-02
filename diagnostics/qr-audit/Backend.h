#pragma once
#include "../joint/Problem.h"
#include "../../src/AutonomousReference.cpp"
#include <array>
#include <chrono>
#include <iostream>
namespace strandblock {
inline std::vector<double> frozenRefU,frozenBlockU,frozenQRU,frozenM,frozenC,frozenA,frozenB,frozenD;
inline std::string fixtureName; 
using namespace jointcpu;
using autonomous::Layout;using autonomous::ReferenceResult;using autonomous::evaluate;using autonomous::apply;using autonomous::axis;
using Clock=std::chrono::steady_clock;
inline double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
struct Work {
    uint64_t assemblies=0,factors=0,rows=0,pairs=0,storedPairs=0,factorMultiplySubtracts=0,pivots=0,factorFailures=0;
    size_t peakMatrixBytes=0,peakLinearPayloadBytes=0;
    double assemblyMs=0,factorMs=0;
};
struct Trial {unsigned phase,iteration,retry,line;double lambda,before,after;bool accepted;};
struct Point {unsigned phase,iteration,total;double lambda;ReferenceResult result;State state;};
struct Trace {
    bool enabled=true,factorFailure=false,noAcceptedTrial=false;unsigned phase=0,accepted=0;Work work;
    uint64_t trials=0,rejected=0;std::vector<Trial> trialLog;std::vector<Point> points;
    void observe(const State& s,const ReferenceResult& r,unsigned i,double lambda){if(enabled)points.push_back({phase,i,accepted,lambda,r,s});}
    void trial(unsigned i,unsigned retry,unsigned line,double lambda,double before,double after,bool ok){trials++;rejected+=!ok;if(enabled)trialLog.push_back({phase,i,retry,line,lambda,before,after,ok});}
};
struct Spec {
    unsigned n=0;std::vector<unsigned> start,size,station,slot;
    Spec(const GuideCpuState& cpu,const Layout& l):n(l.size),station(n),slot(n){
        require(cpu.ranges.size()==1,"One strand per diagnostic system");
        for(unsigned v=0;v<cpu.masses.size();v++){start.push_back(v?start.back()+size.back():0);unsigned count=0;
            for(int k:{l.x[v],v<l.q.size()?l.q[v]:-1})if(k>=0){require(unsigned(k)==start.back()+count,"Reference station ordering changed");for(unsigned a=0;a<3;a++){station[unsigned(k)+a]=v;slot[unsigned(k)+a]=count+a;}count+=3;}
            require(count<=6,"Station larger than six");size.push_back(count);
        }require(start.back()+size.back()==n,"Layout size mismatch");require(size.back()==3,"Terminal position-only block required");
    }
};
using Mat=std::array<double,36>;
inline double& at(Mat& a,unsigned r,unsigned c){return a[r*6+c];}
inline double at(const Mat& a,unsigned r,unsigned c){return a[r*6+c];}
struct Bands {
    Spec spec;unsigned n;double energy=0;std::vector<double> gradient;std::vector<Mat> diagonal,lower;
    uint64_t rows=0,pairs=0,storedPairs=0;unsigned maxRowStationSpan=0;
    explicit Bands(Spec s):spec(std::move(s)),n(spec.n),gradient(n),diagonal(spec.size.size()),lower(spec.size.size()){}
    void row(double r,const std::vector<std::pair<unsigned,double>>& j){
        energy+=.5*r*r;rows++;pairs+=j.size()*j.size();unsigned lo=unsigned(spec.size.size()),hi=0;bool any=false;
        for(auto [a,ja]:j)if(ja!=0){any=true;lo=std::min(lo,spec.station[a]);hi=std::max(hi,spec.station[a]);}
        if(any){maxRowStationSpan=std::max(maxRowStationSpan,hi-lo);require(hi-lo<=1,"Actual Jacobian row spans nonadjacent stations");}
        for(auto [a,ja]:j){gradient[a]+=ja*r;for(auto [b,jb]:j){unsigned sa=spec.station[a],sb=spec.station[b];double value=ja*jb;
            if(sa==sb){at(diagonal[sa],spec.slot[a],spec.slot[b])+=value;storedPairs++;}
            else if(sa==sb+1){at(lower[sa],spec.slot[a],spec.slot[b])+=value;storedPairs++;}
            else if(sb!=sa+1)require(value==0,"Off-band contribution must not be discarded");
        }}
    }
    double get(unsigned a,unsigned b)const{unsigned sa=spec.station[a],sb=spec.station[b];if(sa==sb)return at(diagonal[sa],spec.slot[a],spec.slot[b]);if(sa==sb+1)return at(lower[sa],spec.slot[a],spec.slot[b]);if(sb==sa+1)return at(lower[sb],spec.slot[b],spec.slot[a]);return 0;}
    size_t matrixBytes()const{return 2*spec.size.size()*sizeof(Mat);}
};
#include "Equations.generated.h"
inline std::vector<double> scales(const autonomous::System& s){std::vector<double> a(s.n);for(unsigned i=0;i<s.n;i++)a[i]=std::sqrt(std::max(s.hessian[size_t(i)*s.n+i],1e-30));return a;}
inline std::vector<double> scales(const Bands& s){std::vector<double> a(s.n);for(unsigned i=0;i<s.n;i++)a[i]=std::sqrt(std::max(s.get(i,i),1e-30));return a;}

// Exact original scalar-band-12 factor/back-substitution, including dense L
// allocation. Failed pivots are returned explicitly instead of an exception.
inline bool referenceSolve(const autonomous::System& system,double lambda,std::vector<double>& step,Work& work){
    auto time=Clock::now();work.factors++;unsigned n=system.n;constexpr unsigned band=12;
    std::vector<double> a(size_t(n)*n),rhs(n),scale(n);step.assign(n,0);
    work.peakMatrixBytes=std::max(work.peakMatrixBytes,2*size_t(n)*n*sizeof(double));work.peakLinearPayloadBytes=std::max(work.peakLinearPayloadBytes,(2*size_t(n)*n+4*n)*sizeof(double));
    for(unsigned i=0;i<n;i++){scale[i]=std::sqrt(std::max(system.hessian[size_t(i)*n+i],1e-30));rhs[i]=-system.gradient[i]/scale[i];}
    frozenM.assign(size_t(n)*n,0);frozenC=rhs;frozenD=scale;for(unsigned i=0;i<n;i++)for(unsigned j=i>band?i-band:0;j<=i;j++){double value=system.hessian[size_t(i)*n+j]/(scale[i]*scale[j])+(i==j?lambda:0);frozenM[size_t(i)*n+j]=value;frozenM[size_t(j)*n+i]=value;unsigned first=std::max(i>band?i-band:0,j>band?j-band:0);work.factorMultiplySubtracts+=j-first;for(unsigned k=first;k<j;k++)value-=a[size_t(i)*n+k]*a[size_t(j)*n+k];if(i==j){work.pivots++;if(!(value>0&&std::isfinite(value))){work.factorFailures++;work.factorMs+=ms(time);return false;}a[size_t(i)*n+i]=std::sqrt(value);}else a[size_t(i)*n+j]=value/a[size_t(j)*n+j];}
    for(unsigned i=0;i<n;i++){unsigned first=i>band?i-band:0;work.factorMultiplySubtracts+=i-first;for(unsigned j=first;j<i;j++)rhs[i]-=a[size_t(i)*n+j]*rhs[j];rhs[i]/=a[size_t(i)*n+i];}
    for(unsigned i=n;i-->0;){double value=rhs[i];work.factorMultiplySubtracts+=std::min(n,i+band+1)-i-1;for(unsigned j=i+1;j<std::min(n,i+band+1);j++)value-=a[size_t(j)*n+i]*step[j];step[i]=value/a[size_t(i)*n+i];}
    frozenRefU=step;for(unsigned i=0;i<n;i++)step[i]/=scale[i];work.factorMs+=ms(time);return true;
}
// Block Cholesky: C_i = E_i L_(i-1)^(-T), D'_i=D_i-C_i C_i^T.
// Every inverse-looking expression is implemented as triangular solves.
inline bool blockSolve(const Bands& s,double lambda,std::vector<double>& step,Work& work){
    auto time=Clock::now();work.factors++;auto scale=scales(s);auto count=s.spec.size.size();std::vector<Mat> L(count),C(count);std::vector<double> y(s.n);step.assign(s.n,0);
    work.peakMatrixBytes=std::max(work.peakMatrixBytes,4*count*sizeof(Mat));size_t maps=(2*count+2*s.n)*sizeof(unsigned);
    work.peakLinearPayloadBytes=std::max(work.peakLinearPayloadBytes,4*count*sizeof(Mat)+4*s.n*sizeof(double)+maps);
    for(unsigned i=0;i<count;i++){unsigned ni=s.spec.size[i],si=s.spec.start[i],np=i?s.spec.size[i-1]:0,sp=i?s.spec.start[i-1]:0;
        for(unsigned a=0;a<ni;a++)for(unsigned k=0;k<np;k++){double v=at(s.lower[i],a,k)/(scale[si+a]*scale[sp+k]);work.factorMultiplySubtracts+=k;for(unsigned j=0;j<k;j++)v-=at(L[i-1],k,j)*at(C[i],a,j);at(C[i],a,k)=v/at(L[i-1],k,k);}
        for(unsigned a=0;a<ni;a++)for(unsigned b=0;b<=a;b++){double v=at(s.diagonal[i],a,b)/(scale[si+a]*scale[si+b])+(a==b?lambda:0);work.factorMultiplySubtracts+=np+b;for(unsigned k=0;k<np;k++)v-=at(C[i],a,k)*at(C[i],b,k);for(unsigned k=0;k<b;k++)v-=at(L[i],a,k)*at(L[i],b,k);
            if(a==b){work.pivots++;if(!(v>0&&std::isfinite(v))){work.factorFailures++;work.factorMs+=ms(time);return false;}at(L[i],a,b)=std::sqrt(v);}else at(L[i],a,b)=v/at(L[i],b,b);}
        for(unsigned a=0;a<ni;a++){double v=-s.gradient[si+a]/scale[si+a];work.factorMultiplySubtracts+=np+a;for(unsigned k=0;k<np;k++)v-=at(C[i],a,k)*y[sp+k];for(unsigned k=0;k<a;k++)v-=at(L[i],a,k)*y[si+k];y[si+a]=v/at(L[i],a,a);}
    }
    for(unsigned i=unsigned(count);i-->0;){unsigned ni=s.spec.size[i],si=s.spec.start[i],nn=i+1<count?s.spec.size[i+1]:0,sn=i+1<count?s.spec.start[i+1]:0;
        for(unsigned a=ni;a-->0;){double v=y[si+a];work.factorMultiplySubtracts+=nn+ni-a-1;for(unsigned k=0;k<nn;k++)v-=at(C[i+1],k,a)*step[sn+k];for(unsigned k=a+1;k<ni;k++)v-=at(L[i],k,a)*step[si+k];step[si+a]=v/at(L[i],a,a);}}
    frozenBlockU=step;for(unsigned j=0;j<s.n;j++)step[j]/=scale[j];work.factorMs+=ms(time);return true;
}
struct ReferenceBackend {
    static autonomous::System assemble(const GuideCpuState& cpu,const State& s,const std::vector<D>& y,const windowmath::Settings& set,const Layout& l,Work& w){auto t=Clock::now();auto system=evaluate(cpu,s,y,set,l,true);w.assemblies++;w.rows+=3*cpu.masses.size()+3*cpu.lengths.size()+4*cpu.bends.size();w.assemblyMs+=ms(t);return system;}
    static ReferenceResult diagnose(const GuideCpuState& cpu,const State& s,const windowmath::Settings& set,const Layout& l,const autonomous::System& sys){return autonomous::diagnose(cpu,s,set,l,sys);}
    static bool solve(const autonomous::System& s,double lambda,std::vector<double>& step,Work& w){return referenceSolve(s,lambda,step,w);}
};
struct BlockBackend {
    static Bands assemble(const GuideCpuState& cpu,const State& s,const std::vector<D>& y,const windowmath::Settings& set,const Layout& l,Work& w){auto t=Clock::now();Bands sys(Spec(cpu,l));emitPhysics(sys,cpu,s,y,set,l,true);w.assemblies++;w.rows+=sys.rows;w.pairs+=sys.pairs;w.storedPairs+=sys.storedPairs;w.assemblyMs+=ms(t);return sys;}
    static ReferenceResult diagnose(const GuideCpuState& cpu,const State& s,const windowmath::Settings& set,const Layout& l,const Bands& sys){return diagnoseBands(cpu,s,set,l,sys);}
    static bool solve(const Bands& s,double lambda,std::vector<double>& step,Work& w){return blockSolve(s,lambda,step,w);}
};
#include "Nonlinear.generated.h"
inline double rms(const State& a,const State& b){double d=0;for(unsigned i=0;i<a.x.size();i++){auto v=a.x[i]-b.x[i];d+=v.dot(v);}return std::sqrt(d/a.x.size());}
inline double maxPosition(const State& a,const State& b){double d=0;for(unsigned i=0;i<a.x.size();i++)d=std::max(d,(a.x[i]-b.x[i]).norm());return d;}
inline double maxAngle(const State& a,const State& b){double d=0;for(unsigned i=0;i<a.q.size();i++)d=std::max(d,hair::angle(a.q[i],b.q[i]));return d;}
} // namespace strandblock
