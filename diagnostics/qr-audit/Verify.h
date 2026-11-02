#pragma once
#include "Backend.h"
namespace strandblock {
struct Rows {
    unsigned n;double energy=0;std::vector<double> r;std::vector<std::vector<double>> j;
    explicit Rows(unsigned count):n(count){}
    void row(double residual,const std::vector<std::pair<unsigned,double>>& entries){energy+=.5*residual*residual;r.push_back(residual);j.emplace_back(n);for(auto [a,v]:entries)j.back()[a]=v;}
};
// Independent Householder QR on [J D^-1; sqrt(lambda) I], rhs=[-r;0].
// Verification only: this quadratic-storage path is NOT a structured backend.
inline bool qrSolve(const Rows& rows,const std::vector<double>& scale,double lambda,std::vector<double>& step){
    unsigned n=rows.n,m=unsigned(rows.r.size())+n;std::vector<double>a(size_t(m)*n),b(m);step.assign(n,0);
    for(unsigned i=0;i<rows.r.size();i++){b[i]=-rows.r[i];for(unsigned j=0;j<n;j++)a[size_t(i)*n+j]=rows.j[i][j]/scale[j];}
    for(unsigned j=0;j<n;j++)a[size_t(rows.r.size()+j)*n+j]=std::sqrt(lambda);
    frozenA=a;frozenB=b;for(unsigned k=0;k<n;k++){double norm=0;for(unsigned i=k;i<m;i++)norm=std::hypot(norm,a[size_t(i)*n+k]);if(!(norm>0&&std::isfinite(norm)))return false;
        double alpha=-std::copysign(norm,a[size_t(k)*n+k]);std::vector<double>v(m-k);for(unsigned i=k;i<m;i++)v[i-k]=a[size_t(i)*n+k];v[0]-=alpha;double vnorm=0;for(auto x:v)vnorm=std::hypot(vnorm,x);for(auto& x:v)x/=vnorm;
        for(unsigned j=k;j<n;j++){double dot=0;for(unsigned i=k;i<m;i++)dot+=v[i-k]*a[size_t(i)*n+j];for(unsigned i=k;i<m;i++)a[size_t(i)*n+j]-=2*v[i-k]*dot;}
        double dot=0;for(unsigned i=k;i<m;i++)dot+=v[i-k]*b[i];for(unsigned i=k;i<m;i++)b[i]-=2*v[i-k]*dot;
    }
    for(unsigned i=n;i-->0;){double v=b[i];for(unsigned j=i+1;j<n;j++)v-=a[size_t(i)*n+j]*step[j];if(a[size_t(i)*n+i]==0)return false;step[i]=v/a[size_t(i)*n+i];}frozenQRU=step;for(unsigned j=0;j<n;j++)step[j]/=scale[j];return true;
}
inline double backward(const autonomous::System& s,const std::vector<double>& scale,double lambda,const std::vector<double>& d){
    double normM=0,normU=0,normB=0,normR=0;for(unsigned j=0;j<s.n;j++)normU=std::max(normU,std::abs(d[j]*scale[j]));
    for(unsigned i=0;i<s.n;i++){double rhs=-s.gradient[i]/scale[i],v=-rhs,rowNorm=0;normB=std::max(normB,std::abs(rhs));for(unsigned j=0;j<s.n;j++){double a=s.hessian[size_t(i)*s.n+j]/(scale[i]*scale[j])+(i==j?lambda:0);v+=a*(d[j]*scale[j]);rowNorm+=std::abs(a);}normR=std::max(normR,std::abs(v));normM=std::max(normM,rowNorm);}
    double den=normM*normU+normB;return den?normR/den:normR;
}
inline double prediction(const autonomous::System& s,const std::vector<double>& d){double p=0;for(unsigned i=0;i<s.n;i++){p-=s.gradient[i]*d[i];for(unsigned j=0;j<s.n;j++)p-=.5*d[i]*s.hessian[size_t(i)*s.n+j]*d[j];}return p;}
// Input-aware KKT residual of the SAME augmented least-squares problem.
// A normal-RHS-relative residual is misleading when J^T r cancels to zero.
inline double augmentedBackward(const Rows& rows,const std::vector<double>& scale,double lambda,const std::vector<double>& d){
    std::vector<double> u(rows.n),gradient(rows.n),colSum(rows.n);double normU=0,normR=0,normA=std::sqrt(lambda);
    for(unsigned j=0;j<rows.n;j++){u[j]=d[j]*scale[j];normU=std::max(normU,std::abs(u[j]));gradient[j]=lambda*u[j];colSum[j]=std::sqrt(lambda);}
    for(unsigned i=0;i<rows.r.size();i++){double residual=rows.r[i],rowSum=0;normR=std::max(normR,std::abs(rows.r[i]));for(unsigned j=0;j<rows.n;j++){double a=rows.j[i][j]/scale[j];residual+=a*u[j];rowSum+=std::abs(a);colSum[j]+=std::abs(a);}normA=std::max(normA,rowSum);for(unsigned j=0;j<rows.n;j++)gradient[j]+=(rows.j[i][j]/scale[j])*residual;}
    double normAt=*std::max_element(colSum.begin(),colSum.end()),g=0;for(double v:gradient)g=std::max(g,std::abs(v));double den=normAt*(normA*normU+normR);return den?g/den:g;
}
struct StepDifference {double scaledRelative=0,position=0,angle=0,maxPosition=0,maxAngle=0;};
inline StepDifference stepDifference(const Spec& spec,const std::vector<double>& scale,const std::vector<double>& a,const std::vector<double>& b,const Layout& l){
    StepDifference d;double delta=0,magnitude=0;
    for(unsigned j=0;j<spec.n;j++){delta=std::max(delta,std::abs((a[j]-b[j])*scale[j]));magnitude=std::max(magnitude,std::abs(a[j]*scale[j]));}
    d.scaledRelative=delta/std::max(magnitude,1e-300);
    for(unsigned kind=0;kind<2;kind++)for(int j:kind?l.q:l.x)if(j>=0){D diff{a[unsigned(j)]-b[unsigned(j)],a[unsigned(j)+1]-b[unsigned(j)+1],a[unsigned(j)+2]-b[unsigned(j)+2]},v{a[unsigned(j)],a[unsigned(j)+1],a[unsigned(j)+2]};if(kind){d.angle=std::max(d.angle,diff.norm());d.maxAngle=std::max(d.maxAngle,v.norm());}else{d.position=std::max(d.position,diff.norm());d.maxPosition=std::max(d.maxPosition,v.norm());}}
    return d;
}
struct LinearRecord {
    std::string label;unsigned n=0,stations=0,maxBlock=0,rowSpan=0;double lambda=0,offBand=0,offBandScaled=0,matrixDifference=0,rhsDifference=0,backRef=0,backBlock=0,backQR=0,predRef=0,predBlock=0,predQR=0,trialRef=0,trialBlock=0,alphaRef=0,alphaBlock=0;
    double qrAugmentedBackward=0;StepDifference block,qr;bool referenceOK=false,blockOK=false,qrOK=false,pass=false;
};
#include "Freeze.h"
inline LinearRecord verifyLinear(const Problem& p,const State& state,double lambda,const std::string& label){
    LinearRecord r;r.label=label;r.lambda=lambda;Layout l(p.cpu,p.set);Spec spec(p.cpu,l);r.n=l.size;r.stations=unsigned(spec.size.size());for(unsigned n:spec.size)r.maxBlock=std::max(r.maxBlock,n);
    auto dense=evaluate(p.cpu,state,p.dy,p.set,l,true);Bands direct(spec);emitPhysics(direct,p.cpu,state,p.dy,p.set,l,true);r.rowSpan=direct.maxRowStationSpan;
    Bands extracted(spec);extracted.energy=dense.energy;extracted.gradient=dense.gradient;
    auto scale=scales(dense),bs=scales(direct);
    for(unsigned a=0;a<l.size;a++){r.rhsDifference=std::max(r.rhsDifference,std::abs(-dense.gradient[a]/scale[a]+direct.gradient[a]/bs[a]));for(unsigned b=0;b<l.size;b++){double v=dense.hessian[size_t(a)*l.size+b];unsigned sa=spec.station[a],sb=spec.station[b];if(sa==sb)at(extracted.diagonal[sa],spec.slot[a],spec.slot[b])=v;else if(sa==sb+1)at(extracted.lower[sa],spec.slot[a],spec.slot[b])=v;else if(sb!=sa+1){r.offBand=std::max(r.offBand,std::abs(v));r.offBandScaled=std::max(r.offBandScaled,std::abs(v/(scale[a]*scale[b])));}r.matrixDifference=std::max(r.matrixDifference,std::abs(v-direct.get(a,b)));}}
    Work a,b,c;std::vector<double> da,db,dc,dq;r.referenceOK=referenceSolve(dense,lambda,da,a);r.blockOK=blockSolve(direct,lambda,db,b);bool extractedOK=blockSolve(extracted,lambda,dc,c);
    require(r.offBand==0&&r.matrixDifference==0&&r.rhsDifference==0,"Reference chain/assembly mismatch: do not project away couplings");require(extractedOK==r.blockOK,"Extraction mismatch");
    if(!r.referenceOK||!r.blockOK){r.pass=r.referenceOK==r.blockOK;return r;}
    require(db==dc,"Direct and extracted bands differ");r.block=stepDifference(spec,scale,da,db,l);r.backRef=backward(dense,scale,lambda,da);r.backBlock=backward(dense,scale,lambda,db);
    Rows rows(l.size);emitPhysics(rows,p.cpu,state,p.dy,p.set,l,true);r.qrOK=qrSolve(rows,scale,lambda,dq);require(r.qrOK,"Augmented QR failure");r.qr=stepDifference(spec,scale,da,dq,l);r.backQR=backward(dense,scale,lambda,dq);
    r.qrAugmentedBackward=augmentedBackward(rows,scale,lambda,dq);
    r.predRef=prediction(dense,da);r.predBlock=prediction(dense,db);r.predQR=prediction(dense,dq);
    auto firstAccepted=[&](const std::vector<double>& d,double& alpha,double& energy){alpha=0;energy=dense.energy;for(unsigned k=0;k<16;k++){double x=std::ldexp(1.,-int(k)),f=evaluate(p.cpu,apply(state,l,d,x),p.dy,p.set,l,false).energy;if(std::isfinite(f)&&f<dense.energy){alpha=x;energy=f;break;}}};
    firstAccepted(da,r.alphaRef,r.trialRef);firstAccepted(db,r.alphaBlock,r.trialBlock);
    bool stepOK=r.block.scaledRelative<=1e-7&&r.block.position<=std::max(1e-10,1e-8*r.block.maxPosition)&&r.block.angle<=std::max(1e-9,1e-8*r.block.maxAngle);
    r.pass=stepOK&&r.backRef<=5e-14&&r.backBlock<=5e-14&&r.qrAugmentedBackward<=5e-14&&std::abs(r.predRef-r.predBlock)<=1e-22+1e-9*std::abs(r.predRef);
    r.pass&=(r.qr.scaledRelative<=1e-7||(r.qr.position<=1e-10&&r.qr.angle<=1e-9))&&r.qr.position<=std::max(1e-10,1e-8*r.qr.maxPosition)&&r.qr.angle<=std::max(1e-9,1e-8*r.qr.maxAngle);
    // Alpha differences near the objective's last ulps are explicitly recorded,
    // not hidden by forcing a common accepted line-search result.
    if(r.alphaRef==r.alphaBlock)r.pass&=std::abs(r.trialRef-r.trialBlock)<=1e-22+1e-9*std::abs(r.trialRef);
    freeze(p,state,l,dense,rows,scale,lambda,da,db,dq,r);
    return r;
}
} // namespace strandblock
