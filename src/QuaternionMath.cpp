#include "QuaternionMath.h"
#include <limits>
namespace hair {
// Direct transcription of OrientationLocalReference.cs: dense DOUBLE Jacobi +
// 200-step secular solve. Independent of GPU projectors and lambda refinement.
D localReference(D v,D b,D old) {
    double scale=std::max(v.norm(),b.norm());if(scale==0)return old;
    v=v/scale;b=b/scale;
    double a[4][4]={{v.z,0,-v.x,v.y},{0,v.z,-v.y,-v.x},{-v.x,-v.y,-v.z,0},{v.y,-v.x,0,-v.z}},u[4][4]{};
    for(int i=0;i<4;++i)u[i][i]=1;
    for(int step=0;step<128;++step) {
        int p=0,r=1;double largest=0;
        for(int i=0;i<4;++i)for(int j=i+1;j<4;++j)if(std::abs(a[i][j])>largest){largest=std::abs(a[i][j]);p=i;r=j;}
        if(largest<1e-15)break;
        double theta=.5*std::atan2(2*a[p][r],a[r][r]-a[p][p]),c=std::cos(theta),s=std::sin(theta);
        for(int i=0;i<4;++i){double ap=a[i][p],ar=a[i][r];a[i][p]=c*ap-s*ar;a[i][r]=s*ap+c*ar;}
        for(int i=0;i<4;++i){double ap=a[p][i],ar=a[r][i];a[p][i]=c*ap-s*ar;a[r][i]=s*ap+c*ar;}
        for(int i=0;i<4;++i){double up=u[i][p],ur=u[i][r];u[i][p]=c*up-s*ur;u[i][r]=s*up+c*ur;}
    }
    double top=-std::numeric_limits<double>::infinity();for(int i=0;i<4;++i)top=std::max(top,a[i][i]);
    double beta[4]{},coeff[4]{},topB2=0,lower2=0;
    for(int i=0;i<4;++i){for(int j=0;j<4;++j)beta[i]+=u[j][i]*b[j];if(top-a[i][i]<1e-13)topB2+=beta[i]*beta[i];else lower2+=std::pow(beta[i]/(top-a[i][i]),2);}
    if(topB2<=1e-28*b.dot(b)&&lower2<=1) {
        double projection2=0;for(int i=0;i<4;++i)if(top-a[i][i]<1e-13){for(int j=0;j<4;++j)coeff[i]+=u[j][i]*old[j];projection2+=coeff[i]*coeff[i];}
        if(projection2<1e-24){for(auto& c:coeff)c=0;for(int i=0;i<4;++i)if(top-a[i][i]<1e-13){coeff[i]=1;break;}projection2=1;}
        for(int i=0;i<4;++i)coeff[i]=top-a[i][i]<1e-13?coeff[i]*std::sqrt((1-lower2)/projection2):beta[i]/(top-a[i][i]);
    } else {
        double lo=0,hi=b.norm();for(int n=0;n<200;++n){double gap=(lo+hi)*.5,total=0;for(int i=0;i<4;++i){double den=gap+top-a[i][i];total+=std::pow(beta[i]/den,2);}if(total>1)lo=gap;else hi=gap;}
        for(int i=0;i<4;++i)coeff[i]=beta[i]/(hi+top-a[i][i]);
    }
    D q;for(int i=0;i<4;++i)for(int j=0;j<4;++j)q[i]+=u[i][j]*coeff[j];return q.unit();
}
double physicalEnergy(D q,D edge,double length,double kss,D left,D right,D rl,D rr,double kl,double kr) {
    D tangent=q*D{0,0,1,0}*q.conj(),css=edge/length-tangent;double e=.5*kss*css.dot(css);
    if(kl>0){D rel=left.conj()*q,c=rel-(rel.dot(rl)>=0?1.:-1.)*rl;e+=.5*kl*c.dot(c);}
    if(kr>0){D rel=q.conj()*right,c=rel-(rel.dot(rr)>=0?1.:-1.)*rr;e+=.5*kr*c.dot(c);}return e;
}
}
