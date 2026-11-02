#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
namespace hair {
template<class T> struct Q {
    T x{},y{},z{},w{};
    Q()=default; Q(T x_,T y_,T z_,T w_=0):x(x_),y(y_),z(z_),w(w_){}
    template<class U> explicit Q(Q<U> q):x(T(q.x)),y(T(q.y)),z(T(q.z)),w(T(q.w)){}
    T& operator[](int i) { return i==0?x:i==1?y:i==2?z:w; }
    T operator[](int i) const { return i==0?x:i==1?y:i==2?z:w; }
    T dot(Q b) const { return x*b.x+y*b.y+z*b.z+w*b.w; }
    T norm() const { return std::sqrt(dot(*this)); }
    Q unit() const { T n=norm(); if(!(n>0)||!std::isfinite(n)) throw std::runtime_error("Invalid quaternion/vector normalization"); return *this/n; }
    Q conj() const { return {-x,-y,-z,w}; }
    Q operator+(Q b) const {return {x+b.x,y+b.y,z+b.z,w+b.w};}
    Q operator-(Q b) const {return {x-b.x,y-b.y,z-b.z,w-b.w};}
    Q operator-() const {return {-x,-y,-z,-w};}
    Q operator*(T s) const {return {x*s,y*s,z*s,w*s};}
    Q operator/(T s) const {return *this*(T(1)/s);}
    // Hamilton a*b. Deliberately does NOT use XMQuaternionMultiply(a,b) (=b*a).
    Q operator*(Q b) const {return {w*b.x+b.w*x+y*b.z-z*b.y,w*b.y+b.w*y+z*b.x-x*b.z,w*b.z+b.w*z+x*b.y-y*b.x,w*b.w-x*b.x-y*b.y-z*b.z};}
};
template<class T> Q<T> operator*(T s,Q<T> q) {return q*s;}
template<class T> Q<T> cross(Q<T> a,Q<T> b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
template<class T> Q<T> rotate(Q<T> q,Q<T> v) {Q<T> xyz{q.x,q.y,q.z}; return v+T(2)*cross(xyz,cross(xyz,v)+q.w*v);}
template<class T> Q<T> axisAngle(Q<T> axis,T radians) {axis=axis.unit(); return {axis.x*std::sin(radians/T(2)),axis.y*std::sin(radians/T(2)),axis.z*std::sin(radians/T(2)),std::cos(radians/T(2))};}
template<class T> T angle(Q<T> a,Q<T> b) {a=a.unit();b=b.unit();if(a.dot(b)<0)b=-b;return T(4)*std::asin(std::min(T(1),(a-b).norm()/T(2)));}
using F=Q<float>; using D=Q<double>;
static_assert(sizeof(F)==16 && offsetof(F,x)==0 && offsetof(F,y)==4 && offsetof(F,z)==8 && offsetof(F,w)==12);
// .NET/Unity seeded System.Random subtractive implementation, not std::mt19937.
class CsRandom {
    int values[56]{},next=0,nextp=21;
public:
    explicit CsRandom(int seed) {
        int mj=161803398-(seed==INT32_MIN?INT32_MAX:std::abs(seed)),mk=1; values[55]=mj;
        for(int i=1;i<55;++i) {int ii=(21*i)%55;values[ii]=mk;mk=mj-mk;if(mk<0)mk+=INT32_MAX;mj=values[ii];}
        for(int k=0;k<4;++k) for(int i=1;i<56;++i) {values[i]-=values[1+(i+30)%55];if(values[i]<0)values[i]+=INT32_MAX;}
    }
    double sample() {if(++next>=56)next=1;if(++nextp>=56)nextp=1;int n=values[next]-values[nextp];if(n==INT32_MAX)--n;if(n<0)n+=INT32_MAX;values[next]=n;return n*(1.0/INT32_MAX);}
};
inline D randomQ(CsRandom& r) { double x=r.sample()*2-1,y=r.sample()*2-1,z=r.sample()*2-1,w=r.sample()*2-1;return D{x,y,z,w}.unit(); }
inline D apply(D v,D q) {return v*q*D{0,0,1,0};}
inline double objective(D v,D b,D q) {return -.5*q.dot(apply(v,q))-b.dot(q);}
inline double residual(D v,D b,D q,double& violation) {q=q.unit();D s=apply(v,q)+b;double lambda=q.dot(s);violation=std::max(0.,v.norm()-lambda);return (s-lambda*q).norm();}
D localReference(D v,D b,D old);
double physicalEnergy(D q,D edge,double length,double kss,D left,D right,D rl,D rr,double kl,double kr);
}
