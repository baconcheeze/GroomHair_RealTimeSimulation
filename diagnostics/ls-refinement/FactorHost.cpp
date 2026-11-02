#include "Factor.h"
#include <io.h>
#include <fcntl.h>
#include <cstdio>
template<class T>void read(T* p,size_t n=1){if(fread(p,sizeof(T),n,stdin)!=n)throw std::runtime_error("Protocol read");}
template<class T>void write(const T* p,size_t n=1){if(fwrite(p,sizeof(T),n,stdout)!=n)throw std::runtime_error("Protocol write");}
int main(){_setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);try{refinement::Factor factor;std::vector<double>u,d;for(;;){uint32_t cmd;read(&cmd);if(!cmd)break;
 if(cmd==1){uint32_t n,count;read(&n);read(&count);std::vector<unsigned>sizes(count);read(sizes.data(),count);std::vector<double>m(size_t(n)*n),c(n);d.resize(n);read(m.data(),m.size());read(c.data(),n);read(d.data(),n);auto t=strandblock::Clock::now();uint32_t valid=factor.init(n,sizes,m);double fm=strandblock::ms(t);write(&valid);write(&fm);uint64_t bytes=2*count*sizeof(strandblock::Mat);write(&bytes);if(valid){t=strandblock::Clock::now();u=factor.solve(c);double sm=strandblock::ms(t);write(&sm);write(u.data(),n);auto delta=u;for(unsigned i=0;i<n;i++)delta[i]/=d[i];write(delta.data(),n);}}
 else if(cmd==2){std::vector<double>rhs(factor.n);read(rhs.data(),rhs.size());auto t=strandblock::Clock::now();auto du=factor.solve(rhs);double sm=strandblock::ms(t);uint32_t lost=0;for(unsigned i=0;i<factor.n;i++){double old=u[i];u[i]+=du[i];lost+=old==u[i]&&du[i]!=0;if(!std::isfinite(u[i]))throw std::runtime_error("Nonfinite solution");}write(&sm);write(&lost);write(du.data(),du.size());write(u.data(),u.size());auto delta=u;for(unsigned i=0;i<factor.n;i++)delta[i]/=d[i];write(delta.data(),delta.size());}
 else throw std::runtime_error("Unknown protocol command");fflush(stdout);}return 0;}catch(const std::exception& e){fprintf(stderr,"%s\n",e.what());return 2;}}
