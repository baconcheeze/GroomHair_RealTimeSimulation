#pragma once
#include "QuaternionMath.h"
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
namespace hair {
struct LocalProblem {
    std::string name;D q,v,b,edge,left,right,rl,rr;double length{},kss{},kl{},kr{};bool physical=false;
    double energy(D value) const {return physical?physicalEnergy(value,edge,length,kss,left,right,rl,rr,kl,kr):objective(v,b,value);}
};
inline LocalProblem physical(std::string name,D q,D edge,double ss,D left,D right,D rl,D rr,double kl,double kr){
    q=D(F(q)).unit();D b=kl*((left.conj()*q).dot(rl)>=0?1.:-1.)*(left*rl)+kr*((q.conj()*right).dot(rr)>=0?1.:-1.)*(right*rr.conj());
    return {name,D(F(q)),D(F((-2*ss)*edge)),D(F(b)),edge,left,right,rl,rr,edge.norm(),ss*edge.norm(),kl,kr,true};
}
inline std::vector<LocalProblem> localProblems(){
    std::vector<LocalProblem> cases;D identity{0,0,0,1},flip{1,0,0,0},edge{0,0,.01,0};
    cases.push_back(physical("exact180",flip,edge,1,{},flip,identity,identity,0,.004));
    for(int mask=0;mask<16;++mask){D q=D{.3,.2,.1,.9}.unit(),neighbor=D{-.1,.4,.2,.8}.unit(),rest=D{.1,-.2,.3,.9}.unit();cases.push_back(physical("sign_combination_"+std::to_string(mask),((mask&1)?-1.:1.)*q,edge,1,((mask&2)?-1.:1.)*neighbor,((mask&4)?-1.:1.)*neighbor,((mask&8)?-1.:1.)*rest,rest,.004,.003));}
    for(double scale:{1e-6,1.,1e6})for(double offset:{-1e-2,-1e-4,-1e-6,-1e-8,0.,1e-8,1e-6,1e-4,1e-2})for(int sign:{-1,1}){
        D perturbed=D{1,offset*.23,offset*.31,offset}.unit();std::ostringstream name;name<<std::setprecision(12)<<"near_"<<offset<<"_scale"<<scale<<"_sign"<<sign;
        cases.push_back(physical(name.str(),double(sign)*perturbed,edge,scale,{},double(sign)*perturbed,identity,double(sign)*identity,0,.004*scale));
    }
    CsRandom random(71831);
    for(int i=0;i<300;++i){double ss=i%3==0?100:i%3==1?.0001:1;D axis=randomQ(random);D e{axis.x,axis.y,axis.z};e=.01*e.unit();D q=randomQ(random),l=randomQ(random),r=randomQ(random),rl=randomQ(random),rr=randomQ(random);cases.push_back(physical("random_"+std::to_string(i),q,e,ss,l,r,rl,rr,.004,.003));}
    LocalProblem c;c.name="b_zero";c.q=flip;c.v={0,0,-.02};cases.push_back(c);
    c={};c.name="alpha_zero";c.q=identity;c.b={.1,.2,.3,.4};cases.push_back(c);
    c={};c.name="both_zero";c.q=flip;cases.push_back(c);
    c={};c.name="minus_above_hard";c.q=flip;c.v={0,0,-.02};c.b={.06,0,0,0};cases.push_back(c);
    return cases;
}
inline void require(bool condition,const std::string& text){if(!condition)throw std::runtime_error(text);}
struct MathResult {unsigned problems{},derivatives{};double finiteDifference{},symmetry{},projector{},referenceResidual{},spectral{},hardEnergy{};std::string json() const;};
MathResult validateMath();
}
