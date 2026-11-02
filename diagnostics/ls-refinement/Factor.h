#pragma once
#include "../strand-block/Backend.h"
namespace refinement {
using namespace strandblock;
// Same block Cholesky and triangular arithmetic as blockSolve; split lifetime
// so all RHS corrections reuse the one factor. Input M is already scaled and regularized.
struct Factor {
 unsigned n=0;std::vector<unsigned> size,start;std::vector<Mat>L,C;
 bool init(unsigned count,const std::vector<unsigned>& sizes,const std::vector<double>& m){
  n=count;size=sizes;start.resize(size.size());for(unsigned i=1;i<size.size();i++)start[i]=start[i-1]+size[i-1];if(start.back()+size.back()!=n)return false;L.assign(size.size(),{});C.assign(size.size(),{});
  for(unsigned i=0;i<size.size();i++){unsigned ni=size[i],si=start[i],np=i?size[i-1]:0,sp=i?start[i-1]:0;
   for(unsigned a=0;a<ni;a++)for(unsigned k=0;k<np;k++){double v=m[size_t(si+a)*n+sp+k];for(unsigned j=0;j<k;j++)v-=at(L[i-1],k,j)*at(C[i],a,j);at(C[i],a,k)=v/at(L[i-1],k,k);}
   for(unsigned a=0;a<ni;a++)for(unsigned b=0;b<=a;b++){double v=m[size_t(si+a)*n+si+b];for(unsigned k=0;k<np;k++)v-=at(C[i],a,k)*at(C[i],b,k);for(unsigned k=0;k<b;k++)v-=at(L[i],a,k)*at(L[i],b,k);if(a==b){if(!(v>0&&std::isfinite(v)))return false;at(L[i],a,b)=std::sqrt(v);}else at(L[i],a,b)=v/at(L[i],b,b);}
  }return true;
 }
 std::vector<double> solve(const std::vector<double>& rhs)const{
  std::vector<double> y(n),u(n);
  for(unsigned i=0;i<size.size();i++){unsigned ni=size[i],si=start[i],np=i?size[i-1]:0,sp=i?start[i-1]:0;
   for(unsigned a=0;a<ni;a++){double v=rhs[si+a];for(unsigned k=0;k<np;k++)v-=at(C[i],a,k)*y[sp+k];for(unsigned k=0;k<a;k++)v-=at(L[i],a,k)*y[si+k];y[si+a]=v/at(L[i],a,a);}}
  for(unsigned i=unsigned(size.size());i-->0;){unsigned ni=size[i],si=start[i],nn=i+1<size.size()?size[i+1]:0,sn=i+1<size.size()?start[i+1]:0;
   for(unsigned a=ni;a-->0;){double v=y[si+a];for(unsigned k=0;k<nn;k++)v-=at(C[i+1],k,a)*u[sn+k];for(unsigned k=a+1;k<ni;k++)v-=at(L[i],k,a)*u[si+k];u[si+a]=v/at(L[i],a,a);}}
  return u;
 }
};
}
