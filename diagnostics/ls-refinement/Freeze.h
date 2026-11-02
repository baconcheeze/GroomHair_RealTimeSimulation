#pragma once
#include <bit>
inline void freeze(const Problem& p,const State& state,const Layout& l,const autonomous::System& dense,const Rows& rows,const std::vector<double>& scale,double lambda,const std::vector<double>& da,const std::vector<double>& db,const std::vector<double>& dq,const LinearRecord& rec){
 const char* path=std::getenv("QRAUDIT_OUT");require(path,"QRAUDIT_OUT required");fs::create_directories(path);
 std::string id=fixtureName+"__"+rec.label;for(char& c:id)if(c==' ')c='_';
 std::ostringstream o;o<<"{\"id\":\""<<id<<"\",\"case\":\""<<fixtureName<<"\",\"label\":\""<<rec.label<<"\",\"n\":"<<l.size<<",\"rows\":"<<rows.r.size()<<",\"originalPass\":"<<(rec.pass?"true":"false");
 auto vals=[&](const std::string& key,const std::vector<double>& v){o<<",\""<<key<<"\":[";for(size_t i=0;i<v.size();i++){if(i)o<<',';o<<'"'<<std::hex<<std::setw(16)<<std::setfill('0')<<std::bit_cast<uint64_t>(v[i])<<'"'<<std::dec;}o<<']';};
 auto vecs=[&](const std::string& key,const std::vector<D>& v,unsigned dims){std::vector<double>a;for(auto x:v)for(unsigned j=0;j<dims;j++)a.push_back(x[int(j)]);vals(key,a);};
 std::vector<double> j;for(auto& row:rows.j)j.insert(j.end(),row.begin(),row.end());vals("J",j);vals("r",rows.r);require(scale==frozenD,"Captured scale differs from actual reference scale");vals("D",frozenD);vals("lambda",{lambda});vals("A",frozenA);vals("b",frozenB);vals("M",frozenM);vals("c",frozenC);vals("H",dense.hessian);vals("gradient",dense.gradient);
 vecs("x",state.x,3);vecs("q",state.q,4);vecs("y",p.dy,3);std::vector<double> a;for(auto x:p.cpu.masses)a.push_back(x);vals("mass",a);a.clear();for(auto x:p.cpu.lengths)a.push_back(x);vals("L",a);a.clear();for(auto x:p.cpu.bends)for(int k=0;k<4;k++)a.push_back(x[k]);vals("restBT",a);vals("hSSBT",{p.set.h,p.set.ss,p.set.bt});
 o<<",\"mapX\":[";for(unsigned i=0;i<l.x.size();i++){if(i)o<<',';o<<l.x[i];}o<<"],\"mapQ\":[";for(unsigned i=0;i<l.q.size();i++){if(i)o<<',';o<<l.q[i];}o<<']';
 const std::vector<double>* ds[]={&da,&db,&dq};const std::vector<double>* us[]={&frozenRefU,&frozenBlockU,&frozenQRU};const char* names[]={"reference","block","QR"};
 for(unsigned k=0;k<3;k++){std::string name=names[k];vals(name+"U",*us[k]);vals(name+"Delta",*ds[k]);auto trial=apply(state,l,*ds[k],1);vecs(name+"TrialQ",trial.q,4);vecs(name+"TrialX",trial.x,3);double f=evaluate(p.cpu,trial,p.dy,p.set,l,false).energy;vals(name+"F",{f});double alpha=0,accepted=dense.energy;State acceptedState=state;for(unsigned z=0;z<16;z++){double t=std::ldexp(1.,-int(z));auto s=apply(state,l,*ds[k],t);double e=evaluate(p.cpu,s,p.dy,p.set,l,false).energy;if(std::isfinite(e)&&e<dense.energy){alpha=t;accepted=e;acceptedState=s;break;}}vals(name+"Alpha",{alpha});vals(name+"AcceptedF",{accepted});vecs(name+"AcceptedQ",acceptedState.q,4);}
 vals("F",{dense.energy});o<<'}';text(fs::path(path)/(id+".json"),o.str());
}
