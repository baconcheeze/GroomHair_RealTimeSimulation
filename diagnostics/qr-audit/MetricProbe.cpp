#include "../../src/QuaternionMath.h"
#include <iostream>
#include <iomanip>
int main(){std::cout<<std::setprecision(17);auto q=hair::D{.2,-.3,.1,1}.unit();for(double theta:{0.,1e-12,1e-9,1e-8}){auto dq=hair::axisAngle(hair::D{1,0,0},theta);auto b=(dq*q).unit();auto r=(b*q.conj()).unit();double stable=2*std::atan2(std::sqrt(r.x*r.x+r.y*r.y+r.z*r.z),std::abs(r.w));std::cout<<theta<<','<<hair::angle(q,b)<<','<<stable<<','<<hair::angle(b,b)<<','<<hair::angle(b,-b);for(auto c:{q,b})std::cout<<','<<c.x<<','<<c.y<<','<<c.z<<','<<c.w;std::cout<<'\n';}}
