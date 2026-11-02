#include "OrientationFixtures.h"
#include "GuideState.h"
namespace hair {
std::string MathResult::json() const {std::ostringstream s;s<<std::setprecision(17)<<"{\"cpuOnly\":true,\"problems\":"<<problems<<",\"finiteDifferenceSamples\":"<<derivatives<<",\"maxNormalizedFiniteDifference\":"<<finiteDifference<<",\"symmetryError\":"<<symmetry<<",\"projectorError\":"<<projector<<",\"referenceResidual\":"<<referenceResidual<<",\"spectralViolation\":"<<spectral<<",\"hardEnergy\":"<<hardEnergy<<"}";return s.str();}
MathResult validateMath(){
    MathResult result;CsRandom random(3901);
    for(int n=0;n<128;++n){D q=randomQ(random),other=randomQ(random),left=randomQ(random),right=randomQ(random),rl=randomQ(random),rr=randomQ(random);D edge{.003,.004,.01};double l=edge.norm(),kss=.01,kl=.004,kr=.007;D v=(-2*kss/l)*edge,b=kl*((left.conj()*q).dot(rl)>=0?1.:-1.)*(left*rl)+kr*((q.conj()*right).dot(rr)>=0?1.:-1.)*(right*rr.conj());double alpha=v.norm();
        result.symmetry=std::max({result.symmetry,std::abs(q.dot(apply(v,other))-apply(v,q).dot(other)),(apply(v,apply(v,q))-alpha*alpha*q).norm()});
        D plus=.5*(q+apply(v,q)/alpha),minus=.5*(q-apply(v,q)/alpha);result.projector=std::max({result.projector,std::abs(plus.dot(minus)),(apply(v,plus)-alpha*plus).norm(),(apply(v,minus)+alpha*minus).norm()});
        D candidate=localReference(v,b,q);double violation;result.referenceResidual=std::max(result.referenceResidual,residual(v,b,candidate,violation)/(alpha+b.norm()));result.spectral=std::max(result.spectral,violation/(alpha+b.norm()));require(objective(v,b,candidate)<=objective(v,b,q)+1e-12,"Reference energy minimum");++result.problems;
        if(std::abs((left.conj()*q).dot(rl))<.05||std::abs((q.conj()*right).dot(rr))<.05)continue;
        D direction=(other-q.dot(other)*q).unit(),s=apply(v,q)+b,r=s-q.dot(s)*q;
        for(double h:{1e-3,1e-4,1e-5,1e-6}){D qp=(q+h*direction).unit(),qm=(q-h*direction).unit();double ep=physicalEnergy(qp,edge,l,kss,left,right,rl,rr,kl,kr),em=physicalEnergy(qm,edge,l,kss,left,right,rl,rr,kl,kr);double error=std::abs((ep-em)/(2*h)+r.dot(direction))/(alpha+b.norm());result.finiteDifference=std::max(result.finiteDifference,error);require(error<3e-6,"Independent finite difference sign/factor failure");++result.derivatives;require(std::abs((ep-em)-(objective(v,b,qp)-objective(v,b,qm)))<1e-12,"Physical/objective energy difference");}
    }
    D v{0,0,-.02},b{.004,0,0,0},old{1,0,0,0},hard=localReference(v,b,old);result.hardEnergy=physicalEnergy(hard,{0,0,.01},.01,.01,{},old,{},D{0,0,0,1},0,.004);double oldViolation;residual(v,b,old,oldViolation);
    require(std::abs(result.hardEnergy-.0038)<1e-12&&oldViolation>.035,"Hard case dense reference");require(result.symmetry<1e-12&&result.projector<1e-12&&result.referenceResidual<1e-10&&result.spectral<1e-10,"Independent double reference identities");
    D x=axisAngle(D{1,0,0},.7),y=axisAngle(D{0,1,0},1.1),p{.3,.2,.8};require((rotate(x*y,p)-rotate(x,rotate(y,p))).norm()<1e-12,"Noncommuting Hamilton composition");require(angle(x*y,y*x)>.1,"Quaternion order test is nontrivial");require(angle(x.conj()*(x*y),y)<1e-12&&angle(-x,x)<1e-12,"Inverse, relative and sign equivalence");
    // Known .NET sequence, independent of local-problem math.
    CsRandom r(1);require(std::abs(r.sample()-.24866858415709278)<1e-15,"System.Random sequence mismatch");
    Strands fixture;fixture.positions={{0,0,0},{0,0,.01f},{0,0,0},{0,0,-.01f},{.01f,0,-.01f}};fixture.offsets={0,3,5};fixture.sourceIds={0,1};fixture.derive();GuideCpuState state;state.initialize(fixture);require(state.ranges.size()==2&&state.orientations.size()==3&&state.bends.size()==1,"Variable guide topology / U-turn fixture");
    for(auto range:state.ranges)for(unsigned j=0;j<range.segmentCount;++j){auto a=fixture.positions[range.firstVertex+j],bpos=fixture.positions[range.firstVertex+j+1];F tangent=F{bpos.x-a.x,bpos.y-a.y,bpos.z-a.z}.unit();require((rotate(state.orientations[range.firstSegment+j],F{0,0,1})-tangent).norm()<2e-5f,"Actual basis +Z mapping");}
    require(std::abs(state.masses[0]-.5f*GuideCpuState::Density*3.14159265358979323846f*GuideCpuState::Radius*GuideCpuState::Radius*.01f)<1e-14,"Endpoint half-mass distribution");
    auto bad=fixture;bad.positions[1]=bad.positions[0];bool rejected=false;try{GuideCpuState invalid;invalid.initialize(bad);}catch(const std::exception&){rejected=true;}require(rejected,"Invalid rest geometry must not become valid local algebra");
    return result;
}
}
