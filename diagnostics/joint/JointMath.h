#pragma once
#include "Problem.h"
#include <chrono>
#include <set>
// Reuse, without editing, the existing verified manifold reference evaluator,
// retraction, residual diagnostics and small-problem reference implementation.
#include "../../src/AutonomousReference.cpp"
namespace jointcpu {
struct Block {
    autonomous::Layout l;unsigned positionDOFs=0;
    Block(const Problem& p,const std::vector<unsigned>& x,const std::vector<unsigned>& q):l(p.cpu,p.set){
        std::fill(l.x.begin(),l.x.end(),-1);std::fill(l.q.begin(),l.q.end(),-1);l.size=0;
        for(unsigned v:x)if(!(v==0&&p.set.pinX)){l.x[v]=int(l.size);l.size+=3;}
        positionDOFs=l.size;for(unsigned s:q)if(!(s==0&&p.set.pinQ)){l.q[s]=int(l.size);l.size+=3;}
    }
};
inline std::vector<Block> blocks(const Problem& p,unsigned width){
    unsigned S=unsigned(p.cpu.lengths.size());std::vector<Block> b;
    if(S==1){b.emplace_back(p,std::vector<unsigned>{0,1},std::vector<unsigned>{0});return b;}
    b.emplace_back(p,std::vector<unsigned>{0},std::vector<unsigned>{});
    unsigned w=std::min(width,S);for(unsigned i=0;i+w<=S;i++){std::vector<unsigned>x,q;for(unsigned j=i+1;j<i+w;j++)x.push_back(j);for(unsigned j=i;j<i+w;j++)q.push_back(j);b.emplace_back(p,x,q);}
    b.emplace_back(p,std::vector<unsigned>{S},std::vector<unsigned>{S-1});return b;
}
inline void coverage(const Problem& p,unsigned width){
    auto b=blocks(p,width);for(unsigned j=0;j<p.initial.x.size();j++)if(!(j==0&&p.set.pinX)){bool found=false;for(auto& a:b)found|=a.l.x[j]>=0;require(found,"Unvisited free x");}
    for(unsigned j=0;j<p.initial.q.size();j++)if(!(j==0&&p.set.pinQ)){bool found=false;for(auto& a:b)found|=a.l.q[j]>=0;require(found,"Unvisited free q");}
}
inline std::vector<int> poles(const Problem& p,const State& s){std::vector<int> v;for(unsigned j=0;j<p.cpu.bends.size();j++)v.push_back((s.q[j].conj()*s.q[j+1]).dot(D(p.cpu.bends[j]))>=0?1:-1);return v;}
struct Rows {
    unsigned n;std::vector<double> r;std::vector<std::vector<double>> j;unsigned ties=0,crossingBT=0;
    explicit Rows(unsigned count):n(count){}
    void add(double residual,const std::vector<double>& jac){r.push_back(residual);j.push_back(jac);}
};
inline Rows rows(const Problem& p,const State& s,const Block& b,const std::vector<int>& phi){
    Rows out(b.l.size);auto zero=[&](){return std::vector<double>(b.l.size);};
    for(unsigned v=0;v<s.x.size();v++)if(b.l.x[v]>=0){double w=std::sqrt(double(p.cpu.masses[v]))/p.set.h;D r=w*(s.x[v]-p.dy[v]);for(unsigned a=0;a<3;a++){auto j=zero();j[unsigned(b.l.x[v])+a]=w;out.add(r[int(a)],j);}}
    for(unsigned seg=0;seg<s.q.size();seg++)if(b.l.x[seg]>=0||b.l.x[seg+1]>=0||b.l.q[seg]>=0){
        double l=p.cpu.lengths[seg],w=std::sqrt(double(p.set.ss)*l);D d=hair::rotate(s.q[seg],D{0,0,1}),r=w*((s.x[seg+1]-s.x[seg])/l-d);
        for(unsigned a=0;a<3;a++){auto j=zero();if(b.l.x[seg]>=0)j[unsigned(b.l.x[seg])+a]=-w/l;if(b.l.x[seg+1]>=0)j[unsigned(b.l.x[seg+1])+a]=w/l;
            if(b.l.q[seg]>=0)for(unsigned k=0;k<3;k++)j[unsigned(b.l.q[seg])+k]=-w*hair::cross(autonomous::axis(k),d)[int(a)];out.add(r[int(a)],j);}
    }
    for(unsigned bend=0;bend<p.cpu.bends.size();bend++)if(b.l.q[bend]>=0||b.l.q[bend+1]>=0){
        D q=s.q[bend],right=s.q[bend+1],rest(p.cpu.bends[bend]),relative=q.conj()*right,r=relative-double(phi[bend])*rest;double w=std::sqrt(4*double(p.set.bt)/p.cpu.lengths[bend]);
        if(std::abs(relative.dot(rest))<=1e-6)out.ties++;if((b.l.q[bend]>=0)!=(b.l.q[bend+1]>=0))out.crossingBT++;
        for(unsigned a=0;a<4;a++){auto j=zero();for(unsigned k=0;k<3;k++){
            if(b.l.q[bend]>=0){D dq=.5*(autonomous::axis(k)*q);j[unsigned(b.l.q[bend])+k]=w*(dq.conj()*right)[int(a)];}
            if(b.l.q[bend+1]>=0){D dq=.5*(autonomous::axis(k)*right);j[unsigned(b.l.q[bend+1])+k]=w*(q.conj()*dq)[int(a)];}}
            out.add(w*r[int(a)],j);}
    }return out;
}
inline double physicalF(const Problem& p,const State& s,const Block& b){return autonomous::evaluate(p.cpu,s,p.dy,p.set,b.l,false).energy;}
// Copy the reference's diagonally scaled Cholesky algorithm, using the full small
// local matrix instead of its full-strand band. No inverse, no physical penalty.
inline bool solve(const std::vector<double>& h,const std::vector<double>& g,unsigned n,double lambda,std::vector<double>& step){
    std::vector<double>a(size_t(n)*n),rhs(n),scale(n);step.assign(n,0);
    for(unsigned i=0;i<n;i++){scale[i]=std::sqrt(std::max(h[size_t(i)*n+i],1e-30));rhs[i]=-g[i]/scale[i];}
    for(unsigned i=0;i<n;i++)for(unsigned j=0;j<=i;j++){double v=h[size_t(i)*n+j]/(scale[i]*scale[j])+(i==j?lambda:0);for(unsigned k=0;k<j;k++)v-=a[size_t(i)*n+k]*a[size_t(j)*n+k];if(i==j){if(!(v>0&&std::isfinite(v)))return false;a[size_t(i)*n+i]=std::sqrt(v);}else a[size_t(i)*n+j]=v/a[size_t(j)*n+j];}
    for(unsigned i=0;i<n;i++){for(unsigned j=0;j<i;j++)rhs[i]-=a[size_t(i)*n+j]*rhs[j];rhs[i]/=a[size_t(i)*n+i];}
    for(unsigned i=n;i-->0;){double v=rhs[i];for(unsigned j=i+1;j<n;j++)v-=a[size_t(j)*n+i]*step[j];step[i]=v/a[size_t(i)*n+i];}
    for(unsigned i=0;i<n;i++)step[i]/=scale[i];return std::all_of(step.begin(),step.end(),[](double v){return std::isfinite(v);});
}
struct Work {
    uint64_t visits=0,linearizations=0,jacobianRows=0,solves=0,trials=0,rejectedTrials=0,accepted=0,rejectedVisits=0,nonSPD=0,nonfinite=0,negligible=0,activeDOFs=0,ties=0,poleChanges=0,acceptedPoleChanges=0,chartNormalizations=0,pSolves=0,qSolves=0;
    double milliseconds=0,crossNormMax=0,lambdaMax=0,scaledStepMax=0,positionStepMax=0,angleStepMax=0;
    std::string json() const{std::ostringstream s;s.precision(17);s<<"{\"visits\":"<<visits<<",\"linearizations\":"<<linearizations<<",\"jacobianRows\":"<<jacobianRows<<",\"linearSolves\":"<<solves<<",\"trials\":"<<trials<<",\"rejectedTrials\":"<<rejectedTrials<<",\"accepted\":"<<accepted<<",\"rejectedVisits\":"<<rejectedVisits<<",\"nonSPD\":"<<nonSPD<<",\"nonfinite\":"<<nonfinite<<",\"negligibleDecreaseAccepted\":"<<negligible<<",\"activeDOFsProcessed\":"<<activeDOFs<<",\"poleTies\":"<<ties<<",\"trialPoleChanges\":"<<poleChanges<<",\"acceptedPoleChanges\":"<<acceptedPoleChanges<<",\"chartNormalizations\":"<<chartNormalizations<<",\"pSolves\":"<<pSolves<<",\"qSolves\":"<<qSolves<<",\"cpuMilliseconds\":"<<milliseconds<<",\"maxCrossFrobenius\":"<<crossNormMax<<",\"maxLambda\":"<<lambdaMax<<",\"maxScaledStepSqrtJoules\":"<<scaledStepMax<<",\"maxPositionStepMeters\":"<<positionStepMax<<",\"maxAngleStepRadians\":"<<angleStepMax<<"}";return s.str();}
};
inline void visit(const Problem& p,State& state,const Block& b,bool ablate,Work& w,std::ostream& log,unsigned macro,unsigned blockIndex){
    w.visits++;if(!b.l.size)return;
    // A unit chart is constructed in a COPY. No normalization is committed unless
    // the complete trial reduces the actual physical objective of the old state.
    State base=state;for(unsigned j=0;j<base.q.size();j++)if(b.l.q[j]>=0){if(std::abs(base.q[j].norm()-1)>2e-16)w.chartNormalizations++;base.q[j]=base.q[j].unit();}
    double before=physicalF(p,state,b);auto phi=poles(p,base);auto r=rows(p,base,b,phi);unsigned n=b.l.size;w.linearizations++;w.jacobianRows+=r.r.size();w.activeDOFs+=n;w.ties+=r.ties;
    std::vector<double> h(size_t(n)*n),g(n);
    for(unsigned k=0;k<r.r.size();k++)for(unsigned a=0;a<n;a++){g[a]+=r.j[k][a]*r.r[k];for(unsigned c=0;c<n;c++)h[size_t(a)*n+c]+=r.j[k][a]*r.j[k][c];}
    double cross=0;for(unsigned a=0;a<b.positionDOFs;a++)for(unsigned c=b.positionDOFs;c<n;c++)cross+=h[size_t(a)*n+c]*h[size_t(a)*n+c];w.crossNormMax=std::max(w.crossNormMax,std::sqrt(cross));
    if(ablate)for(unsigned a=0;a<b.positionDOFs;a++)for(unsigned c=b.positionDOFs;c<n;c++)h[size_t(a)*n+c]=h[size_t(c)*n+a]=0;
    double lambda=1e-8;for(unsigned retry=0;retry<8;retry++,lambda*=10){std::vector<double> d;w.solves++;w.lambdaMax=std::max(w.lambdaMax,lambda);
        if(!solve(h,g,n,lambda,d)){w.nonSPD++;log<<macro<<','<<blockIndex<<','<<n<<','<<lambda<<",0,"<<before<<",nan,nonSPD,0\n";continue;}
        double norm=0,px=0,angle=0;for(unsigned a=0;a<n;a++)norm+=h[size_t(a)*n+a]*d[a]*d[a];
        for(unsigned a=0;a<n;a+=3){double v=std::sqrt(d[a]*d[a]+d[a+1]*d[a+1]+d[a+2]*d[a+2]);if(a<b.positionDOFs)px=std::max(px,v);else angle=std::max(angle,v);}
        w.scaledStepMax=std::max(w.scaledStepMax,std::sqrt(norm));
        for(unsigned line=0;line<16;line++){double alpha=std::ldexp(1.,-int(line));auto candidate=autonomous::apply(base,b.l,d,alpha);double after=physicalF(p,candidate,b);w.trials++;auto trialPoles=poles(p,candidate);unsigned changes=0;for(unsigned j=0;j<phi.size();j++)changes+=phi[j]!=trialPoles[j];w.poleChanges+=changes;
            bool accept=std::isfinite(after)&&after<before;log<<macro<<','<<blockIndex<<','<<n<<','<<lambda<<','<<alpha<<','<<before<<','<<after<<','<<(accept?"accepted":std::isfinite(after)?"noDecrease":"nonfinite")<<','<<changes<<'\n';
            if(accept){w.accepted++;w.acceptedPoleChanges+=changes;w.negligible+=(before-after)<=1e-10*std::max(std::abs(before),1e-300);w.positionStepMax=std::max(w.positionStepMax,alpha*px);w.angleStepMax=std::max(w.angleStepMax,alpha*angle);state=std::move(candidate);return;}
            w.rejectedTrials++;w.nonfinite+=!std::isfinite(after);
        }
    }w.rejectedVisits++;
}
inline double jacobianTest(const Problem& p,const State& input,unsigned width,unsigned& tested,unsigned& crossing){
    double worst=0;for(auto& b:blocks(p,width)){if(!b.l.size)continue;State s=input;for(unsigned j=0;j<s.q.size();j++)if(b.l.q[j]>=0)s.q[j]=s.q[j].unit();auto phi=poles(p,s);auto r=rows(p,s,b,phi);crossing+=r.crossingBT;
        auto ref=autonomous::evaluate(p.cpu,s,p.dy,p.set,b.l,true);std::vector<double> g(b.l.size),h(size_t(b.l.size)*b.l.size);
        for(unsigned k=0;k<r.r.size();k++)for(unsigned a=0;a<b.l.size;a++){g[a]+=r.j[k][a]*r.r[k];for(unsigned c=0;c<b.l.size;c++)h[size_t(a)*b.l.size+c]+=r.j[k][a]*r.j[k][c];}
        for(unsigned a=0;a<b.l.size;a++){require(std::abs(g[a]-ref.gradient[a])<1e-11*std::max(1.,std::abs(ref.gradient[a])),"Touched-term gradient mismatch");for(unsigned c=0;c<b.l.size;c++)require(std::abs(h[size_t(a)*b.l.size+c]-ref.hessian[size_t(a)*b.l.size+c])<1e-11*std::max(1.,std::abs(ref.hessian[size_t(a)*b.l.size+c])),"Crossing-term Hessian mismatch");}
        for(unsigned a=0;a<b.l.size;a++){std::vector<double>d(b.l.size);d[a]=1;double eps=a<b.positionDOFs?1e-7:1e-6;auto left=rows(p,autonomous::apply(s,b.l,d,eps),b,phi),right=rows(p,autonomous::apply(s,b.l,d,-eps),b,phi);
            for(unsigned k=0;k<r.r.size();k++){double fd=(left.r[k]-right.r[k])/(2*eps);worst=std::max(worst,std::abs(fd-r.j[k][a])/std::max(1.,std::abs(r.j[k][a])));tested++;}
        }
    }require(worst<2e-7,"Residual Jacobian finite difference mismatch");return worst;
}
} // namespace jointcpu
